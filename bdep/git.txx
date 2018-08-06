// file      : bdep/git.txx -*- C++ -*-
// copyright : Copyright (c) 2014-2018 Code Synthesis Ltd
// license   : MIT; see accompanying LICENSE file

namespace bdep
{
  template <typename... A>
  void
  run_git (const dir_path& repo, A&&... args)
  {
    process pr (start_git (repo,
                           0 /* stdin  */,
                           1 /* stdout */,
                           2 /* stderr */,
                           forward<A> (args)...));

    finish_git (pr);
  }

  void
  git_check_version ();

  template <typename I, typename O, typename E, typename... A>
  process
  start_git (I&& in, O&& out, E&& err, A&&... args)
  {
    git_check_version ();

    return start (forward<I> (in),
                  forward<O> (out),
                  forward<E> (err),
                  "git",
                  forward<A> (args)...);
  }

  template <typename... A>
  optional<string>
  git_line (bool ie, A&&... args)
  {
    fdpipe pipe (fdopen_pipe ());
    auto_fd null (ie ? fdnull () : auto_fd ());

    process pr (start_git (0                    /* stdin  */,
                           pipe                 /* stdout */,
                           ie ? null.get () : 2 /* stderr */,
                           forward<A> (args)...));

    return git_line (move (pr), move (pipe), ie);
  }
}
