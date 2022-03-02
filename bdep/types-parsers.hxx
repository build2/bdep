// file      : bdep/types-parsers.hxx -*- C++ -*-
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

      static void
      merge (url& b, const url& a) {b = a;}
    };

    template <>
    struct parser<path>
    {
      static void
      parse (path&, bool&, scanner&);

      static void
      merge (path& b, const path& a) {b = a;}
    };

    template <>
    struct parser<dir_path>
    {
      static void
      parse (dir_path&, bool&, scanner&);

      static void
      merge (dir_path& b, const dir_path& a) {b = a;}
    };

    template <>
    struct parser<stdout_format>
    {
      static void
      parse (stdout_format&, bool&, scanner&);

      static void
      merge (stdout_format& b, const stdout_format& a)
      {
        b = a;
      }
    };
  }
}

#endif // BDEP_TYPES_PARSERS_HXX
