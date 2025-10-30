// file      : bdep/options-types.hxx -*- C++ -*-
// license   : MIT; see accompanying LICENSE file

#ifndef BDEP_OPTIONS_TYPES_HXX
#define BDEP_OPTIONS_TYPES_HXX

#include <bdep/types.hxx>

namespace bdep
{
  enum class stdout_format
  {
    lines,
    json
  };

  enum class sqlite_synchronous
  {
    off,
    normal,
    full,
    extra
  };

  // Return nullopt if the passed string is not a valid --sqlite-synchronous
  // option value.
  //
  optional<sqlite_synchronous>
  to_sqlite_synchronous (const string&);

  string
  to_string (sqlite_synchronous);
}

#endif // BDEP_OPTIONS_TYPES_HXX
