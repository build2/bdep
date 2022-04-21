// file      : bdep/deinit.cxx -*- C++ -*-
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
    bool force (o.force ());
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
    if (!force               &&
        c->auto_sync         &&
        c->packages.empty () &&
        configuration_projects (o, cfg, prj).empty ())
    {
      path f (cfg / hook_file);
      if (exists (f))
        rm (f);
    }

    // Disfigure configuration forwarding. Note that we have to do this even
    // if forced.
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

        // See sync for details on --no-external-modules.
        //
        run_b (o,
               "--no-external-modules",
               "disfigure:",
               '\'' + src.representation () + "'@'" + out.representation () +
               "',forward");
      }
    }

    // Note that --keep-dependent is important: if we drop dependent packages
    // that are managed by bdep, then its view of what has been initialized
    // in the configuration will become invalid.
    //
    if (!force)
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

    bool force (o.force ());

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

    configurations cfgs;
    {
      transaction t (db.begin ());
      pair<configurations, bool> cs (
        find_configurations (o,
                             prj,
                             t,
                             true   /* fallback_default */,
                             !force /* validate */));
      t.commit ();

      // If specified, verify packages are present in at least one
      // configuration.
      //
      if (!pp.packages.empty ())
        verify_project_packages (pp, cs);

      cfgs = move (cs.first);

      // If this is fallback to default configurations, then reverse them:
      // this adds a bit of magic to typical tool/module development setups
      // where we normally create/initialize the host/build2 configuration
      // first and which needs to be deinitialized last.
      //
      // @@ TODO: maybe we should deinitialize/sync them all at once if they
      //    belong to the same configuration cluster?
      //
      if (cs.second)
        reverse (cfgs.begin (), cfgs.end ());
    }

    // If no packages were explicitly specified, then we deinitalize all that
    // have been initialized in each configuration. Otherwise, we deinitalize
    // only specified packages initialized in the (specified) configurations.
    //
    const package_locations& pkgs (pp.packages);

    bool all (pkgs.empty ());

    // Deinitialize in each configuration, skipping those where no packages
    // needs to be deinitialized.
    //
    // We do each configuration in a separate transaction so that our state
    // reflects the bpkg configuration as closely as possible.
    //
    bool first (true);
    for (const shared_ptr<configuration>& c: cfgs)
    {
      // Collect packages to deinitialize.
      //
      strings ps;

      for (const package_state& s: c->packages)
      {
        if (all ||
            find_if (pkgs.begin (),
                     pkgs.end (),
                     [&s] (const package_location& p)
                     {
                       return p.name == s.name;
                     }) != pkgs.end ())
          ps.push_back (s.name.string ());
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

      if (ps.empty ())
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

      transaction t (db.begin ());

      // Remove collected packages from the configuration.
      //
      c->packages.erase (
        remove_if (c->packages.begin (),
                   c->packages.end (),
                   [&ps] (const package_state& p)
                   {
                     return find_if (ps.begin (),
                                     ps.end (),
                                     [&p] (const string& n)
                                     {
                                       return p.name == n;
                                     }) != ps.end ();
                   }),
        c->packages.end ());

      // If we are deinitializing multiple packages, print their names.
      //
      if (verb && ps.size () > 1)
      {
        for (const string& n: ps)
          text << "deinitializing package " << n;
      }

      // The same story as in init with regard to the state update order.
      //
      cmd_deinit (o, prj, c, ps);

      db.update (c);
      t.commit ();

      // Remove our repository from the configuration if we have no more
      // packages that are initialized in it.
      //
      if (!force && c->packages.empty ())
        run_bpkg (3,
                  o,
                  "remove",
                  "-d", c->path,
                  repository_name (prj));
    }

    return 0;
  }
}
