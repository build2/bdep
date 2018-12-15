// file      : bdep/git.txx -*- C++ -*-
// copyright : Copyright (c) 2014-2018 Code Synthesis Ltd
// license   : MIT; see accompanying LICENSE file

namespace bdep
{
  template <typename... A>
  void
  run_git (const semantic_version& min_ver, const dir_path& repo, A&&... args)
  {
    // We don't expect git to print anything to stdout, as the caller would use
    // start_git() and pipe otherwise. Thus, let's redirect stdout to stderr
    // for good measure, as git is known to print some informational messages
    // to stdout.
    //
    process pr (start_git (min_ver,
                           repo,
                           0 /* stdin  */,
                           2 /* stdout */,
                           2 /* stderr */,
                           forward<A> (args)...));

    finish_git (pr);
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

    return start (forward<I> (in), forward<O> (out), forward<E> (err),
                  "git",
                  forward<A> (args)...);
  }

  template <typename... A>
  optional<string>
  git_line (const semantic_version& min_ver, bool ie, A&&... args)
  {
    fdpipe pipe (fdopen_pipe ());
    auto_fd null (ie ? fdnull () : auto_fd ());

    process pr (start_git (min_ver,
                           0                    /* stdin  */,
                           pipe                 /* stdout */,
                           ie ? null.get () : 2 /* stderr */,
                           forward<A> (args)...));

    return git_line (move (pr), move (pipe), ie);
  }
}
