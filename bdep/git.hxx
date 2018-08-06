// file      : bdep/git.hxx -*- C++ -*-
// copyright : Copyright (c) 2014-2018 Code Synthesis Ltd
// license   : MIT; see accompanying LICENSE file

#ifndef BDEP_GIT_HXX
#define BDEP_GIT_HXX

#include <libbutl/git.mxx>

#include <bdep/types.hxx>
#include <bdep/utility.hxx>

namespace bdep
{
  using butl::git_repository;

  template <typename I, typename O, typename E, typename... A>
  process
  start_git (I&& in, O&& out, E&& err, A&&... args);

  template <typename I, typename O, typename E, typename... A>
  process
  start_git (const dir_path& repo, I&& in, O&& out, E&& err, A&&... args);

  void
  finish_git (process& pr, bool io_read = false);

  template <typename... A>
  void
  run_git (const dir_path& repo, A&&... args);

  // Return the first line of the git output. If ignore_error is true, then
  // suppress stderr, ignore (normal) error exit status, and return nullopt.
  //
  template <typename... A>
  optional<string>
  git_line (bool ignore_error, A&&... args);

  template <typename... A>
  optional<string>
  git_line (const dir_path& repo, bool ignore_error, A&&... args);

  // Similar to the above but takes the already started git process with a
  // redirected output pipe.
  //
  optional<string>
  git_line (process&& pr, fdpipe&& pipe, bool ignore_error);
}

#include <bdep/git.ixx>
#include <bdep/git.txx>

#endif // BDEP_GIT_HXX
