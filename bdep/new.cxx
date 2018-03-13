// file      : bdep/new.cxx -*- C++ -*-
// copyright : Copyright (c) 2014-2017 Code Synthesis Ltd
// license   : MIT; see accompanying LICENSE file

#include <bdep/new.hxx>

#include <bdep/diagnostics.hxx>

using namespace std;

namespace bdep
{
  using type = cmd_new_type;
  using lang = cmd_new_lang;

  int
  cmd_new (const cmd_new_options& o, cli::scanner& args)
  {
    tracer trace ("new");

    //@@ TODO: validate options (cpp/cxx, -A/-C, etc).

    return 0;
  }
}
