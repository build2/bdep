// file      : bdep/git.hxx -*- C++ -*-
// license   : MIT; see accompanying LICENSE file

#ifndef BDEP_GIT_HXX
#define BDEP_GIT_HXX

#include <libbutl/git.hxx>

#include <bdep/types.hxx>
#include <bdep/utility.hxx>
#include <bdep/common-options.hxx>

namespace bdep
{
  using butl::git_repository;

  // All functions that start git process take the minimum supported git
  // version as an argument.
  //
  // They also take the system flag (only considered on Windows) that if true
  // causes these functions to prefer the system git program (determined via
  // PATH) over the bundled one (the bundled git is still used as a fallback).
  // Normally, the caller should use the system git when operating on behalf
  // of the user (commits, pushes, etc), so that the user interacts with the
  // git program they expect. For the query requests (git repository status,
  // etc) the bundled git should be used instead to avoid compatibility issues
  // (e.g., symlink handling, etc; but when querying for potentially global
  // configuration values such as author, system git should be used). Note
  // that on POSIX the system git is used for everything.
  //
  // Start git process.
  //
  template <typename I, typename O, typename E, typename... A>
  process
  start_git (const semantic_version&,
             bool system,
             I&& in, O&& out, E&& err,
             A&&... args);

  template <typename I, typename O, typename E, typename... A>
  process
  start_git (const semantic_version&,
             bool system,
             const dir_path& repo,
             I&& in, O&& out, E&& err,
             A&&... args);

  template <typename I, typename O, typename E, typename... A>
  inline process
  start_git (const semantic_version& min_ver,
             bool system,
             dir_path& repo,
             I&& in, O&& out, E&& err,
             A&&... args)
  {
    return start_git (min_ver,
                      system,
                      const_cast<const dir_path&> (repo),
                      forward<I> (in), forward<O> (out), forward<E> (err),
                      forward<A> (args)...);
  }

  // Wait for git process to terminate.
  //
  void
  finish_git (process& pr, bool io_read = false);

  // Run git process.
  //
  // Pass NULL as the repository argument if the git command is not
  // repository-specific (e.g., init).
  //
  template <typename... A>
  void
  run_git (const semantic_version&,
           bool system,
           bool progress,
           const dir_path* repo,
           A&&... args);

  template <typename... A>
  inline void
  run_git (const semantic_version& min_ver,
           bool system,
           const dir_path* repo,
           A&&... args)
  {
    run_git (min_ver,
             system,
             true /* progress */,
             repo,
             forward<A> (args)...);
  }

  template <typename... A>
  inline void
  run_git (const semantic_version& min_ver,
           bool system,
           bool progress,
           const dir_path& repo,
           A&&... args)
  {
    run_git (min_ver,
             system,
             progress,
             &repo,
             forward<A> (args)...);
  }

  template <typename... A>
  inline void
  run_git (const semantic_version& min_ver,
           bool system,
           const dir_path& repo,
           A&&... args)
  {
    run_git (min_ver,
             system,
             true /* progress */,
             repo,
             forward<A> (args)...);
  }

  // Return the first line of the git output. If ignore_error is true, then
  // suppress stderr, ignore (normal) error exit status, and return nullopt.
  //
  template <typename... A>
  optional<string>
  git_line (const semantic_version&,
            bool system,
            bool ignore_error,
            A&&... args);

  template <typename... A>
  optional<string>
  git_line (const semantic_version&,
            bool system,
            const dir_path& repo,
            bool ignore_error,
            A&&... args);

  // Similar to the above but takes the already started git process with a
  // redirected output pipe.
  //
  optional<string>
  git_line (process&&, fdpipe&&, bool ignore_error, char delim = '\n');

  // Similar to git_line() functions but return the complete git output.
  //
  template <typename... A>
  optional<string>
  git_string (const semantic_version&,
              bool system,
              bool ignore_error,
              A&&... args);

  template <typename... A>
  optional<string>
  git_string (const semantic_version&,
              bool system,
              const dir_path& repo,
              bool ignore_error,
              A&&... args);

  optional<string>
  git_string (process&&, fdpipe&&, bool ignore_error);

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

  // Repository status.
  //
  struct git_repository_status
  {
    string commit;   // Current commit or empty if initial.
    string branch;   // Local branch or empty if detached.
    string upstream; // Upstream in <remote>/<branch> form or empty if not set.

    // Note that unmerged and untracked entries are considered as unstaged.
    //
    bool staged   = false; // Repository has staged changes.
    bool unstaged = false; // Repository has unstaged changes.

    // Note that we can be both ahead and behind.
    //
    bool ahead  = false; // Local branch is ahead of upstream.
    bool behind = false; // Local branch is behind of upstream.
  };

  // Note: requires git 2.11.0 or higher.
  //
  git_repository_status
  git_status (const dir_path& repo);

  // Run the git push command.
  //
  template <typename... A>
  void
  git_push (const common_options&, const dir_path& repo, A&&... args);

  // Return true if git is at least of the specified minimum supported
  // version.
  //
  bool
  git_try_check_version (const semantic_version&, bool system);
}

#include <bdep/git.ixx>
#include <bdep/git.txx>

#endif // BDEP_GIT_HXX
