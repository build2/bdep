// file      : bdep/build.hxx -*- C++ -*-
// license   : MIT; see accompanying LICENSE file

#ifndef BDEP_BUILD_HXX
#define BDEP_BUILD_HXX

#include <bdep/types.hxx>
#include <bdep/utility.hxx>

namespace bdep
{
  // Common "build system command" (update, clean, test) implementation.
  //
  template <typename O>
  int
  cmd_build (const O& o,
             void (*build) (const O&,
                            const shared_ptr<configuration>&,
                            const cstrings&,
                            const strings&),
             cli::scanner& args);
}

#include <bdep/build.txx>

#endif // BDEP_BUILD_HXX
