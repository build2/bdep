// file      : bdep/init.hxx -*- C++ -*-
// copyright : Copyright (c) 2014-2017 Code Synthesis Ltd
// license   : MIT; see accompanying LICENSE file

#ifndef BDEP_INIT_HXX
#define BDEP_INIT_HXX

#include <bdep/types.hxx>
#include <bdep/utility.hxx>

#include <bdep/init-options.hxx>

namespace bdep
{
  int
  cmd_init (const cmd_init_options&, cli::scanner& args);
}

#endif // BDEP_INIT_HXX
