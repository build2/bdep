// file      : bdep/ci.cxx -*- C++ -*-
// license   : MIT; see accompanying LICENSE file

#include <bdep/ci.hxx>

#include <sstream>
#include <algorithm> // equal()

#include <libbutl/path-pattern.hxx>
#include <libbutl/manifest-types.hxx>

#include <libbpkg/manifest.hxx>

#include <bdep/git.hxx>
#include <bdep/project.hxx>
#include <bdep/ci-types.hxx>
#include <bdep/database.hxx>
#include <bdep/diagnostics.hxx>
#include <bdep/http-service.hxx>

using namespace std;
using namespace butl;

namespace bdep
{
  using bpkg::repository_location;
  using bpkg::package_manifest;

  // Note: the git_status() function, that we use, requires git 2.11.0 or
  // higher.
  //
  static const semantic_version git_ver {2, 11, 0};

  static const url default_server (
#ifdef BDEP_STAGE
    "https://ci.stage.build2.org"
#else
    "https://ci.cppget.org"
#endif
  );

  // Get the project's remote repository location corresponding to the current
  // (local) state of the repository. Fail if the working directory is not
  // clean or if the local state isn't in sync with the remote.
  //
  static repository_location
  git_repository_url (const cmd_ci_options& o, const dir_path& prj)
  {
    // This is what we need to do:
    //
    // 1. Check that the working directory is clean.
    //
    // 2. Check that we are not ahead of upstream.
    //
    // 3. Get the corresponding upstream branch.
    //
    // 4. Get the current commit id.
    //
    string branch;
    string commit;
    {
      git_repository_status s (git_status (prj));

      if (s.commit.empty ())
        fail << "no commits in project repository" <<
          info << "run 'git status' for details";

      commit = move (s.commit);

      // Note: not forcible. The use case could be to CI some commit from the
      // past. But in this case we also won't have upstream. So maybe it will
      // be better to invent the --commit option or some such.
      //
      if (s.branch.empty ())
        fail << "project directory is in the detached HEAD state" <<
          info << "run 'git status' for details";

      // Upstream is normally in the <remote>/<branch> form, for example
      // 'origin/master'. Note, however, that we cannot unambiguously split it
      // into the remote and branch names (see release.cxx for details).
      //
      {
        if (s.upstream.empty ())
          fail << "no upstream branch set for local branch '"
               << s.branch << "'" <<
            info << "run 'git push --set-upstream' to set";

        // It's unlikely that the branch remote is configured globally, so we
        // use the bundled git.
        //
        optional<string> rem (git_line (git_ver,
                                        false /* system */,
                                        prj,
                                        false /* ignore_error */,
                                        "config",
                                        "branch." + s.branch + ".remote"));

        if (!rem)
          fail << "unable to obtain remote for '" << s.branch << "'";

        // For good measure verify that the remote name is a prefix for the
        // upstream branch.
        //
        size_t n (rem->size ());
        if (s.upstream.compare (0, n, *rem) != 0 ||
            !path_traits::is_separator (s.upstream[n]))
          fail << "remote '" << *rem << "' is not a prefix for upstream "
               << "branch '" << s.upstream << "'";

        branch.assign (s.upstream, n + 1, string::npos);
      }

      // Note: not forcible (for now). While the use case is valid, the
      // current and committed package versions are likely to differ (in
      // snapshot id). Obtaining the committed versions feels too hairy for
      // now.
      //
      if (s.staged || s.unstaged)
        fail << "project directory has uncommitted changes" <<
          info << "run 'git status' for details" <<
          info << "use 'git stash' to temporarily hide the changes";

      // We definitely don't want to be ahead (upstream doesn't have this
      // commit) but there doesn't seem be anything wrong with being behind.
      //
      if (s.ahead)
        fail << "local branch '" << s.branch << "' is ahead of '"
             << s.upstream << "'" <<
          info << "run 'git push' to update";
    }

    // We treat the URL specified with --repository as a "base", that is, we
    // still add the fragment.
    //
    url u (o.repository_specified ()
           ? o.repository ()
           : git_remote_url (prj, "--repository"));

    if (u.fragment)
      fail << "remote git repository URL '" << u << "' already has fragment";

    // Try to construct the remote repository location out of the URL and fail
    // if that's not possible.
    //
    try
    {
      // We specify both the branch and the commit to give bpkg every chance
      // to minimize the amount of history to fetch (see
      // bpkg-repository-types(1) for details).
      //
      repository_location r (
        bpkg::repository_url (u.string () + '#' + branch + '@' + commit),
        bpkg::repository_type::git);

      if (!r.local ())
        return r;

      // Fall through.
    }
    catch (const invalid_argument&)
    {
      // Fall through.
    }

    fail << "unable to derive bpkg repository location from git repository "
         << "URL '" << u << "'" << endf;
  }

  static repository_location
  repository_url (const cmd_ci_options& o, const dir_path& prj)
  {
    if (git_repository (prj))
      return git_repository_url (o, prj);

    fail << "project has no known version control-based repository" << endf;
  }

  int
  cmd_ci (const cmd_ci_options& o, cli::scanner&)
  {
    tracer trace ("ci");

    if (o.forward ())
    {
      if (const char* n = (o.config_name_specified () ? "@<cfg-name>" :
                           o.config_id_specified ()   ? "--config-id" :
                           o.config_specified ()      ? "--config|-c" :
                           o.all ()                   ? "--all|-a"    : nullptr))
        fail << n << " specified together with --forward";
    }

    // Collect the packages manifest value overrides parsing the --override,
    // etc options and verify that the resulting overrides list contains valid
    // package manifest values and is semantically correct.
    //
    // Note that if we end up with any build package configuration-specific
    // overrides or any [*-]build-auxiliary[-*] overrides, then we will need
    // to verify the overrides using the package manifests to make sure that
    // these overrides are valid for the specified packages.
    //
    vector<manifest_name_value> overrides;

    using origin = cmd_ci_override_origin;

    auto override = [&overrides] (string n, string v, origin o)
    {
      uint64_t orig (static_cast<uint64_t> (o));

      overrides.push_back (
        manifest_name_value {move (n), move (v), // Name and value.
                             orig, 0,            // Name line and column.
                             orig, 0,            // Value line and column.
                             0, 0, 0});          // File positions.
    };

    // Append the overrides specified by --override, --overrides-file,
    // --build-email, and --builds which are all handled by
    // cli::parser<cmd_ci_override>. But first verify that they don't clash
    // with the other build constraints-related options. Also detect if any of
    // them are build package configuration-specific build constraint
    // overrides.
    //
    bool pkg_config_ovr (o.build_config_specified ()   ||
                         o.package_config_specified () ||
                         (o.interactive_specified () &&
                          o.interactive ().find ('/') != string::npos));

    bool build_email_ovr (false);
    bool aux_ovr         (false);

    // Return true, if the [*-]builds override is specified.
    //
    auto builds_override = [&overrides] (const string& config = empty_string)
    {
      if (config.empty ())
      {
        return find_if (overrides.begin (), overrides.end (),
                        [] (const manifest_name_value& v)
                        {
                          return v.name == "builds";
                        }) != overrides.end ();
      }
      else
      {
        string n (config + "-builds");

        return find_if (overrides.begin (), overrides.end (),
                        [&n] (const manifest_name_value& v)
                        {
                          return v.name == n;
                        }) != overrides.end ();
      }
    };

    if (o.overrides_specified ())
    {
      const char* co (o.target_config_specified ()  ? "--target-config"  :
                      o.build_config_specified ()   ? "--build-config"   :
                      o.package_config_specified () ? "--package-config" :
                      o.interactive_specified ()    ? "--interactive|-i" :
                                                      nullptr);

      for (const manifest_name_value& nv: o.overrides ())
      {
        const string& n (nv.name);

        // True if the name is *-builds.
        //
        bool cbso (
          n.size () > 7 && n.compare (n.size () - 7, 7, "-builds") == 0);

        // True if the name is one of {*-builds, *-build-{include,exclude}}
        // and update the pkg_config_ovr flag accordingly if that's the case.
        //
        bool cbo (cbso                                                    ||
                  (n.size () > 14 &&
                   n.compare (n.size () - 14, 14, "-build-include") == 0) ||
                  (n.size () > 14 &&
                   n.compare (n.size () - 14, 14, "-build-exclude") == 0));

        if (cbo)
          pkg_config_ovr = true;

        // Fail if --{target,build,package}-config or --interactive is
        // combined with a [*-]build-{include,exclude} override (but not with
        // [*-]builds).
        //
        if (co != nullptr &&
            ((cbo && !cbso)       ||
             n == "build-include" ||
             n == "build-exclude"))
        {
          fail << "invalid " << to_string (static_cast<origin> (nv.name_line))
               << ": " << "'" << n << "' override specified together with "
               << co <<
            info << "override: " << n << ": " << nv.value;
        }

        // Check if the name is one of {[*-]build-*email} and update the
        // pkg_config_ovr and build_email_ovr flags accordingly if that's the
        // case.
        //
        if (!cbo)
        {
          bool ceo (
            (n.size () > 12 &&
             n.compare (n.size () - 12, 12, "-build-email") == 0)         ||
            (n.size () > 20 &&
             n.compare (n.size () - 20, 20, "-build-warning-email") == 0) ||
            (n.size () > 18 &&
             n.compare (n.size () - 18, 18, "-build-error-email") == 0));

          if (ceo)
            pkg_config_ovr = true;

          build_email_ovr = (ceo                        ||
                             n == "build-email"         ||
                             n == "build-warning-email" ||
                             n == "build-error-email");
        }

        // Check if the name is one of {[*-]build-auxiliary[-*]} and update
        // the pkg_config_ovr and aux_ovr flags accordingly if that's the
        // case.
        //
        if (!cbo && !build_email_ovr)
        {
          if (optional<pair<string, string>> an =
              bpkg::build_auxiliary::parse_value_name (n))
          {
            aux_ovr = true;

            if (!an->first.empty ())
              pkg_config_ovr = true;
          }
        }

        // Check if the name is one of {*-build-bot}and update the
        // pkg_config_ovr flag accordingly if that's the case.
        //
        if (!cbo && !build_email_ovr && !aux_ovr)
        {
          if (n.size () > 10 &&
              n.compare (n.size () - 10, 10, "-build-bot") == 0)
            pkg_config_ovr = true;
        }
      }

      overrides.insert (overrides.end (),
                        o.overrides ().begin (),
                        o.overrides ().end ());
    }

    // Add the default overrides.
    //
    if (!build_email_ovr)
      override ("build-email", "", origin::build_email);

    // Append the overrides specified by --target-config, but first verify
    // that they don't clash with the other build constraints-related options.
    //
    if (o.target_config_specified ())
    {
      if (o.build_config_specified ())
        fail << "--target-config specified together with --build-config";

      if (o.package_config_specified ())
        fail << "--target-config specified together with --package-config";

      if (o.interactive_specified ())
        fail << "--target-config specified together with --interactive|-i";

      // Add "builds: all", unless the builds value is already overridden.
      //
      if (!builds_override ())
        override ("builds", "all", origin::target_config);

      for (const string& c: o.target_config ())
        override ("build-include", c, origin::target_config);

      override ("build-exclude", "**", origin::target_config);
    }

    // Note that we will add the build package configuration-specific
    // overrides after the packages being CI-ed are determined.

    // If we are submitting the entire project, then we have two choices: we
    // can list all the packages in the project or we can only do so for
    // packages that were initialized in the specified configurations.
    //
    // Note that other than getting the list of packages, we would only need
    // the configurations to obtain their versions. Since we can only have one
    // version for each package this is not strictly necessary but is sure a
    // good sanity check against local/remote mismatches. Also, it would be
    // nice to print the versions we are submitting in the prompt.
    //
    // While this isn't as clear cut, it also feels like a configuration could
    // be expected to serve as a list of packages, in case, for example, one
    // has configurations for subsets of packages or some such. And in the
    // future, who knows, we could have multi-project CI.
    //
    // So, let's go with the configurations. Specifically, if packages were
    // explicitly specified, we verify they are initialized. Otherwise, we use
    // the list of packages that are initialized in configurations. In both
    // cases we also verify that for each package only one configuration in
    // which it is initialized is specified (while we currently don't need
    // this restriction, this may change in the future if we decide to support
    // archive-based CI or some such).
    //
    // In the forward mode we use package's forwarded source directories to
    // obtain their versions. Also we load the project packages if the
    // specified directory is a project directory.
    //
    // Note also that no pre-sync is needed since we are only getting versions
    // (via the info meta-operation).
    //
    project_packages pp (
      find_project_packages (o,
                             false        /* ignore_packages */,
                             o.forward () /* load_packages */));

    const dir_path& prj (pp.project);

    // Collect package names, versions, and configurations used (except for
    // the forward mode).
    //
    struct package
    {
      package_name              name;
      string                    version;
      shared_ptr<configuration> config;   // NULL in the forward mode.
      dir_path                  src_root;
    };
    vector<package> pkgs;

    if (o.forward ())
    {
      // Add a package to the list and suppressing duplicates.
      //
      auto add_package = [&pkgs] (package_name n,
                                  const dir_path& d,
                                  package_info&& pi)
      {
        auto i (find_if (pkgs.begin (),
                         pkgs.end (),
                         [&n] (const package& p) {return p.name == n;}));

        if (i != pkgs.end ())
          return;

        // Verify the package version, unless it is standard and thus is
        // already verified.
        //
        if (pi.version.empty ())
        try
        {
          bpkg::version (pi.version_string);
        }
        catch (const invalid_argument& e)
        {
          fail << "invalid package " << n << " version '" << pi.version_string
               << "': " << e <<
            info << "package source directory is " << d;
        }

        pkgs.push_back (package {
            move (n), move (pi.version_string), nullptr, move (pi.src_root)});
      };

      for (package_location& p: pp.packages)
      {
        dir_path d (pp.project / p.path);
        package_info pi (package_b_info (o, d, b_info_flags::none));

        if (pi.src_root == pi.out_root)
          fail << "package " << p.name << " source directory is not forwarded" <<
            info << "package source directory is " << d;

        add_package (p.name, d, move (pi));
      }
    }
    else
    {
      pair<configurations, bool> cfgs;
      {
        // Don't keep the database open longer than necessary.
        //
        database db (open (prj, trace));

        transaction t (db.begin ());
        cfgs = find_configurations (o, prj, t);
        t.commit ();

        verify_project_packages (pp, cfgs);
      }

      // Add a package to the list, suppressing duplicates and verifying that
      // it is initialized in only one configuration.
      //
      auto add_package = [&o, &pkgs] (package_name n,
                                      shared_ptr<configuration> c)
      {
        auto i (find_if (pkgs.begin (),
                         pkgs.end (),
                         [&n] (const package& p) {return p.name == n;}));

        if (i != pkgs.end ())
        {
          if (i->config == c)
            return;

          fail << "package " << n << " is initialized in multiple specified "
               << "configurations" <<
            info << *i->config <<
            info << *c;
        }

        package_info pi (package_b_info (o,
                                         dir_path (c->path) /= n.string (),
                                         b_info_flags::none));

        verify_package_info (pi, n);

        pkgs.push_back (package {
            move (n),
            pi.version.string (),
            move (c),
            move (pi.src_root)});
      };

      if (pp.packages.empty ())
      {
        for (const shared_ptr<configuration>& c: cfgs.first)
        {
          for (const package_state& p: c->packages)
            add_package (p.name, c);
        }
      }
      else
      {
        for (package_location& p: pp.packages)
        {
          bool init (false);

          for (const shared_ptr<configuration>& c: cfgs.first)
          {
            if (find_if (c->packages.begin (),
                         c->packages.end (),
                         [&p] (const package_state& s)
                         {
                           return p.name == s.name;
                         }) != c->packages.end ())
            {
              // Add the package, but continue the loop to detect a potential
              // configuration ambiguity.
              //
              add_package (p.name, c);
              init = true;
            }
          }

          assert (init); // Wouldn't be here otherwise.
        }
      }
    }

    // If there are any build package configuration-specific overrides or any
    // build auxiliary overrides, then load the package manifests to use them
    // later for validation of the complete override list. Note that we also
    // need these manifests for producing the --package-config overrides.
    //
    vector<package_manifest> override_manifests;

    if (pkg_config_ovr || aux_ovr)
    {
      override_manifests.reserve (pkgs.size ());

      for (const package& p: pkgs)
      {
        path f (p.src_root / manifest_file);

        if (!exists (f))
          fail << "package manifest file " << f << " does not exist";

        try
        {
          ifdstream is (f);
          manifest_parser p (is, f.string ());
          override_manifests.emplace_back (p);
        }
        catch (const manifest_parsing& e)
        {
          fail << "invalid package manifest: " << f << ':'
               << e.line << ':' << e.column << ": " << e.description;
        }
        catch (const io_error& e)
        {
          fail << "unable to read " << f << ": " << e;
        }
      }
    }

    // Append the overrides specified by --build-config, but first verify that
    // they don't clash with the other build constraints-related options. Also
    // collect the names of the build package configs they involve.
    //
    strings package_configs;

    if (o.build_config_specified ())
    {
      if (o.interactive_specified ())
        fail << "--build-config specified together with --interactive|-i";

      for (const string& bc: o.build_config ()) // <pc>/<tc>[/<tg>]
      {
        size_t n (bc.find ('/'));

        if (n == string::npos)
          fail << "invalid --build-config option value: no target "
               << "configuration in '" << bc << '\'';

        if (n == 0)
          fail << "invalid --build-config option value: no package "
               << "configuration in '" << bc << '\'';

        string pc (bc, 0, n);

        bool first (find (package_configs.begin (),  package_configs.end (),
                          pc) == package_configs.end ());

        // For the specific <pc> add "<pc>-builds: all" when the first
        // --build-config <pc>/... option is encountered, unless the
        // "<pc>-builds" value is already overridden.
        //
        if (first && !builds_override (pc))
          override (pc + "-builds", "all", origin::build_config);

        override (pc + "-build-include",
                  string (bc, n + 1),
                  origin::build_config);

        if (first)
          package_configs.push_back (move (pc));
      }

      for (const string& pc: package_configs)
        override (pc + "-build-exclude", "**", origin::build_config);
    }

    // Append the overrides specified by --package-config, but first verify
    // that they don't clash with the other build constraints-related options.
    //
    // Note that we also need to make sure that we end up with the same
    // overrides regardless of the package we use to produce them.
    //
    if (o.package_config_specified ())
    {
      if (o.interactive_specified ())
        fail << "--package-config specified together with --interactive|-i";

      for (const string& pc: o.package_config ())
      {
        if (find (package_configs.begin (), package_configs.end (), pc) !=
            package_configs.end ())
          fail << "package configuration " << pc << " is specified using "
               << "both --package-config and --build-config";

        // If for the specific <pc> the "<pc>-builds" value is already
        // overridden, then skip the --package-config <pc> option, assuming
        // that the former override has already selected the <pc>
        // configuration for the CI task.
        //
        if (builds_override (pc))
          continue;

        using bpkg::build_package_config;
        using bpkg::build_class_expr;
        using bpkg::build_constraint;

        auto override_builds = [&pc, &override]
                               (const small_vector<build_class_expr, 1>& bs,
                                const vector<build_constraint>& cs)
        {
          if (!bs.empty ())
          {
            string n (pc + "-builds");
            for (const build_class_expr& e: bs)
              override (n, e.string (), origin::package_config);
          }

          if (!cs.empty ())
          {
            string in (pc + "-build-include");
            string en (pc + "-build-exclude");

            for (const build_constraint& bc: cs)
              override (bc.exclusion ? en : in,
                        (!bc.target
                         ? bc.config
                         : bc.config + '/' + *bc.target),
                        origin::package_config);
          }
        };

        // Generate overrides based on the --package-config option for every
        // package and verify that we always end up with the same overrides.
        //
        optional<vector<manifest_name_value>> overrides_initial;
        optional<vector<manifest_name_value>> overrides_cache;

        for (const package_manifest& m: override_manifests)
        {
          // Save/restore the initial overrides.
          //
          if (overrides_initial)
            overrides = *overrides_initial;
          else if (override_manifests.size () > 1) // Will we compare overrides?
            overrides_initial = overrides;

          const small_vector<build_package_config, 1>& cs (m.build_configs);

          // Fail if the specified build configuration is not found, unless
          // there is a corresponding *-build-config override which means that
          // this configuration will be created. Note that no configuration-
          // specific build constraint overrides have been specified for it,
          // since we would fail earlier in that case (they would clash with
          // *-package-config). Thus, we will just override this being created
          // build configuration with the common build constraints.
          //
          const build_package_config* c (nullptr);

          auto i (find_if (cs.begin (), cs.end (),
                           [&pc] (const build_package_config& c)
                           {return c.name == pc;}));

          if (i == cs.end ())
          {
            string v (pc + "-build-config");

            if(find_if (overrides.begin (), overrides.end (),
                        [&v] (const manifest_name_value& nv)
                        {return nv.name == v;}) == overrides.end ())
            {
              fail << "invalid --package-config option value: package "
                   << m.name << " has no build configuration '" << pc << '\'';
            }
          }
          else
            c = &*i;

          // Override the package configuration with it's current build
          // constraints, if present, and with the common build constraints
          // otherwise.
          //
          if (c != nullptr && (!c->builds.empty () || !c->constraints.empty ()))
            override_builds (c->builds, c->constraints);
          else if (!m.builds.empty () || !m.build_constraints.empty ())
            override_builds (m.builds, m.build_constraints);
          else
            override (pc + "-builds", "default", origin::package_config);

          // Save the overrides for the first package and verify they are equal
          // to the saved one for the remaining packages.
          //
          if (!overrides_cache)
          {
            overrides_cache = move (overrides);
          }
          else if (!equal (overrides.begin (), overrides.end (),
                           overrides_cache->begin (), overrides_cache->end (),
                           [] (const auto& x, const auto& y)
                           {return x.name == y.name && x.value == y.value;}))
          {
            fail << "invalid --package-config option value: overrides for "
                 << "configuration '" << pc << "' differ for packages "
                 << override_manifests[0].name << " and " << m.name;
          }
        }

        assert (overrides_cache); // Must be at least one package.

        overrides = move (*overrides_cache);
      }
    }

    // Extract the interactive mode configuration and breakpoint from the
    // --interactive|-i option value, reducing the former to the build
    // manifest value overrides.
    //
    // Both are present in the interactive mode and are absent otherwise.
    //
    optional<string> icfg;
    optional<string> ibpk;

    if (o.interactive_specified ())
    {
      if (pkgs.size () > 1)
        fail << "multiple packages specified with --interactive|-i";

      const string& s (o.interactive ());
      validate_utf8_graphic (s, "--interactive|-i option value");

      size_t p (s.find (':'));

      if (p != string::npos)
      {
        icfg = string (s, 0, p);
        ibpk = string (s, p + 1);
      }
      else
        icfg = s;

      p = icfg->find ('/');

      string pc;
      string tc;
      string tg;

      if (p != string::npos)
      {
        if (p == 0)
          fail << "invalid --interactive|-i option value '" << s
               << "': package configuration name is empty";

        pc = string (*icfg, 0, p);

        string t (*icfg, p + 1); // tc[/tg]

        p = t.find ('/');

        if (p != string::npos)
        {
          if (p == t.size () - 1)
            fail << "invalid --interactive|-i option value '" << s
                 << "': target name is empty";

          tc = string (t, 0, p);
          tg = string (t, p + 1);
        }
        else
          tc = move (t);
      }
      else
        tc = *icfg;

      if (tc.empty ())
        fail << "invalid --interactive|-i option value '" << s
             << "': target configuration name is empty";

      // For the specific <pc> add "<pc>-builds: all", unless the
      // "<pc>-builds" value is already overridden.
      //
      bool bo (builds_override (pc));

      if (!pc.empty ())
        pc += '-';

      if (!tg.empty ())
        tg = '/' + tg;

      if (!bo)
        override (pc + "builds", "all", origin::interactive);

      override (pc + "build-include", tc + tg, origin::interactive);
      override (pc + "build-exclude", "**", origin::interactive);

      if (!ibpk)
        ibpk = "error";
    }

    // Verify the collected overrides.
    //
    if (!overrides.empty ())
    {
      // Let's also save the override value index as a name/value columns
      // (similar to what we do with the origin options), so that we can
      // attribute the potential error back to the override value and add it
      // to the diagnostics.
      //
      for (uint64_t i (0); i != overrides.size (); ++i)
      {
        manifest_name_value& nv (overrides[i]);
        nv.name_column = nv.value_column = i;
      }

      auto bad_ovr = [&overrides, &override_manifests]
                     (const manifest_parsing& e, const package_name& n = {})
      {
        assert (e.column < overrides.size ());

        const manifest_name_value& nv (overrides[e.column]);

        diag_record dr (fail);
        dr << "invalid " << to_string (static_cast<origin> (e.line))
           << ": " << e.description <<
          info << "override: " << nv.name << ':';

        if (nv.value.find ('\n') == string::npos)
          dr << ' ' << nv.value;
        else
          dr << "\n\\\n" << nv.value << "\n\\";

        if (!n.empty () && override_manifests.size () != 1)
          dr << info << "package: " << n;
      };

      // If the package manifests are loaded (which happens if there are any
      // build package configuration-specific or build auxiliary overrides),
      // then override them all. Otherwise, use
      // package_manifest::validate_overrides().
      //
      // Specify the name argument for the override validation call to make
      // sure the origin/value information (saved into the values'
      // lines/columns) is provided in a potential exception.
      //
      if (!override_manifests.empty ())
      {
        for (package_manifest& m: override_manifests)
        {
          try
          {
            m.override (overrides, "options");
          }
          catch (const manifest_parsing& e)
          {
            bad_ovr (e, m.name);
          }
        }
      }
      else
      {
        try
        {
          package_manifest::validate_overrides (overrides, "options");
        }
        catch (const manifest_parsing& e)
        {
          bad_ovr (e);
        }
      }
    }

    // Get the server and repository URLs.
    //
    const url& srv (o.server_specified () ? o.server () : default_server);
    string rep (repository_url (o, prj).string ());

    // Make sure that parameters we post to the CI service are UTF-8 encoded
    // and contain only the graphic Unicode codepoints.
    //
    validate_utf8_graphic (rep, "repository URL", "--repository");

    if (o.simulate_specified ())
      validate_utf8_graphic (o.simulate (), "--simulate option value");

    // Print the plan and ask for confirmation.
    //
    if (!o.yes ())
    {
      text << "submitting:" << '\n'
           << "  to:      " << srv << '\n'
           << "  in:      " << rep;

      for (const package& p: pkgs)
      {
        diag_record dr (text);

        // If printing multiple packages, separate them with a blank line.
        //
        if (pkgs.size () > 1)
          dr << '\n';

        dr << "  package: " << p.name    << '\n'
           << "  version: " << p.version;
      }

      if (icfg)
      {
        assert (ibpk);
        text << "  config:  " << *icfg << '\n'
             << "  b/point: " << *ibpk;
      }

      if (!yn_prompt ("continue? [Y/n]", 'y'))
        return 1;
    }

    // Submit the request.
    //
    {
      // Print progress unless we had a prompt.
      //
      if (o.yes () && ((verb && !o.no_progress ()) || o.progress ()))
        text << "submitting to " << srv;

      url u (srv);
      u.query = "ci";

      using namespace http_service;

      parameters params ({{parameter::text, "repository", move (rep)}});

      for (const package& p: pkgs)
        params.push_back ({parameter::text,
                           "package",
                           p.name.string () + '/' + p.version});

      if (ibpk)
        params.push_back ({parameter::text, "interactive", move (*ibpk)});

      try
      {
        ostringstream os;
        manifest_serializer s (os, "" /* name */);
        serialize_manifest (s, overrides);

        params.push_back ({parameter::file_text, "overrides", os.str ()});
      }
      catch (const manifest_serialization&)
      {
        // Values are verified by package_manifest::validate_overrides ();
        //
        assert (false);
      }

      if (o.simulate_specified ())
        params.push_back ({parameter::text, "simulate", o.simulate ()});

      // Disambiguates with odb::result.
      //
      http_service::result r (post (o, u, params));

      if (!r.reference)
        fail << "no reference in response";

      if (verb)
        text << r.message << '\n'
             << "reference: " << *r.reference;
    }

    return 0;
  }
}
