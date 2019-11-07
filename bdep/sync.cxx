// file      : bdep/sync.cxx -*- C++ -*-
// copyright : Copyright (c) 2014-2019 Code Synthesis Ltd
// license   : MIT; see accompanying LICENSE file

#include <bdep/sync.hxx>

#include <cstring>  // strchr()

#include <libbpkg/manifest.hxx>

#include <bdep/database.hxx>
#include <bdep/diagnostics.hxx>
#include <bdep/project-odb.hxx>

#include <bdep/fetch.hxx>

using namespace std;

namespace bdep
{
  const path hook_file (
    dir_path ("build") / "bootstrap" / "pre-bdep-sync.build");

  dir_paths
  configuration_projects (const common_options& co,
                          const dir_path& cfg,
                          const dir_path& prj)
  {
    using bpkg::repository_type;
    using bpkg::repository_location;

    dir_paths r;

    // Use bpkg-rep-list to discover the list of project directories.
    //
    fdpipe pipe (open_pipe ()); // Text mode seems appropriate.

    process pr (start_bpkg (3,
                            co,
                            pipe /* stdout */,
                            2    /* stderr */,
                            "rep-list",
                            "-d", cfg));

    // Shouldn't throw, unless something is severely damaged.
    //
    pipe.out.close ();

    bool io (false);
    try
    {
      ifdstream is (move (pipe.in), fdstream_mode::skip, ifdstream::badbit);

      for (string l; !eof (getline (is, l)); )
      {
        // Skip repository locations other than dir (who knows what else the
        // user might have added).
        //
        if (l.compare (0, 4, "dir:") != 0)
          continue;

        // Note that the dir repository type can not be guessed and so its URL
        // is always typed. Thus, it has a URL notation and so can't contain
        // the space characters (unlike the canonical name). That's why we can
        // just search for the rightmost space to find the beginning of the
        // repository URL.
        //
        size_t p (l.rfind (' '));
        if (p == string::npos)
          fail << "invalid bpkg-rep-list output: no repository location";

        dir_path d;

        try
        {
          repository_location rl (string (l, p + 1));

          assert (rl.type () == repository_type::dir);

          // Note that the directory is absolute and normalized (see
          // <libbpkg/manifest.hxx> for details).
          //
          d = path_cast<dir_path> (rl.path ());

          assert (d.absolute ());
        }
        catch (const invalid_argument& e)
        {
          fail << "invalid bpkg-rep-list output: " << e;
        }

        if (d == prj)
          continue;

        // Next see if it looks like a bdep-managed project.
        //
        if (!exists (d / bdep_file))
          continue;

        r.push_back (move (d));
      }

      is.close (); // Detect errors.
    }
    catch (const io_error&)
    {
      // Presumably the child process failed and issued diagnostics so let
      // finish_bpkg() try to deal with that first.
      //
      io = true;
    }

    finish_bpkg (co, pr, io);

    return r;
  }

  // Project to be synchronized.
  //
  namespace
  {
    struct project
    {
      dir_path path;
      shared_ptr<configuration> config;

      bool implicit;
      bool fetch;
    };

    using projects = small_vector<project, 1>;
  }

  // Append the list of additional (to origin, if not empty) projects that are
  // using this configuration.
  //
  static void
  load_implicit (const common_options& co,
                 const dir_path& cfg,
                 const dir_path& origin_prj,
                 projects& r)
  {
    tracer trace ("load_implicit");

    for (dir_path& d: configuration_projects (co, cfg, origin_prj))
    {
      shared_ptr<configuration> c;
      {
        using query = bdep::query<configuration>;

        // Save and restore the current transaction, if any.
        //
        transaction* ct (nullptr);
        if (transaction::has_current ())
        {
          ct = &transaction::current ();
          transaction::reset_current ();
        }

        auto tg (make_guard ([ct] ()
                             {
                               if (ct != nullptr)
                                 transaction::current (*ct);
                             }));

        {
          database db (open (d, trace));
          transaction t (db.begin ());
          c = db.query_one<configuration> (query::path == cfg.string ());
          t.commit ();
        }
      }

      // If the project is a repository of this configuration but the bdep
      // database has no knowledge of this configuration, then assume it is
      // not managed by bdep (i.e., the user added the project manually or
      // some such).
      //
      if (c == nullptr)
        continue;

      r.push_back (project {move (d),
                            move (c),
                            true /* implicit */,
                            true /* fetch */});
    }
  }

  // Sync with optional upgrade.
  //
  // If upgrade is not nullopt, then: If there are dep_pkgs, then we are
  // upgrading specific dependency packages. Othewise -- project packages.
  //
  static void
  cmd_sync (const common_options& co,
            const dir_path& cfg,
            const dir_path& origin_prj,
            const shared_ptr<configuration>& origin_config,
            const strings& pkg_args,
            bool implicit,
            bool fetch,
            bool yes,
            bool name_cfg,
            optional<bool> upgrade,   // true - upgrade,   false - patch
            optional<bool> recursive, // true - recursive, false - immediate
            const package_locations& prj_pkgs,
            const strings&           dep_pkgs)
  {
    assert (origin_config == nullptr || !origin_config->packages.empty ());
    assert (prj_pkgs.empty () || dep_pkgs.empty ()); // Can't have both.

    projects prjs;

    if (origin_config != nullptr)
      prjs.push_back (project {origin_prj, origin_config, implicit, fetch});

    // Load other projects that might be using the same configuration -- we
    // have to synchronize everything at once.
    //
    load_implicit (co, cfg, origin_prj, prjs);

    // Verify that no initialized package in any of the projects sharing this
    // configuration is specified as a dependency.
    //
    for (const string& n: dep_pkgs)
    {
      for (const project& prj: prjs)
      {
        auto& pkgs (prj.config->packages);

        if (find_if (pkgs.begin (),
                     pkgs.end (),
                     [&n] (const package_state& ps)
                     {
                       return n == ps.name;
                     }) != pkgs.end ())
          fail << "initialized package " << n << " specified as a dependency";
      }
    }

    // Prepare the list of packages to build and repositories to fetch.
    //
    strings args;
    strings reps;

    // First add configuration variables from pkg_args, if any.
    //
    {
      for (const string& a: pkg_args)
        if (a.find ('=') != string::npos)
          args.push_back (a);

      if (!args.empty ())
        args.push_back ("--");
    }

    for (const project& prj: prjs)
    {
      if (prj.fetch)
        reps.push_back ("dir:" + prj.path.string ());

      for (const package_state& pkg: prj.config->packages)
      {
        if (upgrade && !prj.implicit)
        {
          // We synchronize all the init'ed packages but only upgrade the
          // specified.
          //
          if (find_if (prj_pkgs.begin (),
                       prj_pkgs.end (),
                       [&pkg] (const package_location& pl)
                       {
                         return pl.name == pkg.name;
                       }) != prj_pkgs.end ())
          {
            // The project package itself must always be upgraded to the
            // latest version/iteration. So we have to translate to
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

        // We need to add the explicit location qualification (@<rep-loc>)
        // since there is no guarantee a higher version isn't available from
        // another repository.
        //
        args.push_back (pkg.name.string () + '@' + prj.path.string ());
      }
    }

    // Add dependencies to upgrade (if any).
    //
    if (upgrade)
    {
      for (const string& n: dep_pkgs)
      {
        // Unless this is the default "non-recursive upgrade" we need to add a
        // group.
        //
        if (recursive || !*upgrade)
        {
          args.push_back ("{");

          string o (*upgrade ? "-u" : "-p");
          if (recursive) o += *recursive ? 'r' : 'i';
          args.push_back (move (o));

          args.push_back ("}+");
        }

        // Make sure it is treated as a dependency.
        //
        args.push_back ('?' + n);
      }
    }

    // Finally, add packages from pkg_args, if any.
    //
    for (const string& a: pkg_args)
      if (a.find ('=') == string::npos)
        args.push_back (a);

    // We do a separate fetch instead of letting pkg-build do it. This way we
    // get better control of the diagnostics (no "fetching ..." for the
    // project itself). We also make sure that if the user specifies a
    // repository for a dependency to upgrade, then that repository is listed
    // as part of the project prerequisites/complements. Or, in other words,
    // we only want to allow specifying the location as a proxy for specifying
    // version (i.e., "I want this dependency upgraded to the latest version
    // available from this repository").
    //
    // Note also that we use the repository name rather than the location as a
    // sanity check (the repository must have been added as part of init).
    // Plus, without 'dir:' it will be treated as version control-based.
    //
    // @@ For implicit fetch this will print diagnostics/progress before
    //    "synchronizing <cfg-dir>:". Maybe rep-fetch also needs something
    //    like --plan but for progress? Plus there might be no sync at all.
    //
    if (!reps.empty ())
      run_bpkg (3, co, "fetch", "-d", cfg, "--shallow", reps);

    string plan ("synchronizing");
    if (name_cfg)
    {
      plan += ' ';

      // Use name if available, directory otherwise.
      //
      if (origin_config != nullptr && origin_config->name)
      {
        plan += '@';
        plan += *origin_config->name;
      }
      else
        plan += cfg.representation ();
    }
    plan += ':';

    run_bpkg (2,
              co,
              "build",
              "-d", cfg,
              "--no-fetch",
              "--configure-only",
              "--keep-out",
              "--plan", plan,
              (yes ? "--yes" : nullptr),
              args);

    // Handle configuration forwarding.
    //
    // We do it here (instead of, say init) because a change in a package may
    // introduce new subprojects. Though it would be nice to only do this if
    // the package was upgraded.
    //
    // @@ TODO: changed flag below: Probably by comparing before/after
    //          versions. Or, even simpler, if pkg-build signalled that
    //          nothing has changed (the --exit-result idea) -- then we
    //          can skip the whole thing.
    //
    // Also, the current thinking is that config set --[no-]forward is best
    // implemented by just changing the flag on the configuration and then
    // requiring an explicit sync to configure/disfigure forwards.
    //
    for (const project& prj: prjs)
    {
      package_locations pls (load_packages (prj.path));

      for (const package_state& pkg: prj.config->packages)
      {
        // If this is a forwarded configuration, make sure forwarding is
        // configured and is up-to-date. Otherwise, make sure it is disfigured
        // (the config set --no-forward case).
        //
        dir_path src (prj.path);
        {
          auto i (find_if (pls.begin (),
                           pls.end (),
                           [&pkg] (const package_location& pl)
                           {
                             return pkg.name == pl.name;
                           }));

          if (i == pls.end ())
            fail << "package " << pkg.name << " is not listed in " << prj.path;

          src /= i->path;
        }

        // We could run 'b info' and used the 'forwarded' value but this is
        // both faster and simpler. Or at least it was until we got the
        // alternative naming scheme.
        //
        auto check = [&src] ()
        {
          path f (src / "build2" / "bootstrap" / "out-root.build2");
          bool e (exists (f));

          if (!e)
          {
            f = src / "build" / "bootstrap" / "out-root.build";
            e = exists (f);
          }

          return e;
        };

        const char* o (nullptr);
        if (prj.config->forward)
        {
          bool changed (true);

          if (changed || !check ())
            o = "configure:";
        }
        else if (!prj.implicit) // Requires explicit sync.
        {
          //@@ This is broken: we will disfigure forwards to other configs.
          //   Looks like we will need to test that the forward is to this
          //   config. 'b info' here we come?

          //if (check ())
          //  o = "disfigure:";
        }

        if (o != nullptr)
        {
          dir_path out (dir_path (cfg) /= pkg.name.string ());
          run_b (co,
                 o,
                 "'" + src.representation () + "'@'" + out.representation () +
                 "',forward");
        }
      }
    }

    // Add/remove auto-synchronization build system hook.
    //
    if (origin_config != nullptr && !implicit)
    {
      path f (cfg / hook_file);
      bool e (exists (f));

      if (origin_config->auto_sync)
      {
        if (!e)
        {
          mk (f.directory ());

          try
          {
            ofdstream os (f);

            // Should we analyze BDEP_SYNCED_CONFIGS ourselves or should we
            // let bdep-sync do it for us? Doing it here instead of spawning a
            // process (which will load the database, etc) will be faster.
            // But, on the other hand, this is only an issue for commands like
            // update and test that do their own implicit sync.
            //
            // cfgs = $getenv(BDEP_SYNCED_CONFIGS)
            // if! $null($cfgs)
            //   cfgs = [dir_paths] $regex.split($cfgs, ' *"([^"]*)" *', '\1')
            //
            // Also note that we try to avoid setting any variables in order
            // not to pollute the configuration's root scope.
            //
            os << "# Created automatically by bdep."                   << endl
               << "#"                                                  << endl
               << "if ($build.meta_operation != 'info'      && \\"     << endl
               << "    $build.meta_operation != 'configure' && \\"     << endl
               << "    $build.meta_operation != 'disfigure')"          << endl
               << "{"                                                  << endl
               << "  if ($getenv('BDEP_SYNC') == [null] || \\"         << endl
               << "      $getenv('BDEP_SYNC') == true   || \\"         << endl
               << "      $getenv('BDEP_SYNC') == 1)"                   << endl
               << "    run '" << argv0 << "' sync --hook=1 "           <<
              "--verbose $build.verbosity "                            <<
              "--config \"$out_root\""                                 << endl
               << "}"                                                  << endl;

            os.close ();
          }
          catch (const io_error& e)
          {
            fail << "unable to write to " << f << ": " << e;
          }
        }
      }
      else
      {
        if (e)
          rm (f);
      }
    }
  }

  // The BDEP_SYNCED_CONFIGS environment variable.
  //
  // Note that it covers both depth and breadth (i.e., we don't restore the
  // previous value before returning). The idea here is for commands like
  // update or test would perform an implicit sync which will then be
  // "noticed" by the build system hook. This should be both faster (no need
  // to spawn multiple bdep processes) and simpler (no need to worry about
  // who has the database open, etc).
  //
  // We also used to do this only in the first form of sync but it turns out
  // we may end up invoking a hook during upgrade (e.g., to prepare a
  // distribution of a package as part of pkg-checkout which happens in the
  // configuration we have "hooked" -- yeah, this rabbit hole is deep).
  //
  static const char synced_name[] = "BDEP_SYNCED_CONFIGS";

  // Check if the specified configuration directory is already (being)
  // synchronized. If it is not and add is true, then add it to the
  // BDEP_SYNCED_CONFIGS environment variable.
  //
  static bool
  synced (const dir_path& d, bool implicit, bool add = true)
  {
    string v;
    const string& p (d.string ());

    if (optional<string> e = getenv (synced_name))
      v = move (*e);

    for (size_t b (0), e (0);
         (e = v.find ('"', e)) != string::npos; // Skip leading ' '.
         ++e)                                   // Skip trailing '"'.
    {
      size_t n (next_word (v, b, e, '"'));

      // Both paths are normilized so we can just compare them as
      // strings.
      //
      if (path::traits_type::compare (v.c_str () + b, n,
                                      p.c_str (),     p.size ()) == 0)
      {
        if (implicit)
          return true;
        else
          fail << "explicit re-synchronization of " << d;
      }
    }

    if (add)
    {
      v += (v.empty () ? "\"" : " \"") + p + '"';
      setenv (synced_name, v);
    }

    return false;
  }

  void
  cmd_sync (const common_options& co,
            const dir_path& prj,
            const shared_ptr<configuration>& c,
            const strings& pkg_args,
            bool implicit,
            bool fetch,
            bool yes,
            bool name_cfg)
  {
    if (!synced (c->path, implicit))
      cmd_sync (co,
                c->path,
                prj,
                c,
                pkg_args,
                implicit,
                fetch,
                yes,
                name_cfg,
                nullopt              /* upgrade   */,
                nullopt              /* recursive */,
                package_locations () /* prj_pkgs  */,
                strings ()           /* dep_pkgs  */);
  }

  void
  cmd_sync_implicit (const common_options& co,
                     const dir_path& cfg,
                     bool fetch,
                     bool yes,
                     bool name_cfg)
  {
    if (!synced (cfg, true /* implicit */))
      cmd_sync (co,
                cfg,
                dir_path (),
                nullptr,
                strings (),
                true /* implicit */,
                fetch,
                yes,
                name_cfg,
                nullopt              /* upgrade   */,
                nullopt              /* recursive */,
                package_locations () /* prj_pkgs  */,
                strings ()           /* dep_pkgs  */);
  }

  int
  cmd_sync (cmd_sync_options&& o, cli::group_scanner& args)
  {
    tracer trace ("sync");

    // We have two pretty different upgrade modes: project package upgrade and
    // dependency package upgrade (have non-pkg-args arguments).
    //
    if (o.upgrade () && o.patch ())
      fail << "both --upgrade|-u and --patch|-p specified";

    if (o.immediate () && o.recursive ())
      fail << "both --immediate|-i and --recursive|-r specified";

    // The --immediate or --recursive option can only be specified with
    // an explicit --upgrade or --patch.
    //
    if (const char* n = (o.immediate () ? "--immediate" :
                         o.recursive () ? "--recursive" : nullptr))
    {
      if (!o.upgrade () && !o.patch ())
        fail << n << " requires explicit --upgrade|-u or --patch|-p";
    }

    // Sort arguments (if any) into pkg-args and dep-spec: if the argument
    // starts with '?' (dependency flag) or contains '=' (config variable),
    // then we assume it is pkg-args.
    //
    strings pkg_args;
    strings dep_pkgs;
    while (args.more ())
    {
      const char* r (args.peek ());
      scan_argument (
        (*r == '?' || strchr (r, '=') != nullptr) ? pkg_args : dep_pkgs,
        args);
    }

    // --hook
    //
    if (o.hook_specified ())
    {
      if (o.hook () != 1)
        fail << "unsupported build system hook protocol" <<
          info << "project requires re-initialization";

      if (o.directory_specified ())
        fail << "--hook specified with project directory";

      o.implicit (true); // Implies --implicit.
    }

    // --implicit
    //
    if (o.implicit ())
    {
      if (const char* n = (o.upgrade () ? "--upgrade|-u" :
                           o.patch ()   ? "--patch|-p"   : nullptr))
        fail << "--implicit specified with " << n;

      if (!dep_pkgs.empty ())
        fail << "--implicit specified with dependency package";

      if (!o.directory_specified ())
      {
        // All of these options require an originating project.
        //
        if (o.fetch () || o.fetch_full ())
          fail << "--implicit specified with --fetch";

        if (o.config_id_specified ())
          fail << "--implicit specified with --config-id";

        if (o.config_name_specified ())
          fail << "--implicit specified with --config-name";

        if (!o.config_specified ())
          fail << "no project or configuration specified with --implicit";
      }
    }

    dir_path prj; // Empty if we have no originating project.
    package_locations prj_pkgs;
    configurations cfgs;
    dir_paths cfg_dirs;

    // In the implicit mode we don't search the current working directory
    // for a project.
    //
    if (o.directory_specified () || !o.implicit ())
    {
      // We could be running from a package directory (or the user specified
      // one with -d) that has not been init'ed in this configuration. Unless
      // we are upgrading specific dependencies, we want to diagnose that
      // since such a package will not be present in the bpkg configuration.
      // But if we are running from the project, then we don't want to treat
      // all the available packages as specified by the user (thus
      // load_packages is false).
      //
      project_packages pp (
        find_project_packages (o,
                               !dep_pkgs.empty () /* ignore_packages */,
                               false              /* load_packages   */));

      // Initialize tmp directory.
      //
      init_tmp (pp.project);

      // Load project configurations.
      //
      {
        database db (open (pp.project, trace));

        transaction t (db.begin ());
        cfgs = find_configurations (o, pp.project, t);
        t.commit ();
      }

      // If specified, verify packages are present in each configuration.
      //
      if (!pp.packages.empty ())
        verify_project_packages (pp, cfgs);

      prj = move (pp.project);
      prj_pkgs = move (pp.packages);
    }
    else
    {
      // Implicit sync without an originating project.
      //
      // Here, besides the BDEP_SYNCED_CONFIGS we also check BPKG_OPEN_CONFIG
      // which will be set if we are called by bpkg while it has the database
      // open.
      //
      // The question, of course, is whether it's a good idea to suppress sync
      // in this case. Currently, this is a problem for two operations (others
      // like pkg-update/test are careful enough to close the database before
      // executing the build system): pkg-checkout which uses dist to checkout
      // a new package and pkg-disfigure which uses clean to clean a package.
      // In both cases it seems harmless to suppress sync.
      //
      // @@ Maybe it makes sense for bpkg to run 'b noop' in order to "flush"
      //    the callbacks before certain commands (like pkg-build)? Note that
      //    noop loads the buildfiles. Maybe need something like bootstrap
      //    and load meta-operation?
      //
      optional<string> open (getenv ("BPKG_OPEN_CONFIG"));

      for (dir_path d: o.config ())
      {
        normalize (d, "configuration");

        if (open && d.string () == *open)
          continue;

        if (synced (d, o.implicit (), false /* add */))
          continue;

        cfgs.push_back (nullptr);
        cfg_dirs.push_back (move (d));
      }

      if (cfgs.empty ())
        return 0; // All configuration are already (being) synchronized.

      // Initialize tmp directory.
      //
      init_tmp (empty_dir_path);
    }

    // Synchronize each configuration.
    //
    for (size_t i (0), n (cfgs.size ()); i != n; ++i)
    {
      const shared_ptr<configuration>& c (cfgs[i]); // Can be NULL.
      const dir_path& cd (c != nullptr ? c->path : cfg_dirs[i]);

      // Check if this configuration is already (being) synchronized.
      //
      if (synced (cd, o.implicit ()))
        continue;

      // Skipping empty ones.
      //
      if (c != nullptr && c->packages.empty ())
      {
        if (verb)
          info << "skipping empty configuration " << *c;

        continue;
      }

      // If we are synchronizing multiple configurations, separate them with a
      // blank line and print the configuration name/directory.
      //
      if (verb && n > 1)
        text << (i == 0 ? "" : "\n")
             << "in configuration " << *c << ':';

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
                  cd,
                  prj,
                  c,
                  pkg_args,
                  false /* implicit */,
                  !fetch,
                  o.recursive () || o.immediate () ? o.yes () : true,
                  false /* name_cfg */,
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
                  cd,
                  prj,
                  c,
                  pkg_args,
                  false /* implicit */,
                  !fetch,
                  o.yes (),
                  false /* name_cfg */,
                  o.upgrade (),
                  o.recursive (),
                  prj_pkgs,
                  strings () /* dep_pkgs  */);
      }
      else
      {
        // The first form: sync of project packages (potentially implicit).
        //
        // For implicit sync (normally performed on one configuration at a
        // time) add the configuration name/directory to the plan header.
        //
        cmd_sync (o,
                  cd,
                  prj,
                  c,
                  pkg_args,
                  o.implicit (),
                  !fetch,
                  true                 /* yes       */,
                  o.implicit ()        /* name_cfg  */,
                  nullopt              /* upgrade   */,
                  nullopt              /* recursive */,
                  package_locations () /* prj_pkgs  */,
                  strings ()           /* dep_pkgs  */);
      }
    }

    return 0;
  }

  default_options_files
  options_files (const char*, const cmd_sync_options& o, const strings&)
  {
    // NOTE: remember to update the documentation if changing anything here.

    // bdep.options
    // bdep-{sync|sync-implicit}.options

    default_options_files r {{path ("bdep.options")}, nullopt};

    // Add bdep-sync-implicit.options for an implicit sync and
    // bdep-sync.options otherwise. In the former case try to find a common
    // start directory using the configuration directories.
    //
    auto add = [&r] (const string& n)
    {
      r.files.push_back (path ("bdep-" + n + ".options"));
    };

    if (o.implicit () || o.hook_specified ())
    {
      add ("sync-implicit");

      r.start = default_options_start (home_directory (), o.config ());
    }
    else
    {
      add ("sync");

      r.start = find_project (o);
    }

    return r;
  }

  cmd_sync_options
  merge_options (const default_options<cmd_sync_options>& defs,
                 const cmd_sync_options& cmd)
  {
    // NOTE: remember to update the documentation if changing anything here.

    return merge_default_options (
      defs,
      cmd,
      [] (const default_options_entry<cmd_sync_options>& e,
          const cmd_sync_options&)
      {
        const cmd_sync_options& o (e.options);

        auto forbid = [&e] (const char* opt, bool specified)
        {
          if (specified)
            fail (e.file) << opt << " in default options file";
        };

        forbid ("--directory|-d", o.directory_specified ());
        forbid ("--implicit",     o.implicit ());
        forbid ("--hook",         o.hook_specified ());
        forbid ("--config|-c",    o.config_specified ());
      });
  }
}
