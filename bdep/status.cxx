// file      : bdep/status.cxx -*- C++ -*-
// copyright : Copyright (c) 2014-2017 Code Synthesis Ltd
// license   : MIT; see accompanying LICENSE file

#include <bdep/status.hxx>

#include <bdep/project.hxx>
#include <bdep/project-odb.hxx>
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
              const cstrings& pkgs,
              bool fetch)
  {
    // Shallow fetch the project to make sure we show latest iterations and
    // pick up any new repositories.
    //
    // We do it in a separate command for the same reason as in sync.
    //
    if (fetch)
      run_bpkg (o,
                "fetch",
                "-d", cfg,
                "--shallow",
                "dir:" + prj.string ());

    // Don't show the hold status since the only packages that will normally
    // be held are the project's. But do show dependency constraints.
    //
    run_bpkg (o,
              "status",
              "-d", cfg,
              "--no-hold",
              "--constraint",
              (o.old_available () ? "--old-available" : nullptr),
              (o.immediate () ? "--immediate" :
               o.recursive () ? "--recursive" :
               nullptr),
              pkgs);
  }

  static void
  cmd_status (const cmd_status_options& o,
              const dir_path& prj,
              const shared_ptr<configuration>& c,
              const package_locations& ps,
              bool fetch)
  {
    assert (!c->packages.empty ());

    // If no packages were explicitly specified, then we print the status for
    // all that have been initialized in the configuration.
    //
    cstrings pkgs;

    if (ps.empty ())
    {
      for (const package_state& p: c->packages)
        pkgs.push_back (p.name.c_str ());
    }
    else
    {
      for (const package_location& p: ps)
        pkgs.push_back (p.name.c_str ());
    }

    cmd_status (o, prj, c->path, pkgs, fetch);
  }

  int
  cmd_status (const cmd_status_options& o, cli::scanner& args)
  {
    tracer trace ("status");

    if (o.immediate () && o.recursive ())
      fail << "both --immediate|-i and --recursive|-r specified";

    // We have two pretty different modes: project status and dependency
    // status (have arguments).
    //
    cstrings pkgs;
    for (; args.more (); pkgs.push_back (args.next ())) ;

    // For the project status the same story as in sync.
    //
    project_packages pp (
      find_project_packages (o,
                             !pkgs.empty () /* ignore_packages */,
                             false          /* load_packages */));

    const dir_path& prj (pp.project);

    database db (open (prj, trace));

    transaction t (db.begin ());
    configurations cfgs (find_configurations (prj, t, o));
    t.commit ();

    // If specified, verify packages are present in each configuration.
    //
    if (!pp.packages.empty ())
      verify_project_packages (pp, cfgs);

    // Print status in each configuration skipping empty ones.
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

      // If we are printing multiple configurations, separate them with a
      // blank line and print the configuration name/directory.
      //
      if (verb && cfgs.size () > 1)
      {
        text << (first ? "" : "\n")
             << "status in configuration " << *c;

        first = false;
      }

      bool fetch (o.fetch () || o.fetch_full ());

      if (fetch)
        cmd_fetch (o, prj, c, o.fetch_full ());

      // Don't re-fetch if we just fetched.
      //
      if (pkgs.empty ())
        cmd_status (o, prj, c, pp.packages, !fetch);
      else
        cmd_status (o, prj, c->path, pkgs, !fetch);
    }

    return 0;
  }
}
