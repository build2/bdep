// file      : bdep/help.cxx -*- C++ -*-
// license   : MIT; see accompanying LICENSE file

#include <bdep/help.hxx>

#include <libbutl/pager.hxx>

#include <bdep/diagnostics.hxx>
#include <bdep/bdep-options.hxx>

// Help topics.
//
#include <bdep/projects-configs.hxx>
#include <bdep/argument-grouping.hxx>
#include <bdep/default-options-files.hxx>

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
      else if (t == "argument-grouping")
        usage = &print_bdep_argument_grouping_usage;
      else if (t == "default-options-files")
        usage = &print_bdep_default_options_files_usage;
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

  default_options_files
  options_files (const char*, const help_options&, const strings&)
  {
    // bdep.options
    // bdep-help.options

    return default_options_files {
      {path ("bdep.options"), path ("bdep-help.options")},
      nullopt /* start */};
  }

  help_options
  merge_options (const default_options<help_options>& defs,
                 const help_options& cmd)
  {
    return merge_default_options (defs, cmd);
  }
}
