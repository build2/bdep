// file      : bdep/config.cxx -*- C++ -*-
// license   : MIT; see accompanying LICENSE file

#include <bdep/config.hxx>

#include <iostream> // cout

#include <bdep/database.hxx>
#include <bdep/project-odb.hxx>
#include <bdep/diagnostics.hxx>

using namespace std;

namespace bdep
{
  template <typename O>
  static void
  print_configuration (O& o,
                       const shared_ptr<configuration>& c,
                       bool flags = true)
  {
    if (c->name)
      o << '@' << *c->name << ' ';

    o << c->path << ' ' << *c->id << ' ' << c->type;

    if (flags)
    {
      char s (' ');
      if (c->default_)  {o << s << "default";           s = ',';}
      if (c->forward)   {o << s << "forwarded";         s = ',';}
      if (c->auto_sync) {o << s << "auto-synchronized"; s = ',';}
    }
  }

  const char*
  cmd_config_validate_add (const configuration_add_options& o)
  {
    // --[no-]default
    //
    if (o.default_ () && o.no_default ())
      fail << "both --default and --no-default specified";

    // --[no-]forward
    //
    if (o.forward () && o.no_forward ())
      fail << "both --forward and --no-forward specified";

    // --[no-]auto-sync
    //
    if (o.auto_sync () && o.no_auto_sync ())
      fail << "both --auto-sync and --no-auto-sync specified";

    if (o.existing () && o.wipe ())
      fail << "both --existing|-e and --wipe specified";

    return (o.config_type_specified () ? "--config-type"  :
            o.default_ ()              ? "--default"      :
            o.no_default ()            ? "--no-default"   :
            o.forward ()               ? "--forward"      :
            o.no_forward ()            ? "--no-forward"   :
            o.auto_sync ()             ? "--auto-sync"    :
            o.no_auto_sync ()          ? "--no-auto-sync" :
            o.existing ()              ? "--existing|-e"  :
            o.wipe ()                  ? "--wipe"         : nullptr);
  }

  void
  cmd_config_validate_add (const configuration_name_options& o,
                           const char* what,
                           optional<string>& name,
                           optional<uint64_t>& id)
  {
    name = nullopt;
    id = nullopt;

    if (size_t n = o.config_name ().size ())
    {
      if (n > 1)
        fail << "multiple configuration names specified for " << what;

      name = o.config_name ()[0];
    }

    if (size_t n = o.config_id ().size ())
    {
      if (n > 1)
        fail << "multiple configuration ids specified for " << what;

      id = o.config_id ()[0];
    }
  }

  // Translate the configuration directory that is actually a name (@foo or
  // -@foo) to the real directory (prj-foo) and name (@foo).
  //
  static inline void
  translate_path_name (const dir_path& prj,
                       dir_path& path,
                       optional<string>& name)
  {
    if (name || !path.simple ())
      return;

    size_t n;
    {
      const string& s (path.string ());
      if      (s.size () > 1 && s[0] == '@')                n = 1;
      else if (s.size () > 2 && s[0] == '-' && s[1] == '@') n = 2;
      else return;
    }

    string s (move (path).string ()); // Move out.
    s.erase (0, n);                   // Remove leading '@' or '-@'.

    path  = prj;
    path += '-';
    path += s;

    name = move (s);
  }

  // Verify the configuration directory is not inside one of the packages.
  //
  static void
  verify_configuration_path (const dir_path& cfg,
                             const dir_path& prj,
                             const package_locations& pkgs)
  {
    for (const package_location& p: pkgs)
    {
      dir_path d (prj / p.path); // Should already be normalized.

      if (cfg.sub (d))
        fail << "configuration directory " << cfg << " is inside package "
             << p.name << " (" << d << ")";
    }
  }

  shared_ptr<configuration>
  cmd_config_add (const common_options&            co,
                  const configuration_add_options& ao,
                  const dir_path&                  prj,
                  const package_locations&         pkgs,
                  database&                        db,
                  dir_path                         path,
                  optional<string>                 name,
                  optional<string>                 type,
                  optional<uint64_t>               id,
                  const char*                      what)
  {
    translate_path_name (prj, path, name);

    if (name && name->empty ())
      fail << "empty configuration name specified";

    if (!exists (path))
      fail << "configuration directory " << path << " does not exist";

    // Make sure the configuration path is absolute and normalized. Also
    // derive relative to project directory path if possible.
    //
    normalize (path, "configuration");

    verify_configuration_path (path, prj, pkgs);

    // Use bpkg-cfg-info to query the configuration type, unless specified
    // explicitly.
    //
    if (!type)
    {
      fdpipe pipe (open_pipe ()); // Text mode seems appropriate.

      process pr (start_bpkg (3,
                              co,
                              pipe /* stdout */,
                              2    /* stderr */,
                              "cfg-info",
                              "-d", path));

      // Shouldn't throw, unless something is severely damaged.
      //
      pipe.out.close ();

      bool io (false);
      try
      {
        ifdstream is (move (pipe.in), fdstream_mode::skip, ifdstream::badbit);

        for (string l; !eof (getline (is, l)); )
        {
          if (l.compare (0, 6, "type: ") == 0)
          {
            type = string (l, 6);
            break;
          }
        }

        is.close (); // Detect errors.

        if (!type || type->empty ())
          fail << "invalid bpkg-cfg-info output: no configuration type";
      }
      catch (const io_error&)
      {
        // Presumably the child process failed and issued diagnostics so let
        // finish_bpkg() try to deal with that first.
        //
        io = true;
      }

      finish_bpkg (co, pr, io);
    }

    transaction t (db.begin ());

    using count = configuration_count;

    optional<bool> def, fwd;
    {
      using query = bdep::query<configuration>;

      // By default the first added for its type configuration is the default.
      //
      if (ao.default_ () || ao.no_default ())
        def = ao.default_ () && !ao.no_default ();

      if (!def)
        def = (db.query_value<count> (query::type == *type) == 0);
      else if (*def)
      {
        if (auto p = db.query_one<configuration> (query::default_ &&
                                                  query::type == *type))
          fail << "configuration " << *p << " of type " << *type
               << " is already the default" <<
            info << "use 'bdep config set --no-default' to clear";
      }

      // By default the default configuration is forwarded unless another is
      // already forwarded for its configuration type.
      //
      if (ao.forward () || ao.no_forward ())
        fwd = ao.forward () && !ao.no_forward ();

      // Note: there can be multiple forwarded configurations for a type.
      //
      if (!fwd)
        fwd = *def &&
              db.query_value<count> (query::forward &&
                                     query::type == *type) == 0;
    }

    shared_ptr<configuration> r (
      new configuration {
        id,
        name,
        move (*type),
        path,
        path.try_relative (prj),
        *def,
        *fwd,
        !ao.no_auto_sync (),
        {} /* packages */});

    try
    {
      db.persist (r);
    }
    catch (const odb::exception&)
    {
      //@@ TODO: Maybe redo by querying the conflicting configuration and then
      //         printing its path, line in rename? Also do it before persist.

      using query = bdep::query<count>;

      // See if this is id, name, or path conflict.
      //
      if (id && db.query_value<count> (query::id == *id) != 0)
        fail << "configuration with id " << *id << " already exists "
             << "in project " << prj;

      if (name && db.query_value<count> (query::name == *name) != 0)
        fail << "configuration with name '" << *name << "' already exists "
             << "in project " << prj;

      if (db.query_value<count> (query::path == path.string ()) != 0)
        fail << "configuration with directory " << path << " already exists "
             << "in project " << prj;

      // Hm, what could that be?
      //
      throw;
    }

    t.commit ();

    if (verb)
    {
      diag_record dr (text);
      dr << what << " configuration ";
      print_configuration (dr, r);
    }

    return r;
  }

  shared_ptr<configuration>
  cmd_config_create (const common_options&            co,
                     const configuration_add_options& ao,
                     const dir_path&                  prj,
                     const package_locations&         pkgs,
                     database&                        db,
                     dir_path                         path,
                     const strings&                   args,
                     optional<string>                 name,
                     string                           type,
                     optional<uint64_t>               id)
  {
    // Similar logic to *_add().
    //
    translate_path_name (prj, path, name);

    normalize (path, "configuration");

    verify_configuration_path (path, prj, pkgs);

    // Call bpkg to create the configuration.
    //
    run_bpkg (2,
              co,
              "create",
              "-d", path,
              (name
               ? strings ({"--name", *name})
               : strings ()),
              "--type", type,
              (ao.existing () ? "--existing" : nullptr),
              (ao.wipe ()     ? "--wipe"     : nullptr),
              args);

    return cmd_config_add (co,
                           ao,
                           prj,
                           package_locations {}, // Already verified.
                           db,
                           move (path),
                           move (name),
                           move (type),
                           id,
                           ao.existing () ? "initialized" : "created");
  }

  static int
  cmd_config_add (const cmd_config_options& o, cli::scanner& args)
  {
    tracer trace ("config_add");

    optional<string> name;
    optional<uint64_t> id;
    cmd_config_validate_add (o, "config add", name, id);

    string arg;
    if (args.more ())
      arg = args.next ();
    else if (name)
    {
      // Reverse into the shortcut form expected by translate_path_name().
      //
      arg = '@' + *name;
      name = nullopt;
    }
    else
      fail << "configuration directory argument expected";

    dir_path path;
    try
    {
      path = dir_path (move (arg));
    }
    catch (const invalid_path&)
    {
      fail << "invalid configuration directory '" << arg << "'";
    }

    dir_path prj (find_project (o));
    database db (open (prj, trace));

    cmd_config_add (o,
                    o,
                    prj,
                    load_packages (prj, true /* allow_empty */),
                    db,
                    move (path),
                    move (name),
                    nullopt,     /* type */
                    move (id));
    return 0;
  }

  static int
  cmd_config_create (const cmd_config_options& o, cli::scanner& args)
  {
    tracer trace ("config_create");

    optional<string> name;
    optional<uint64_t> id;
    cmd_config_validate_add (o, "config create", name, id);

    // Note that the shortcut will only work if there are no cfg-args which
    // is not very likely. Oh, well.
    //
    string arg;
    if (args.more ())
      arg = args.next ();
    else if (name)
    {
      // Reverse into the shortcut form expected by translate_path_name().
      //
      arg = '@' + *name;
      name = nullopt;
    }
    else
      fail << "configuration directory argument expected";

    dir_path path;
    try
    {
      path = dir_path (move (arg));
    }
    catch (const invalid_path&)
    {
      fail << "invalid configuration directory '" << arg << "'";
    }

    dir_path prj (find_project (o));
    database db (open (prj, trace));

    strings cfg_args;
    for (; args.more (); cfg_args.push_back (args.next ())) ;

    cmd_config_create (o,
                       o,
                       prj,
                       load_packages (prj, true /* allow_empty */),
                       db,
                       move (path),
                       cfg_args,
                       move (name),
                       o.config_type (),
                       move (id));
    return 0;
  }

  static int
  cmd_config_link (const cmd_config_options& o, cli::scanner&)
  {
    tracer trace ("config_link");

    // Load project configurations.
    //
    configurations cfgs;
    {
      dir_path prj (find_project (o));
      database db (open (prj, trace));

      transaction t (db.begin ());

      cfgs = find_configurations (o,
                                  prj,
                                  t,
                                  false /* fallback_default */,
                                  true  /* validate         */).first;

      t.commit ();
    }

    if (cfgs.size () != 2)
      fail << "two configurations must be specified for config link";

    const dir_path& cd (cfgs[0]->path);
    const dir_path& ld (cfgs[1]->path);

    // Call bpkg to link the configurations.
    //
    // If possible, rebase the linked configuration directory path relative to
    // the other configuration path.
    //
    run_bpkg (2,
              o,
              "cfg-link",
              ld.try_relative (cd) ? "--relative" : nullptr,
              "-d", cd,
              ld);

    if (verb)
      text << "linked configuration " << *cfgs[0] << " (" << cfgs[0]->type
           << ") with configuration " << *cfgs[1] << " (" << cfgs[1]->type
           << ")";

    return 0;
  }

  static int
  cmd_config_list (const cmd_config_options& o, cli::scanner&)
  {
    tracer trace ("config_list");

    dir_path prj (find_project (o));
    database db (open (prj, trace));

    transaction t (db.begin ());

    configurations cfgs;
    if (o.config_specified ()    || // Note: handling --all|-a ourselves.
        o.config_id_specified () ||
        o.config_name_specified ())
    {
      cfgs = find_configurations (o,
                                  prj,
                                  t,
                                  false /* fallback_default */,
                                  false /* validate         */).first;
    }
    else
    {
      using query = bdep::query<configuration>;

      // We want to show the default configuration first, then sort them
      // by name, and then by path.
      //
      for (auto c: pointer_result (
             db.query<configuration> ("ORDER BY" +
                                      query::default_ + "DESC," +
                                      query::name     + "IS NULL," +
                                      query::name     + "," +
                                      query::path)))
        cfgs.push_back (move (c));
    }

    t.commit ();

    for (const shared_ptr<configuration>& c: cfgs)
    {
      //@@ TODO: use tabular layout facility when ready.

      print_configuration (cout, c);
      cout << endl;
    }

    return 0;
  }

  static int
  cmd_config_move (cmd_config_options& o, cli::scanner& args)
  {
    tracer trace ("config_move");

    if (!args.more ())
      fail << "configuration directory argument expected";

    dir_path prj (find_project (o));

    // Similar story to config-add.
    //
    dir_path path;
    optional<dir_path> rel_path;
    {
      const char* a (args.next ());
      try
      {
        path = dir_path (a);

        if (!exists (path))
          fail << "configuration directory " << path << " does not exist";

        normalize (path, "configuration");
      }
      catch (const invalid_path&)
      {
        fail << "invalid configuration directory '" << a << "'";
      }

      rel_path = path.try_relative (prj);
    }

    database db (open (prj, trace));

    session ses;
    transaction t (db.begin ());

    configurations cfgs (
      find_configurations (o,
                           prj,
                           t,
                           false /* fallback_default */,
                           false /* validate         */).first);

    if (cfgs.size () > 1)
      fail << "multiple configurations specified for config move";

    const shared_ptr<configuration>& c (cfgs.front ());

    // Check if there is already a configuration with this path.
    //
    using query = bdep::query<configuration>;

    if (auto p = db.query_one<configuration> (query::path == path.string ()))
    {
      // Note that this also covers the case where p == c.
      //
      fail << "configuration " << *p << " already uses directory " << path;
    }

    // Save the old path for diagnostics.
    //
    // @@ We should probably also adjust explicit and implicit links in the
    //    respective bpkg configurations, if/when bpkg provides the required
    //    API.
    //
    c->path.swap (path);
    c->relative_path = move (rel_path);

    db.update (c);
    t.commit ();

    if (verb)
    {
      // Restore the original path so that we can use print_configuration().
      //
      path.swap (c->path);

      {
        diag_record dr (text);
        dr << "moved configuration ";
        print_configuration (dr, c, false /* flags */);
        dr << " to " << path;
      }

      info << "explicit sync command is required for changes to take effect";
    }

    return 0;
  }

  static int
  cmd_config_rename (cmd_config_options& o, cli::scanner& args)
  {
    tracer trace ("config_rename");

    // Let's be nice and allow specifying the new name as noth <name> and
    // @<name>.
    //
    string name;
    if (args.more ())
    {
      name = args.next ();

      if (name.empty ())
        fail << "empty configuration name specified";
    }
    else
    {
      strings& ns (o.config_name ());
      size_t n (ns.size ());

      if (n > 1 || (n == 1 && (o.config_specified () ||
                               o.config_id_specified ())))
      {
        name = move (ns.back ());
        ns.pop_back ();
      }
      else
        fail << "configuration name argument expected";
    }

    dir_path prj (find_project (o));
    database db (open (prj, trace));

    session ses;
    transaction t (db.begin ());

    configurations cfgs (
      find_configurations (o,
                           prj,
                           t,
                           false /* fallback_default */,
                           false /* validate         */).first);

    if (cfgs.size () > 1)
      fail << "multiple configurations specified for config rename";

    const shared_ptr<configuration>& c (cfgs.front ());

    // Check if this name is already taken.
    //
    using query = bdep::query<configuration>;

    if (auto p = db.query_one<configuration> (query::name == name))
    {
      // Note that this also covers the case where p == c.
      //
      fail << "configuration " << p->path << " is already called " << name;
    }

    // Save the old name for diagnostics.
    //
    if (c->name)
      name.swap (*c->name);
    else
      c->name = move (name);

    db.update (c);
    t.commit ();

    if (verb)
    {
      // Restore the original name so that we can use print_configuration().
      //
      name.swap (*c->name);

      diag_record dr (text);
      dr << "renamed configuration ";
      print_configuration (dr, c, false /* flags */);
      dr << " to @" << name;
    }

    return 0;
  }

  static int
  cmd_config_remove (const cmd_config_options& o, cli::scanner&)
  {
    tracer trace ("config_remove");

    dir_path prj (find_project (o));
    database db (open (prj, trace));

    transaction t (db.begin ());

    configurations cfgs (
      find_configurations (o,
                           prj,
                           t,
                           false /* fallback_default */,
                           false /* validate         */).first);

    for (const shared_ptr<configuration>& c: cfgs)
    {
      if (!c->packages.empty ())
      {
        bool e (exists (c->path));

        fail << "configuration " << *c << " contains initialized packages" <<
          info << "use deinit " << (e ? "" : "--force ") << "command to "
             << "deinitialize packages" <<
          info << "use status command to list initialized packages";
      }

      db.erase (c);
    }

    t.commit ();

    if (verb)
    {
      for (const shared_ptr<configuration>& c: cfgs)
      {
        diag_record dr (text);
        dr << "removed configuration ";
        print_configuration (dr, c, false /* flags */);
      }
    }

    return 0;
  }

  static int
  cmd_config_set (const cmd_config_options& o, cli::scanner&)
  {
    tracer trace ("config_set");

    // Note that these have been validate by cmd_config_validate_add().
    //
    optional<bool> d, f, s;
    if (o.default_  () || o.no_default   ()) d = o.default_  ();
    if (o.forward   () || o.no_forward   ()) f = o.forward   ();
    if (o.auto_sync () || o.no_auto_sync ()) s = o.auto_sync ();

    if (!d && !f && !s)
      fail << "nothing to set";

    dir_path prj (find_project (o));
    database db (open (prj, trace));

    session ses;
    transaction t (db.begin ());

    configurations cfgs (
      find_configurations (o,
                           prj,
                           t,
                           false /* fallback_default */,
                           false /* validate         */).first);

    for (const shared_ptr<configuration>& c: cfgs)
    {
      using query = bdep::query<configuration>;

      // Verify that there is no other default or forwarded configuration with
      // the same package as us.
      //
      auto verify = [&c, &db] (const query& q, const char* what)
      {
        for (const shared_ptr<configuration>& o:
               pointer_result (db.query<configuration> (q)))
        {
          auto i (find_first_of (
                    o->packages.begin (), o->packages.end (),
                    c->packages.begin (), c->packages.end (),
                    [] (const package_state& x, const package_state& y)
                    {
                      return x.name == y.name;
                    }));

          if (i != o->packages.end ())
            fail << "configuration " << *o << " is also " << what << " and "
                 << "also has package " << i->name << " initialized" <<
              info << "while updating configuration " << *c;
        }
      };

      if (d)
      {
        if (*d && !c->default_)
        {
          if (auto p = db.query_one<configuration> (query::default_ &&
                                                    query::type == c->type))
            fail << "configuration " << *p << " of type " << p->type
                 << " is already the default" <<
              info << "while updating configuration " << *c;

          verify (query::default_, "default");
        }

        c->default_ = *d;
      }

      if (f)
      {
        if (*f && !c->forward)
          verify (query::forward, "forwarded");

        c->forward = *f;
      }

      if (s)
        c->auto_sync = *s;

      db.update (c);
    }

    t.commit ();

    if (verb)
    {
      for (const shared_ptr<configuration>& c: cfgs)
      {
        diag_record dr (text);
        dr << "updated configuration ";
        print_configuration (dr, c);
      }

      info << "explicit sync command is required for changes to take effect";
    }

    return 0;
  }

  int
  cmd_config (cmd_config_options&& o, cli::scanner& scan)
  {
    tracer trace ("config");

    cmd_config_subcommands c (
      parse_command<cmd_config_subcommands> (scan,
                                             "config subcommand",
                                             "bdep help config"));
    // Validate options/subcommands.
    //
    if (const char* n = cmd_config_validate_add (o))
    {
      if (!c.add () && !c.create () && !c.set ())
        fail << n << " not valid for this subcommand";

      if (o.config_type_specified () && !c.create ())
        fail << "--config-type is not valid for this subcommand";

      if (o.existing () && !c.create ())
        fail << "--existing|-e is not valid for this subcommand";

      if (o.wipe () && !c.create ())
        fail << "--wipe is not valid for this subcommand";
    }

    // --all
    //
    if (o.all ())
    {
      if (!c.list () && !c.remove () && !c.set ())
        fail << "--all not valid for this subcommand";
    }

    // Dispatch to subcommand function.
    //
    if (c.add    ()) return cmd_config_add    (o, scan);
    if (c.create ()) return cmd_config_create (o, scan);
    if (c.link   ()) return cmd_config_link   (o, scan);
    if (c.list   ()) return cmd_config_list   (o, scan);
    if (c.move   ()) return cmd_config_move   (o, scan);
    if (c.rename ()) return cmd_config_rename (o, scan);
    if (c.remove ()) return cmd_config_remove (o, scan);
    if (c.set    ()) return cmd_config_set    (o, scan);

    assert (false); // Unhandled (new) subcommand.
    return 1;
  }

  default_options_files
  options_files (const char*,
                 const cmd_config_options& o,
                 const strings& args)
  {
    // NOTE: remember to update the documentation if changing anything here.

    // bdep.options
    // bdep-config.options
    // bdep-config-<subcmd>.options
    //
    // Note that bdep-config-add.options is loaded for both the add and
    // create subcommands since create is-a add.
    //
    // Also note that these files are loaded by other commands (bdep-init
    // and bdep-new).

    default_options_files r {
      {path ("bdep.options"), path ("bdep-config.options")},
      find_project (o)};

    // Validate the subcommand.
    //
    {
      cli::vector_scanner scan (args);
      parse_command<cmd_config_subcommands> (scan,
                                             "config subcommand",
                                             "bdep help config");
    }

    const string& sc (args[0]);

    auto add = [&r] (const string& n)
    {
      r.files.push_back (path ("bdep-" + n + ".options"));
    };

    if (sc == "create")
      add ("config-add");

    add ("config-" + sc);

    return r;
  }

  cmd_config_options
  merge_options (const default_options<cmd_config_options>& defs,
                 const cmd_config_options& cmd)
  {
    // NOTE: remember to update the documentation if changing anything here.

    return merge_default_options (
      defs,
      cmd,
      [] (const default_options_entry<cmd_config_options>& e,
          const cmd_config_options&)
      {
        const cmd_config_options& o (e.options);

        auto forbid = [&e] (const char* opt, bool specified)
        {
          if (specified)
            fail (e.file) << opt << " in default options file";
        };

        forbid ("--directory|-d", o.directory_specified ());
        forbid ("--wipe",         o.wipe ()); // Dangerous.
      });
  }
}
