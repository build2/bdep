// file      : bdep/new.hxx -*- C++ -*-
// copyright : Copyright (c) 2014-2018 Code Synthesis Ltd
// license   : MIT; see accompanying LICENSE file

#ifndef BDEP_NEW_HXX
#define BDEP_NEW_HXX

#include <bdep/types.hxx>
#include <bdep/utility.hxx>

#include <bdep/new-options.hxx>

namespace bdep
{
  int
  cmd_new (const cmd_new_options&, cli::group_scanner& args);
}

#endif // BDEP_NEW_HXX
