// file      : bdep/types-parsers.hxx -*- C++ -*-
// copyright : Copyright (c) 2014-2018 Code Synthesis Ltd
// license   : MIT; see accompanying LICENSE file

// CLI parsers, included into the generated source files.
//

#ifndef BDEP_TYPES_PARSERS_HXX
#define BDEP_TYPES_PARSERS_HXX

#include <bdep/types.hxx>
#include <bdep/options-types.hxx>

namespace bdep
{
  namespace cli
  {
    class scanner;

    template <typename T>
    struct parser;

    template <>
    struct parser<url>
    {
      static void
      parse (url&, bool&, scanner&);
    };

    template <>
    struct parser<path>
    {
      static void
      parse (path&, bool&, scanner&);
    };

    template <>
    struct parser<dir_path>
    {
      static void
      parse (dir_path&, bool&, scanner&);
    };
  }
}

#endif // BDEP_TYPES_PARSERS_HXX
