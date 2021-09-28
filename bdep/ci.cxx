// file      : bdep/ci.cxx -*- C++ -*-
// license   : MIT; see accompanying LICENSE file

#include <bdep/ci.hxx>

#include <sstream>

#include <libbutl/path-pattern.hxx>
#include <libbutl/manifest-types.hxx>

#include <libbpkg/manifest.hxx>

#include <bdep/git.hxx>
#include <bdep/project.hxx>
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
            !path::traits_type::is_separator (s.upstream[n]))
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

    vector<manifest_name_value> overrides;

    auto override = [&overrides] (string n, string v)
    {
      overrides.push_back (
        manifest_name_value {move (n), move (v),    // Name and value.
                             0, 0, 0, 0, 0, 0, 0}); // Locations, etc.
    };

    // Add the default overrides.
    //
    override ("build-email", "");

    // Validate and append the specified overrides.
    //
    if (o.overrides_specified ())
    try
    {
      if (o.interactive_specified () || o.build_config_specified ())
      {
        for (const manifest_name_value& nv: o.overrides ())
        {
          if (nv.name == "builds"        ||
              nv.name == "build-include" ||
              nv.name == "build-exclude")
            fail << "'" << nv.name << "' override specified together with "
                 << (o.interactive_specified ()
                     ? "--interactive|-i"
                     : "--build-config");
        }
      }

      package_manifest::validate_overrides (o.overrides (), "" /* name */);

      overrides.insert (overrides.end (),
                        o.overrides ().begin (),
                        o.overrides ().end ());
    }
    catch (const manifest_parsing& e)
    {
      fail << "invalid overrides: " << e;
    }

    // Validate the --build-config option values and convert them into build
    // manifest value overrides.
    //
    if (o.build_config_specified ())
    try
    {
      if (o.interactive_specified ())
        fail << "--build-config specified together with --interactive|-i";

      override ("builds", "all");

      for (const string& c: o.build_config ())
        override ("build-include", c);

      override ("build-exclude", "**");

      // Note that some of the overrides are knowingly valid (builds:all,
      // etc), but let's keep it simple and validate all of them.
      //
      package_manifest::validate_overrides (overrides, "" /* name */);
    }
    catch (const manifest_parsing& e)
    {
      fail << "invalid --build-config option value: " << e;
    }

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
    // cases we also verify that for each package only one configuration, it
    // is initialized in, is specified (while we currently don't need this
    // restriction, this may change in the future if we decide to support
    // archive-based CI or some such).
    //
    //
    // Note also that no pre-sync is needed since we are only getting versions
    // (via the info meta-operation).
    //
    project_packages pp (
      find_project_packages (o,
                             false /* ignore_packages */,
                             false /* load_packages   */));

    const dir_path& prj (pp.project);

    configurations cfgs;
    {
      // Don't keep the database open longer than necessary.
      //
      database db (open (prj, trace));

      transaction t (db.begin ());
      cfgs = find_configurations (o, prj, t).first;
      t.commit ();
    }

    // Collect package names, versions, and configurations used.
    //
    struct package
    {
      package_name              name;
      standard_version          version;
      shared_ptr<configuration> config;
    };
    vector<package> pkgs;

    // Add a package to the list, suppressing duplicates and verifying that it
    // is initialized in only one configuration.
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

      standard_version v (package_version (o, c->path, n));
      pkgs.push_back (package {move (n), move (v), move (c)});
    };

    if (pp.packages.empty ())
    {
      for (const shared_ptr<configuration>& c: cfgs)
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

        for (const shared_ptr<configuration>& c: cfgs)
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

        if (!init)
          fail << "package " << p.name << " is not initialized in any "
               << "configuration";
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

      size_t p (s.find ('/'));

      if (p != string::npos)
      {
        icfg = string (s, 0, p);
        ibpk = string (s, p + 1);
      }
      else
        icfg = s;

      if (icfg->empty ())
        fail << "invalid --interactive|-i option value '" << s
             << "': configuration name is empty";

      if (path_pattern (*icfg))
        fail << "invalid --interactive|-i option value '" << s
             << "': configuration name is a pattern";

      override ("builds", "all");
      override ("build-include", *icfg);
      override ("build-exclude", "**");

      if (!ibpk)
        ibpk = "error";
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

      if (!yn_prompt ("continue? [y/n]"))
        return 1;
    }

    // Submit the request.
    //
    {
      // Print progress unless we had a prompt.
      //
      if (verb && o.yes () && !o.no_progress ())
        text << "submitting to " << srv;

      url u (srv);
      u.query = "ci";

      using namespace http_service;

      parameters params ({{parameter::text, "repository", move (rep)}});

      for (const package& p: pkgs)
        params.push_back ({parameter::text,
                           "package",
                           p.name.string () + '/' + p.version.string ()});

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
