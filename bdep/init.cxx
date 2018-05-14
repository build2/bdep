// file      : bdep/init.cxx -*- C++ -*-
// copyright : Copyright (c) 2014-2017 Code Synthesis Ltd
// license   : MIT; see accompanying LICENSE file

#include <bdep/init.hxx>

#include <bdep/project.hxx>
#include <bdep/project-odb.hxx>
#include <bdep/database.hxx>
#include <bdep/diagnostics.hxx>

#include <bdep/sync.hxx>
#include <bdep/config.hxx>

using namespace std;

namespace bdep
{
  shared_ptr<configuration>
  cmd_init_config (const configuration_name_options& o,
                   const configuration_add_options& ao,
                   const dir_path& prj,
                   database& db,
                   const dir_path& cfg,
                   cli::scanner& args,
                   bool ca,
                   bool cc)
  {
    const char* m (!ca ? "--config-create" :
                   !cc ? "--config-add"    : nullptr);

    if (m == nullptr)
      fail << "both --config-add and --config-create specified";

    optional<string> nm;
    if (size_t n = o.config_name ().size ())
    {
      if (n > 1)
        fail << "multiple configuration names specified for " << m;

      nm = o.config_name ()[0];
    }

    optional<uint64_t> id;
    if (size_t n = o.config_id ().size ())
    {
      if (n > 1)
        fail << "multiple configuration ids specified for " << m;

      id = o.config_id ()[0];
    }

    return ca
      ? cmd_config_add    (   ao, prj, db, cfg,       move (nm), move (id))
      : cmd_config_create (o, ao, prj, db, cfg, args, move (nm), move (id));
  }

  void
  cmd_init (const common_options& o,
            const dir_path& prj,
            database& db,
            const configurations& cfgs,
            const package_locations& pkgs,
            const strings& pkg_args)
  {
    // We do each configuration in a separate transaction so that our state
    // reflects the bpkg configuration as closely as possible.
    //
    bool first (true);
    for (const shared_ptr<configuration>& c: cfgs)
    {
      // If we are initializing in multiple configurations, separate them with
      // a blank line and print the configuration name/directory.
      //
      if (verb && cfgs.size () > 1)
      {
        text << (first ? "" : "\n")
             << "in configuration " << *c << ':';

        first = false;
      }

      // Add project repository to the configuration. Note that we don't fetch
      // it since sync is going to do it anyway.
      //
      run_bpkg (3,
                o,
                "add",
                "-d", c->path,
                "--type", "dir",
                prj);

      transaction t (db.begin ());

      for (const package_location& p: pkgs)
      {
        if (find_if (c->packages.begin (),
                     c->packages.end (),
                     [&p] (const package_state& s)
                     {
                       return p.name == s.name;
                     }) != c->packages.end ())
        {
          if (verb)
            info << "package " << p.name << " is already initialized";

          continue;
        }

        // If we are initializing multiple packages, print their names.
        //
        if (verb && pkgs.size () > 1)
          text << "initializing package " << p.name;

        c->packages.push_back (package_state {p.name});
      }

      // Should we sync then commit the database or commit and then sync?
      // Either way we can end up with an incosistent state. Note, however,
      // that the state in the build configuration can in most cases be
      // corrected with a retry (e.g., "upgrade" the package to the fixed
      // version, etc) while if we think (from the database state) that the
      // package has already been initialized, then there will be no way to
      // retry anything.
      //
      cmd_sync (o, prj, c, pkg_args, false /* implicit */);

      db.update (c);
      t.commit ();
    }
  }

  int
  cmd_init (const cmd_init_options& o, cli::group_scanner& args)
  {
    tracer trace ("init");

    bool ca (o.config_add_specified ());
    bool cc (o.config_create_specified ());

    if (o.empty ())
    {
      if (ca) fail << "both --empty and --config-add specified";
      if (cc) fail << "both --empty and --config-create specified";
    }

    if (const char* n = cmd_config_validate_add (o))
    {
      if (!ca && !cc)
        fail << n << " specified without --config-(add|create)";

      if (o.wipe () && !cc)
        fail << "--wipe specified without --config-create";
    }

    project_packages pp (
      find_project_packages (o, o.empty () /* ignore_packages */));

    const dir_path& prj (pp.project);

    if (verb)
      text << "initializing in project " << prj;

    // Create .bdep/.
    //
    {
      dir_path d (prj / bdep_dir);

      if (!exists (d))
        mk (prj / bdep_dir);
    }

    // Open the database creating it if necessary.
    //
    database db (open (prj, trace, true /* create */));

    // --empty
    //
    if (o.empty ())
    {
      //@@ TODO: what should we do if the database already exists?

      return 0;
    }

    configurations cfgs;
    {
      // Make sure everyone refers to the same objects across all the
      // transactions.
      //
      session s;

      // --config-add/create
      //
      if (ca || cc)
      {
        cfgs.push_back (
          cmd_init_config (
            o,
            o,
            prj,
            db,
            ca ? o.config_add () : o.config_create (),
            args,
            ca,
            cc));
      }
      else
      {
        // If this is the default mode, then find the configurations the user
        // wants us to use.
        //
        transaction t (db.begin ());
        cfgs = find_configurations (prj, t, o);
        t.commit ();
      }
    }

    // Initialize each package in each configuration.
    //
    cmd_init (o,
              prj,
              db,
              cfgs,
              pp.packages,
              scan_arguments (args) /* pkg_args */);

    return 0;
  }
}
