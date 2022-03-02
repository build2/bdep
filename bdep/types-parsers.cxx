// file      : bdep/types-parsers.cxx -*- C++ -*-
// license   : MIT; see accompanying LICENSE file

#include <bdep/types-parsers.hxx>

#include <bdep/common-options.hxx> // bdep::cli namespace

namespace bdep
{
  namespace cli
  {
    void parser<url>::
    parse (url& x, bool& xs, scanner& s)
    {
      const char* o (s.next ());

      if (!s.more ())
        throw missing_value (o);

      const char* v (s.next ());

      try
      {
        x = url (v);
      }
      catch (const invalid_argument& e)
      {
        throw invalid_value (o, v, e.what ());
      }

      xs = true;
    }

    template <typename T>
    static void
    parse_path (T& x, scanner& s)
    {
      const char* o (s.next ());

      if (!s.more ())
        throw missing_value (o);

      const char* v (s.next ());

      try
      {
        x = T (v);

        if (x.empty ())
          throw invalid_value (o, v);
      }
      catch (const invalid_path&)
      {
        throw invalid_value (o, v);
      }
    }

    void parser<path>::
    parse (path& x, bool& xs, scanner& s)
    {
      xs = true;
      parse_path (x, s);
    }

    void parser<dir_path>::
    parse (dir_path& x, bool& xs, scanner& s)
    {
      xs = true;
      parse_path (x, s);
    }

    void parser<stdout_format>::
    parse (stdout_format& r, bool& xs, scanner& s)
    {
      const char* o (s.next ());

      if (!s.more ())
        throw missing_value (o);

      string v (s.next ());

      if      (v == "lines") r = stdout_format::lines;
      else if (v == "json")  r = stdout_format::json;
      else throw invalid_value (o, v);

      xs = true;
    }
  }
}
