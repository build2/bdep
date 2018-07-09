// file      : bdep/publish.cxx -*- C++ -*-
// copyright : Copyright (c) 2014-2018 Code Synthesis Ltd
// license   : MIT; see accompanying LICENSE file

#include <bdep/publish.hxx>

#include <libbutl/standard-version.mxx>

#include <bdep/git.hxx>
#include <bdep/project.hxx>
#include <bdep/project-email.hxx>
#include <bdep/database.hxx>
#include <bdep/diagnostics.hxx>

#include <bdep/sync.hxx>

using namespace std;
using namespace butl;

namespace bdep
{
  static inline url
  parse_url (const string& s, const char* what)
  {
    try
    {
      return url (s);
    }
    catch (const invalid_argument& e)
    {
      fail << "invalid " << what << " value '" << s << "': " << e << endf;
    }
  };

  // Get the project's control repository URL.
  //
  static url
  control_url (const dir_path& prj)
  {
    if (git (prj))
    {
      // First try remote.origin.build2ControlUrl which can be used to specify
      // a custom URL (e.g., if a correct one cannot be automatically derived
      // from remote.origin.url).
      //
      if (optional<string> l = git_line (prj,
                                         true /* ignore_error */,
                                         "config",
                                         "--get",
                                         "remote.origin.build2ControlUrl"))
      {
        return parse_url (*l, "remote.origin.build2ControlUrl");
      }

      // Otherwise, get remote.origin.url and try to derive an https URL from
      // it.
      //
      if (optional<string> l = git_line (prj,
                                         true /* ignore_error */,
                                         "config",
                                         "--get",
                                         "remote.origin.url"))
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
        // Note that in example.org:/path/... example.org is a valid scheme.
        // To distinguish this, we check if the scheme contains any dots (none
        // of the schemes recognized by git currently do and probably never
        // will).
        //
        size_t p (s.find (':'));
        if (p != string::npos                  && // Has ':'.
            url::traits::find (s, p) == 0      && // Scheme starts at 0.
            s.rfind ('.', p - 1) == string::npos) // No dots in scheme.
        {
          u = parse_url (s, "remote.origin.url");
        }
        else
        {
          // Absolute path or the SSH thing.
          //
          if (path::traits::absolute (s))
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
            // This can still include user (user@host) so let's add the
            // scheme, replace/erase ':', and parse it as a string
            // representation of a URL.
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

        if (u.scheme == "http" || u.scheme == "https")
          return u;

        // Derive an HTTPS URL from a remote URL (and hope for the best).
        //
        if (u.scheme != "file" && u.authority && u.path)
          return url ("https", u.authority->host, *u.path);

        fail << "unable to derive control repository URL from " << u <<
          info << "consider setting remote.origin.build2ControlUrl" <<
          info << "or use --control to specify explicitly";
      }

      fail << "unable to discover control repository URL: no git "
           << "remote.origin.url value";
    }

    fail << "unable to discover control repository URL" <<
      info << "use --control to specify explicitly" << endf;
  }

  static standard_version
  package_version (const common_options& o,
                   const dir_path& cfg,
                   const string& p)
  {
    // We could have used bpkg-pkg-status but then we would have to deal with
    // iterations. So we use the build system's info meta-operation directly.
    //
    string v;
    {
      process pr;
      bool io (false);
      try
      {
        fdpipe pipe (fdopen_pipe ()); // Text mode seems appropriate.

        // Note: the package directory inside the configuration is a bit of an
        // assumption.
        //
        pr = start_b (o,
                      pipe /* stdout */,
                      2    /* stderr */,
                      "info:", (dir_path (cfg) /= p).representation ());

        pipe.out.close ();
        ifdstream is (move (pipe.in), fdstream_mode::skip, ifdstream::badbit);

        for (string l; !eof (getline (is, l)); )
        {
          // Verify the name for good measure (comes before version).
          //
          if (l.compare (0, 9, "project: ") == 0)
          {
            if (l.compare (9, string::npos, p) != 0)
              fail << "name mismatch for package " << p;
          }
          else if (l.compare (0, 9, "version: ") == 0)
          {
            v = string (l, 9);
            break;
          }
        }

        is.close (); // Detect errors.
      }
      catch (const io_error&)
      {
        // Presumably the child process failed and issued diagnostics so let
        // finish_b() try to deal with that first.
        //
        io = true;
      }

      finish_b (o, pr, io);
    }

    try
    {
      return standard_version (v);
    }
    catch (const invalid_argument& e)
    {
      fail << "invalid package " << p << " version " << v << ": " << e << endf;
    }
  }

  static int
  cmd_publish (const cmd_publish_options& o,
               const dir_path& prj,
               const dir_path& cfg,
               const cstrings& pkg_names)
  {
    const url& repo (o.repository ());

    optional<url> ctrl;
    if (o.control_specified ())
    {
      if (o.control () != "none")
        ctrl = parse_url (o.control (), "--control option");
    }
    else
      ctrl = control_url (prj);

    string email;
    if (o.email_specified ())
      email = o.email ();
    else if (optional<string> r = project_email (prj))
      email = move (*r);
    else
      fail << "unable to obtain publisher's email" <<
        info << "use --email to specify explicitly";


    // Collect package information (version, project, section).
    //
    // @@ It would have been nice to publish them in the dependency order.
    //    Perhaps we need something like bpkg-pkg-order (also would be needed
    //    in init --clone).
    //
    struct package
    {
      string           name;
      standard_version version;
      string           project;
      string           section; // alpha|beta|stable (or --section)

      path            archive;
      string          checksum;
    };
    vector<package> pkgs;

    for (string n: pkg_names)
    {
      standard_version v (package_version (o, cfg, n));

      // Should we allow publishing snapshots and, if so, to which section?
      // For example, is it correct to consider a "between betas" snapshot a
      // beta version?
      //
      if (v.snapshot ())
        fail << "package " << n << " version " << v << " is a snapshot";

      string p (prj.leaf ().string ()); // @@ TODO/TMP

      // Per semver we treat zero major versions as alpha.
      //
      string s (o.section_specified () ? o.section ()   :
                v.alpha () || v.major () == 0 ? "alpha" :
                v.beta ()                     ? "beta"  : "stable");

      pkgs.push_back (
        package {move (n), move (v), move (p), move (s), path (), string ()});
    }

    // Print the plan and ask for confirmation.
    //
    if (!o.yes ())
    {
      text << "publishing:"    << '\n'
           << "  to:      " << repo << '\n'
           << "  as:      " << email
           << '\n';

      for (size_t i (0); i != pkgs.size (); ++i)
      {
        const package& p (pkgs[i]);

        diag_record dr (text);

        // If printing multiple packages, separate them with a blank line.
        //
        if (i != 0)
          dr << '\n';

        // While currently the control repository is the same for all
        // packages, this could change in the future (e.g., multi-project
        // publishing).
        //
        dr << "  package: " << p.name    << '\n'
           << "  version: " << p.version << '\n'
           << "  project: " << p.project << '\n'
           << "  section: " << p.section;

        if (ctrl)
          dr << '\n'
             << "  control: " << *ctrl;
      }

      if (!yn_prompt ("continue? [y/n]"))
        return 1;
    }

    // Prepare package archives and calculate their checksums. Also verify
    // each archive with bpkg-pkg-verify for good measure.
    //
    auto_rmdir dr_rm (dir_path ("/tmp/publish")); //@@ TODO tmp facility like in bpkg.
    const dir_path& dr (dr_rm.path);   // dist.root
    mk (dr);

    for (package& p: pkgs)
    {
      // Similar to extracting package version, we call the build system
      // directly to prepare the distribution. If/when we have bpkg-pkg-dist,
      // we may want to switch to that.
      //
      run_b (o,
             "dist:", (dir_path (cfg) /= p.name).representation (),
             "config.dist.root=" + dr.representation (),
             "config.dist.archives=tar.gz",
             "config.dist.checksums=sha256");

      // This is the canonical package archive name that we expect dist to
      // produce.
      //
      path a (dr / p.name + '-' + p.version.string () + ".tar.gz");
      path c (a + ".sha256");

      if (!exists (a))
        fail << "package distribution did not produce expected archive " << a;

      if (!exists (c))
        fail << "package distribution did not produce expected checksum " << c;

      //@@ TODO: call bpkg-pkg-verify to verify archive name/content all match.

      //@@ TODO: read checksum from .sha256 file and store in p.checksum.

      p.archive = move (a);
    }

    // Submit each package.
    //
    for (const package& p: pkgs)
    {
      //@@ TODO: call curl to upload the archive, parse response manifest,
      //   and print message/reference.

      text << "submitting " << p.archive;

      //@@ TODO [phase 2]: add checksum file to build2-control branch, commit
      //   and push (this will need some more discussion).
      //
      //  - name (abbrev 12 char checksum) and subdir?
      //
      //  - make the checksum file a manifest with basic info (name, version)
      //
      //  - what if already exists (previous failed attempt)? Ignore?
      //
      //  - make a separate checkout (in tmp facility) reusing the external
      //    .git/ dir?
      //
      //  - should probably first fetch to avoid push conflicts. Or maybe
      //    fetch on push conflict (more efficient/robust)?
      //
    }

    return 0;
  }

  int
  cmd_publish (const cmd_publish_options& o, cli::scanner&)
  {
    tracer trace ("publish");

    // The same ignore/load story as in sync.
    //
    project_packages pp (
      find_project_packages (o,
                             false /* ignore_packages */,
                             false /* load_packages   */));

    const dir_path& prj (pp.project);
    database db (open (prj, trace));

    // We need a single configuration to prepare package distribution.
    //
    shared_ptr<configuration> cfg;
    {
      transaction t (db.begin ());
      configurations cfgs (find_configurations (o, prj, t));
      t.commit ();

      if (cfgs.size () > 1)
        fail << "multiple configurations specified for publish";

      shared_ptr<configuration>& c (cfgs[0]);

      // If specified, verify packages are present in the configuration.
      // Otherwise, make sure the configuration is not empty.
      //
      if (!pp.packages.empty ())
        verify_project_packages (pp, cfgs);
      else if (c->packages.empty ())
        fail << "no packages initialized in configuration " << *c;

      cfg = move (c);
    }

    // Pre-sync the configuration to avoid triggering the build system hook
    // (see sync for details).
    //
    cmd_sync (o, prj, cfg, strings () /* pkg_args */, true /* implicit */);

    // If no packages were explicitly specified, then we publish all that have
    // been initialized in the configuration.
    //
    cstrings pkgs;
    if (pp.packages.empty ())
    {
      for (const package_state& p: cfg->packages)
        pkgs.push_back (p.name.string ().c_str ());
    }
    else
    {
      for (const package_location& p: pp.packages)
        pkgs.push_back (p.name.string ().c_str ());
    }

    return cmd_publish (o, prj, cfg->path, pkgs);
  }
}
