// file      : bdep/git.txx -*- C++ -*-
// license   : MIT; see accompanying LICENSE file

namespace bdep
{
  template <typename... A>
  void
  run_git (const semantic_version& min_ver,
           bool progress,
           const dir_path& repo,
           A&&... args)
  {
    // Unfortunately git doesn't have any kind of a no-progress option but
    // suppresses progress automatically for a non-terminal. So we use this
    // feature for the progress suppression by redirecting git's stderr to our
    // own diagnostics stream via a proxy pipe.
    //
    fdpipe pipe;

    if (!progress)
      pipe = open_pipe ();

    int err (!progress ? pipe.out.get () : 2);

    // We don't expect git to print anything to stdout, as the caller would
    // use start_git() and pipe otherwise. Thus, let's redirect stdout to
    // stderr for good measure, as git is known to print some informational
    // messages to stdout.
    //
    process pr (start_git (min_ver,
                           repo,
                           0   /* stdin  */,
                           err /* stdout */,
                           err /* stderr */,
                           forward<A> (args)...));

    bool io (false);

    if (!progress)
    {
      // Shouldn't throw, unless something is severely damaged.
      //
      pipe.out.close ();

      try
      {
        ifdstream is (move (pipe.in), fdstream_mode::skip, ifdstream::badbit);

        // We could probably write something like this, instead:
        //
        // *diag_stream << is.rdbuf () << flush;
        //
        // However, it would never throw and we could potentially miss the
        // reading failure.
        //
        for (string l; !eof (getline (is, l)); )
          *diag_stream << l << std::endl;

        is.close ();
      }
      catch (const io_error&)
      {
        // Presumably the child process failed and issued diagnostics so let
        // finish_git() try to deal with that.
        //
        io = true;
      }
    }

    finish_git (pr, io);
  }

  void
  git_check_version (const semantic_version& min_ver);

  template <typename I, typename O, typename E, typename... A>
  process
  start_git (const semantic_version& min_ver,
             I&& in, O&& out, E&& err,
             A&&... args)
  {
    git_check_version (min_ver);

    try
    {
      using namespace butl;

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
      // to running some other git that appear in the PATH variable. To
      // prevent this we pass the git's exec directory via the --exec-path
      // option explicitly.
      //
      string ep;
      process_path pp (process::path_search ("git", true /* init */));

#ifdef _WIN32
      ep = "--exec-path=" + pp.effect.directory ().string ();
#endif

      return process_start_callback (
        [] (const char* const args[], size_t n)
        {
          if (verb >= 2)
            print_process (args, n);
        },
        forward<I> (in), forward<O> (out), forward<E> (err),
        pp,
        !ep.empty () ? ep.c_str () : nullptr,
        forward<A> (args)...);
    }
    catch (const process_error& e)
    {
      fail << "unable to execute git: " << e << endf;
    }
  }

  template <typename... A>
  optional<string>
  git_line (const semantic_version& min_ver, bool ie, char delim, A&&... args)
  {
    fdpipe pipe (open_pipe ());
    auto_fd null (ie ? open_null () : auto_fd ());

    process pr (start_git (min_ver,
                           0                    /* stdin  */,
                           pipe                 /* stdout */,
                           ie ? null.get () : 2 /* stderr */,
                           forward<A> (args)...));

    return git_line (move (pr), move (pipe), ie, delim);
  }

  template <typename... A>
  inline optional<string>
  git_line (const semantic_version& min_ver, bool ie, A&&... args)
  {
    return git_line (min_ver, ie, '\n' /* delim */, forward<A> (args)...);
  }

  template <typename... A>
  inline optional<string>
  git_string (const semantic_version& min_ver, bool ie, A&&... args)
  {
    return git_line (min_ver, ie, '\0' /* delim */, forward<A> (args)...);
  }

  template <typename... A>
  void
  git_push (const common_options& o, const dir_path& repo, A&&... args)
  {
    // Map verbosity level. Suppress the (too detailed) push command output if
    // the verbosity level is 1. However, we still want to see the progress in
    // this case, unless we were asked to suppress it (git also suppress
    // progress for a non-terminal stderr).
    //
    cstrings v;
    bool progress (!o.no_progress ());

    if (verb < 2)
    {
      v.push_back ("-q");

      if (progress)
      {
        if (verb == 1 && stderr_term)
          v.push_back ("--progress");
      }
      else
        progress = true; // No need to suppress (already done with -q).
    }
    else if (verb > 3)
      v.push_back ("-v");

    run_git (semantic_version {2, 1, 0},
             progress,
             repo,
             "push",
             v,
             forward<A> (args)...);
  }
}
