// file      : bdep/utility.hxx -*- C++ -*-
// copyright : Copyright (c) 2014-2017 Code Synthesis Ltd
// license   : MIT; see accompanying LICENSE file

#ifndef BDEP_UTILITY_HXX
#define BDEP_UTILITY_HXX

#include <memory>   // make_shared()
#include <string>   // to_string()
#include <utility>  // move(), forward(), declval(), make_pair()
#include <cassert>  // assert()
#include <iterator> // make_move_iterator()

#include <libbutl/ft/lang.hxx>

#include <libbutl/utility.mxx> // casecmp(), reverse_iterate(), etc

#include <libbutl/filesystem.mxx>

#include <bdep/types.hxx>
#include <bdep/version.hxx>

namespace bdep
{
  using std::move;
  using std::forward;
  using std::declval;

  using std::make_pair;
  using std::make_shared;
  using std::make_move_iterator;
  using std::to_string;

  // <libbutl/utility.mxx>
  //
  using butl::casecmp;
  using butl::reverse_iterate;

  using butl::exception_guard;
  using butl::make_exception_guard;

  // <libbutl/filesystem.mxx>
  //
  using butl::auto_rmfile;
  using butl::auto_rmdir;

  // Empty string and path.
  //
  extern const string empty_string;
  extern const path empty_path;
  extern const dir_path empty_dir_path;

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

  // Directory extracted from argv[0] (i.e., this process' recall directory)
  // or empty if there is none. Can be used as a search fallback.
  //
  extern dir_path exec_dir;
}

#endif // BDEP_UTILITY_HXX
