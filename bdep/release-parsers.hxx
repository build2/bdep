// file      : bdep/release-parsers.hxx -*- C++ -*-
// license   : MIT; see accompanying LICENSE file

// CLI parsers, included into the generated source files.
//

#ifndef BDEP_RELEASE_PARSERS_HXX
#define BDEP_RELEASE_PARSERS_HXX

#include <bdep/release-types.hxx>

namespace bdep
{
  namespace cli
  {
    class scanner;

    template <typename T>
    struct parser;

    template <>
    struct parser<cmd_release_current_tag>
    {
      static void
      parse (cmd_release_current_tag&, bool&, scanner&);

      static void
      merge (cmd_release_current_tag& b, const cmd_release_current_tag& a)
      {
        b = a;
      }
    };
  }
}

#endif // BDEP_RELEASE_PARSERS_HXX
