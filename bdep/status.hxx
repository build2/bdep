// file      : bdep/status.hxx -*- C++ -*-
// license   : MIT; see accompanying LICENSE file

#ifndef BDEP_STATUS_HXX
#define BDEP_STATUS_HXX

#include <bdep/types.hxx>
#include <bdep/utility.hxx>

#include <bdep/status-options.hxx>

namespace bdep
{
  int
  cmd_status (const cmd_status_options&, cli::scanner& args);
}

#endif // BDEP_STATUS_HXX
