// file      : bdep/sync.cxx -*- C++ -*-
// license   : MIT; see accompanying LICENSE file

#include <bdep/sync.hxx>

#include <list>
#include <cstring>  // strchr(), strcmp(), strspn()

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

  // Configurations linked into a cluster.
  //
  struct linked_config
  {
    dir_path   path;
    bdep::uuid uuid;
  };

  class linked_configs: public small_vector<linked_config, 16>
  {
  public:
    const linked_config*
    find (const dir_path& p)
    {
      auto i (find_if (begin (), end (),
                       [&p] (const linked_config& c)
                       {
                         return c.path == p;
                       }));
      return i != end () ? &*i : nullptr;
    }
  };

  // Configurations to be synced are passed either as configuration objects
  // (if syncing from a project) or as directories (if syncing from a
  // configuration, normally implicit/hook).
  //
  struct sync_config: shared_ptr<configuration>
  {
    dir_path path_; // Empty if not NULL.

    const dir_path&
    path () const {return *this != nullptr ? (*this)->path : path_;}

    sync_config (shared_ptr<configuration> c)
        : shared_ptr<configuration> (move (c)) {}
    sync_config (dir_path p): path_ (move (p)) {}
  };

  using sync_configs = small_vector<sync_config, 16>;

  // Projects and their configurations to be synchronized.
  //
  struct sync_project
  {
    struct config: shared_ptr<configuration>
    {
      bool origin;
      bool implicit;
      bool fetch;

      config (shared_ptr<configuration> p, bool o, bool i, bool f)
        : shared_ptr<configuration> (move (p)),
          origin (o), implicit (i), fetch (f) {}
    };

    dir_path path;
    small_vector<config, 16> configs;

    explicit
    sync_project (dir_path p) : path (move (p)) {}
  };

  using sync_projects = small_vector<sync_project, 1>;

  static inline ostream&
  operator<< (ostream& o, const sync_config& sc)
  {
    if (const shared_ptr<configuration>& c = sc)
      o << *c; // Prints as @name if available.
    else
      o << sc.path_;
    return o;
  }

  // Append the list of projects that are using this configuration. Note that
  // if the project is already on the list, then add the configuration to the
  // existing entry unless it's already there.
  //
  static void
  load_implicit (const common_options& co,
                 const dir_path& cfg,
                 sync_projects& r,
                 const dir_path& origin_prj,
                 transaction* origin_tr)
  {
    tracer trace ("load_implicit");

    for (dir_path& d: configuration_projects (co, cfg))
    {
      // Do duplicate suppression before any heavy lifting.
      //
      auto i (find_if (r.begin (), r.end (),
                       [&d] (const sync_project& p)
                       {
                         return p.path == d;
                       }));

      if (i != r.end ())
      {
        if (find_if (i->configs.begin (), i->configs.end (),
                     [&cfg] (const sync_project::config& c)
                     {
                       return c->path == cfg;
                     }) != i->configs.end ())
          continue;
      }

      shared_ptr<configuration> c;
      {
        using query = bdep::query<configuration>;

        query q (query::path == cfg.string ());

        // Reuse the transaction (if any) if this is origin project.
        //
        if (origin_tr != nullptr && origin_prj == d)
        {
          c = origin_tr->database ().query_one<configuration> (q);
        }
        else
        {
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
            c = db.query_one<configuration> (q);
            t.commit ();
          }
        }
      }

      // If the project is a repository of this configuration but the bdep
      // database has no knowledge of this configuration, then assume it is
      // not managed by bdep (i.e., the user added the project manually or
      // some such).
      //
      if (c == nullptr)
        continue;

      if (i == r.end ())
      {
        r.push_back (sync_project (d));
        i = r.end () - 1;
      }

      i->configs.push_back (
        sync_project::config {move (c),
                              false /* origin */,
                              true  /* implicit */,
                              true  /* fetch */});
    }
  }

  // Find/create and link a configuration suitable for build-time dependency.
  //
  static void
  link_dependency_config (const common_options& co,
                          const dir_path& origin_prj, // Can be empty.
                          const sync_configs& origin_cfgs,
                          const sync_projects& prjs,
                          const strings& dep_chain,
                          bool create_host_config,
                          bool create_build2_config,
                          transaction* origin_tr,
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
          version =
            bpkg::extract_package_version (s,
                                           bpkg::version::fold_zero_revision |
                                           bpkg::version::allow_iteration);

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
    auto find_config = [&origin_prj, &origin_cfgs] (
      database& db,
      const dir_path& prj,
      const dir_path& cfg) -> shared_ptr<configuration>
    {
      // Note that this is not merely an optimization since the origin
      // configuration can be changed but not updated in the database yet (see
      // cmd_init() for a use case).
      //
      if (origin_prj == prj)
      {
        for (const sync_config& ocfg: origin_cfgs)
          if (ocfg.path () == cfg)
            return ocfg;
      }

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
    auto add_info = [&dep, &dependents, &origin_cfgs] (const basic_mark& bm)
    {
      const dependent& dpt (dependents[0]);

      bm << "while searching for configuration for build-time dependency "
         << dep << " of package " << dpt.name << "/" << dpt.version
         << " [" << dpt.config << "]";

      for (const sync_config& cfg: origin_cfgs)
        bm << "while synchronizing configuration " << cfg.path ();
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
      for (const sync_project& prj: prjs)
      {
        database_transaction t (prj.path == origin_prj ? origin_tr : nullptr,
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
              find_if (dc->packages.begin (), dc->packages.end (),
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
            for (const sync_project& p: prjs)
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

      strings cfg_args {"cc", "config.config.load=~" + dep_type};

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
             << dep_dir << " and associating it with project(s):" << '\n';

          for (const dir_path& d: dpt_prjs)
            dr << "  " << d << '\n';

          dr << "as if by executing command(s):" << '\n';

          dr << "  ";
          cmd_config_create_print (dr,
                                   src_prj,
                                   dep_dir,
                                   dep_type,
                                   dep_type,
                                   false, true, true, // See below.
                                   cfg_args);

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
        database_transaction t (prj == origin_prj ? origin_tr : nullptr,
                                prj,
                                trace);

        if (create)
        {
          // Before we committed the newly created dependency configuration
          // association to the project database or linked the dependent
          // configuration to it, we can safely remove it on error.
          //
          auto_rmdir rmd (dep_dir);

          // Add to the command line the configuration variable which we omit
          // from printing to make the prompt less hairy.
          //
          cfg_args.push_back (
            "config.config.persist+='config.*'@unused=drop");

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
                                       true  /* auto_sync */,
                                       false /* existing */,
                                       false /* wipe */,
                                       cfg_args);

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

  // Check whether the specified .build (or .build2) file exists in the build/
  // (or build2/) subdirectory of the specified root directory. Note: the file
  // is expected to be specified with the .build extension.
  //
  static inline bool
  exists (const dir_path& root, const path& file)
  {
    // Check the build2 naming first as more specific.
    //
    path p = root; p /= "build2"; p /= file; p += '2';

    if (exists (p))
      return true;

    p = root; p /= "build"; p /= file;

    return exists (p);
  }

  // Sync with optional upgrade.
  //
  // If upgrade is not nullopt, then: If there are dep_pkgs, then we are
  // upgrading specific dependency packages. Otherwise -- project packages.
  //
  // If deinit_pkgs is not empty, then sync in the deinit mode.
  //
  // Note that if origin_prj is not empty, then origin_cfgs are specified as
  // configurations (as opposed to paths). Also upgrade can only be specified
  // with origin_prj.
  //
  // Note that pkg_args and dep_pkgs may contain groups.
  //
  struct config // VC14 doesn't like it inside the function.
  {
    reference_wrapper<const dir_path> path; // Reference to prjs.
    strings                           reps;
  };

  static void
  cmd_sync (const common_options& co,
            const dir_path& origin_prj,
            sync_configs&& origin_cfgs,
            linked_configs&& linked_cfgs,
            const strings& pkg_args,
            bool implicit,
            optional<bool> fetch,
            uint16_t bpkg_fetch_verb,
            bool yes,
            bool name_cfg,
            optional<bool> upgrade,   // true - upgrade,   false - patch
            optional<bool> recursive, // true - recursive, false - immediate
            bool disfigure,
            const package_locations& prj_pkgs,
            const strings&           dep_pkgs,
            const strings&           deinit_pkgs,
            const sys_options& so,
            bool create_host_config,
            bool create_build2_config,
            transaction* origin_tr = nullptr,
            vector<pair<dir_path, string>>* created_cfgs = nullptr)
  {
    tracer trace ("cmd_sync");

    // True if we have originating project.
    //
    bool origin (!origin_prj.empty ());

    // Cannot have more than one package list specified.
    //
    assert ((prj_pkgs.empty ()    ? 0 : 1) +
            (dep_pkgs.empty ()    ? 0 : 1) +
            (deinit_pkgs.empty () ? 0 : 1) <= 1);

    // If a transaction is specified, then it must be started on the origin
    // project's database (which therefore must be specified) and it must be
    // the current.
    //
    if (origin_tr != nullptr)
      assert (origin && origin_tr == &transaction::current ());

    // Must both be either specified or not.
    //
    assert ((origin_tr == nullptr) == (created_cfgs == nullptr));

    // Collect all the projects that will be involved in this synchronization
    // (we synchronize everything at once).
    //
    sync_projects prjs;

    if (origin)
    {
      prjs.push_back (sync_project (origin_prj));

      for (sync_config& c: origin_cfgs)
      {
        // If we have origin project then we should have origin config.
        //
        assert (c != nullptr);
        prjs.back ().configs.push_back (
          sync_project::config {
            c, true /* origin */, implicit, fetch.has_value ()});
      }
    }

    // Load other projects that might be using the same configuration cluster.
    //
    // Note that this may add more (implicit) configurations to origin_prj's
    // entry.
    //
    // @@ We may end up openning the database (in load_implicit()) for each
    //    project multiple times.
    //
    for (const linked_config& cfg: linked_cfgs)
      load_implicit (co, cfg.path, prjs, origin_prj, origin_tr);

    // Verify that no initialized package in any of the projects sharing this
    // configuration is specified as a dependency.
    //
    if (!dep_pkgs.empty ())
    {
      for (const sync_project& prj: prjs)
      {
        for (const sync_project::config& cfg: prj.configs)
        {
          auto& pkgs (cfg->packages);

          for (cli::vector_group_scanner s (dep_pkgs); s.more (); )
          {
            const char* n (s.next ());

            if (find_if (pkgs.begin (), pkgs.end (),
                         [&n] (const package_state& ps)
                         {
                           return n == ps.name;
                         }) != pkgs.end ())
              fail << "initialized package " << n << " specified as dependency" <<
                info << "package initialized in project " << prj.path;

            s.skip_group ();
          }
        }
      }
    }

    // Prepare the list of packages to build, configurations involved, and
    // repositories to fetch in each such configuration.
    //
    strings args;
    small_vector<config, 16> cfgs;

    // First collect configurations and their repositories. We do it as a
    // separate (from the one below) pass in order to determine how many
    // projects/configurations will be involved. If it's just one, then we
    // often can have a simpler command line.
    //
    bool origin_only (dep_pkgs.empty ()); // Only origin packages on cmd line.
    for (const sync_project& prj: prjs)
    {
      for (const sync_project::config& cfg: prj.configs)
      {
        bool empty (cfg->packages.empty ());

        if (empty)
        {
          // Note that we keep empty origin configurations if we have any
          // dependencies to upgrade or we deinitialize some packages (see
          // below for details).
          //
          if (dep_pkgs.empty () && deinit_pkgs.empty ())
            continue;
          else
          {
            if (find_if (origin_cfgs.begin (), origin_cfgs.end (),
                         [&cfg] (const sync_config& ocfg)
                         {
                           return ocfg.path () == cfg->path;
                         }) == origin_cfgs.end ())
              continue;
          }
        }
        else if (!cfg.origin)
          origin_only = false;

        auto i (find_if (cfgs.begin (), cfgs.end (),
                         [&cfg] (const config& c)
                         {
                           return cfg->path == c.path.get ();
                         }));

        if (i == cfgs.end ())
        {
          cfgs.push_back (config {cfg->path, {}});
          i = cfgs.end () - 1;
        }

        if (cfg.fetch && !empty)
          i->reps.push_back (repository_name (prj.path));
      }
    }

    bool multi_cfg (cfgs.size () != 1);

    // Position the pointer to the first non-whitespace character.
    //
    auto ltrim = [] (const char*& s)
    {
      s += strspn (s, " \t");
    };

    // Start by adding configuration variables from pkg_args, if any.
    //
    // If we have dep_pkgs (third form), then non-global configuration
    // variables should only apply to them. Otherwise, if we have origin
    // (first form), then they should only apply to the specified packages or,
    // if unspecified, to all packages from the origin project in origin
    // configurations. They don't seem to make sense otherwise (second form,
    // implicit).
    //
    bool dep_vars (false);
    bool origin_vars (false);

    if (!pkg_args.empty ())
    {
      if (origin_only)
      {
        for (cli::vector_group_scanner s (pkg_args); s.more (); )
        {
          if (strchr (s.next (), '=') == nullptr)
          {
            origin_only = false;
            break;
          }

          s.skip_group ();
        }
      }

      for (cli::vector_group_scanner s (pkg_args); s.more (); )
      {
        const char* a (s.next ());
        const char* p (strchr (a, '='));

        if (p == nullptr)
        {
          s.skip_group ();
          continue;
        }

        ltrim (a);

        if (*a != '!')
        {
          if (!dep_pkgs.empty ())
            dep_vars = true;
          else if (origin)
          {
            // Simplify the command line if we only have origin on the command
            // line.
            //
            if (!origin_only)
              origin_vars = true;
          }
          else
            fail << "non-global configuration variable "
                 << trim (string (a, 0, p - a))
                 << " without packages or dependencies";

          if (dep_vars || origin_vars)
            continue;
        }

        args.push_back (trim (a));

        // Note: we let diagnostics for unhandled groups cover groups for
        //       configuration variables.
      }

      if (!args.empty ())
        args.push_back ("--");
    }

    // Next collect init'ed packages.
    //
    // Note that in the deinit mode the being deinitialized packages must have
    // already been removed from the configuration (see cmd_deinit() for
    // details) and will be collected differently (see below). The details of
    // their collection, however, depends on if any packages will remain
    // initialized in the origin project. Thus, while at it, let's note that.
    //
    bool deinit_remain_initialized (false);

    for (const sync_project& prj: prjs)
    {
      for (const sync_project::config& cfg: prj.configs)
      {
        if (cfg->packages.empty ())
          continue;

        for (const package_state& pkg: cfg->packages)
        {
          if (!deinit_pkgs.empty () && prj.path == origin_prj)
          {
            // Must contain the configuration where the packages are being
            // deinitialized.
            //
            assert (origin_cfgs.size () == 1);

            if (origin_cfgs.front ().path () == cfg->path)
            {
              // Must have been removed from the configuration.
              //
              assert (find (deinit_pkgs.begin (),
                            deinit_pkgs.end (),
                            pkg.name) == deinit_pkgs.end ());

              deinit_remain_initialized = true;
            }
          }

          // Return true if this package is part of the list specified
          // explicitly or, if none are specified, init'ed in the origin
          // project.
          //
          // @@ Feels like cfg.origin should be enough for the latter case?
          //
          auto origin_pkg = [origin,
                             &origin_cfgs,
                             &prj_pkgs,
                             &pkg,
                             r = optional<bool> ()] () mutable
          {
            if (!r)
            {
              auto contains = [] (const auto& pkgs, const package_state& pkg)
              {
                return find_if (pkgs.begin (), pkgs.end (),
                                [&pkg] (const auto& p)
                                {
                                  return p.name == pkg.name;
                                }) != pkgs.end ();
              };

              if (prj_pkgs.empty ())
              {
                r = false;
                if (origin)
                {
                  for (const sync_config& cfg: origin_cfgs)
                    if ((r = contains (cfg->packages, pkg)))
                      break;
                }
              }
              else
                r = contains (prj_pkgs, pkg);
            }

            return *r;
          };

          // See if we need to pass --disfigure. We only do this for the
          // origin packages.
          //
          bool disf (disfigure && origin_pkg ());

          // See if we need to pass config.<package>.develop=true. We only do
          // this if:
          //
          // 1. It's an explicit sync (init is explicit).
          //
          // 2. This is a from-scratch (re)configuration of this package.
          //
          // 3. The user did not specify custom develop value in pkg_args.
          //
          // One "hole" we have with this approach is if the user adds the
          // config directive after init. In this case the only way to
          // reconfigure the package for development would be for the user
          // to do an explicit sync and pass config.*.develop=true.
          //
          optional<string> dev;
          if (!cfg.implicit &&
              (disf ||
               !exists (dir_path (cfg->path) /= pkg.name.string (),
                        path ("config.build"))))
          {
            dev = "config." + pkg.name.variable () + ".develop";

            for (cli::vector_group_scanner s (pkg_args); s.more (); )
            {
              const char* a (s.next ());
              const char* p (strchr (a, '='));
              if (p == nullptr)
              {
                s.skip_group ();
                continue;
              }

              ltrim (a);

              if (*a == '!')
                ++a;

              if (trim (string (a, 0, p - a)) == *dev)
              {
                dev = nullopt;
                break;
              }
            }
          }

          bool vars (origin_vars && cfg.origin && origin_pkg ());

          bool g (multi_cfg || vars || dev || disf);
          if (g)
            args.push_back ("{");

          if (multi_cfg)
            args.push_back ("--config-uuid=" +
                            linked_cfgs.find (cfg->path)->uuid.string ());

          if (disf)
            args.push_back ("--disfigure");

          if (upgrade && dep_pkgs.empty () && !cfg.implicit)
          {
            // We synchronize all the init'ed packages, including those from
            // other projects. But if the dependencies are not specified, we
            // only upgrade dependencies of the origin packages.
            //
            if (origin_pkg ())
            {
              // The project package itself must always be upgraded to the
              // latest version/iteration. So we have to translate to
              // dependency-only --{upgrade,patch}-{recursive,immediate}.
              //
              assert (recursive);

              if (!g)
              {
                args.push_back ("{");
                g = true;
              }

              args.push_back (
                *upgrade
                ? *recursive ? "--upgrade-recursive" : "--upgrade-immediate"
                : *recursive ? "--patch-recursive"   : "--patch-immediate");
            }
          }

          // Note: must come after options, if any.
          //
          if (vars)
          {
            for (cli::vector_group_scanner s (pkg_args); s.more (); )
            {
              const char* a (s.next ());
              if (strchr (a, '=') != nullptr)
              {
                ltrim (a);

                if (*a != '!')
                  args.push_back (trim (a));
              }
              else
                s.skip_group ();
            }
          }

          if (dev)
            args.push_back (*dev += "=true");

          if (g)
            args.push_back ("}+");

          // We need to add the explicit location qualification (@<rep-loc>)
          // since there is no guarantee a better version isn't available from
          // another repository.
          //
          args.push_back (pkg.name.string () + '@' + prj.path.string ());
        }
      }
    }

    // Add dependencies to upgrade (if any).
    //
    // This gets quite fuzzy when we are dealing with multiple configurations.
    // To start, we only want to upgrade dependencies in the configurations
    // specified by the user (origin_cfgs). This part is pretty clear. But if
    // we just qualify the dependencies with configurations, then that will be
    // interpreted by bpkg as a request to move if any other configurations
    // (of the same type) happened to also have this package as a dependency.
    // So we pass --no-move below to prevent this (which basically means
    // "upgrade there if present, build there if necessary, and otherwise
    // ignore").
    //
    // The (admittedly still fuzzy) conceptual model behind this is that if
    // the user wishes to partition dependencies into multiple configurations
    // (of the same type; say base/target) then they do it manually with bpkg
    // and bdep does not alter this partitioning of dependencies in any way.
    //
    // Note that in this model, while the user may use bpkg directly to
    // upgrade/downgrate such dependencies, that feels a bit awkward (imagine
    // host and base having the same dependency with one upgraded via bdep
    // while the other -- via bpkg). Instead, we support associating such a
    // base configuration with a bdep-managed project and using that to manage
    // upgrades by not skipping empty origin configurations in this mode (see
    // above). Note also that in this case we don't need to worry about
    // fetching empty configuration's repository information since it won't be
    // used for the upgrade anyway (instead, information from ultimate
    // dependent's configuration will be used).
    //
    if (upgrade)
    {
      for (cli::vector_group_scanner s (dep_pkgs); s.more (); )
      {
        const char* n (s.next ());

        // Note that we are using the leading group for our options since
        // we pass the user's group as trailing below.
        //
        bool g (multi_cfg || dep_vars);
        if (g)
          args.push_back ("{");

        if (multi_cfg)
          for (const sync_config& cfg: origin_cfgs)
            args.push_back ("--config-uuid=" +
                            linked_cfgs.find (cfg.path ())->uuid.string ());

        // Unless this is the default "non-recursive upgrade" we need to add a
        // group.
        //
        if (recursive || !*upgrade)
        {
          if (!g)
          {
            args.push_back ("{");
            g = true;
          }

          string o (*upgrade ? "-u" : "-p");
          if (recursive) o += *recursive ? 'r' : 'i';
          args.push_back (move (o));
        }

        // Note: must come after options, if any.
        //
        if (dep_vars)
        {
          for (cli::vector_group_scanner s (pkg_args); s.more (); )
          {
            const char* a (s.next ());
            if (strchr (a, '=') != nullptr)
            {
              ltrim (a);

              if (*a != '!')
                args.push_back (trim (a));
            }
            else
              s.skip_group ();
          }
        }

        if (g)
          args.push_back ("}+");

        // Make sure it is treated as a dependency.
        //
        args.push_back (string ("?") + n);

        // Note that bpkg expects options first and configuration variables
        // last. Which mean that if we have dep_vars above and an option
        // below, then things will blow up. Though it's unclear what option
        // someone may want to pass here.
        //
        cli::scanner& gs (s.group ());
        if (gs.more ())
        {
          args.push_back ("+{");
          for (; gs.more (); args.push_back (gs.next ())) ;
          args.push_back ("}");
        }
      }
    }

    // Finally, add packages (?<pkg>) from pkg_args, if any.
    //
    // Similar to the dep_pkgs case above, we restrict this to the origin
    // configurations unless configuration(s) were explicitly specified by the
    // user.
    //
    for (cli::vector_group_scanner s (pkg_args); s.more (); )
    {
      const char* ca (s.next ());

      if (strchr (ca, '=') != nullptr)
        continue;

      string a (ca); // Not guaranteed to be valid after group processing.

      cli::scanner& gs (s.group ());

      if (gs.more () || multi_cfg)
      {
        args.push_back ("{");

        cmd_sync_pkg_options po;
        try
        {
          while (gs.more ())
          {
            // Stop (rather than fail) on unknown option to handle
            // -@<cfg-name>.
            //
            if (po.parse (gs, cli::unknown_mode::stop) && !gs.more ())
              break;

            const char* a (gs.peek ());

            // Handle @<cfg-name> & -@<cfg-name>.
            //
            if (*a == '@' || (*a == '-' && a[1] == '@'))
            {
              string n (a + (*a == '@' ? 1 : 2));

              if (n.empty ())
                fail << "missing configuration name in '" << a << "'";

              po.config_name ().emplace_back (move (n));
              po.config_name_specified (true);

              gs.next ();
            }
            //
            // Handle unknown option and argument.
            //
            else
            {
              // Don't report '-' and '--' as unknown options and let bpkg
              // deal with arguments other than configuration variables.
              //
              if (a[0] == '-' && a[1] != '\0' && strcmp (a, "--") != 0)
                throw cli::unknown_option (a);

              args.push_back (gs.next ());
            }
          }
        }
        catch (const cli::exception& e)
        {
          fail << e << " grouped for package " << a;
        }

        if (po.config_specified ()    ||
            po.config_id_specified () ||
            po.config_name_specified ())
        {
          auto append = [&linked_cfgs, &args, &a] (const dir_path& d)
          {
            if (const linked_config* cfg = linked_cfgs.find (d))
            {
              args.push_back ("--config-uuid=" + cfg->uuid.string ());
            }
            else
              fail << "configuration " << d << " is not part of linked "
                   << "configuration cluster being synchronized" <<
                info << "specified for package " << a;
          };

          // We will be a bit lax and allow specifying with --config any
          // configuration in the cluster, not necessarily associated with the
          // origin project (which we may not have).
          //
          for (dir_path d: po.config ())
            append (normalize (d, "configuration directory"));

          if (const char* o = (po.config_id_specified ()  ? "--config-id" :
                               po.config_name_specified () ? "--config-name|-n" :
                               nullptr))
          {
            if (!origin)
              fail << o << "specified without project" <<
                info << "specified for package " << a;

            // Origin project is first.
            //
            const dir_path& pd (prjs.front ().path);

            auto lookup = [&append, &po, &pd] (database& db)
            {
              // Similar code to find_configurations().
              //
              using query = bdep::query<configuration>;

              for (uint64_t id: po.config_id ())
              {
                if (auto cfg = db.find<configuration> (id))
                  append (cfg->path);
                else
                  fail << "no configuration id " << id << " in project " << pd;
              }

              for (const string& n: po.config_name ())
              {
                if (auto cfg = db.query_one<configuration> (query::name == n))
                  append (cfg->path);
                else
                  fail << "no configuration name '" << n << "' in project "
                       << pd;
              }
            };

            // Reuse the transaction, if any (similar to load_implicit()).
            //
            if (origin_tr != nullptr)
            {
              lookup (origin_tr->database ());
            }
            else
            {
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

              database db (open (pd, trace));
              transaction t (db.begin ());
              lookup (db);
              t.commit ();
            }
          }
        }
        else
        {
          // Note that here (unlike the dep_pkgs case above), we have to make
          // sure the configuration is actually involved.
          //
          for (const sync_config& ocfg: origin_cfgs)
          {
            if (find_if (cfgs.begin (), cfgs.end (),
                         [&ocfg] (const config& cfg)
                         {
                           return ocfg.path () == cfg.path.get ();
                         }) == cfgs.end ())
              continue;

            args.push_back ("--config-uuid=" +
                            linked_cfgs.find (ocfg.path ())->uuid.string ());
          }
        }

        args.push_back ("}+");
      }

      args.push_back (move (a));
    }

    // Add the being deinitialized packages.
    //
    if (!deinit_pkgs.empty ())
    {
      assert (!origin_prj.empty ()); // Project the packages belong to.

      // Must contain the configuration where the packages are being
      // deinitialized.
      //
      assert (origin_cfgs.size () == 1);

      string config_uuid (
        linked_cfgs.find (origin_cfgs.front ().path ())->uuid.string ());

      // If any initialized packages will remain in the origin project, then
      // the respective repository won't be removed from the bpkg
      // configuration (see cmd_deinit() for details) and thus we don't mask
      // it nor deorphan the being deinitialized packages (just unhold them).
      //
      if (!deinit_remain_initialized)
      {
        args.push_back ("--mask-repository-uuid");
        args.push_back (config_uuid + '=' + repository_name (origin_prj));
      }

      args.push_back ("{");

      if (multi_cfg)
        args.push_back ("--config-uuid=" + config_uuid);

      if (!deinit_remain_initialized)
        args.push_back ("--deorphan");

      args.push_back ("--dependency");
      args.push_back ("}+");

      if (deinit_pkgs.size () > 1)
        args.push_back ("{");

      for (const string& p: deinit_pkgs)
        args.push_back (p);

      if (deinit_pkgs.size () > 1)
        args.push_back ("}");
    }

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
    for (const config& cfg: cfgs)
    {
      if (cfg.reps.empty ())
        continue;

      const path& p (cfg.path);

      // If we are deep-fetching multiple configurations, print their names.
      // Failed that it will be quite confusing since we may be re-fetching
      // the same repositories over and over.
      //
      // Note: counter-intuitively, we may end up here even if fetch is
      // nullopt; see load_implicit() for details.
      //
      bool deep_fetch (fetch && *fetch);

      if (cfgs.size () != 1 && deep_fetch)
        text << "fetching in configuration " << p.representation ();

      run_bpkg (bpkg_fetch_verb, co,
                "fetch",
                "-d", p,
                (deep_fetch ? nullptr : "--shallow"),
                cfg.reps);
    }

    string plan;
    if (name_cfg)
    {
      for (const sync_config& cfg: origin_cfgs)
      {
        if (!plan.empty ())
          plan += ",\n";

        plan += "synchronizing ";

        // Use name if available, directory otherwise.
        //
        if (cfg != nullptr && cfg->name)
        {
          plan += '@';
          plan += *cfg->name;
        }
        else
          plan += cfg.path ().representation ();
      }

      plan += ':';
    }
    else
      plan = "synchronizing:";

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
        // -d options
        //
        small_vector<const char*, 32> d;
        for (const config& cfg: cfgs)
        {
          d.push_back ("-d");
          d.push_back (cfg.path.get ().string ().c_str ());
        }

        fdpipe pipe (open_pipe ()); // Text mode seems appropriate.

        process pr (
          start_bpkg (2,
                      co,
                      pipe /* stdout */,
                      2    /* stderr */,
                      "build",
                      d,
                      (linked_cfgs.size () != 1 ? "--no-move" : nullptr),
                      "--no-fetch",
                      "--no-refinement",
                      "--no-private-config", 125,
                      "--noop-exit", 124,
                      "--configure-only",
                      "--keep-out",
                      "--plan", plan,
                      (yes ? "--yes" : nullptr),
                      (so.no_query ? "--sys-no-query" : nullptr),
                      (so.install  ? "--sys-install"  : nullptr),
                      (so.no_fetch ? "--sys-no-fetch" : nullptr),
                      (so.no_stub  ? "--sys-no-stub"  : nullptr),
                      (so.yes      ? "--sys-yes"      : nullptr),
                      (so.sudo
                       ? cstrings ({"--sys-sudo", so.sudo->c_str ()})
                       : cstrings ()),
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
            dr << '\n' << s;
        }

        break;
      }

      link_dependency_config (co,
                              origin_prj, origin_cfgs,
                              prjs,
                              dep_chain,
                              create_host_config,
                              create_build2_config,
                              origin_tr,
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
      for (const sync_project& prj: prjs)
      {
        package_locations pls (load_packages (prj.path));

        for (const sync_project::config& cfg: prj.configs)
        {
          for (const package_state& pkg: cfg->packages)
          {
            // If this is a forwarded configuration, make sure forwarding is
            // configured and is up-to-date. Otherwise, make sure it is
            // disfigured (the config set --no-forward case).
            //
            dir_path src (prj.path);
            {
              auto i (find_if (pls.begin (), pls.end (),
                               [&pkg] (const package_location& pl)
                               {
                                 return pkg.name == pl.name;
                               }));

              if (i == pls.end ())
                fail << "package " << pkg.name << " is not listed in "
                     << prj.path;

              src /= i->path;
            }

            const char* o (nullptr);
            if (cfg->forward)
            {
              o = "configure:";
            }
            else if (!cfg.implicit) // Requires explicit sync.
            {
              //@@ This is broken: we will disfigure forwards to other
              //   configs. Looks like we will need to test that the forward
              //   is to this config. 'b info' here we come?
#if 0
              if (exists (src, path ("bootstrap") /= "out-root.build"))
                o = "disfigure:";
#endif
            }

            if (o != nullptr)
            {
              dir_path out (dir_path (cfg->path) /= pkg.name.string ());

              // Note that --no-external-modules makes a difference for
              // developing build system modules that require bootstrapping
              // (which without that option would trigger a recursive sync).
              //
              run_b (
                co,
                "--no-external-modules",
                o,
                '\'' + src.representation () + "'@'" + out.representation () +
                "',forward");
            }
          }
        }
      }
    }

    // Add/remove auto-synchronization build system hook.
    //
    // It feels right to only do this for origin_cfgs (remember, we require
    // explicit sync after changing the auto-sync flag).
    //
    if (origin && !implicit)
    {
      for (const sync_config& cfg: origin_cfgs)
      {
        path f (cfg->path / hook_file);
        bool e (exists (f));

        if (cfg->auto_sync)
        {
          if (!e)
          {
            mk (f.directory ());

            try
            {
              ofdstream os (f);

              // Should we analyze BDEP_SYNCED_CONFIGS ourselves or should we
              // let bdep-sync do it for us? Doing it here instead of spawning
              // a process (which will load the database, etc) will be faster.
              // But, on the other hand, this is only an issue for commands
              // like update and test that do their own implicit sync.
              //
              // cfgs = $getenv(BDEP_SYNCED_CONFIGS)
              // if! $null($cfgs)
              //   cfgs = [dir_paths] $regex.split($cfgs, ' *"([^"]*)" *', '\1')
              //
              // Also note that we try to avoid setting any variables in order
              // not to pollute the configuration's root scope.
              //
              os << "# Created automatically by bdep."                 << endl
                 << "#"                                                << endl
                 << "if ($build.meta_operation != 'info'      && \\"   << endl
                 << "    $build.meta_operation != 'configure' && \\"   << endl
                 << "    $build.meta_operation != 'disfigure')"        << endl
                 << "{"                                                << endl
                 << "  if ($getenv('BDEP_SYNC') == [null] || \\"       << endl
                 << "      $getenv('BDEP_SYNC') == true   || \\"       << endl
                 << "      $getenv('BDEP_SYNC') == 1)"                 << endl
                 << "    run '" << argv0 << "' sync --hook=1 "         <<
                "--verbose $build.verbosity "                          <<
                "($build.progress == [null] ? : $build.progress ? --progress : --no-progress) " <<
                "($build.diag_color == [null] ? : $build.diag_color ? --diag-color : --no-diag-color) " <<
                "--config \"$out_root\""                               << endl
                 << "}"                                                << endl;

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
      if (path_traits::compare (l.c_str () + b, n,
                                s.c_str (),     s.size ()) == 0)
        return true;
    }

    return false;
  }

  // The BDEP_SYNCED_CONFIGS environment variable.
  //
  // Note that it covers both depth and breadth (i.e., we don't restore the
  // previous value before returning, instead returning the "restorer"
  // guard). The idea here is for commands like update or test would perform
  // an implicit sync which will then be "noticed" by the build system
  // hook. This should be both faster (no need to spawn multiple bdep
  // processes) and simpler (no need to worry about who has the database open,
  // etc).
  //
  // We also used to do this only in the first form of sync but it turns out
  // we may end up invoking a hook during upgrade (e.g., to prepare a
  // distribution of a package as part of pkg-checkout which happens in the
  // configuration we have "hooked" -- yeah, this rabbit hole is deep).
  //
  static const char synced_configs[] = "BDEP_SYNCED_CONFIGS";

  synced_configs_guard::
  ~synced_configs_guard ()
  {
    if (original)
    {
      if (*original)
        setenv (synced_configs, **original);
      else
        unsetenv (synced_configs);
    }
  }

  // Check if the specified configuration directory is already (being)
  // synchronized. If it is not and add is true, then add it to the
  // BDEP_SYNCED_CONFIGS environment variable.
  //
  static bool
  synced (const dir_path& d, bool implicit, bool add = true)
  {
    string v;
    if (optional<string> e = getenv (synced_configs))
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
      setenv (synced_configs, v);
    }

    return false;
  }

  // Given the configuration directory, return absolute and normalized
  // directories and UUIDs of the entire linked configuration cluster.
  //
  static linked_configs
  find_config_cluster (const common_options& co, const dir_path& d)
  {
    linked_configs r;

    // Run bpkg-cfg-info to get the list of linked configurations.
    //
    fdpipe pipe (open_pipe ()); // Text mode seems appropriate.

    process pr (start_bpkg (3,
                            co,
                            pipe /* stdout */,
                            2    /* stderr */,
                            "cfg-info",
                            "-d", d,
                            "--link",
                            "--backlink",
                            "--recursive"));

    pipe.out.close (); // Shouldn't throw unless very broken.

    bool io (false);
    try
    {
      ifdstream is (move (pipe.in), fdstream_mode::skip, ifdstream::badbit);

      string l; // Reuse the buffer.
      do
      {
        linked_config c;

        optional<pair<bool /* path */, bool /* uuid */>> s;
        while (!eof (getline (is, l)))
        {
          if (!s)
            s = make_pair (false, false);

          if (l.empty ())
            break;

          if (l.compare (0, 6, "path: ") == 0)
          {
            try
            {
              c.path = dir_path (string (l, 6));
              s->first = true;
            }
            catch (const invalid_path&)
            {
              fail << "invalid bpkg-cfg-info output line '" << l
                   << "': invalid configuration path";
            }
          }
          else if (l.compare (0, 6, "uuid: ") == 0)
          {
            try
            {
              c.uuid = uuid (string (l, 6));
              s->second = true;
            }
            catch (const invalid_argument&)
            {
              fail << "invalid bpkg-cfg-info output line '" << l
                   << "': invalid configuration uuid";
            }
          }
        }

        if (s)
        {
          if (!s->first)
            fail << "invalid bpkg-cfg-info output: missing configuration path";

          if (!s->second)
            fail << "invalid bpkg-cfg-info output: missing configuration uuid";

          r.push_back (move (c));
        }
      }
      while (!is.eof ());

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

    if (r.empty ()) // We should have at leas the main configuration.
      fail << "invalid bpkg-cfg-info output: missing configuration information";

    return r;
  }

  synced_configs_guard
  cmd_sync (const common_options& co,
            const dir_path& prj,
            const shared_ptr<configuration>& c,
            bool implicit,
            const strings& pkg_args,
            optional<bool> fetch,
            bool yes,
            bool name_cfg,
            const package_locations& prj_pkgs,
            const sys_options& so,
            bool create_host_config,
            bool create_build2_config,
            transaction* t,
            vector<pair<dir_path, string>>* created_cfgs)
  {
    assert (!c->packages.empty ());

    synced_configs_guard r (getenv (synced_configs));

    if (synced (c->path, implicit))
    {
      r.original = nullopt; // Nothing changed.
      return r;
    }

    linked_configs lcfgs (find_config_cluster (co, c->path));
    for (auto j (lcfgs.begin () + 1); j != lcfgs.end (); ++j)
    {
      bool r (synced (j->path, true /* implicit */));
      assert (!r); // Should have been skipped via the first above.
    }

    cmd_sync (co,
              prj,
              {sync_config (c)},
              move (lcfgs),
              pkg_args,
              implicit,
              fetch,
              3                    /* bpkg_fetch_verb */,
              yes,
              name_cfg,
              nullopt              /* upgrade         */,
              nullopt              /* recursive       */,
              false                /* disfigure       */,
              prj_pkgs,
              strings ()           /* dep_pkgs        */,
              strings ()           /* deinit_pkgs     */,
              so,
              create_host_config,
              create_build2_config,
              t,
              created_cfgs);

    return r;
  }

  void
  cmd_sync (const common_options& co,
            const dir_path& prj,
            const configurations& xcfgs,
            bool implicit,
            const strings& pkg_args,
            bool fetch,
            bool yes,
            bool name_cfg,
            const package_locations& prj_pkgs,
            const sys_options& so,
            bool create_host_config,
            bool create_build2_config)
  {
    // Similar approach to the args overload below.
    //
    list<sync_config> cfgs (xcfgs.begin (), xcfgs.end ());

    while (!cfgs.empty ())
    {
      sync_configs ocfgs; // Originating configurations for this sync.

      ocfgs.push_back (move (cfgs.front ()));
      cfgs.pop_front ();

      assert (!ocfgs.back ()->packages.empty ());

      const dir_path& cd (ocfgs.back ().path ());

      if (synced (cd, implicit))
        continue;

      linked_configs lcfgs (find_config_cluster (co, cd));

      for (auto j (lcfgs.begin () + 1); j != lcfgs.end (); ++j)
      {
        const linked_config& cfg (*j);

        bool r (synced (cfg.path, true /* implicit */));
        assert (!r);

        for (auto i (cfgs.begin ()); i != cfgs.end (); )
        {
          if (cfg.path == i->path ())
          {
            ocfgs.push_back (move (*i));
            i = cfgs.erase (i);

            assert (!ocfgs.back ()->packages.empty ());
          }
          else
            ++i;
        }
      }

      cmd_sync (co,
                prj,
                move (ocfgs),
                move (lcfgs),
                pkg_args,
                implicit,
                fetch ? false /* shallow */ : optional<bool> (),
                3                    /* bpkg_fetch_verb */,
                yes,
                name_cfg,
                nullopt              /* upgrade         */,
                nullopt              /* recursive       */,
                false                /* disfigure       */,
                prj_pkgs,
                strings ()           /* dep_pkgs        */,
                strings ()           /* deinit_pkgs     */,
                so,
                create_host_config,
                create_build2_config,
                nullptr,
                nullptr);
    }
  }

  synced_configs_guard
  cmd_sync_implicit (const common_options& co,
                     const dir_path& cfg,
                     bool fetch,
                     bool yes,
                     bool name_cfg,
                     const sys_options& so,
                     bool create_host_config,
                     bool create_build2_config)
  {
    synced_configs_guard r (getenv (synced_configs));

    if (synced (cfg, true /* implicit */))
    {
      r.original = nullopt; // Nothing changed.
      return r;
    }

    linked_configs lcfgs (find_config_cluster (co, cfg));
    for (auto j (lcfgs.begin () + 1); j != lcfgs.end (); ++j)
    {
      bool r (synced (j->path, true /* implicit */));
      assert (!r); // Should have been skipped via the first above.
    }

    cmd_sync (co,
              dir_path () /* prj */,
              {sync_config (cfg)},
              move (lcfgs),
              strings ()            /* pkg_args        */,
              true                  /* implicit        */,
              fetch ? false /* shallow */ : optional<bool> (),
              3                     /* bpkg_fetch_verb */,
              yes,
              name_cfg,
              nullopt               /* upgrade         */,
              nullopt               /* recursive       */,
              false                 /* disfigure       */,
              package_locations ()  /* prj_pkgs        */,
              strings ()            /* dep_pkgs        */,
              strings ()            /* deinit_pkgs     */,
              so,
              create_host_config,
              create_build2_config);

    return r;
  }

  void
  cmd_sync_implicit (const common_options& co,
                     const dir_paths& xcfgs,
                     bool fetch,
                     bool yes,
                     bool name_cfg,
                     const sys_options& so,
                     bool create_host_config,
                     bool create_build2_config)
  {
    // Similar approach to the args overload below.
    //
    list<sync_config> cfgs (xcfgs.begin (), xcfgs.end ());

    while (!cfgs.empty ())
    {
      sync_configs ocfgs; // Originating configurations for this sync.

      ocfgs.push_back (move (cfgs.front ()));
      cfgs.pop_front ();

      const dir_path& cd (ocfgs.back ().path ());

      if (synced (cd, true /* implicit */))
        continue;

      linked_configs lcfgs (find_config_cluster (co, cd));

      for (auto j (lcfgs.begin () + 1); j != lcfgs.end (); ++j)
      {
        const linked_config& cfg (*j);

        bool r (synced (cfg.path, true /* implicit */));
        assert (!r);

        for (auto i (cfgs.begin ()); i != cfgs.end (); )
        {
          if (cfg.path == i->path ())
          {
            ocfgs.push_back (move (*i));
            i = cfgs.erase (i);
          }
          else
            ++i;
        }
      }

      cmd_sync (co,
                dir_path ()           /* prj             */,
                move (ocfgs),
                move (lcfgs),
                strings ()            /* pkg_args        */,
                true                  /* implicit        */,
                fetch ? false /* shallow */ : optional<bool> (),
                3,                    /* bpkg_fetch_verb */
                yes,
                name_cfg,
                nullopt               /* upgrade         */,
                nullopt               /* recursive       */,
                false                 /* disfigure       */,
                package_locations ()  /* prj_pkgs        */,
                strings ()            /* dep_pkgs        */,
                strings ()            /* deinit_pkgs     */,
                so,
                create_host_config,
                create_build2_config);
    }
  }

  void
  cmd_sync_deinit (const common_options& co,
                   const dir_path& prj,
                   const shared_ptr<configuration>& cfg,
                   const strings& pkgs,
                   transaction* origin_tr,
                   vector<pair<dir_path, string>>* created_cfgs)
  {
    sync_configs ocfgs {cfg};
    linked_configs lcfgs (find_config_cluster (co, cfg->path));

    cmd_sync (co,
              prj,
              move (ocfgs),
              move (lcfgs),
              strings ()            /* pkg_args             */,
              true                  /* implicit             */,
              false                 /* shallow fetch        */,
              3                     /* bpkg_fetch_verb      */,
              true                  /* yes                  */,
              false                 /* name_cfg             */,
              nullopt               /* upgrade              */,
              nullopt               /* recursive            */,
              false                 /* disfigure            */,
              package_locations ()  /* prj_pkgs             */,
              strings ()            /* dep_pkgs             */,
              pkgs,
              sys_options (),
              false                 /* create_host_config   */,
              false                 /* create_build2_config */,
              origin_tr,
              created_cfgs);
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
    // Note: scan_argument() passes through groups.
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

    // Note that all elements are either NULL/path or not (see initialization
    // below for details).
    //
    list<sync_config> cfgs;

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
      cfgs.assign (make_move_iterator (cs.first.begin ()),
                   make_move_iterator (cs.first.end ()));
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

        normalize (d, "configuration directory");

        if (open && contains (*open, d))
          continue;

        if (synced (d, o.implicit (), false /* add */))
          continue;

        cfgs.push_back (move (d));
      }

      if (cfgs.empty ())
        return 0; // All configuration are already (being) synchronized.

      // Initialize tmp directory.
      //
      init_tmp (empty_dir_path);
    }

    // Synchronize each configuration.
    //
    // A configuration can be part of a linked cluster which we sync all at
    // once. And some configurations in cfgs can be part of earlier clusters
    // that we have already sync'ed. Re-synchronizing them (actually the whole
    // clusters) again would be strange. Plus, we want to treat them as
    // "originating configurations" for dependency upgrade purposes.
    //
    // So what we are going to do is remove configurations from cfgs/cfg_dirs
    // as we go along.
    //
    bool empty (true); // All configurations are empty.
    for (size_t i (0), n (cfgs.size ()); !cfgs.empty (); )
    {
      sync_configs ocfgs; // Originating configurations for this sync.
      optional<size_t> m; // Number of packages in ocfgs.

      ocfgs.push_back (move (cfgs.front ()));
      cfgs.pop_front ();

      if (const shared_ptr<configuration>& c = ocfgs.back ())
        m = c->packages.size ();

      const dir_path& cd (ocfgs.back ().path ());

      // Check if this configuration is already (being) synchronized.
      //
      // Note that we should ignore the whole cluster but we can't run bpkg
      // here. So we will just ignore the configurations one by one (we expect
      // them all to be on the list, see below).
      //
      if (synced (cd, o.implicit ()))
      {
        empty = false;
        continue;
      }

      // Get the linked configuration cluster and "pull out" of cfgs
      // configurations that belong to this cluster. While at it also mark the
      // entire cluster as being synced.
      //
      // Note: we have already deatl with the first configuration in lcfgs.
      //
      linked_configs lcfgs (find_config_cluster (o, cd));

      for (auto j (lcfgs.begin () + 1); j != lcfgs.end (); ++j)
      {
        const linked_config& cfg (*j);

        bool r (synced (cfg.path, true /* implicit */));
        assert (!r); // Should have been skipped via the first above.

        for (auto i (cfgs.begin ()); i != cfgs.end (); )
        {
          if (cfg.path == i->path ())
          {
            ocfgs.push_back (move (*i));
            i = cfgs.erase (i);

            if (const shared_ptr<configuration>& c = ocfgs.back ())
              *m += c->packages.size ();
          }
          else
            ++i;
        }
      }

      // Skipping empty ones (part one).
      //
      // Note that we would normally be printing that for build-time
      // dependency configurations (which normally will not have any
      // initialized packages) and that would be annoying. So we suppress it
      // in case of the default configuration fallback (but also check and
      // warn if all of them were empty below).
      //
      if (m && *m == 0 && default_fallback)
        continue;

      // If we are synchronizing multiple configurations, separate them with a
      // blank line and print the configuration name/directory.
      //
      if (verb && n > 1)
      {
        diag_record dr (text);

        if (i++ != 0)
          dr << '\n';

        for (auto b (ocfgs.begin ()), j (b); j != ocfgs.end (); ++j)
        {
          if (j != b)
            dr << ",\n";

          dr << "in configuration " << *j;
        }

        dr << ':';
      }

      // Skipping empty ones (part two).
      //
      if (m && *m == 0)
      {
        if (verb)
        {
          diag_record dr (info);
          dr << "no packages initialized in ";

          // Note that in case of a cluster, we know we have printed the
          // configurations (see above) and thus can omit mentioning them
          // here.
          //
          if (ocfgs.size () == 0)
            dr << "configuration " << *ocfgs.back () << ", skipping";
          else
            dr << "configuration cluster, skipping";
        }

        continue;
      }

      empty = false;

      bool fetch (o.fetch () || o.fetch_full ());

      if (fetch)
      {
        for (const sync_config& c: ocfgs)
        {
          // Make sure this configuration is known in this project and has
          // some of its packages initialized. Failed, that, the project
          // repository we are trying to fetch will not be know in this
          // configuration and we will fail (see GitHub issue 391 for
          // details).
          //
          if (c != nullptr && !c->packages.empty ())
            cmd_fetch (o, prj, c, o.fetch_full ());
        }
      }

      // Increase verbosity for bpkg-fetch command in cmd_sync() if we perform
      // explicit fetches as well.
      //
      uint16_t bpkg_fetch_verb (fetch && !ocfgs.empty () ? 2 : 3);

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
                  prj,
                  move (ocfgs),
                  move (lcfgs),
                  pkg_args,
                  false                    /* implicit */,
                  !fetch ? false /* shallow */ : optional<bool> (),
                  bpkg_fetch_verb,
                  o.recursive () || o.immediate () ? o.yes () : true,
                  false                    /* name_cfg */,
                  !o.patch (), // Upgrade by default unless patch requested.
                  (o.recursive () ? optional<bool> (true)  :
                   o.immediate () ? optional<bool> (false) : nullopt),
                  o.disfigure (),
                  package_locations ()     /* prj_pkgs  */,
                  dep_pkgs,
                  strings ()               /* deinit_pkgs */,
                  sys_options (o),
                  o.create_host_config (),
                  o.create_build2_config ());
      }
      else if (o.upgrade () || o.patch ())
      {
        // The second form: upgrade of project packages' dependencies
        // (immediate by default, recursive if requested).
        //
        cmd_sync (o,
                  prj,
                  move (ocfgs),
                  move (lcfgs),
                  pkg_args,
                  false                    /* implicit */,
                  !fetch ? false /* shallow */ : optional<bool> (),
                  bpkg_fetch_verb,
                  o.yes (),
                  false                    /* name_cfg */,
                  o.upgrade (),
                  o.recursive (),
                  o.disfigure (),
                  prj_pkgs,
                  strings ()               /* dep_pkgs  */,
                  strings ()               /* deinit_pkgs */,
                  sys_options (o),
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
                  prj,
                  move (ocfgs),
                  move (lcfgs),
                  pkg_args,
                  o.implicit (),
                  !fetch ? false /* shallow */ : optional<bool> (),
                  bpkg_fetch_verb,
                  true                     /* yes         */,
                  o.implicit ()            /* name_cfg    */,
                  nullopt                  /* upgrade     */,
                  nullopt                  /* recursive   */,
                  o.disfigure (),
                  prj_pkgs,
                  strings ()               /* dep_pkgs    */,
                  strings ()               /* deinit_pkgs */,
                  sys_options (o),
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
