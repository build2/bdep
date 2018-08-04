// file      : bdep/git.hxx -*- C++ -*-
// copyright : Copyright (c) 2014-2018 Code Synthesis Ltd
// license   : MIT; see accompanying LICENSE file

#ifndef BDEP_GIT_HXX
#define BDEP_GIT_HXX

#include <bdep/types.hxx>
#include <bdep/utility.hxx>

namespace bdep
{
  // Return true if the specified directory is a git repository root (contains
  // the .git filesystem entry).
  //
  bool
  git (const dir_path&);

  template <typename... A>
  inline void
  run_git (const dir_path& repo, A&&... args);

  template <typename I, typename O, typename E, typename... A>
  inline process
  start_git (I&& in, O&& out, E&& err, const dir_path& repo, A&&... args);

  // Return the first line of the git output. If ignore_error is true, then
  // suppress stderr, ignore (normal) error exit status, and return nullopt.
  //
  template <typename... A>
  optional<string>
  git_line (const dir_path& repo, bool ignore_error, A&&... args);

  // Similar to the above but takes the already started git process with a
  // redirected output pipe.
  //
  optional<string>
  git_line (process&& pr, fdpipe&& pipe, bool ignore_error = false);
}

#include <bdep/git.ixx>

#endif // BDEP_GIT_HXX
