// file      : bdep/deinit.hxx -*- C++ -*-
// copyright : Copyright (c) 2014-2017 Code Synthesis Ltd
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
