// file      : bdep/config.hxx -*- C++ -*-
// copyright : Copyright (c) 2014-2017 Code Synthesis Ltd
// license   : MIT; see accompanying LICENSE file

#ifndef BDEP_CONFIG_HXX
#define BDEP_CONFIG_HXX

#include <bdep/types.hxx>
#include <bdep/utility.hxx>

#include <bdep/config-options.hxx>

namespace bdep
{
  int
  cmd_config (const cmd_config_options&, cli::scanner& args);
}

#endif // BDEP_CONFIG_HXX
