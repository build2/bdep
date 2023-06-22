// file      : bdep/git.cxx -*- C++ -*-
// license   : MIT; see accompanying LICENSE file

#include <bdep/git.hxx>

#include <libbutl/git.hxx>

#include <bdep/diagnostics.hxx>

using namespace butl;

namespace bdep
{
  static pair<process_path, optional<string>> sys_git;
  static optional<semantic_version>           sys_git_ver;

// On POSIX the system git is always assumed.
//
#ifndef _WIN32
  static pair<process_path, optional<string>>& bun_git     (sys_git);
  static optional<semantic_version>&           bun_git_ver (sys_git_ver);
#else
  static pair<process_path, optional<string>> bun_git;
  static optional<semantic_version>           bun_git_ver;
#endif

  bool
  git_try_check_version (const semantic_version& min_ver, bool system)
  {
    // Query and cache git version on the first call.
    //
    optional<semantic_version>& gv (system ? sys_git_ver : bun_git_ver);

    if (!gv)
    {
      // Make sure that the getline() function call doesn't end up with an
      // infinite recursion.
      //
      gv = semantic_version ();

      optional<string> s (
        git_line (*gv, system, false /* ignore_error */, "--version"));

      if (!s || !(gv = git_version (*s)))
        fail << "unable to obtain git version";
    }

    // Note that we don't expect the min_ver to contain the build component,
    // that doesn't matter functionality-wise for git.
    //
    return *gv >= min_ver;
  }

  // As above but issue diagnostics and fail if git is older than the
  // specified minimum supported version.
  //
  void
  git_check_version (const semantic_version& min_ver, bool system)
  {
    if (!git_try_check_version (min_ver, system))
    {
      optional<semantic_version>& gv (system ? sys_git_ver : bun_git_ver);

      assert (gv); // Must have been cached by git_try_check_version().

      fail << "unsupported git version " << *gv <<
        info << "minimum supported version is " << min_ver << endf;
    }
  }

  // Return git process path and the --exec-path option, if it is required for
  // the execution.
  //
  const pair<process_path, optional<string>>&
  git_search (bool system)
  {
    tracer trace ("git_search");

    // On the first call search for the git process path, calculate the
    // --exec-path option value required for its execution, and cache them for
    // subsequent use.
    //
    pair<process_path, optional<string>>& git (system ? sys_git : bun_git);

    if (git.first.empty ())
    {
      // On startup git prepends the PATH environment variable value with the
      // computed directory path where its sub-programs are supposedly located
      // (--exec-path option, GIT_EXEC_PATH environment variable, etc; see
      // cmd_main() in git's git.c for details).
      //
      // Then, when git needs to run itself or one of its components as a
      // child process, it resolves the full executable path searching in
      // directories listed in PATH (see locate_in_PATH() in git's
      // run-command.c for details).
      //
      // On Windows we install git and its components into a place where it is
      // not expected to be, which results in the wrong path in PATH as set by
      // git (for example, c:/build2/libexec/git-core) which in turn may lead
      // to running some other git that appears in the PATH variable. To
      // prevent this we pass the git's exec directory via the --exec-path
      // option explicitly.
      //
      process_path pp;

#ifdef _WIN32

      // Figure out the bundle absolute directory path based on our own
      // process path and realize it for comparison. Though, we only need it
      // when searching for the system git.
      //
      dir_path bd;

      if (system)
      {
        // We should be able to construct/realize our process path so we don't
        // handle invalid_path.
        //
        bd = path (process::path_search (argv0, true /* init */).
                   effect_string ()).directory ().realize ();

        // Search in PATH first and fallback to the bundle directory.
        //
        pp = process::path_search ("git",
                                   true /* init */,
                                   bd,
                                   true /* path_only */);
      }
      else
#endif
        pp = process::path_search ("git", true /* init */);

      optional<string> ep;
#ifdef _WIN32

      // Note that we need to add --exec-path not only if the bundled git is
      // requested but also if we ended up with the bundled git while
      // searching for the system one.

      // Realize for comparison.
      //
      // We should be able to realize the existing program parent directory
      // path so we don't handle invalid_path.
      //
      dir_path d (pp.effect.directory ().realize ());

      if (!system || d == bd)
        ep = "--exec-path=" + d.string ();

      l4 ([&]{trace << (system ? "system: '" : "bundled: '") << pp.effect
                    << "'" << (ep ? ' ' + *ep : "");});
#endif

      git = make_pair (move (pp), move (ep));
    }

    return git;
  }

  optional<string>
  git_line (process&& pr, fdpipe&& pipe, bool ie, char delim)
  {
    optional<string> r;

    bool io (false);
    try
    {
      pipe.out.close ();
      ifdstream is (move (pipe.in), fdstream_mode::skip, ifdstream::badbit);

      string l;
      if (!eof (getline (is, l, delim)))
        r = move (l);

      is.close (); // Detect errors.
    }
    catch (const io_error&)
    {
      io = true; // Presumably git failed so check that first.
    }

    // Note: cannot use finish() since ignoring normal error.
    //
    if (!pr.wait ())
    {
      const process_exit& e (*pr.exit);

      if (!e.normal ())
        fail << "process git " << e;

      if (ie)
        r = nullopt;
      else
        throw failed (); // Assume git issued diagnostics.
    }
    else if (io)
      fail << "unable to read git output";

    return r;
  }

  url
  git_remote_url (const dir_path& repo,
                  const char* opt,
                  const char* what,
                  const char* cfg)
  {
    auto git_config = [&repo] (const char* name) -> optional<string>
    {
      // It's unlikely that the remote URL is configured globally, thus we use
      // the bundled git.
      //
      return git_line (semantic_version {2, 1, 0},
                       false /* system */,
                       repo,
                       true  /* ignore_error */,
                       "config",
                       "--get",
                       name);
    };

    auto parse_url = [] (const string& s, const char* w)
    {
      try
      {
        return url (s);
      }
      catch (const invalid_argument& e)
      {
        fail << "invalid " << w << " value '" << s << "': " << e << endf;
      }
    };

    // First try the custom config value if specified.
    //
    if (cfg != nullptr)
    {
      if (optional<string> l = git_config (cfg))
      {
        return parse_url (*l, cfg);
      }
    }

    // Next is remote.origin.build2Url.
    //
    if (optional<string> l = git_config ("remote.origin.build2Url"))
    {
      return parse_url (*l, "remote.origin.build2Url");
    }

    // Finally, get remote.origin.url and try to derive an HTTPS URL from it.
    //
    if (optional<string> l = git_config ("remote.origin.url"))
    {
      string& s (*l);

      // This one will be fuzzy and hairy. Here are some representative
      // examples of what we can encounter:
      //
      //            example.org:/path/to/repo.git
      //       user@example.org:/path/to/repo.git
      //       user@example.org:~user/path/to/repo.git
      // ssh://user@example.org/path/to/repo.git
      //
      // git://example.org/path/to/repo.git
      //
      // http://example.org/path/to/repo.git
      // https://example.org/path/to/repo.git
      //
      //        /path/to/repo.git
      // file:///path/to/repo.git
      //
      // Note that git seem to always make remote.origin.url absolute in
      // case of a local filesystem path.
      //
      // So the algorithm will be as follows:
      //
      // 1. If there is scheme, then parse as URL.
      //
      // 2. Otherwise, check if this is an absolute path.
      //
      // 3. Otherwise, assume SSH <host>:<path> thing.
      //
      url u;

      // Find the scheme.
      //
      // Note that in example.org:/path/... example.org is a valid scheme. To
      // distinguish this, we check if the scheme contains any dots (none of
      // the schemes recognized by git currently do and probably never will).
      //
      size_t p (s.find (':'));
      if (p != string::npos                       && // Has ':'.
          url::traits_type::find (s, p) == 0      && // Scheme starts at 0.
          s.rfind ('.', p - 1) == string::npos)      // No dots in scheme.
      {
        u = parse_url (s, "remote.origin.url");
      }
      else
      {
        // Absolute path or the SSH thing.
        //
        if (path_traits::absolute (s))
        {
          // This is what we want to end up with:
          //
          // file:///tmp
          // file:///c:/tmp
          //
          const char* h (s[0] == '/' ? "file://" : "file:///");
          u = parse_url (h + s, "remote.origin.url");
        }
        else if (p != string::npos)
        {
          // This can still include user (user@host) so let's add the scheme,
          // replace/erase ':', and parse it as a string representation of a
          // URL.
          //
          if (s[p + 1] == '/') // POSIX notation.
            s.erase (p, 1);
          else
            s[p] = '/';

          u = parse_url ("ssh://" + s, "remote.origin.url");
        }
        else
          fail << "invalid remote.origin.url value '" << s << "': not a URL";
      }

      // A remote URL gotta have authority.
      //
      if (u.authority)
      {
        if (u.scheme == "http" || u.scheme == "https")
        {
          // This can still include the user which we most likely don't want.
          //
          u.authority->user.clear ();
          return u;
        }

        // Derive an HTTPS URL from a remote URL (and hope for the best).
        //
        if (u.scheme != "file" && u.path)
          return url ("https", u.authority->host, *u.path);
      }

      diag_record dr (fail);

      dr << "unable to derive " << what << " from " << u;

      dr << info << "consider setting git ";
      if (cfg != nullptr)
        dr << cfg << " or ";
      dr << "remote.origin.build2Url value";

      if (opt != nullptr)
        dr << info << "or use " << opt << " to specify explicitly";
    }

    // We don't necessarily want to suggest remote.origin.build2* or the
    // option since preferably this should be derived automatically from
    // remote.origin.url.
    //
    fail << "unable to discover " << what << ": no git remote.origin.url "
         << "value" << endf;
  }

  git_repository_status
  git_status (const dir_path& repo)
  {
    git_repository_status r;

    // git-status --porcelain=2 (available since git 2.11.0) gives us all the
    // information with a single invocation.
    //
    fdpipe pipe (open_pipe ()); // Text mode seems appropriate.

    process pr (start_git (semantic_version {2, 11, 0},
                           false /* system */,
                           repo,
                           0     /* stdin  */,
                           pipe  /* stdout */,
                           2     /* stderr */,
                           "status",
                           "--porcelain=2",
                           "--branch"));

    // Shouldn't throw, unless something is severely damaged.
    //
    pipe.out.close ();

    bool io (false);
    try
    {
      ifdstream is (move (pipe.in), fdstream_mode::skip, ifdstream::badbit);

      // Lines starting with '#' are headers (come first) with any other line
      // indicating some kind of change.
      //
      // The headers we are interested in are:
      //
      // # branch.oid <commit>  | (initial)       Current commit.
      // # branch.head <branch> | (detached)      Current branch.
      // # branch.upstream <upstream_branch>      If upstream is set.
      // # branch.ab +<ahead> -<behind>           If upstream is set and
      //                                          the commit is present.
      //
      // Note that if we are in the detached HEAD state, then we will only
      // see the first two with branch.head being '(detached)'.
      //
      for (string l; !eof (getline (is, l)); )
      {
        char c (l[0]);

        if (c == '#')
        {
          if (l.compare (2, 10, "branch.oid") == 0)
          {
            r.commit = string (l, 13);

            if (r.commit == "(initial)")
              r.commit.clear ();
          }
          else if (l.compare (2, 11, "branch.head") == 0)
          {
            r.branch = string (l, 14);

            if (r.branch == "(detached)")
              r.branch.clear ();
          }
          else if (l.compare (2, 15, "branch.upstream") == 0)
          {
            r.upstream = string (l, 18);
          }
          else if (l.compare (2, 9, "branch.ab") == 0)
          {
            // Both +<ahead> and -<behind> are always present, even if 0.
            //
            size_t a (l.find ('+', 12)); assert (a != string::npos);
            size_t b (l.find ('-', 12)); assert (b != string::npos);

            if (l[a + 1] != '0') r.ahead  = true;
            if (l[b + 1] != '0') r.behind = true;
          }

          continue; // Some other header.
        }

        // Change line. For tracked entries it has the following format:
        //
        // 1 <XY> ...
        // 2 <XY> ...
        //
        // Where <XY> is a two-character field with X describing the staged
        // status and Y -- unstaged and with '.' indicating no change.
        //
        // All other lines (untracked/unmerged entries) we treat as an
        // indication of an unstaged change (see git-status(1) for details).
        //
        if (c == '1' || c == '2')
        {
          if (l[2] != '.') r.staged   = true;
          if (l[3] != '.') r.unstaged = true;
        }
        else
          r.unstaged = true;

        // Skip the rest if we already know the outcome (remember, headers
        // always come first).
        //
        if (r.staged && r.unstaged)
          break;
      }

      is.close (); // Detect errors.
    }
    catch (const io_error&)
    {
      // Presumably the child process failed and issued diagnostics so let
      // finish_git() try to deal with that.
      //
      io = true;
    }

    finish_git (pr, io);

    return r;
  }
}
