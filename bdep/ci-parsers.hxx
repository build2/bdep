// file      : bdep/ci-parsers.hxx -*- C++ -*-
// copyright : Copyright (c) 2014-2019 Code Synthesis Ltd
// license   : MIT; see accompanying LICENSE file

// CLI parsers, included into the generated source files.
//

#ifndef BDEP_CI_PARSERS_HXX
#define BDEP_CI_PARSERS_HXX

#include <bdep/ci-types.hxx>

namespace bdep
{
  namespace cli
  {
    class scanner;

    template <typename T>
    struct parser;

    // Accumulate package manifest override values from all the override-
    // related options (--override, --overrides-file, etc), preserving their
    // order. Translate each option into one or more values according to the
    // option-specific semantics.
    //
    template <>
    struct parser<ci_override>
    {
      static void
      parse (ci_override&, bool&, scanner&);
    };
  }
}

#endif // BDEP_CI_PARSERS_HXX
