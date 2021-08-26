// file      : bdep/status.cxx -*- C++ -*-
// license   : MIT; see accompanying LICENSE file

#include <bdep/status.hxx>

#include <iostream> // cout

#include <bdep/project.hxx>
#include <bdep/database.hxx>
#include <bdep/diagnostics.hxx>

#include <bdep/fetch.hxx>

using namespace std;

namespace bdep
{
  static void
  cmd_status (const cmd_status_options& o,
              const dir_path& prj,
              const dir_path& cfg,
              const strings& pkgs,
              bool fetch)
  {
    // Shallow fetch the project to make sure we show latest iterations and
    // pick up any new repositories.
    //
    // We do it in a separate command for the same reason as in sync.
    //
    if (fetch)
      run_bpkg (3,
                o,
                "fetch",
                "-d", cfg,
                "--shallow",
                repository_name (prj));

    // Don't show the hold status since the only packages that will normally
    // be held are the project's. But do show dependency constraints.
    //
    run_bpkg (2,
              o,
              "status",
              "-d", cfg,
              "--no-hold",
              "--constraint",
              (o.old_available () ? "--old-available" : nullptr),
              (o.immediate () ? "--immediate" :
               o.recursive () ? "--recursive" : nullptr),
              pkgs);
  }

  int
  cmd_status (const cmd_status_options& o, cli::scanner& args)
  {
    tracer trace ("status");

    if (o.immediate () && o.recursive ())
      fail << "both --immediate|-i and --recursive|-r specified";

    // We have two pretty different modes: project package status and
    // dependency package status (have arguments).
    //
    strings dep_pkgs;
    for (; args.more (); dep_pkgs.push_back (args.next ())) ;

    // The same ignore/load story as in sync.
    //
    project_packages pp (
      find_project_packages (o,
                             !dep_pkgs.empty () /* ignore_packages */,
                             false              /* load_packages   */));

    const dir_path& prj (pp.project);

    database db (open (prj, trace));

    configurations cfgs;
    {
      transaction t (db.begin ());
      pair<configurations, bool> cs (find_configurations (o, prj, t));
      t.commit ();

      // If specified, verify packages are present in at least one
      // configuration.
      //
      if (!pp.packages.empty ())
        verify_project_packages (pp, cs);

      cfgs = move (cs.first);
    }

    // Print status in each configuration, skipping those where no package
    // statuses needs to be printed.
    //
    bool first (true);
    for (const shared_ptr<configuration>& c: cfgs)
    {
      // Collect the packages to print, unless the dependency packages are
      // specified.
      //
      // If no packages were explicitly specified, then we print the status
      // for all that have been initialized in the configuration. Otherwise,
      // only for specified packages initialized in the (specified)
      // configurations.
      //
      strings pkgs;

      if (dep_pkgs.empty ())
      {
        const package_locations& ps (pp.packages);
        bool all (ps.empty ());

        for (const package_state& s: c->packages)
        {
          if (all ||
              find_if (ps.begin (),
                       ps.end (),
                       [&s] (const package_location& p)
                       {
                         return p.name == s.name;
                       }) != ps.end ())
            pkgs.push_back (s.name.string ());
        }
      }

      // If we are printing multiple configurations, separate them with a
      // blank line and print the configuration name/directory.
      //
      if (verb && cfgs.size () > 1)
      {
        cout << (first ? "" : "\n")
             << "in configuration " << *c << ':' << endl;

        first = false;
      }

      if (c->packages.empty () || (pkgs.empty () && dep_pkgs.empty ()))
      {
        if (verb)
        {
          diag_record dr (info);

          if (c->packages.empty ())
            dr << "no packages ";
          else
            dr << "none of specified packages ";

          dr << "initialized in configuration " << *c << ", skipping";
        }

        continue;
      }

      bool fetch (o.fetch () || o.fetch_full ());

      if (fetch)
        cmd_fetch (o, prj, c, o.fetch_full ());

      // Status for either packages or their dependencies must be printed, but
      // not for both.
      //
      assert (pkgs.empty () == !dep_pkgs.empty ());

      cmd_status (o, prj, c->path, !pkgs.empty () ? pkgs : dep_pkgs, !fetch);
    }

    return 0;
  }
}
