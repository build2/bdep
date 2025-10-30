// file      : bdep/options-types.cxx -*- C++ -*-
// license   : MIT; see accompanying LICENSE file

#include <bdep/options-types.hxx>

#include <bdep/utility.hxx>

namespace bdep
{
  optional<sqlite_synchronous>
  to_sqlite_synchronous (const string& v)
  {
    if      (icasecmp (v, "OFF")    == 0) return sqlite_synchronous::off;
    else if (icasecmp (v, "NORMAL") == 0) return sqlite_synchronous::normal;
    else if (icasecmp (v, "FULL")   == 0) return sqlite_synchronous::full;
    else if (icasecmp (v, "EXTRA")  == 0) return sqlite_synchronous::extra;
    else                                  return nullopt;
  }

  string
  to_string (sqlite_synchronous sync)
  {
    switch (sync)
    {
    case sqlite_synchronous::off:    return "OFF";
    case sqlite_synchronous::normal: return "NORMAL";
    case sqlite_synchronous::full:   return "FULL";
    case sqlite_synchronous::extra:  return "EXTRA";
    }

    assert (false); // Can't be here.
    return string ();
  }
}
