// file      : bdep/git.ixx -*- C++ -*-
// license   : MIT; see accompanying LICENSE file

namespace bdep
{
  template <typename I, typename O, typename E, typename... A>
  inline process
  start_git (const semantic_version& min_ver,
             bool system,
             const dir_path& repo,
             I&& in, O&& out, E&& err,
             A&&... args)
  {
    return start_git (min_ver,
                      system,
                      forward<I> (in), forward<O> (out), forward<E> (err),
                      "-C", repo,
                      forward<A> (args)...);
  }

  inline void
  finish_git (process& pr, bool io_read)
  {
    finish ("git", pr, io_read);
  }

  template <typename... A>
  inline optional<string>
  git_line (const semantic_version& min_ver,
            bool system,
            const dir_path& repo,
            bool ie,
            A&&... args)
  {
    return git_line (min_ver,
                     system,
                     ie,
                     "-C", repo,
                     forward<A> (args)...);
  }

  template <typename... A>
  inline optional<string>
  git_string (const semantic_version& min_ver,
              bool system,
              const dir_path& repo,
              bool ie,
              A&&... args)
  {
    return git_string (min_ver,
                       system,
                       ie,
                       "-C", repo,
                       forward<A> (args)...);
  }

  inline optional<string>
  git_string (process&& pr, fdpipe&& pipe, bool ignore_error)
  {
    return git_line (move (pr), move (pipe), ignore_error, '\0' /* delim */);
  }
}
