// file      : bdep/ci.hxx -*- C++ -*-
// license   : MIT; see accompanying LICENSE file

#ifndef BDEP_CI_HXX
#define BDEP_CI_HXX

#include <bdep/types.hxx>
#include <bdep/utility.hxx>

#include <bdep/ci-options.hxx>

namespace bdep
{
  int
  cmd_ci (const cmd_ci_options&, cli::scanner& args);
}

#endif // BDEP_CI_HXX
