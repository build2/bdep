// file      : bdep/config.cxx -*- C++ -*-
// copyright : Copyright (c) 2014-2017 Code Synthesis Ltd
// license   : MIT; see accompanying LICENSE file

#include <bdep/config.hxx>

#include <bdep/diagnostics.hxx>

using namespace std;

namespace bdep
{
  int
  cmd_config (const cmd_config_options& o, cli::scanner& args)
  {
    //@@ TODO: get subcommand and pass to tracer.

    tracer trace ("config");

    //@@ TODO: validate project/config options for subcommands.

    for (const string& n: o.config_name ())
      text << n;

    return 0;
  }
}
