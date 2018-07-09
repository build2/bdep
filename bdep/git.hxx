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

  // Return the first line of the git output. If ignore_error is true, then
  // suppress stderr, ignore (normal) error exist status, and return nullopt.
  //
  template <typename... A>
  optional<string>
  git_line (const dir_path& repo, bool ignore_error, A&&... args);
}

#include <bdep/git.ixx>

#endif // BDEP_GIT_HXX
