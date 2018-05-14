// file      : bdep/utility.hxx -*- C++ -*-
// copyright : Copyright (c) 2014-2017 Code Synthesis Ltd
// license   : MIT; see accompanying LICENSE file

#ifndef BDEP_UTILITY_HXX
#define BDEP_UTILITY_HXX

#include <memory>    // make_shared()
#include <string>    // to_string()
#include <utility>   // move(), forward(), declval(), make_pair()
#include <cassert>   // assert()
#include <iterator>  // make_move_iterator()
#include <algorithm> // find(), find_if()

#include <libbutl/ft/lang.hxx>

#include <libbutl/utility.mxx>    // casecmp(), reverse_iterate(), etc
#include <libbutl/fdstream.mxx>
#include <libbutl/filesystem.mxx>

#include <bdep/types.hxx>
#include <bdep/version.hxx>
#include <bdep/common-options.hxx>

namespace bdep
{
  using std::move;
  using std::forward;
  using std::declval;

  using std::make_pair;
  using std::make_shared;
  using std::make_move_iterator;
  using std::to_string;

  using std::find;
  using std::find_if;

  // <libbutl/utility.mxx>
  //
  using butl::ucase;
  using butl::lcase;
  using butl::casecmp;

  using butl::trim;
  using butl::next_word;

  using butl::reverse_iterate;

  using butl::make_guard;
  using butl::make_exception_guard;

  // <libbutl/filesystem.mxx>
  //
  using butl::auto_rmfile;
  using butl::auto_rmdir;

  // <libbutl/fdstream.mxx>
  //
  using butl::fdopen_pipe;

  // Empty string and path.
  //
  extern const string   empty_string;
  extern const path     empty_path;
  extern const dir_path empty_dir_path;

  // Widely-used paths.
  //
  extern const dir_path bdep_dir;  // .bdep/
  extern const path     bdep_file; // .bdep/bdep.sqlite3

  extern const path manifest_file;       // manifest
  extern const path packages_file;       // packages.manifest
  extern const path repositories_file;   // repositories.manifest
  extern const path configurations_file; // configurations.manifest

  // Process path (argv[0]).
  //
  extern const char* argv0;

  // Directory extracted from argv[0] (i.e., this process' recall directory)
  // or empty if there is none. Can be used as a search fallback.
  //
  extern dir_path exec_dir;

  // Filesystem.
  //
  bool
  exists (const path&, bool ignore_error = false);

  bool
  exists (const dir_path&, bool ignore_error = false);

  bool
  empty (const dir_path&);

  void
  mk (const dir_path&);

  void
  mk_p (const dir_path&);

  void
  rm (const path&, uint16_t verbosity = 3);

  // Run a process.
  //
  template <typename I, typename O, typename E, typename P, typename... A>
  process
  start (I&& in, O&& out, E&& err, const P& prog, A&&... args);

  template <typename P, typename... A>
  void
  run (const P& prog, A&&... args);

  // Run the bpkg process.
  //
  const char*
  name_bpkg (const common_options&);

  template <typename O, typename E, typename... A>
  process
  start_bpkg (uint16_t verbosity,
              const common_options&,
              O&& out,
              E&& err,
              A&&... args);

  void
  finish_bpkg (const common_options&, process&, bool io_error = false);

  template <typename... A>
  void
  run_bpkg (uint16_t verbosity, const common_options&, A&&... args);

  // Run the b process.
  //
  const char*
  name_b (const common_options&);

  template <typename O, typename E, typename... A>
  process
  start_b (const common_options&, O&& out, E&& err, A&&... args);

  template <typename... A>
  void
  run_b (const common_options&, A&&... args);

  // Manifest parsing and serialization.
  //
  // For parsing, if path is '-', then read from stdin.
  //
  template <typename T>
  T
  parse_manifest (const path&,
                  const char* what,
                  bool ignore_unknown = false);

  template <typename T>
  T
  parse_manifest (istream&,
                  const string& name,
                  const char* what,
                  bool ignore_unknown = false);

  template <typename T>
  void
  serialize_manifest (const T&, const path&, const char* what);

  template <typename T>
  void
  serialize_manifest (const T&,
                      ostream&,
                      const string& name,
                      const char* what);

  // CLI (sub)command parsing helper.
  //
  template <typename C>
  C
  parse_command (cli::scanner& scan, const char* what, const char* help);

  template <typename C>
  bool
  parse_command (cli::scanner& scan, C&);

  // Scan and return/append arguments preserving grouping.
  //
  void
  scan_argument (strings&, cli::group_scanner&);

  inline void
  scan_arguments (strings& r, cli::group_scanner& s)
  {
    for (; s.more (); scan_argument (r, s)) ;
  }

  inline strings
  scan_arguments (cli::group_scanner& s)
  {
    strings r;
    scan_arguments (r, s);
    return r;
  }
}

#include <bdep/utility.txx>

#endif // BDEP_UTILITY_HXX
