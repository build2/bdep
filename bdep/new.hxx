// file      : bdep/new.hxx -*- C++ -*-
// license   : MIT; see accompanying LICENSE file

#ifndef BDEP_NEW_HXX
#define BDEP_NEW_HXX

#include <bdep/types.hxx>
#include <bdep/utility.hxx>

#include <bdep/new-options.hxx>

namespace bdep
{
  int
  cmd_new (cmd_new_options&&, cli::group_scanner& args);

  default_options_files
  options_files (const char* cmd, const cmd_new_options&, const strings& args);

  cmd_new_options
  merge_options (const default_options<cmd_new_options>&,
                 const cmd_new_options&);
}

#endif // BDEP_NEW_HXX
