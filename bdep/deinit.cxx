// file      : bdep/deinit.cxx -*- C++ -*-
// copyright : Copyright (c) 2014-2018 Code Synthesis Ltd
// license   : MIT; see accompanying LICENSE file

#include <bdep/deinit.hxx>

#include <bdep/project.hxx>
#include <bdep/project-odb.hxx>
#include <bdep/database.hxx>
#include <bdep/diagnostics.hxx>

#include <bdep/sync.hxx> // configuration_projects(), hook_file

using namespace std;

namespace bdep
{
  static void
  cmd_deinit (const cmd_deinit_options& o,
              const dir_path& prj,
              const shared_ptr<configuration>& c,
              const strings& pkgs)
  {
    const dir_path& cfg (c->path);

    // Remove auto-synchronization build system hook.
    //
    // We have to check if there are any other projects that share this
    // configuration. Note that we don't load the other project's database,
    // check if the configuration is also auto-synchronized (we assume things
    // are consistent) or that it has initialized packages (we assume it would
    // have been removed from the configuration's repositories if that were
    // the case).
    //
    if (c->auto_sync &&
        c->packages.empty () &&
        configuration_projects (o, cfg, prj).empty ())
    {
      path f (cfg / hook_file);
      if (exists (f))
        rm (f);
    }

    // Disfigure configuration forwarding.
    //
    if (c->forward)
    {
      package_locations pls (load_packages (prj));

      for (const string& n: pkgs)
      {
        dir_path out (dir_path (cfg) /= n);
        dir_path src (prj);
        {
          auto i (find_if (pls.begin (),
                           pls.end (),
                           [&n] (const package_location& pl)
                           {
                             return pl.name == n;
                           }));

          if (i == pls.end ())
            fail << "package " << n << " is not listed in " << prj;

          src /= i->path;
        }

        run_b (o,
               "disfigure:",
               src.representation () + '@' + out.representation () +
               ",forward");
      }
    }

    // Note that --keep-dependent is important: if we drop dependent packages
    // that are managed by bdep, then its view of what has been initialized
    // in the configuration will become invalid.
    //
    run_bpkg (2,
              o,
              "drop",
              "-d", cfg,
              "--keep-dependent",
              "--plan", "synchronizing:",
              "--yes",
              pkgs);
  }

  int
  cmd_deinit (const cmd_deinit_options& o, cli::scanner&)
  {
    tracer trace ("deinit");

    // The same ignore/load story as in sync.
    //
    project_packages pp (
      find_project_packages (o,
                             false /* ignore_packages */,
                             false /* load_packages   */));

    const dir_path& prj (pp.project);

    if (verb)
      text << "deinitializing in project " << prj;

    database db (open (prj, trace));

    transaction t (db.begin ());
    configurations cfgs (find_configurations (prj, t, o));
    t.commit ();

    // If specified, verify packages are present in each configuration.
    //
    if (!pp.packages.empty ())
      verify_project_packages (pp, cfgs);

    // If no packages were explicitly specified, then we deinitalize all that
    // have been initialized in each configuration.
    //
    strings pkgs;

    bool all (pp.packages.empty ());
    if (!all)
    {
      for (const package_location& p: pp.packages)
        pkgs.push_back (p.name);
    }

    // Deinitialize in each configuration skipping empty ones.
    //
    // We do each configuration in a separate transaction so that our state
    // reflects the bpkg configuration as closely as possible.
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
             << "in configuration " << *c << ':';

        first = false;
      }

      transaction t (db.begin ());

      // Collect packages to drop and remove them from the configuration.
      //
      if (all)
      {
        pkgs.clear ();

        for (const package_state& p: c->packages)
          pkgs.push_back (p.name);
      }

      c->packages.erase (
        remove_if (c->packages.begin (),
                   c->packages.end (),
                   [&pkgs] (const package_state& p)
                   {
                     return find_if (pkgs.begin (),
                                     pkgs.end (),
                                     [&p] (const string& n)
                                     {
                                       return p.name == n;
                                     }) != pkgs.end ();
                   }),
        c->packages.end ());

      // If we are deinitializing multiple packages, print their names.
      //
      if (verb && pkgs.size () > 1)
      {
        for (const string& n: pkgs)
          text << "deinitializing package " << n;
      }

      // The same story as in init with regard to the state update order.
      //
      cmd_deinit (o, prj, c, pkgs);

      db.update (c);
      t.commit ();

      // Remove our repository from the configuration if we have no more
      // packages that are initialized in it.
      //
      if (c->packages.empty ())
        run_bpkg (3,
                  o,
                  "remove",
                  "-d", c->path,
                  "dir:" + prj.string ());
    }

    return 0;
  }
}
