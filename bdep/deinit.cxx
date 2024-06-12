// file      : bdep/deinit.cxx -*- C++ -*-
// license   : MIT; see accompanying LICENSE file

#include <bdep/deinit.hxx>

#include <bdep/project.hxx>
#include <bdep/project-odb.hxx>
#include <bdep/database.hxx>
#include <bdep/diagnostics.hxx>

#include <bdep/sync.hxx>
#include <bdep/fetch.hxx>
#include <bdep/config.hxx>

using namespace std;

namespace bdep
{
  static void
  cmd_deinit (const cmd_deinit_options& o,
              const dir_path& prj,
              const shared_ptr<configuration>& c,
              const strings& pkgs,
              transaction& t,
              vector<pair<dir_path, string>>& created_cfgs)
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

    // Try to drop the packages watching out for their potential dependents.
    // If there are any dependents, then sync in the deinit mode. This way we
    // handle the plausible scenario when a dependency is pushed/submitted to
    // a git/pkg repository after being developed and now we switch (back) to
    // tracking it from that repository.
    //
    if (!force)
    {
      process pr (start_bpkg (2,
                              o,
                              1 /* stdout */,
                              2 /* stderr */,
                              "drop",
                              "-d", cfg,
                              "--dependent-exit", 125,
                              "--plan", "synchronizing:",
                              "--yes",
                              pkgs));

      if (pr.wait ())
        return;

      const process_exit& e (*pr.exit);
      if (e.normal ())
      {
        if (e.code () != 125)
          throw failed (); // Assume the child issued diagnostics.

        // Fall through.
      }
      else
        fail << "process " << name_bpkg (o) << " " << e;

      // Give the user a hint on what's going on. Failed that, a fetch during
      // deinit looks very strange.
      //
      if (verb)
        text << "deinitializing in replacement mode due to existing dependents";

      if (!o.no_fetch ())
        cmd_fetch (o, prj, c, true /* fetch_full */);

      cmd_sync_deinit (o, prj, c, pkgs, &t, &created_cfgs);
    }
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

      vector<pair<dir_path, string>> created_cfgs;

      try
      {
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
        cmd_deinit (o, prj, c, ps, t, created_cfgs);

        db.update (c);
        t.commit ();
      }
      catch (const failed&)
      {
        if (!created_cfgs.empty ())
        {
          transaction t (db.begin ());

          for (const auto& c: created_cfgs)
            cmd_config_add (prj,
                            t,
                            c.first  /* path */,
                            c.second /* name */,
                            c.second /* type */);

          t.commit ();
        }

        throw;
      }

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
