// file      : bdep/init.cxx -*- C++ -*-
// copyright : Copyright (c) 2014-2017 Code Synthesis Ltd
// license   : MIT; see accompanying LICENSE file

#include <bdep/init.hxx>

#include <bdep/diagnostics.hxx>

using namespace std;

namespace bdep
{
  int
  cmd_init (const cmd_init_options& o, cli::scanner& args)
  {
    tracer trace ("init");

    //@@ TODO: validate project/config options for subcommands.

    for (const string& n: o.config_name ())
      text << n;

    return 0;
  }
}
