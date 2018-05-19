// file      : bdep/help.cxx -*- C++ -*-
// copyright : Copyright (c) 2014-2018 Code Synthesis Ltd
// license   : MIT; see accompanying LICENSE file

#include <bdep/help.hxx>

#include <libbutl/pager.mxx>

#include <bdep/diagnostics.hxx>
#include <bdep/bdep-options.hxx>

// Help topics.
//
#include <bdep/projects-configs.hxx>

using namespace std;
using namespace butl;

namespace bdep
{
  int
  help (const help_options& o, const string& t, usage_function* usage)
  {
    if (usage == nullptr) // Not a command.
    {
      if (t.empty ())             // General help.
        usage = &print_bdep_usage;
      //
      // Help topics.
      //
      else if (t == "common-options")
        usage = &print_bdep_common_options_long_usage;
      else if (t == "projects-configs")
        usage = &print_bdep_projects_configs_usage;
      else
        fail << "unknown bdep command/help topic '" << t << "'" <<
          info << "run 'bdep help' for more information";
    }

    try
    {
      pager p ("bdep " + (t.empty () ? "help" : t),
               verb >= 2,
               o.pager_specified () ? &o.pager () : nullptr,
               &o.pager_option ());

      usage (p.stream (), cli::usage_para::none);

      // If the pager failed, assume it has issued some diagnostics.
      //
      return p.wait () ? 0 : 1;
    }
    // Catch io_error as std::system_error together with the pager-specific
    // exceptions.
    //
    catch (const system_error& e)
    {
      error << "pager failed: " << e;

      // Fall through.
    }

    throw failed ();
  }
}
