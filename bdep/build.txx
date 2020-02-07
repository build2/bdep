// file      : bdep/build.txx -*- C++ -*-
// license   : MIT; see accompanying LICENSE file

#include <cstring> // strchr()

#include <bdep/project.hxx>
#include <bdep/database.hxx>
#include <bdep/diagnostics.hxx>

#include <bdep/sync.hxx>

namespace bdep
{
  template <typename O>
  int
  cmd_build (const O& o,
             void (*build) (const O&,
                            const shared_ptr<configuration>&,
                            const cstrings&,
                            const strings&),
             cli::scanner& args)
  {
    tracer trace ("build");

    // Save cfg-vars with some sanity checking.
    //
    strings cfg_vars;
    while (args.more ())
    {
      const char* a (args.next ());

      if (strchr (a , '=') == nullptr)
        fail << "'" << a << "' does not look like a variable assignment";

      cfg_vars.push_back (a);
    }

    // The same ignore/load story as in sync.
    //
    project_packages pp (
      find_project_packages (o,
                             false /* ignore_packages */,
                             false /* load_packages   */));

    const dir_path& prj (pp.project);

    // Load the configurations without keeping the database open longer
    // than necessary.
    //
    configurations cfgs;
    {
      database db (open (prj, trace));

      transaction t (db.begin ());
      cfgs = find_configurations (o, prj, t);
      t.commit ();
    }

    // If specified, verify packages are present in each configuration.
    //
    if (!pp.packages.empty ())
      verify_project_packages (pp, cfgs);

    // If no packages were explicitly specified, then we build all that have
    // been initialized in each configuration.
    //
    cstrings pkgs;

    bool all (pp.packages.empty ());
    if (!all)
    {
      for (const package_location& p: pp.packages)
        pkgs.push_back (p.name.string ().c_str ());
    }

    // Build in each configuration skipping empty ones.
    //
    bool first (true);
    for (const shared_ptr<configuration>& c: cfgs)
    {
      if (c->packages.empty ())
      {
        if (verb)
          info << "skipping empty configuration " << *c;

        continue;
      }

      // Collect packages.
      //
      if (all)
      {
        pkgs.clear ();

        for (const package_state& p: c->packages)
          pkgs.push_back (p.name.string ().c_str ());
      }

      // If we are printing multiple configurations, separate them with a
      // blank line and print the configuration name/directory.
      //
      if (verb && cfgs.size () > 1)
      {
        text << (first ? "" : "\n")
             << "in configuration " << *c << ':';

        first = false;
      }

      // Pre-sync the configuration to avoid triggering the build system
      // hook (see sync for details).
      //
      cmd_sync (o, prj, c, strings () /* pkg_args */, true /* implicit */);

      build (o, c, pkgs, cfg_vars);
    }

    return 0;
  }
}
