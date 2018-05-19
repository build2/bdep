// file      : bdep/help.hxx -*- C++ -*-
// copyright : Copyright (c) 2014-2018 Code Synthesis Ltd
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
}

#endif // BDEP_HELP_HXX
