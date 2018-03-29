// file      : bdep/sync.cxx -*- C++ -*-
// copyright : Copyright (c) 2014-2017 Code Synthesis Ltd
// license   : MIT; see accompanying LICENSE file

#include <bdep/sync.hxx>

#include <bdep/database.hxx>
#include <bdep/diagnostics.hxx>

#include <bdep/fetch.hxx>

using namespace std;

namespace bdep
{
  // Sync with optional upgrade.
  //
  // If upgrade is not nullopt, then: If there are dep_pkgs, then we are
  // upgrading specific dependency packages. Othewise -- project packages.
  //
  static void
  cmd_sync (const common_options& co,
            const dir_path& prj,
            const shared_ptr<configuration>& c,
            bool fetch,
            bool yes,
            optional<bool> upgrade,   // true - upgrade,   false - patch
            optional<bool> recursive, // true - recursive, false - immediate
            const package_locations& prj_pkgs,
            const strings&           dep_pkgs)
  {
    assert (!c->packages.empty ());
    assert (prj_pkgs.empty () || dep_pkgs.empty ()); // Can't have both.

    // Do a separate fetch instead of letting pkg-build do it. This way we get
    // better control of the diagnostics (no "fetching ..." for the project
    // itself). We also make sure that if the user specifies a repository for
    // a dependency to upgrade, then that repository is listed as part of the
    // project prerequisites/complements. Or, in other words, we only want to
    // allow specifying the location as a proxy for specifying version (i.e.,
    // "I want this dependency upgraded to the latest version available from
    // this repository").
    //
    // We also use the repository name rather than the location as a sanity
    // check (the repository must have been added as part of init).
    //
    if (fetch)
      run_bpkg (co,
                "fetch",
                "-d", c->path,
                "--shallow",
                "dir:" + prj.string ());

    // Prepare the package list.
    //
    strings args;

    for (const package_state& p: c->packages)
    {
      if (upgrade)
      {
        // We synchronize all the init'ed packages but only upgrade the
        // specified.
        //
        if (find_if (prj_pkgs.begin (),
                     prj_pkgs.end (),
                     [&p] (const package_location& pl)
                     {
                       return pl.name == p.name;
                     }) != prj_pkgs.end ())
        {
          // The project package itself must always be upgraded to the latest
          // version/iteration. So we have to translate our option to their
          // dependency-only --{upgrade,patch}-{recursive,immediate}.
          //
          args.push_back ("{");
          args.push_back (
            *upgrade
            ? *recursive ? "--upgrade-recursive" : "--upgrade-immediate"
            : *recursive ? "--patch-recursive"   : "--patch-immediate");
          args.push_back ("}+");
        }
      }

      // We need to add the explicit location qualification (@<rep-loc>) since
      // there is no guarantee a higher version isn't available from another
      // repository.
      //
      args.push_back (p.name + '@' + prj.string ());
    }

    if (upgrade)
    {
      for (const string& p: dep_pkgs)
      {
        // Unless this is the default "non-recursive upgrade" we need to add a
        // group.
        //
        if (recursive || !*upgrade)
        {
          args.push_back ("{");
          args.push_back (*upgrade ? "-u" : "-p");
          if (recursive) args.push_back (*recursive ? "-r" : "-i");
          args.push_back ("}+");
        }

        // Make sure it is treated as a dependency.
        //
        args.push_back ('?' + p);
      }
    }

    run_bpkg (co,
              "build",
              "-d", c->path,
              "--no-fetch",
              "--configure-only",
              "--keep-out",
              "--plan", "synchronizing:",
              (yes ? "--yes" : nullptr),
              args);
  }

  void
  cmd_sync (const common_options& co,
            const dir_path& prj,
            const shared_ptr<configuration>& c,
            bool fetch,
            bool yes)
  {
    cmd_sync (co,
              prj,
              c,
              fetch,
              yes,
              nullopt              /* upgrade   */,
              nullopt              /* recursive */,
              package_locations () /* prj_pkgs  */,
              strings ()           /* dep_pkgs  */);
  }

  int
  cmd_sync (const cmd_sync_options& o, cli::scanner& args)
  {
    tracer trace ("sync");

    if (o.upgrade () && o.patch ())
      fail << "both --upgrade|-u and --patch|-p specified";

    // The --immediate or --recursive option can only be specified with
    // an explicit --upgrade or --patch.
    //
    if (const char* n = (o.immediate () ? "--immediate" :
                         o.recursive () ? "--recursive" : nullptr))
    {
      if (!o.upgrade () && !o.patch ())
        fail << n << " requires explicit --upgrade|-u or --patch|-p";
    }

    // We have two pretty different upgrade modes: project package upgrade and
    // dependency package upgrade (have arguments).
    //
    strings dep_pkgs;
    for (; args.more (); dep_pkgs.push_back (args.next ())) ;

    // We could be running from a package directory (or the user specified one
    // with -d) that has not been init'ed in this configuration. Unless we are
    // upgrading specific dependencies, we want to diagnose that since such a
    // package will not be present in the bpkg configuration. But if we are
    // running from the project, then we don't want to treat all the available
    // packages as specified by the user (thus load_packages=false).
    //
    project_packages pp (
      find_project_packages (o,
                             !dep_pkgs.empty () /* ignore_packages */,
                             false              /* load_packages   */));

    const dir_path& prj (pp.project);
    const package_locations& prj_pkgs (pp.packages);

    database db (open (prj, trace));

    transaction t (db.begin ());
    configurations cfgs (find_configurations (prj, t, o));
    t.commit ();

    // If specified, verify packages are present in each configuration.
    //
    if (!prj_pkgs.empty ())
      verify_project_packages (pp, cfgs);

    // Synchronize each configuration skipping empty ones.
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

      // If we are synchronizing multiple configurations, separate them with a
      // blank line and print the configuration name/directory.
      //
      if (verb && cfgs.size () > 1)
      {
        text << (first ? "" : "\n")
             << "CONFIGURATION " << *c;

        first = false;
      }

      bool fetch (o.fetch () || o.fetch_full ());

      if (fetch)
        cmd_fetch (o, prj, c, o.fetch_full ());

      if (!dep_pkgs.empty ())
      {
        // The third form: upgrade of the specified dependencies.
        //
        // Only prompt if upgrading their dependencies.
        //
        cmd_sync (o,
                  prj,
                  c,
                  !fetch,
                  o.recursive () || o.immediate () ? o.yes () : true,
                  !o.patch (), // Upgrade by default unless patch requested.
                  (o.recursive () ? optional<bool> (true)  :
                   o.immediate () ? optional<bool> (false) : nullopt),
                  prj_pkgs,
                  dep_pkgs);
      }
      else if (o.upgrade () || o.patch ())
      {
        // The second form: upgrade of project packages' dependencies
        // (immediate by default, recursive if requested).
        //
        cmd_sync (o,
                  prj,
                  c,
                  !fetch,
                  o.yes (),
                  o.upgrade (),
                  o.recursive (),
                  prj_pkgs,
                  dep_pkgs);
      }
      else
      {
        // The first form: sync of project packages.
        //
        cmd_sync (o, prj, c, !fetch, true /* yes */);
      }
    }

    return 0;
  }
}
