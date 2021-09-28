// file      : bdep/ci-types.hxx -*- C++ -*-
// license   : MIT; see accompanying LICENSE file

#ifndef BDEP_CI_TYPES_HXX
#define BDEP_CI_TYPES_HXX

#include <bdep/types.hxx>

#include <libbutl/manifest-types.hxx>

namespace bdep
{
  // This type is intended for accumulating package manifest override values
  // from all the override-related options (see ci-parsers.hxx for details).
  //
  class cmd_ci_override: public vector<butl::manifest_name_value>
  {
  };
}

#endif // BDEP_CI_TYPES_HXX
