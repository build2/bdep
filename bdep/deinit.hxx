// file      : bdep/deinit.hxx -*- C++ -*-
// license   : MIT; see accompanying LICENSE file

#ifndef BDEP_DEINIT_HXX
#define BDEP_DEINIT_HXX

#include <bdep/types.hxx>
#include <bdep/utility.hxx>

#include <bdep/deinit-options.hxx>

namespace bdep
{
  int
  cmd_deinit (const cmd_deinit_options&, cli::scanner& args);
}

#endif // BDEP_DEINIT_HXX
