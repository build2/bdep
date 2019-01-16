// file      : bdep/publish.cxx -*- C++ -*-
// copyright : Copyright (c) 2014-2019 Code Synthesis Ltd
// license   : MIT; see accompanying LICENSE file

#include <bdep/publish.hxx>

#include <libbutl/manifest-parser.mxx>
#include <libbutl/manifest-serializer.mxx>

#include <libbpkg/manifest.hxx>

#include <bdep/git.hxx>
#include <bdep/project.hxx>
#include <bdep/project-author.hxx>
#include <bdep/database.hxx>
#include <bdep/diagnostics.hxx>
#include <bdep/http-service.hxx>

#include <bdep/sync.hxx>

using namespace std;
using namespace butl;

namespace bdep
{
  // The minimum supported git version must be at least 2.5.0 due to the git
  // worktree command used. However, there were quite a few bugs in the early
  // implementation so let's cap it at a more recent and widely used 2.11.0.
  //
  static const semantic_version git_ver {2, 11, 0};

  // Get the project's control repository URL.
  //
  static url
  control_url (const dir_path& prj)
  {
    if (git_repository (prj))
    {
      // First try remote.origin.build2ControlUrl which can be used to specify
      // a completely different control repository URL.
      //
      return git_remote_url (prj,
                             "--control",
                             "control repository URL",
                             "remote.origin.build2ControlUrl");
    }

    fail << "unable to discover control repository URL" <<
      info << "use --control to specify explicitly" << endf;
  }

  static int
  cmd_publish (const cmd_publish_options& o,
               const dir_path& prj,
               const dir_path& cfg,
               package_locations&& pkg_locs)
  {
    using bpkg::package_manifest;

    const url& repo (o.repository ());

    // Control repository URL.
    //
    optional<url> ctrl;
    if (!o.control_specified ())
    {
      ctrl = control_url (prj);
    }
    else if (o.control () != "none")
    try
    {
      ctrl = url (o.control ());
    }
    catch (const invalid_argument& e)
    {
      fail << "invalid --control option value '" << o.control () << "': " << e;
    }

    // Publisher's name/email.
    //
    project_author author;

    if (o.author_name_specified  ()) author.name  = o.author_name  ();
    if (o.author_email_specified ()) author.email = o.author_email ();

    if (!author.name || !author.email)
    {
      project_author a (find_project_author (prj));

      if (!author.name)  author.name  = move (a.name);
      if (!author.email) author.email = move (a.email);
    }

    if (!author.name || author.name->empty ())
      fail << "unable to obtain publisher's name" <<
        info << "use --author-name to specify explicitly";

    if (!author.email || author.email->empty ())
      fail << "unable to obtain publisher's email" <<
        info << "use --author-email to specify explicitly";

    // Collect package information (version, project, section, archive
    // path/checksum, and manifest).
    //
    // @@ It would have been nice to publish them in the dependency order.
    //    Perhaps we need something like bpkg-pkg-order (also would be needed
    //    in init --clone).
    //
    struct package
    {
      package_name     name;
      standard_version version;
      package_name     project;
      string           section; // alpha|beta|stable (or --section)

      path             archive;
      string           checksum;

      package_manifest manifest;
    };
    vector<package> pkgs;

    for (package_location& pl: pkg_locs)
    {
      package_name n (move (pl.name));
      package_name p (pl.project ? move (*pl.project) : n);

      standard_version v (package_version (o, cfg, n));

      // Should we allow publishing snapshots and, if so, to which section?
      // For example, is it correct to consider a "between betas" snapshot a
      // beta version?
      //
      if (v.snapshot ())
        fail << "package " << n << " version " << v << " is a snapshot";

      // Per semver we treat zero major versions as alpha.
      //
      string s (o.section_specified () ? o.section ()   :
                v.alpha () || v.major () == 0 ? "alpha" :
                v.beta ()                     ? "beta"  : "stable");

      pkgs.push_back (package {move (n),
                               move (v),
                               move (p),
                               move (s),
                               path ()   /* archive */,
                               string () /* checksum */,
                               package_manifest ()});
    }

    // Print the plan and ask for confirmation.
    //
    if (!o.yes ())
    {
      text << "publishing:" << '\n'
           << "  to:      " << repo << '\n'
           << "  as:      " << *author.name << " <" << *author.email << '>';

      for (const package& p: pkgs)
      {
        diag_record dr (text);

        // If printing multiple packages, separate them with a blank line.
        //
        if (pkgs.size () > 1)
          dr << '\n';

        // Currently the control repository is the same for all packages, but
        // this could change in the future (e.g., multi-project publishing).
        //
        dr << "  package: " << p.name    << '\n'
           << "  version: " << p.version << '\n'
           << "  project: " << p.project << '\n'
           << "  section: " << p.section;

        if (ctrl)
          dr << '\n'
             << "  control: " << *ctrl;
      }

#ifdef BDEP_STAGE
      warn << "publishing using staged build2 toolchain";
#endif

      if (!yn_prompt ("continue? [y/n]"))
        return 1;
    }

    // Prepare package archives and calculate their checksums. Also verify
    // each archive with bpkg-pkg-verify and parse the package manifest it
    // contains.
    //
    auto_rmdir dr_rm (tmp_dir ("publish"));
    const dir_path& dr (dr_rm.path);        // dist.root
    mk (dr);

    for (package& p: pkgs)
    {
      // Similar to extracting package version, we call the build system
      // directly to prepare the distribution. If/when we have bpkg-pkg-dist,
      // we may want to switch to that.
      //
      run_b (o,
             "dist:", (dir_path (cfg) /= p.name.string ()).representation (),
             "config.dist.root=" + dr.representation (),
             "config.dist.archives=tar.gz",
             "config.dist.checksums=sha256");

      // This is the canonical package archive name that we expect dist to
      // produce.
      //
      path a (dr / p.name.string () + '-' + p.version.string () + ".tar.gz");
      path c (a + ".sha256");

      if (!exists (a))
        fail << "package distribution did not produce expected archive " << a;

      if (!exists (c))
        fail << "package distribution did not produce expected checksum " << c;

      // Verify that archive name/content all match and while at it extract
      // its manifest.
      //
      process pr;
      bool io (false);
      try
      {
        fdpipe pipe (fdopen_pipe ()); // Text mode seems appropriate.

        // Pass the --deep option to make sure that the *-file manifest values
        // are resolvable, so rep-create will not fail due to this package
        // down the road.
        //
        pr = start_bpkg (2    /* verbosity */,
                         o,
                         pipe /* stdout */,
                         2    /* stderr */,
                         "pkg-verify",
                         "--deep",
                         "--manifest",
                         a);

        pipe.out.close ();
        ifdstream is (move (pipe.in), fdstream_mode::skip);

        manifest_parser mp (is, manifest_file.string ());
        p.manifest = package_manifest (mp);
        is.close ();
      }
      // This exception is unlikely to be thrown as the package manifest is
      // already validated by bpkg-pkg-verify. However, it's still possible if
      // something is skew (e.g., different bpkg/bdep versions).
      //
      catch (const manifest_parsing& e)
      {
        finish_bpkg (o, pr);

        fail << "unable to parse package manifest in archive " << a << ": "
             << e;
      }
      catch (const io_error&)
      {
        // Presumably the child process failed and issued diagnostics so let
        // finish_bpkg() try to deal with that first.
        //
        io = true;
      }

      finish_bpkg (o, pr, io);

      // Read the checksum.
      //
      try
      {
        ifdstream is (c);
        string l;
        getline (is, l);
        is.close ();

        p.checksum = string (l, 0, 64);
      }
      catch (const io_error& e)
      {
        fail << "unable to read " << c << ": " << e;
      }

      p.archive = move (a);
    }

    // Add the package archive "authorization" files to the build2-control
    // branch.
    //
    // Their names are 16-character abbreviated checksums (a bit more than the
    // standard 12 for security) and their content is the package manifest
    // header (for the record).
    //
    // See if this is a VCS repository we recognize.
    //
    if (ctrl && git_repository (prj))
    {
      // Checkout the build2-control branch into a separate working tree not
      // to interfere with the user's stuff.
      //
      auto_rmdir wd_rm (tmp_dir ("control"));
      const dir_path& wd (wd_rm.path);
      mk (wd);

      const dir_path submit_dir ("submit");
      dir_path sd (wd / submit_dir);

      // The 'git worktree add' command is quite verbose, printing something
      // like:
      //
      // Preparing /tmp/hello/.bdep/tmp/control-14926-1 (identifier control-14926-1)
      // HEAD is now at 3fd69a3 Publish hello/0.1.0-a.1
      //
      // Note that there doesn't seem to be an option (yet) for suppressing
      // this output. Also note that the first line is printed to stderr and
      // the second one to stdout. So what we are going to do is redirect both
      // stderr and stdout to /dev/null if the verbosity level is less than
      // two and advise the user to re-run with -v on failure.
      //
      auto worktree_add = [&prj, &wd] ()
      {
        bool q (verb < 2);
        auto_fd null (q ? fdnull () : auto_fd ());

        process pr (start_git (git_ver,
                               prj,
                               0                   /* stdin  */,
                               q ? null.get () : 1 /* stdout */,
                               q ? null.get () : 2 /* stderr */,
                               "worktree",
                               "add",
                               wd,
                               "build2-control"));

        if (pr.wait ())
          return;

        if (!q)
          throw failed (); // Diagnostics already issued.

        assert (pr.exit);

        const process_exit& e (*pr.exit);

        if (e.normal ())
          fail << "unable to add worktree for build2-control branch" <<
            info << "re-run with -v for more information";
        else
          fail << "git " << e;
      };

      auto worktree_prune = [&prj] ()
      {
        run_git (git_ver, prj, "worktree", "prune", verb > 2 ? "-v" : nullptr);
      };

      // Create the build2-control branch if it doesn't exist, from scratch if
      // there is no remote-tracking branch and as a checkout with --track -b
      // otherwise. If the local branch exists, then fast-forward it using the
      // locally fetched data (no network).
      //
      // Note that we don't fetch in advance, so push conflicts are possible.
      // The idea behind this is that it will be more efficient in most cases
      // as the remote-tracking branch is likely to already be up-to-date due
      // to the implicit branch fetches peformed by other operations like
      // pull. In the rare conflict cases we will advise the user to run the
      // fetch command and re-try.
      //
      bool local_exists (git_line (git_ver,
                                   prj,
                                   false /* ignore_error */,
                                   "branch",
                                   "--list",
                                   "build2-control"));

      // @@ Should we allow using the remote name other than origin (here and
      //    everywhere) via the --remote option or smth? Maybe later.
      //
      bool remote_exists (git_line (git_ver,
                                    prj,
                                    false /* ignore_error */,
                                    "branch",
                                    "--list",
                                    "--remote",
                                    "origin/build2-control"));

      bool local_new (false); // The branch is created from scratch.

      // Note that the case where the local build2-control branch exists while
      // the remote-tracking one doesn't is treated similar (but different) to
      // the brand new branch: the upstream branch will be set up by the push
      // operation but the local branch will not be deleted on the push
      // failure.
      //
      if (!local_exists)
      {
        // Create the brand new local branch if the remote-tracking branch
        // doesn't exist.
        //
        // The tricky part is to make sure that it doesn't inherit the current
        // branch history. To accomplish this we will create an empty tree
        // object, base the root (no parents) commit on it, and create the
        // build2-control branch pointing to this commit.
        //
        if (!remote_exists)
        {
          // Create the empty tree object.
          //
          auto_fd null (fdnull ());
          fdpipe pipe (fdopen_pipe ());

          process pr (start_git (git_ver,
                                 prj,
                                 null.get () /* stdin  */,
                                 pipe        /* stdout */,
                                 2           /* stderr */,
                                 "hash-object",
                                 "-wt", "tree",
                                 "--stdin"));

          optional<string> tree (git_line (move (pr), move (pipe), false));

          if (!tree)
            fail << "unable to create git tree object for build2-control";

          // Create the (empty) root commit.
          //
          optional<string> commit (git_line (git_ver,
                                             prj,
                                             false,
                                             "commit-tree",
                                             "-m", "Start",
                                             *tree));

          if (!commit)
            fail << "unable to create root git commit for build2-control";

          // Create the branch.
          //
          // Note that we pass the empty oldvalue to make sure that the ref we
          // are creating does not exist. It should be impossible but let's
          // tighten things up a bit.
          //
          run_git (git_ver,
                   prj,
                   "update-ref",
                   "refs/heads/build2-control",
                   *commit,
                   "" /* oldvalue */);

          // Checkout the branch. Note that the upstream branch is not setup
          // for it yet. This will be done by the push operation.
          //
          worktree_add ();

          // Create the checksum files subdirectory.
          //
          mk (sd);

          local_new = true;
        }
        else
        {
          // Create the local branch, setting up the corresponding upstream
          // branch.
          //
          run_git (git_ver,
                   prj,
                   "branch",
                   verb < 2 ? "-q" : nullptr,
                   "build2-control",
                   "origin/build2-control");

          worktree_add ();
        }
      }
      else
      {
        // Checkout the existing local branch. Note that we still need to
        // fast-forward it (see below).
        //
        // Prune the build2-control worktree that could potentially stay from
        // the interrupted previous publishing attempt.
        //
        worktree_prune ();

        worktree_add ();
      }

      // "Release" the checked out branch and delete the worktree, if exists.
      //
      // Note that until this is done the branch can not be checked out in any
      // other worktree.
      //
      auto worktree_remove = [&wd, &wd_rm, &worktree_prune] ()
      {
        if (exists (wd))
        {
          // Note that we cannot (yet) use git-worktree-remove since it is not
          // available in older versions.
          //
          rm_r (wd);
          wd_rm.cancel ();

          worktree_prune ();
        }
      };

      // Now, given that we successfully checked out the build2-control
      // branch, add the authorization files for packages being published,
      // commit, and push. Skip already existing files. Don't push if no files
      // were added.
      //
      {
        // Remove the control2-branch worktree on failure. Failed that we will
        // get the 'build2-control is already checked out' error on the next
        // publish attempt.
        //
        auto wg (make_exception_guard ([&worktree_remove] ()
        {
          try
          {
            worktree_remove (); // Can throw failed.
          }
          catch (const failed&)
          {
            // We can't do much here and will let the user deal with the mess.
            // Note that running 'git worktree prune' will likely be enough.
            // Anyway, that's unlikely to happen.
          }
        }));

        // If the local branch existed from the beginning then fast-forward it
        // over the remote-tracking branch.
        //
        // Note that fast-forwarding can potentially fail. That will mean the
        // local branch has diverged from the remote one for some reason
        // (e.g., inability to revert the commit, etc.). We again leave it to
        // the use to deal with.
        //
        if (local_exists && remote_exists)
          run_git (git_ver,
                   wd,
                   "merge",
                   verb < 2 ? "-q" : verb > 2 ? "-v" : nullptr,
                   "--ff-only",
                   "origin/build2-control");

        // Create the authorization files and add them to the repository.
        //
        bool added (false);

        for (const package& p: pkgs)
        {
          // Use 16 characters of the sha256sum instead of 12 for extra
          // security.
          //
          path ac (string (p.checksum, 0, 16));
          path mf (sd / ac);

          if (exists (mf))
            continue;

          try
          {
            ofdstream os (mf);
            manifest_serializer s (os, mf.string ());
            p.manifest.serialize_header (s);
            os.close ();
          }
          catch (const manifest_serialization&)
          {
            // This shouldn't happen as we just parsed the manifest.
            //
            assert (false);
          }
          catch (const io_error& e)
          {
            fail << "unable to write " << mf << ": " << e;
          }

          run_git (git_ver,
                   wd,
                   "add",
                   verb > 2 ? "-v" : nullptr,
                   submit_dir / ac);

          added = true;
        }

        // Commit and push.
        //
        // Note that we push even if we haven't committed anything in case we
        // have added but haven't managed to push it on the previous run.
        //
        if (added)
        {
          // Format the commit message.
          //
          string m;

          auto pkg_str = [] (const package& p)
          {
            return p.name.string () + '/' + p.version.string ();
          };

          if (pkgs.size () == 1)
            m = "Add " + pkg_str (pkgs[0]) + " publish authorization";
          else
          {
            m = "Add publish authorizations\n";

            for (const package& p: pkgs)
            {
              m += '\n';
              m += pkg_str (p);
            }
          }

          run_git (git_ver,
                   wd,
                   "commit",
                   verb < 2 ? "-q" : verb > 2 ? "-v" : nullptr,
                   "-m", m);
        }

        // If we fail to push the control branch, then revert the commit and
        // advice the user to fetch the repository and re-try.
        //
        auto pg (
          make_exception_guard (
            [added, &prj, &wd, &worktree_remove, local_new] ()
            {
              if (added)
              try
              {
                // If the local build2-control branch was just created, then
                // we need to drop not just the last commit but the whole
                // branch (including it's root commit). Note that this is
                // not an optimization. Imagine that the remote branch is
                // not fetched yet and we just created the local one. If we
                // leave this branch around after the failed push, then we
                // will still be in trouble after the fetch since we won't
                // be able to merge unrelated histories.
                //
                if (local_new)
                {
                  worktree_remove (); // Release the branch before removal.

                  run_git (git_ver,
                           prj,
                           "branch",
                           verb < 2 ? "-q" : nullptr,
                           "-D",
                           "build2-control");
                }
                else
                  run_git (git_ver,
                           wd,
                           "reset",
                           verb < 2 ? "-q" : nullptr,
                           "--hard",
                           "HEAD^");

                error << "unable to push build2-control branch" <<
                  info << "run 'git fetch' and try again";
              }
              catch (const failed&)
              {
                // We can't do much here and will leave the user to deal
                // with the mess. Note that running 'git fetch' will not be
                // enough as the local and remote branches are likely to
                // have diverged.
              }
            }));

        if (verb)
          text << "pushing build2-control";

        // Note that we suppress the (too detailed) push command output if
        // the verbosity level is 1. However, we still want to see the
        // progress in this case.
        //
        run_git (git_ver,
                 wd,
                 "push",

                 verb < 2 ? "-q" : verb > 3 ? "-v" : nullptr,
                 verb == 1 ? "--progress" : nullptr,

                 !remote_exists
                 ? cstrings ({"--set-upstream", "origin", "build2-control"})
                 : cstrings ());
      }

      worktree_remove ();
    }

    // Submit each package.
    //
    for (const package& p: pkgs)
    {
      // The path points into the temporary directory so let's omit the
      // directory part.
      //
      if (verb)
        text << "submitting " << p.archive.leaf ();

      url u (o.repository ());
      u.query = "submit";

      using namespace http_service;

      parameters params ({{parameter::file, "archive",      p.archive.string ()},
                          {parameter::text, "sha256sum",    p.checksum},
                          {parameter::text, "section",      p.section},
                          {parameter::text, "author-name",  *author.name},
                          {parameter::text, "author-email", *author.email}});

      if (ctrl)
        params.push_back ({parameter::text, "control", ctrl->string ()});

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

  int
  cmd_publish (const cmd_publish_options& o, cli::scanner&)
  {
    tracer trace ("publish");

    // If we are publishing the entire project, then we have two choices: we
    // can publish all the packages in the project or we can only do so for
    // packages that were initialized in the configuration that we are going
    // to use for the preparation of distributions. Normally, the two sets
    // will be the same but if they are not, it feels more likely to be a
    // mistake than the desired behavior. So we will assume it's all the
    // packages and verify they are all initialized in the configuration.
    //
    project_packages pp (
      find_project_packages (o,
                             false /* ignore_packages */,
                             true  /* load_packages   */));

    const dir_path& prj (pp.project);
    database db (open (prj, trace));

    // We need a single configuration to prepare package distribution.
    //
    shared_ptr<configuration> cfg;
    {
      transaction t (db.begin ());
      configurations cfgs (find_configurations (o, prj, t));
      t.commit ();

      if (cfgs.size () > 1)
        fail << "multiple configurations specified for publish";

      // Verify packages are present in the configuration.
      //
      verify_project_packages (pp, cfgs);

      cfg = move (cfgs[0]);
    }

    // Pre-sync the configuration to avoid triggering the build system hook
    // (see sync for details).
    //
    cmd_sync (o, prj, cfg, strings () /* pkg_args */, true /* implicit */);

    return cmd_publish (o, prj, cfg->path, move (pp.packages));
  }
}
