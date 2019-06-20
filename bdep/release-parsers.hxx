// file      : bdep/release-parsers.hxx -*- C++ -*-
// copyright : Copyright (c) 2014-2019 Code Synthesis Ltd
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
    };
  }
}

#endif // BDEP_RELEASE_PARSERS_HXX
