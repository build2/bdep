// file      : bdep/publish.hxx -*- C++ -*-
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
