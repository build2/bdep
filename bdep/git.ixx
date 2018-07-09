// file      : bdep/git.ixx -*- C++ -*-
// copyright : Copyright (c) 2014-2018 Code Synthesis Ltd
// license   : MIT; see accompanying LICENSE file

using namespace butl;

namespace bdep
{
  optional<string>
  git_line (process&&, fdpipe&&, bool);

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
