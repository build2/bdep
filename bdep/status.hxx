// file      : bdep/status.hxx -*- C++ -*-
// copyright : Copyright (c) 2014-2018 Code Synthesis Ltd
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
