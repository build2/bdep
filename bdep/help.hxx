// file      : bdep/help.hxx -*- C++ -*-
// license   : MIT; see accompanying LICENSE file

#ifndef BDEP_HELP_HXX
#define BDEP_HELP_HXX

#include <bdep/types.hxx>
#include <bdep/utility.hxx>

#include <bdep/help-options.hxx>

namespace bdep
{
  using usage_function = cli::usage_para (ostream&, cli::usage_para);

  int
  help (const help_options&, const string& topic, usage_function* usage);

  default_options_files
  options_files (const char* cmd, const help_options&, const strings& args);

  help_options
  merge_options (const default_options<help_options>&, const help_options&);
}

#endif // BDEP_HELP_HXX
