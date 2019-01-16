// file      : bdep/release.hxx -*- C++ -*-
// copyright : Copyright (c) 2014-2019 Code Synthesis Ltd
// license   : MIT; see accompanying LICENSE file

#ifndef BDEP_RELEASE_HXX
#define BDEP_RELEASE_HXX

#include <bdep/types.hxx>
#include <bdep/utility.hxx>

#include <bdep/release-options.hxx>

namespace bdep
{
  int
  cmd_release (const cmd_release_options&, cli::scanner& args);
}

#endif // BDEP_RELEASE_HXX
