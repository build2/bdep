// file      : bdep/sync.cxx -*- C++ -*-
// license   : MIT; see accompanying LICENSE file

#include <bdep/sync.hxx>

#include <cstring>  // strchr()

#include <libbpkg/manifest.hxx>

#include <bdep/database.hxx>
#include <bdep/diagnostics.hxx>
#include <bdep/project-odb.hxx>

#include <bdep/fetch.hxx>
#include <bdep/config.hxx>

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

  // Find/create and link a configuration suitable for build-time dependency.
  //
  static void
  link_dependency_config (const common_options& co,
                          const dir_path& cfg,
                          const projects& prjs,
                          const dir_path& origin_prj,
                          const shared_ptr<configuration>& origin_config,
                          const strings& dep_chain,
                          bool create_host_config,
                          bool create_build2_config,
                          transaction* tr,
                          vector<pair<dir_path, string>>* created_cfgs,
                          tracer& trace)
  {
    // If the configuration is required, then the bpkg output contains the
    // build-time dependency line followed by the dependent lines (starting
    // from the immediate dependent; see bpkg-pkg-build(1) for details).
    //
    if (dep_chain.size () < 2)
      fail << "invalid bpkg-pkg-build output: invalid dependency chain";

    // Extract the build-time dependency package name.
    //
    package_name dep;

    try
    {
      const string& l (dep_chain[0]);
      dep = package_name (string (l, 0, l.find (' ')));
    }
    catch (const invalid_argument& e)
    {
      fail << "invalid bpkg-pkg-build output line '" << dep_chain[0]
           << "': invalid package name: " << e;
    }

    // Determine the required configuration type.
    //
    string dep_type (dep.string ().compare (0, 10, "libbuild2-") == 0
                     ? "build2"
                     : "host");

    // Extract the dependent package names, versions, and configuration
    // directories. Note that these configurations can differ from the
    // configuration which we are syncing.
    //
    struct dependent
    {
      package_name  name;
      bpkg::version version;
      dir_path      config;

      // Parse the bpkg's output's dependent line.
      //
      explicit
      dependent (const string& line)
      {
        const char* ep ("invalid bpkg-pkg-build output line ");

        size_t n (line.find (' '));

        try
        {
          if (n == string::npos)
            throw invalid_path ("");

          config = dir_path (string (line, n + 1));
        }
        catch (const invalid_path&)
        {
          fail << ep << "'" << line << "': invalid dependent package "
               << "configuration directory";
        }

        string s (line, 0, n);

        try
        {
          name = bpkg::extract_package_name (s);
        }
        catch (const invalid_argument& e)
        {
          fail << ep << "'" << line << "': invalid dependent package "
               << "name: " << e;
        }

        try
        {
          version = bpkg::extract_package_version (s);

          if (version.empty ())
            fail << ep << "'" << line << "': dependent package version is "
                 << "not specified";
        }
        catch (const invalid_argument& e)
        {
          fail << ep << "'" << line << "': invalid dependent package "
               << "version: " << e;
        }
      }
    };
    vector<dependent> dependents;

    for (size_t i (1); i != dep_chain.size (); ++i)
      dependents.emplace_back (dep_chain[i]);

    // Check if the specified configuration is associated with the specified
    // project returning its configuration object if found and NULL
    // otherwise.
    //
    auto find_config = [&origin_config, &origin_prj] (database& db,
                                                      const dir_path& prj,
                                                      const dir_path& cfg)
    {
      // Note that this is not merely an optimization since the origin
      // configuration can be changed but not updated in the database yet (see
      // cmd_init() for a use case).
      //
      if (origin_config != nullptr   &&
          origin_config->path == cfg &&
          origin_prj == prj)
        return origin_config;

      using query = bdep::query<configuration>;
      return db.query_one<configuration> (query::path == cfg.string ());
    };

    // Given that we can potentially be inside a transaction started on the
    // origin project's database, the transaction handling is a bit
    // complicated.
    //
    // If the transaction is specified, then assumed it is started on the
    // specified project's database and so just wrap it.
    //
    // Otherwise, open the project database and start the transaction on it,
    // stashing the current transaction, if present. In destructor restore the
    // current transaction, if stashed.
    //
    class database_transaction
    {
    public:
      database_transaction (transaction* t, const dir_path& prj, tracer& tr)
          : ct_ (nullptr)
      {
        if (t == nullptr)
        {
          if (transaction::has_current ())
          {
            ct_ = &transaction::current ();
            transaction::reset_current ();
          }

          db_.reset (new database_type (open (prj, tr)));
          t_.reset (db_->begin ());
        }
        else
          ct_ = t;
      }

      ~database_transaction ()
      {
        if (ct_ != nullptr && db_ != nullptr)
          transaction::current (*ct_);
      }

      void
      commit ()
      {
        if (!t_.finalized ())
          t_.commit ();
      }

      using database_type = bdep::database;

      database_type&
      database ()
      {
        assert (db_ != nullptr || ct_ != nullptr);
        return db_ != nullptr ? *db_ : ct_->database ();
      }

      static transaction&
      current ()
      {
        return transaction::current ();
      }

    private:
      transaction               t_;
      transaction*              ct_; // Current transaction.
      unique_ptr<database_type> db_;
    };

    // Show how we got here (used for both info and text).
    //
    auto add_info = [&dep, &dependents, &cfg] (const basic_mark& bm)
    {
      const dependent& dpt (dependents[0]);
      bm << "while searching for configuration for build-time dependency "
         << dep << " of package " << dpt.name << "/" << dpt.version
         << " [" << dpt.config << "]";
      bm << "while synchronizing configuration " << cfg;
    };

    // Show how we got here if things go wrong.
    //
    // To suppress printing this information clear the dependency package name
    // before throwing an exception.
    //
    auto g (make_exception_guard ([&dep, &add_info] ()
                                  {
                                    if (!dep.empty ())
                                      add_info (info);
                                  }));

    // The immediate dependent configuration directory (the one which needs to
    // be linked with the build-time dependency configuration).
    //
    const dir_path& dpt_dir (dependents[0].config);

    // Now find the "source" project along with the associated immediate
    // dependent configuration and the associated build-time dependency
    // configuration, if present.
    //
    // Note that if we end up creating the dependency configuration, then we
    // also need to associate it with all other projects. Such an association
    // can potentially be redundant for some of them. However, it's hard to
    // detect if a project other than the "source" also depends on a package
    // which will be built in this newly created configuration. Generally, it
    // feels worse not to associate when required than create a redundant
    // association.
    //
    // The "source" project comes first.
    //
    vector<reference_wrapper<const dir_path>> dpt_prjs;

    // Immediate dependent configuration, which is associated with the
    // "source" project and needs to be linked with the build-time dependency
    // configuration.
    //
    shared_ptr<configuration> dpt_cfg;

    // Configuration associated with the "source" project and suitable for
    // building the build-time dependency. NULL if such a configuration
    // doesn't exist and needs to be created.
    //
    shared_ptr<configuration> dep_cfg;

    for (const dependent& dpt: dependents)
    {
      for (const project& prj: prjs)
      {
        database_transaction t (prj.path == origin_prj ? tr : nullptr,
                                prj.path,
                                trace);

        database& db (t.database ());

        // Check if the project is associated with the immediate dependent's
        // configuration and also the source of the configured (not
        // necessarily immediate) dependent package belongs to it. If that's
        // the case, we will use this project as a "source" of the build-time
        // dependency configuration.
        //
        if (shared_ptr<configuration> dtc =
            find_config (db, prj.path, dpt_dir))
        {
          shared_ptr<configuration> dc (find_config (db,
                                                     prj.path,
                                                     dpt.config));

          if (dc != nullptr &&
              find_if (dc->packages.begin (),
                       dc->packages.end (),
                       [&dpt] (const package_state& s)
                       {
                         return dpt.name == s.name;
                       }) != dc->packages.end ())
            dpt_cfg = move (dtc);
        }

        if (dpt_cfg != nullptr)
        {
          // Try to find an associated configuration of the suitable type.
          //
          using query = bdep::query<configuration>;

          dep_cfg = db.query_one<configuration> (query::default_ &&
                                                 query::type == dep_type);

          if (dep_cfg == nullptr)
          {
            for (shared_ptr<configuration> c:
                   pointer_result (
                     db.query<configuration> (query::type == dep_type)))
            {
              if (dep_cfg == nullptr)
              {
                dep_cfg = move (c);
              }
              else
                fail << "multiple configurations of " << dep_type
                     << " type associated with project " << prj.path <<
                  info << dep_cfg->path <<
                  info << c->path <<
                  info << "consider making one of them the default";
            }
          }

          // Collect the projects that need to be associated with the
          // dependency configuration (the "source" project comes first).
          //
          dpt_prjs.push_back (prj.path);

          if (dep_cfg == nullptr) // Need to create the config?
          {
            for (const project& p: prjs)
            {
              if (p.path != prj.path)
              {
                for (shared_ptr<configuration> c:
                       pointer_result (
                         db.query<configuration> (query::type == dep_type)))
                  fail << "project " << p.path << " is already associated "
                       << "with configuration of " << dep_type << " type" <<
                    info << "configuration of " << dep_type << " type: "
                       << c->path <<
                    info << "consider associating it with project "
                       << prj.path;

                dpt_prjs.push_back (p.path);
              }
            }
          }
        }

        t.commit ();

        if (!dpt_prjs.empty ())
          break; // Bail out from the loop over the projects.
      }

      if (!dpt_prjs.empty ())
        break; // Bail out from the loop over the dependents.
    }

    // Fail if no "source" project is found.
    //
    if (dpt_prjs.empty ())
      fail << "build-time dependency " << dep << " cannot be attributed to "
           << "any project";

    // If no configuration of the suitable type is found, then create it and
    // associate with all the projects involved.
    //
    if (dep_cfg == nullptr)
    {
      const dir_path& src_prj (dpt_prjs[0]);

      // Path to the build-time dependency configuration which needs to be
      // created.
      //
      dir_path dep_dir (dpt_dir.directory () / src_prj.leaf ());
      dep_dir += "-";
      dep_dir += dep_type;

      // Unless explicitly allowed via the respective create_*_config
      // argument, prompt the user before performing any action. But fail if
      // stderr is redirected.
      //
      if (!((dep_type == "host"   && create_host_config) ||
            (dep_type == "build2" && create_build2_config)))
      {
        if (!stderr_term)
          fail << "unable to find configuration of " << dep_type
               << " type for build-time dependency" <<
            info << "run sync command explicitly";

        {
          diag_record dr (text);

          // Separate prompt from some potential bpkg output (repository
          // fetch, etc).
          //
          dr << '\n'
             << "creating configuration of " << dep_type << " type in "
             << dep_dir << '\n'
             << "and associate it with projects:" << '\n';

          for (const dir_path& d: dpt_prjs)
            dr << "  " << d << '\n';

          dr << "as if by executing commands:" << '\n';

          dr << "  ";
          cmd_config_create_print (dr,
                                   src_prj,
                                   dep_dir,
                                   dep_type,
                                   dep_type,
                                   false, true, true); // See below.

          for (size_t i (1); i != dpt_prjs.size (); ++i)
          {
            dr << "\n  ";
            cmd_config_add_print (dr,
                                  dpt_prjs[i],
                                  dep_dir,
                                  dep_type,
                                  false, true, true); // See below.
          }
        }

        add_info (text);

        if (!yn_prompt ("continue? [Y/n]", 'y'))
        {
          // The dependency information have already been printed, so
          // suppress printing it repeatedly by the above exception guard.
          //
          dep = package_name ();

          throw failed ();
        }
      }

      // Verify that the configuration directory doesn't exist yet (we do it
      // after the prompt to give the user some context).
      //
      if (exists (dep_dir))
        fail << "configuration directory " << dep_dir << " already exists";

      bool create (true);
      for (const dir_path& prj: dpt_prjs)
      {
        database_transaction t (prj == origin_prj ? tr : nullptr,
                                prj,
                                trace);

        if (create)
        {
          // Before we committed the newly created dependency configuration
          // association to the project database or linked the dependent
          // configuration to it, we can safely remove it on error.
          //
          auto_rmdir rmd (dep_dir);

          // Before we used to create the default configuration but that lead
          // to counter-intuitive behavior (like trying to run tests in a host
          // configuration that doesn't have any bdep-init'ed packages). After
          // meditation it became clear that we want the default configuration
          // if we are developing the package and non-default if we just use
          // it. And automatic creation is probably a good proxy for the "just
          // use" case.
          //
          dep_cfg = cmd_config_create (co,
                                       prj,
                                       transaction::current (),
                                       dep_dir,
                                       dep_type /* name */,
                                       dep_type,
                                       false /* default */,
                                       true  /* forward */,
                                       true  /* auto_sync */);

          cmd_config_link (co, dpt_cfg, dep_cfg);

          rmd.cancel ();

          if (created_cfgs != nullptr)
            created_cfgs->emplace_back (dep_dir, dep_type);

          create = false;
        }
        else
        {
          cmd_config_add (prj,
                          transaction::current (),
                          dep_dir,
                          dep_type /* name */,
                          dep_type,
                          false /* default */,
                          true  /* forward */,
                          true  /* auto_sync */);
        }

        t.commit ();
      }
    }
    else
      cmd_config_link (co, dpt_cfg, dep_cfg);
  }

  // Sync with optional upgrade.
  //
  // If upgrade is not nullopt, then: If there are dep_pkgs, then we are
  // upgrading specific dependency packages. Otherwise -- project packages.
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
            const strings&           dep_pkgs,
            bool create_host_config,
            bool create_build2_config,
            transaction* tr = nullptr,
            vector<pair<dir_path, string>>* created_cfgs = nullptr)
  {
    tracer trace ("cmd_sync");

    assert (origin_config == nullptr || !origin_config->packages.empty ());
    assert (prj_pkgs.empty () || dep_pkgs.empty ()); // Can't have both.

    // If a transaction is specified, then it must be started on the origin
    // project's database (which therefore must be specified) and it must be
    // the current.
    //
    if (tr != nullptr)
      assert (!origin_prj.empty () && tr == &transaction::current ());

    // Must both be either specified or not.
    //
    assert ((tr == nullptr) == (created_cfgs == nullptr));

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
        reps.push_back (repository_name (prj.path));

      for (const package_state& pkg: prj.config->packages)
      {
        if (upgrade && !prj.implicit)
        {
          // We synchronize all the init'ed packages, including those from
          // other projects. But if the dependencies are not specified, we
          // only upgrade dependencies of the packages specified explicitly or
          // init'ed in the origin project.
          //
          if (dep_pkgs.empty ())
          {
            auto contains = [] (const auto& pkgs, const package_state& pkg)
            {
              return find_if (pkgs.begin (), pkgs.end (),
                              [&pkg] (const auto& p)
                              {
                                return p.name == pkg.name;
                              }) != pkgs.end ();
            };

            if (prj_pkgs.empty () && origin_config != nullptr
                ? contains (origin_config->packages, pkg)
                : contains (prj_pkgs, pkg))
            {
              // The project package itself must always be upgraded to the
              // latest version/iteration. So we have to translate to
              // dependency-only --{upgrade,patch}-{recursive,immediate}.
              //
              assert (recursive);

              args.push_back ("{");
              args.push_back (
                *upgrade
                ? *recursive ? "--upgrade-recursive" : "--upgrade-immediate"
                : *recursive ? "--patch-recursive"   : "--patch-immediate");
              args.push_back ("}+");
            }
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

    // Now configure the requested packages, preventing bpkg-pkg-build from
    // creating private configurations for build-time dependencies and
    // providing them ourselves on demand.
    //
    // Specifically, if the build-time dependency configuration needs to be
    // provided, then the plan is as follows:
    //
    // 1. Select a project which we can use as a "source" of the build-time
    //    dependency configuration: use its associated configuration of a
    //    suitable type, if exist, or create a new one using this project's
    //    name as a stem.
    //
    //    Such a project needs to have the immediate dependent configuration
    //    associated and contain a source directory of some of the dependents,
    //    not necessary immediate but the closer to the dependency the better.
    //    Not being able to find such a project is an error.
    //
    // 2. If the "source" project already has a default configuration of the
    //    suitable type associated, then use that. Otherwise, if the project
    //    has a single configuration of the suitable type associated, then use
    //    that. Otherwise, if the project has no suitable configuration
    //    associated, then create it. Fail if multiple such configurations are
    //    associated.
    //
    // 3. If the dependency configuration needs to be created then perform the
    //    following steps:
    //
    //    - Confirm with the user the list of commands as-if to be executed,
    //      unless the respective create_*_config argument is true.
    //
    //    - Create the configuration.
    //
    //    - Associate the created configuration with the "source" project and
    //      all other projects, which should have no configurations of this
    //      type associated (fail if that's not the case before creating
    //      anything).
    //
    // 4. Link the immediate dependent configuration with the dependency
    //    configuration (either found via the "source" project or newly
    //    created).
    //
    // 5. Re-run bpkg-pkg-build and if it turns out that (another) build-time
    //    dependency configuration is required, then go to p.1.
    //
    bool noop (false);
    for (;;)
    {
      // Run bpkg with the --no-private-config option, so that it reports the
      // need for the build-time dependency configuration via the specified
      // exit code.
      //
      bool need_config (false);
      strings dep_chain;
      {
        fdpipe pipe (open_pipe ()); // Text mode seems appropriate.

        process pr (start_bpkg (2,
                                co,
                                pipe /* stdout */,
                                2    /* stderr */,
                                "build",
                                "-d", cfg,
                                "--no-fetch",
                                "--no-refinement",
                                "--no-private-config", 125,
                                "--noop-exit", 124,
                                "--configure-only",
                                "--keep-out",
                                "--plan", plan,
                                (yes ? "--yes" : nullptr),
                                args));

        // Shouldn't throw, unless something is severely damaged.
        //
        pipe.out.close ();

        // Note that we need to try reading the dependency chain before we
        // even know that the build-time dependency configuration will be
        // required (not to block the process).
        //
        bool io (false);
        try
        {
          ifdstream is (move (pipe.in),
                        fdstream_mode::skip,
                        ifdstream::badbit);

          for (string l; !eof (getline (is, l)); )
            dep_chain.push_back (move (l));

          is.close (); // Detect errors.
        }
        catch (const io_error&)
        {
          // Presumably the child process failed and issued diagnostics.
          //
          io = true;
        }

        // Handle the process exit, detecting if the build-time dependency
        // configuration is required.
        //
        if (!pr.wait ())
        {
          const process_exit& e (*pr.exit);

          if (e.normal ())
          {
            switch (e.code ())
            {
            case 124: noop = true; break;
            case 125: need_config = true; break;
            default: throw failed (); // Assume the child issued diagnostics.
            }
          }
          else
            fail << "process " << name_bpkg (co) << " " << e;
        }

        if (io)
          fail << "error reading " << name_bpkg (co) << " output";
      }

      // Bail out if no build-time dependency configuration is required, but
      // make sure there is no unexpected bpkg output first (think some
      // buildfile printing junk to stdout).
      //
      if (!need_config)
      {
        if (!dep_chain.empty ())
        {
          diag_record dr (fail);
          dr << "unexpected " << name_bpkg (co) << " output:";
          for (const string& s: dep_chain)
            dr << '\n' << s ;
        }

        break;
      }

      link_dependency_config (co,
                              cfg,
                              prjs,
                              origin_prj, origin_config,
                              dep_chain,
                              create_host_config,
                              create_build2_config,
                              tr,
                              created_cfgs,
                              trace);
    }

    // Handle configuration forwarding.
    //
    // We do it here (instead of, say init) because a change in a package may
    // introduce new subprojects. For an implicit sync we can skip all of this
    // if no packages were upgraded (since our version revision tracking takes
    // into account changes to the subproject set).
    //
    // Also, the current thinking is that config set --[no-]forward is best
    // implemented by just changing the flag on the configuration and then
    // requiring an explicit sync to configure/disfigure forwards.
    //
    if (!implicit || !noop)
    {
      for (const project& prj: prjs)
      {
        package_locations pls (load_packages (prj.path));

        for (const package_state& pkg: prj.config->packages)
        {
          // If this is a forwarded configuration, make sure forwarding is
          // configured and is up-to-date. Otherwise, make sure it is
          // disfigured (the config set --no-forward case).
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
              fail << "package " << pkg.name << " is not listed in "
                   << prj.path;

            src /= i->path;
          }

#if 0
          // We could run 'b info' and used the 'forwarded' value but this is
          // both faster and simpler. Or at least it was until we got the
          // alternative naming scheme.
          //
          auto check = [&src] ()
          {
            path f (src / "build" / "bootstrap" / "out-root.build");
            bool e (exists (f));

            if (!e)
            {
              f = src / "build2" / "bootstrap" / "out-root.build2";
              e = exists (f);
            }

            return e;
          };
#endif

          const char* o (nullptr);
          if (prj.config->forward)
          {
            o = "configure:";
          }
          else if (!prj.implicit) // Requires explicit sync.
          {
            //@@ This is broken: we will disfigure forwards to other configs.
            //   Looks like we will need to test that the forward is to this
            //   config. 'b info' here we come?
#if 0
            if (check ())
              o = "disfigure:";
#endif
          }

          if (o != nullptr)
          {
            dir_path out (dir_path (cfg) /= pkg.name.string ());

            // Note that --no-external-modules makes a difference for
            // developing build system modules that require bootstrapping
            // (which without that option would trigger a recursive sync).
            //
            run_b (co,
                   "--no-external-modules",
                   o,
                   "'" + src.representation () + "'@'" + out.representation () +
                   "',forward");
          }
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

  // Return true if a space-separated list of double-quoted paths contains the
  // specified path. All paths must me absolute and normalized.
  //
  static bool
  contains (const string& l, const path& p)
  {
    const string& s (p.string ());

    for (size_t b (0), e (0);
         (e = l.find ('"', e)) != string::npos; // Skip leading ' '.
         ++e)                                   // Skip trailing '"'.
    {
      size_t n (next_word (l, b, e, '"'));

      // Both paths are normilized so we can just compare them as strings.
      //
      if (path::traits_type::compare (l.c_str () + b, n,
                                      s.c_str (),     s.size ()) == 0)
        return true;
    }

    return false;
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
    if (optional<string> e = getenv (synced_name))
      v = move (*e);

    if (contains (v, d))
    {
      if (implicit)
        return true;
      else
        fail << "explicit re-synchronization of " << d;
    }

    if (add)
    {
      v += (v.empty () ? "\"" : " \"") + d.string () + '"';
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
            bool name_cfg,
            bool create_host_config,
            bool create_build2_config,
            transaction* t,
            vector<pair<dir_path, string>>* created_cfgs)
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
                strings ()           /* dep_pkgs  */,
                create_host_config,
                create_build2_config,
                t,
                created_cfgs);
  }

  void
  cmd_sync_implicit (const common_options& co,
                     const dir_path& cfg,
                     bool fetch,
                     bool yes,
                     bool name_cfg,
                     bool create_host_config,
                     bool create_build2_config)
  {
    if (!synced (cfg, true /* implicit */))
      cmd_sync (co,
                cfg,
                dir_path (),
                nullptr,
                strings (),
                true                  /* implicit */,
                fetch,
                yes,
                name_cfg,
                nullopt               /* upgrade   */,
                nullopt               /* recursive */,
                package_locations ()  /* prj_pkgs  */,
                strings ()            /* dep_pkgs  */,
                create_host_config,
                create_build2_config);
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
    bool default_fallback (false);

    // In the implicit mode we don't search the current working directory
    // for a project.
    //
    if (o.directory_specified () || !o.implicit ())
    {
      // We could be running from a package directory (or the user specified
      // one with -d) that has not been init'ed in this configuration. This,
      // however, doesn't really matter since the specified packages are only
      // used to determine which configuration package dependencies needs to
      // be ugraded (see cmd_sync() for details).
      //
      // If we are running from the project, then we don't want to treat all
      // the available packages as specified by the user (thus load_packages
      // is false).
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
      pair<configurations, bool> cs;
      {
        database db (open (pp.project, trace));

        transaction t (db.begin ());
        cs = find_configurations (o, pp.project, t);
        t.commit ();
      }

      // If specified, verify packages are present in at least one
      // configuration.
      //
      if (!pp.packages.empty ())
        verify_project_packages (pp, cs);

      prj = move (pp.project);
      prj_pkgs = move (pp.packages);
      cfgs = move (cs.first);
      default_fallback = cs.second;
    }
    else
    {
      // Implicit sync without an originating project.
      //
      // Here, besides the BDEP_SYNCED_CONFIGS we also check BPKG_OPEN_CONFIGS
      // which will be set if we are called by bpkg while it has the databases
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
      optional<string> open (getenv ("BPKG_OPEN_CONFIGS"));

      for (const pair<dir_path, size_t>& c: o.config ())
      {
        dir_path d (c.first);

        normalize (d, "configuration");

        if (open && contains (*open, d))
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
    bool empty (true); // All configurations are empty.
    for (size_t i (0), n (cfgs.size ()); i != n; ++i)
    {
      const shared_ptr<configuration>& c (cfgs[i]); // Can be NULL.
      const dir_path& cd (c != nullptr ? c->path : cfg_dirs[i]);

      // Check if this configuration is already (being) synchronized.
      //
      if (synced (cd, o.implicit ()))
      {
        empty = false;
        continue;
      }

      // Skipping empty ones (part one).
      //
      // Note that we would normally be printing that for build-time
      // dependency configurations (which normally will not have any
      // initialized packages) and that would be annoying. So we suppress it
      // in case of the default configuration fallback (but also check and
      // warn if all of them were empty below).
      //
      if (c != nullptr && c->packages.empty () && default_fallback)
        continue;

      // If we are synchronizing multiple configurations, separate them with a
      // blank line and print the configuration name/directory.
      //
      if (verb && n > 1)
        text << (i == 0 ? "" : "\n")
             << "in configuration " << *c << ':';

      // Skipping empty ones (part two).
      //
      if (c != nullptr && c->packages.empty ())
      {
        if (verb)
          info << "no packages initialized in configuration " << *c
               << ", skipping";

        continue;
      }

      empty = false;

      bool fetch (o.fetch () || o.fetch_full ());

      if (fetch)
        cmd_fetch (o, prj, c, o.fetch_full ());

      if (!dep_pkgs.empty ())
      {
        // We ignore the project packages if the dependencies are specified
        // explicitly (see the above find_project_packages() call).
        //
        assert (prj_pkgs.empty ());

        // The third form: upgrade of the specified dependencies.
        //
        // Only prompt if upgrading their dependencies.
        //
        cmd_sync (o,
                  cd,
                  prj,
                  c,
                  pkg_args,
                  false                    /* implicit */,
                  !fetch,
                  o.recursive () || o.immediate () ? o.yes () : true,
                  false                    /* name_cfg */,
                  !o.patch (), // Upgrade by default unless patch requested.
                  (o.recursive () ? optional<bool> (true)  :
                   o.immediate () ? optional<bool> (false) : nullopt),
                  package_locations ()     /* prj_pkgs  */,
                  dep_pkgs,
                  o.create_host_config (),
                  o.create_build2_config ());
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
                  false                    /* implicit */,
                  !fetch,
                  o.yes (),
                  false                    /* name_cfg */,
                  o.upgrade (),
                  o.recursive (),
                  prj_pkgs,
                  strings ()               /* dep_pkgs  */,
                  o.create_host_config (),
                  o.create_build2_config ());
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
                  true                     /* yes       */,
                  o.implicit ()            /* name_cfg  */,
                  nullopt                  /* upgrade   */,
                  nullopt                  /* recursive */,
                  package_locations ()     /* prj_pkgs  */,
                  strings ()               /* dep_pkgs  */,
                  o.create_host_config (),
                  o.create_build2_config ());
      }
    }

    if (empty && default_fallback)
    {
      if (verb)
        info << "no packages initialized in default configuration(s)";
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

      const vector<pair<dir_path, size_t>>& cs (o.config ());
      r.start = default_options_start (home_directory (),
                                       cs.begin (),
                                       cs.end (),
                                       [] (auto i) {return i->first;});
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
