// file      : bdep/release-parsers.cxx -*- C++ -*-
// license   : MIT; see accompanying LICENSE file

#include <bdep/release-parsers.hxx>

#include <bdep/release-options.hxx> // bdep::cli namespace

namespace bdep
{
  namespace cli
  {
    void parser<cmd_release_current_tag>::
    parse (cmd_release_current_tag& r, bool& xs, scanner& s)
    {
      const char* o (s.next ());

      if (!s.more ())
        throw missing_value (o);

      string v (s.next ());

      if      (v == "keep")   r = cmd_release_current_tag::keep;
      else if (v == "update") r = cmd_release_current_tag::update;
      else if (v == "remove") r = cmd_release_current_tag::remove;
      else throw invalid_value (o, v);

      xs = true;
    }
  }
}
