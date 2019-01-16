// file      : bdep/new-parsers.hxx -*- C++ -*-
// copyright : Copyright (c) 2014-2019 Code Synthesis Ltd
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

    template <typename T>
    struct parser;

    template <>
    struct parser<cmd_new_type>
    {
      static void
      parse (cmd_new_type&, bool&, scanner&);
    };

    template <>
    struct parser<cmd_new_lang>
    {
      static void
      parse (cmd_new_lang&, bool&, scanner&);
    };

    template <>
    struct parser<cmd_new_vcs>
    {
      static void
      parse (cmd_new_vcs&, bool&, scanner&);
    };
  }
}

#endif // BDEP_NEW_PARSERS_HXX
