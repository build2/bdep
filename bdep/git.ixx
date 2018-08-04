// file      : bdep/git.ixx -*- C++ -*-
// copyright : Copyright (c) 2014-2018 Code Synthesis Ltd
// license   : MIT; see accompanying LICENSE file

using namespace butl;

namespace bdep
{
  template <typename... A>
  inline void
  run_git (const dir_path& repo, A&&... args)
  {
    return run ("git", "-C", repo, forward<A> (args)...);
  }

  template <typename I, typename O, typename E, typename... A>
  inline process
  start_git (I&& in, O&& out, E&& err, const dir_path& repo, A&&... args)
  {
    return start (forward<I> (in),
                  forward<O> (out),
                  forward<E> (err),
                  "git",
                  "-C", repo,
                  forward<A> (args)...);
  }

  template <typename... A>
  inline optional<string>
  git_line (const dir_path& repo, bool ie, A&&... args)
  {
    fdpipe pipe (fdopen_pipe ());
    auto_fd null (ie ? fdnull () : auto_fd ());

    process pr (start (0                     /* stdin  */,
                       pipe                  /* stdout */,
                       ie ?  null.get () : 2 /* stderr */,
                       "git",
                       "-C", repo,
                       forward<A> (args)...));

    return git_line (move (pr), move (pipe), ie);
  }
}
