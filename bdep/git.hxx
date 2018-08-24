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

  // All functions that start git process take the minimum supported git
  // version as an argument.
  //
  // Start git process.
  //
  template <typename I, typename O, typename E, typename... A>
  process
  start_git (const semantic_version&, I&& in, O&& out, E&& err, A&&... args);

  template <typename I, typename O, typename E, typename... A>
  process
  start_git (const semantic_version&,
             const dir_path& repo,
             I&& in, O&& out, E&& err,
             A&&... args);

  // Wait for git process to terminate.
  //
  void
  finish_git (process& pr, bool io_read = false);

  // Run git process.
  //
  template <typename... A>
  void
  run_git (const semantic_version&, const dir_path& repo, A&&... args);

  // Return the first line of the git output. If ignore_error is true, then
  // suppress stderr, ignore (normal) error exit status, and return nullopt.
  //
  template <typename... A>
  optional<string>
  git_line (const semantic_version&, bool ignore_error, A&&... args);

  template <typename... A>
  optional<string>
  git_line (const semantic_version&,
            const dir_path& repo,
            bool ignore_error,
            A&&... args);

  // Similar to the above but takes the already started git process with a
  // redirected output pipe.
  //
  optional<string>
  git_line (process&& pr, fdpipe&& pipe, bool ignore_error);

  // Try to derive a remote HTTPS repository URL from the optionally specified
  // custom git config value falling back to remote.origin.build2Url and then
  // remote.origin.url. Issue diagnostics (including a suggestion to use
  // option opt, if specified) and fail if unable to.
  //
  url
  git_remote_url (const dir_path& repo,
                  const char* opt = nullptr,
                  const char* what = "remote repository URL",
                  const char* cfg = nullptr);
}

#include <bdep/git.ixx>
#include <bdep/git.txx>

#endif // BDEP_GIT_HXX
