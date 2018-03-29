// file      : bdep/config.cxx -*- C++ -*-
// copyright : Copyright (c) 2014-2017 Code Synthesis Ltd
// license   : MIT; see accompanying LICENSE file

#include <bdep/config.hxx>

#include <bdep/database.hxx>
#include <bdep/project-odb.hxx>
#include <bdep/diagnostics.hxx>

using namespace std;

namespace bdep
{
  shared_ptr<configuration>
  cmd_config_add (const dir_path&    prj,
                  database&          db,
                  dir_path           path,
                  optional<string>   name,
                  optional<bool>     def,
                  optional<uint64_t> id,
                  const char*        what)
  {
    if (!exists (path))
      fail << "configuration directory " << path << " does not exist";

    transaction t (db.begin ());

    using count = configuration_count;
    using query = bdep::query<count>;

    // By default the first added configuration is the default.
    //
    if (!def)
      def = (db.query_value<count> () == 0);
    else if (*def)
    {
      using query = bdep::query<configuration>;

      if (auto p = db.query_one<configuration> (query::default_))
        fail << "configuration " << *p << " is already the default" <<
          info << "use 'bdep config set --no-default' to clear";
    }

    // Make sure the configuration path is absolute and normalized. Also
    // derive relative to project directory path if possible.
    //
    path.complete ();
    path.normalize ();

    optional<dir_path> rel_path;
    try {rel_path = path.relative (prj);} catch (const invalid_path&) {}

    shared_ptr<configuration> r (
      new configuration {
        id,
        name,
        path,
        move (rel_path),
        *def,
        {} /* packages */});

    try
    {
      db.persist (r);
    }
    catch (const odb::exception&)
    {
      // See if this is id, name, or path conflict.
      //
      if (id && db.query_value<count> (query::id == *id) != 0)
        fail << "configuration with id " << *id << " already exists "
             << "in project " << prj;

      if (name && db.query_value<count> (query::name == *name) != 0)
        fail << "configuration with name '" << *name << "' already exists "
             << "in project " << prj;

      if (db.query_value<count> (query::path == path.string ()) != 0)
        fail << "configuration with path " << path << " already exists "
             << "in project " << prj;

      // Hm, what could that be?
      //
      throw;
    }

    t.commit ();

    if (verb)
    {
      diag_record dr (text);
      /*            */ dr << what << " configuration ";
      if (r->name)     dr << '@' << *r->name << ' ';
      /*            */ dr << r->path << " (" << *r->id;
      if (r->default_) dr << ", default";
      /*            */ dr << ')';
    }

    return r;
  }

  static int
  cmd_config_add (const cmd_config_options&, cli::scanner&)
  {
    fail << "@@ TODO" << endf;
  }

  shared_ptr<configuration>
  cmd_config_create (const common_options& co,
                     const dir_path&       prj,
                     database&             db,
                     dir_path              path,
                     cli::scanner&         cfg_args,
                     optional<string>      name,
                     optional<bool>        def,
                     optional<uint64_t>    id)
  {
    // Call bpkg to create the configuration.
    //
    {
      strings args;
      while (cfg_args.more ())
        args.push_back (cfg_args.next ());

      run_bpkg (co, "create", "-d", path, args);
    }

    return cmd_config_add (prj,
                           db,
                           move (path),
                           move (name),
                           def,
                           id,
                           "created");
  }

  static int
  cmd_config_create (const cmd_config_options&, cli::scanner&)
  {
    fail << "@@ TODO" << endf;
  }

  static int
  cmd_config_remove (const cmd_config_options&, cli::scanner&)
  {
    fail << "@@ TODO" << endf;
  }

  static int
  cmd_config_rename (const cmd_config_options&, cli::scanner&)
  {
    fail << "@@ TODO" << endf;
  }

  static int
  cmd_config_set (const cmd_config_options&, cli::scanner&)
  {
    fail << "@@ TODO" << endf;
  }

  int
  cmd_config (const cmd_config_options& o, cli::scanner& scan)
  {
    tracer trace ("config");

    cmd_config_subcommands c (
      parse_command<cmd_config_subcommands> (scan,
                                             "config subcommand",
                                             "bdep help config"));

    // Validate options/subcommands.
    //

    // --[no-]default
    //
    if (o.default_ () && o.no_default ())
      fail << "both --default and --no-default specified";

    if (const char* n = (o.default_ ()   ? "--default"    :
                         o.no_default () ? "--no-default" : nullptr))
    {
      if (!(c.add () || c.create () || c.set ()))
        fail << n << " not valid for this command";
    }

    // --all
    //
    if (o.all ())
    {
      if (!c.remove ())
        fail << "--all not valid for this command";
    }

    // Dispatch to subcommand function.
    //
    if (c.add    ()) return cmd_config_add    (o, scan);
    if (c.create ()) return cmd_config_create (o, scan);
    if (c.remove ()) return cmd_config_remove (o, scan);
    if (c.rename ()) return cmd_config_rename (o, scan);
    if (c.set    ()) return cmd_config_set    (o, scan);

    assert (false); // Unhandled (new) subcommand.
    return 1;
  }
}
