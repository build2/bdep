// file      : bdep/ci-types.hxx -*- C++ -*-
// copyright : Copyright (c) 2014-2019 Code Synthesis Ltd
// license   : MIT; see accompanying LICENSE file

#ifndef BDEP_CI_TYPES_HXX
#define BDEP_CI_TYPES_HXX

#include <bdep/types.hxx>

#include <libbutl/manifest-types.mxx>

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
