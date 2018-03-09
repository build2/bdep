// file      : bdep/init.cxx -*- C++ -*-
// copyright : Copyright (c) 2014-2017 Code Synthesis Ltd
// license   : MIT; see accompanying LICENSE file

#include <bdep/init.hxx>

#include <bdep/config.hxx>
#include <bdep/project.hxx>
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
    for (const dir_path& d: pp.packages)
      text << "  " << (prj / d);

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

    transaction t (db.begin ());

    // If this is the default mode, then find the configurations the user
    // wants us to use.
    //
    if (cfgs.empty ())
      cfgs = find_configurations (prj, t, o);

    //@@ TODO: print project/package(s) being initialized.

    t.commit ();

    return 0;
  }
}
