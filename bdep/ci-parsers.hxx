// file      : bdep/ci-parsers.hxx -*- C++ -*-
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
    struct parser<cmd_ci_override>
    {
      static void
      parse (cmd_ci_override&, bool&, scanner&);

      static void
      merge (cmd_ci_override&, const cmd_ci_override&);
    };
  }
}

#endif // BDEP_CI_PARSERS_HXX
