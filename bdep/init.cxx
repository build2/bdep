// file      : bdep/init.cxx -*- C++ -*-
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
                   const package_locations& ps,
                   database& db,
                   const dir_path& cfg,
                   const strings& args,
                   bool ca,
                   bool cc)
  {
    const char* m (!ca ? "--config-create" :
                   !cc ? "--config-add"    : nullptr);

    if (m == nullptr)
      fail << "both --config-add and --config-create specified";

    optional<string> nm;
    optional<uint64_t> id;
    cmd_config_validate_add (o, m, nm, id);

    return ca
      ? cmd_config_add    (   ao, prj, ps, db, cfg,       move (nm), move (id))
      : cmd_config_create (o, ao, prj, ps, db, cfg, args, move (nm), move (id));
  }

  void
  cmd_init (const common_options& o,
            const dir_path& prj,
            database& db,
            const configurations& cfgs,
            const package_locations& pkgs,
            const strings& pkg_args,
            bool sync)
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

        // If this configuration is forwarded, verify there is no other
        // forwarded configuration that also has this package.
        //
        if (c->forward)
        {
          using query = bdep::query<configuration>;

          for (const shared_ptr<configuration>& o:
                 pointer_result (db.query<configuration> (query::forward)))
          {
            if (o == c)
              continue;

            if (find_if (o->packages.begin (),
                         o->packages.end (),
                         [&p] (const package_state& s)
                         {
                           return p.name == s.name;
                         }) != o->packages.end ())
            {
              fail << "forwarded configuration " << *o << " also has package "
                   << p.name << " initialized" <<
                info << "while initializing in forwarded configuration " << *c;
            }
          }
        }

        // If we are initializing multiple packages or there will be no sync,
        // print their names.
        //
        if (verb && (pkgs.size () > 1 || !sync))
          text << "initializing package " << p.name;;

        c->packages.push_back (package_state {p.name});
      }

      // Should we sync then commit the database or commit and then sync?
      // Either way we can end up with an inconsistent state. Note, however,
      // that the state in the build configuration can in most cases be
      // corrected with a retry (e.g., "upgrade" the package to the fixed
      // version, etc) while if we think (from the database state) that the
      // package has already been initialized, then there will be no way to
      // retry anything (though it could probably be corrected with a sync or,
      // failed that, deinit/init).
      //
      // However, there is a drawback to doing it this way: if we trigger an
      // implicit sync (e.g., via a hook) of something that uses the same
      // database, we will get the "database is used by another process"
      // error. This can be worked around by disabling the implicit sync
      // (BDEP_SYNC=0).
      //
      // Note: semantically equivalent to the first form of the sync command.
      //
      if (sync)
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
      if (ca)           fail << "both --empty and --config-add specified";
      if (cc)           fail << "both --empty and --config-create specified";
      if (o.no_sync ()) fail << "both --empty and --no-sync specified";
    }

    if (const char* n = cmd_config_validate_add (o))
    {
      if (!ca && !cc)
        fail << n << " specified without --config-(add|create)";

      if (o.existing () && !cc)
        fail << "--existing|-e specified without --config-create";

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

    // Skip the first `--` separator, if any.
    //
    if (args.more () && args.peek () == string ("--"))
      args.next ();

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
        strings cfg_args;
        if (cc)
        {
          // Read the configuration arguments until we reach the second `--`
          // separator or eos.
          //
          for (string a; args.more () && (a = args.next ()) != "--"; )
            cfg_args.push_back (move (a));
        }

        cfgs.push_back (
          cmd_init_config (
            o,
            o,
            prj,
            load_packages (prj),
            db,
            ca ? o.config_add () : o.config_create (),
            cfg_args,
            ca,
            cc));
      }
      else
      {
        // If this is the default mode, then find the configurations the user
        // wants us to use.
        //
        transaction t (db.begin ());
        cfgs = find_configurations (o, prj, t);
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
              scan_arguments (args) /* pkg_args */,
              !o.no_sync ());

    return 0;
  }

  default_options_files
  options_files (const char*, const cmd_init_options& o, const strings&)
  {
    // NOTE: remember to update the documentation if changing anything here.

    // bdep.options
    // bdep-{config config-add}.options               # -A
    // bdep-{config config-add config-create}.options # -C
    // bdep-init.options

    default_options_files r {{path ("bdep.options")}, find_project (o)};

    auto add = [&r] (const string& n)
    {
      r.files.push_back (path ("bdep-" + n + ".options"));
    };

    if (o.config_add_specified () || o.config_create_specified ())
    {
      add ("config");
      add ("config-add");
    }

    if (o.config_create_specified ())
      add ("config-create");

    add ("init");

    return r;
  }

  cmd_init_options
  merge_options (const default_options<cmd_init_options>& defs,
                 const cmd_init_options& cmd)
  {
    // NOTE: remember to update the documentation if changing anything here.

    return merge_default_options (
      defs,
      cmd,
      [] (const default_options_entry<cmd_init_options>& e,
          const cmd_init_options&)
      {
        const cmd_init_options& o (e.options);

        auto forbid = [&e] (const char* opt, bool specified)
        {
          if (specified)
            fail (e.file) << opt << " in default options file";
        };

        forbid ("--directory|-d",     o.directory_specified ());
        forbid ("--config-add|-A",    o.config_add_specified ());
        forbid ("--config-create|-C", o.config_create_specified ());
        forbid ("--wipe",             o.wipe ()); // Dangerous.
      });
  }
}
