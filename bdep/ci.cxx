// file      : bdep/ci.cxx -*- C++ -*-
// copyright : Copyright (c) 2014-2018 Code Synthesis Ltd
// license   : MIT; see accompanying LICENSE file

#include <bdep/ci.hxx>

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
    // And aren't we in luck today: git-status --porcelain=2 (available since
    // git 2.11.0) gives us all this information with a single invocation.
    //
    string branch;
    string commit;
    {
      string head;
      string upstream;

      process pr;
      bool io (false);
      try
      {
        fdpipe pipe (fdopen_pipe ()); // Text mode seems appropriate.

        pr = start_git (semantic_version {2, 11, 0},
                        prj,
                        0    /* stdin  */,
                        pipe /* stdout */,
                        2    /* stderr */,
                        "status",
                        "--porcelain=2",
                        "--branch");

        pipe.out.close ();
        ifdstream is (move (pipe.in), fdstream_mode::skip, ifdstream::badbit);

        // Lines starting with '#' are headers with any other line indicating
        // some kind of change.
        //
        // The headers we are interested in are:
        //
        // # branch.oid <commit>  | (initial)       Current commit.
        // # branch.head <branch> | (detached)      Current branch.
        // # branch.upstream <upstream_branch>      If upstream is set.
        // # branch.ab +<ahead> -<behind>           If upstream is set and
        //                                          the commit is present.
        //
        // Note that if we are in the detached HEAD state, then we will only
        // see the first two with branch.head being '(detached)'.
        //
        for (string l; !eof (getline (is, l)); )
        {
          if (l[0] != '#')
            fail << "project directory has uncommitted changes" <<
              info << "run 'git status' for details";

          if (l.compare (2, 10, "branch.oid") == 0)
          {
            commit = string (l, 13);

            if (commit == "(initial)")
              fail << "no commits in project repository" <<
                info << "run 'git status' for details";
          }
          else if (l.compare (2, 11, "branch.head") == 0)
          {
            head = string (l, 14);

            if (head == "(detached)")
              fail << "project directory is in the detached HEAD state" <<
                info << "run 'git status' for details";
          }
          else if (l.compare (2, 15, "branch.upstream") == 0)
          {
            // This is normally in the <remote>/<branch> form, for example
            // 'origin/master'.
            //
            upstream = string (l, 18);
            size_t p (path::traits::rfind_separator (upstream));
            branch = p != string::npos ? string (upstream, p + 1) : upstream;
          }
          else if (l.compare (2, 9, "branch.ab") == 0)
          {
            // We definitely don't want to be ahead (upstream doesn't have
            // this commit) but there doesn't seem be anything wrong with
            // being behind.
            //
            if (l.compare (12, 3, "+0 ") != 0)
              fail << "local branch '" << head << "' is ahead of '"
                   << upstream << "'" <<
                info << "run 'git push' to update";
          }
        }

        is.close (); // Detect errors.
      }
      catch (const io_error&)
      {
        // Presumably the child process failed and issued diagnostics so let
        // finish_git() try to deal with that.
        //
        io = true;
      }

      finish_git (pr, io);

      // Make sure we've got everything we need.
      //
      if (commit.empty ())
        fail << "unable to obtain current commit" <<
          info << "run 'git status' for details";

      if (branch.empty ())
        fail << "no upstream branch set for local branch '" << head << "'" <<
          info << "run 'git push --set-upstream' to set";
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

    // If we are submitting the entire project, then we have two choices: we
    // can list all the packages in the project or we can only do so for
    // packages that were initialized in the (specified) configuration(s?).
    //
    // Note that other than getting the list of packages, we would only need
    // the configuration to obtain their versions. Since we can only have one
    // version for each package this is not strictly necessary but is sure a
    // good sanity check against local/remote mismatches. Also, it would be
    // nice to print the versions we are submitting in the prompt.
    //
    // While this isn't as clear cut, it also feels like a configuration could
    // be expected to serve as a list of packages, in case, for example, one
    // has configurations for subsets of packages or some such. And in the
    // future, who knows, we could have multi-project CI.
    //
    // So, let's go with the configuration. Specifically, if packages were
    // explicitly specified, we verify they are initialized. Otherwise, we use
    // the list of packages that are initialized in a configuration (single
    // for now).
    //
    // Note also that no pre-sync is needed since we are only getting versions
    // (via the info meta-operation).
    //
    project_packages pp (
      find_project_packages (o,
                             false /* ignore_packages */,
                             false /* load_packages   */));

    const dir_path& prj (pp.project);
    database db (open (prj, trace));

    shared_ptr<configuration> cfg;
    {
      transaction t (db.begin ());
      configurations cfgs (find_configurations (o, prj, t));
      t.commit ();

      if (cfgs.size () > 1)
        fail << "multiple configurations specified for ci";

      // If specified, verify packages are present in the configuration.
      //
      if (!pp.packages.empty ())
        verify_project_packages (pp, cfgs);

      cfg = move (cfgs[0]);
    }

    // Collect package names and their versions.
    //
    struct package
    {
      package_name     name;
      standard_version version;
    };
    vector<package> pkgs;

    auto add_package = [&o, &cfg, &pkgs] (package_name n)
    {
      standard_version v (package_version (o, cfg->path, n));
      pkgs.push_back (package {move (n), move (v)});
    };

    if (pp.packages.empty ())
    {
      for (const package_state& p: cfg->packages)
        add_package (p.name);
    }
    else
    {
      for (package_location& p: pp.packages)
        add_package (p.name);
    }

    // Get the server and repository URLs.
    //
    const url& srv (o.server_specified () ? o.server () : default_server);
    const repository_location rep (repository_url (o, prj));

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

      if (!yn_prompt ("continue? [y/n]"))
        return 1;
    }

    // Submit the request.
    //
    {
      // Print progress unless we had a prompt.
      //
      if (verb && o.yes ())
        text << "submitting to " << srv;

      url u (srv);
      u.query = "ci";

      using namespace http_service;

      parameters params ({{parameter::text, "repository", rep.string ()}});

      for (const package& p: pkgs)
        params.push_back ({parameter::text,
                           "package",
                           p.name.string () + '/' + p.version.string ()});

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
