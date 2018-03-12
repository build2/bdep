// file      : bdep/init.cxx -*- C++ -*-
// copyright : Copyright (c) 2014-2017 Code Synthesis Ltd
// license   : MIT; see accompanying LICENSE file

#include <bdep/init.hxx>

#include <bdep/config.hxx>
#include <bdep/project.hxx>
#include <bdep/project-odb.hxx>
#include <bdep/database.hxx>
#include <bdep/diagnostics.hxx>

using namespace std;

namespace bdep
{
  int
  cmd_init (const cmd_init_options& o, cli::scanner& args)
  {
    tracer trace ("init");

    project_packages pp (
      find_project_packages (o, o.empty () /* ignore_packages */));

    const dir_path& prj (pp.project);

    text << prj;
    for (const package_location& pl: pp.packages)
      text << "  " << pl.name << " " << (prj / pl.path);

    // Create .bdep/.
    //
    {
      dir_path d (prj / bdep_dir);

      if (!exists (d))
        mk (prj / bdep_dir);
    }

    // Open the database creating it if necessary.
    //
    database db (open (pp.project, trace, true /* create */));

    // --empty
    //
    bool ca (o.config_add_specified ());
    bool cc (o.config_create_specified ());

    if (o.empty ())
    {
      if (ca) fail << "both --empty and --config-add specified";
      if (cc) fail << "both --empty and --config-create specified";

      //@@ TODO: what should we do if the database already exists?

      return 0;
    }

    // Make sure everyone refers to the same objects across all the
    // transactions.
    //
    session s;

    // --config-add/create
    //
    configurations cfgs;
    if (ca || cc)
    {
      const char* m (!ca ? "--config-create" :
                     !cc ? "--config-add"    :
                     nullptr);

      if (m == nullptr)
        fail << "both --config-add and --config-create specified";

      optional<string> name;
      if (size_t n = o.config_name ().size ())
      {
        if (n > 1)
          fail << "multiple configuration names specified for " << m;

        name = o.config_name ()[0];
      }

      optional<uint64_t> id;
      if (size_t n = o.config_id ().size ())
      {
        if (n > 1)
          fail << "multiple configuration ids specified for " << m;

        id = o.config_id ()[0];
      }

      cfgs.push_back (
        ca
        ? cmd_config_add (prj,
                          db,
                          o.config_add (),
                          move (name),
                          nullopt /* default */, // @@ TODO: --[no]-default
                          move (id))
        : nullptr); // @@ TODO: create

      // Fall through.
    }

    // If this is the default mode, then find the configurations the user
    // wants us to use.
    //
    if (cfgs.empty ())
    {
      transaction t (db.begin ());
      cfgs = find_configurations (prj, t, o);
      t.commit ();
    }

    // Initialize each package in each configuration skipping those that are
    // already initialized. Do each configuration in a separate transaction so
    // that our state reflects the bpkg configuration as closely as possible.
    //
    for (const shared_ptr<configuration>& c: cfgs)
    {
      transaction t (db.begin ());

      // Add project repository to the configuration. Note that we don't fetch
      // it since sync is going to do it anyway.
      //
      run_bpkg (o,
                "add",
                "-d",     c->path,
                "--type", "dir",
                prj);

      for (const package_location& p: pp.packages)
      {
        if (find_if (c->packages.begin (),
                     c->packages.end (),
                     [&p] (const package_state& s)
                     {
                       return p.name == s.name;
                     }) != c->packages.end ())
        {
          info << "package " << p.name << " is already initialized "
               << "in configuration " << *c;
          continue;
        }

        c->packages.push_back (package_state {p.name});
      }

      db.update (c);
      t.commit ();
    }

    //@@ TODO: print project/package(s) being initialized.

    return 0;
  }
}
