// file      : bdep/init.cxx -*- C++ -*-
// copyright : Copyright (c) 2014-2017 Code Synthesis Ltd
// license   : MIT; see accompanying LICENSE file

#include <bdep/init.hxx>

#include <bdep/project.hxx>
#include <bdep/diagnostics.hxx>

using namespace std;

namespace bdep
{
  int
  cmd_init (const cmd_init_options& o, cli::scanner& args)
  {
    tracer trace ("init");

    //@@ TODO: validate project/config options for sub-modes.
    //@@ TODO: print project/package(s) being initialized.

    project_packages pp (
      find_project_packages (o,
                             o.empty () /* ignore_packages */));

    text << pp.project;

    for (const dir_path& d: pp.packages)
      text << "  " << (pp.project / d);

    return 0;
  }
}
