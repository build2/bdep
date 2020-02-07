// file      : bdep/new-parsers.hxx -*- C++ -*-
// license   : MIT; see accompanying LICENSE file

// CLI parsers, included into the generated source files.
//

#ifndef BDEP_NEW_PARSERS_HXX
#define BDEP_NEW_PARSERS_HXX

#include <bdep/new-types.hxx>

namespace bdep
{
  namespace cli
  {
    class scanner;

    // Note that these parsers merge parameter-specific options rather
    // than overwriting them (see new-types.hxx for details).
    //
    template <typename T>
    struct parser;

    template <>
    struct parser<cmd_new_type>
    {
      static void
      parse (cmd_new_type&, bool&, scanner&);

      static void
      merge (cmd_new_type&, const cmd_new_type&);
    };

    template <>
    struct parser<cmd_new_lang>
    {
      static void
      parse (cmd_new_lang&, bool&, scanner&);

      static void
      merge (cmd_new_lang&, const cmd_new_lang&);
    };

    template <>
    struct parser<cmd_new_vcs>
    {
      static void
      parse (cmd_new_vcs&, bool&, scanner&);

      static void
      merge (cmd_new_vcs&, const cmd_new_vcs&);
    };
  }
}

#endif // BDEP_NEW_PARSERS_HXX
