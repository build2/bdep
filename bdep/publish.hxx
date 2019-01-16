// file      : bdep/publish.hxx -*- C++ -*-
// copyright : Copyright (c) 2014-2019 Code Synthesis Ltd
// license   : MIT; see accompanying LICENSE file

#ifndef BDEP_PUBLISH_HXX
#define BDEP_PUBLISH_HXX

#include <bdep/types.hxx>
#include <bdep/utility.hxx>

#include <bdep/publish-options.hxx>

namespace bdep
{
  int
  cmd_publish (const cmd_publish_options&, cli::scanner& args);
}

#endif // BDEP_PUBLISH_HXX
