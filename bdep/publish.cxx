// file      : bdep/publish.cxx -*- C++ -*-
// license   : MIT; see accompanying LICENSE file

#include <bdep/publish.hxx>

#include <libbutl/manifest-parser.hxx>
#include <libbutl/manifest-serializer.hxx>

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

  // Distribute packages in the specified (per-package) directories.
  //
  static int
  cmd_publish (const cmd_publish_options& o,
               const dir_path& prj,
               package_locations&& pkg_locs,
               dir_paths&& dist_dirs)
  {
    using bpkg::package_manifest;

    assert (pkg_locs.size () == dist_dirs.size ()); // Parallel vectors.

    const url& repo (o.repository ());

    // Control repository URL.
    //
    optional<string> ctrl;
    if (!o.control_specified ())
    {
      ctrl = control_url (prj).string ();
    }
    else if (o.control () != "none")
    try
    {
      ctrl = url (o.control ()).string ();
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

    // Make sure that parameters we post to the submission service are UTF-8
    // encoded and contain only the graphic Unicode codepoints.
    //
    validate_utf8_graphic (*author.name,  "author name",  "--author-name");
    validate_utf8_graphic (*author.email, "author email", "--author-email");

    if (o.section_specified ())
      validate_utf8_graphic (o.section (), "--section option value");

    if (ctrl)
      validate_utf8_graphic (*ctrl, "control URL", "--control");

    if (o.simulate_specified ())
      validate_utf8_graphic (o.simulate (), "--simulate option value");

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
      dir_path         dist_dir;
      string           version;
      package_name     project;
      string           section; // alpha|beta|stable (or --section)

      bdep::path       archive;
      string           checksum;

      package_manifest manifest;
    };
    vector<package> pkgs;

    // If the project is under recognized VCS, it feels right to fail if it is
    // uncommitted to decrease chances of publishing package modifications
    // under the same version. It still make sense to allow submitting
    // uncommitted packages if requested explicitly, for whatever reason.
    //
    bool git_repo (git_repository (prj));
    optional<bool> uncommitted;           // Absent if unrecognized VCS.

    if (git_repo)
    {
      git_repository_status st (git_status (prj));
      uncommitted = st.unstaged || st.staged;

      if (*uncommitted &&
          o.force ().find ("uncommitted") == o.force ().end ())
        fail << "project directory has uncommitted changes" <<
          info << "run 'git status' for details" <<
          info << "use 'git stash' to temporarily hide the changes" <<
          info << "use --force=uncommitted to publish anyway";
    }

    for (size_t i (0); i != pkg_locs.size (); ++i)
    {
      package_location& pl (pkg_locs[i]);

      package_name     n (move (pl.name));
      package_name     p (pl.project ? move (*pl.project) : n);
      dir_path         d (move (dist_dirs[i]));

      // If the package has the standard version, then use it to deduce the
      // section, unless it is specified explicitly. Otherwise, there is no
      // way to deduce it and thus it needs to be specified explicitly.
      //
      string s; // Section.
      package_info pi (package_b_info (o, d, b_info_flags::none));

      if (!pi.version.empty ()) // Does the package use the standard version?
      {
        const standard_version& v (pi.version);

        // Should we allow publishing snapshots and, if so, to which section?
        // For example, is it correct to consider a "between betas" snapshot a
        // beta version?
        //
        // Seems nothing wrong with submitting snapshots generally. We do this
        // for staging most of the time. The below logic of choosing a default
        // section requires no changes. Also, the submission service can
        // probably have some policy of version acceptance, rejecting some
        // version types for some sections. For example, rejecting pre-release
        // for stable section and snapshots for any section.
        //
        // Note, however, that if the project is under an unrecognized VCS or
        // the snapshot is uncommitted, then we make it non-forcible. Failed
        // that, we might end up publishing distinct snapshots under the same
        // temporary version. (To be more precise, we should have checked for
        // the latest snapshot but that would require getting the original
        // version from the package manifest, which feels too hairy for now).
        //
        bool non_forcible;
        if (v.snapshot () &&
            ((non_forcible = (!uncommitted || *uncommitted)) ||
             o.force ().find ("snapshot") == o.force ().end ()))
        {
          diag_record dr (fail);
          dr << "package " << n << " version " << v << " is a snapshot";

          if (!non_forcible)
            dr << info << "use --force=snapshot to publish anyway";
        }

        // Per semver we treat zero major versions as alpha.
        //
        s = o.section_specified ()        ? o.section ()   :
            v.alpha () || v.major () == 0 ? "alpha"        :
            v.beta ()                     ? "beta"         :
                                            "stable"       ;
      }
      else
      {
        // Verify the package version.
        //
        try
        {
          bpkg::version (pi.version_string);
        }
        catch (const invalid_argument& e)
        {
          fail << "invalid package " << n << " version '" << pi.version_string
               << "': " << e;
        }

        if (!o.section_specified ())
          fail << "unable to deduce section for package " << n << " version "
               << pi.version_string <<
            info << "use --section to specify it explicitly";

        s = o.section ();
      }

      pkgs.push_back (package {move (n),
                               move (d),
                               move (pi.version_string),
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
      // We need to specify config.dist.uncommitted=true for a snapshot since
      // build2's version module by default does not allow distribution of
      // uncommitted projects.
      //
      run_b (
        o,
        "dist:",
        '\'' + p.dist_dir.representation () + '\'',
        "config.dist.root='" + dr.representation () + '\'',
        "config.dist.archives=tar.gz",
        "config.dist.checksums=sha256",
        (uncommitted && *uncommitted
         ? "config.dist.uncommitted=true"
         : nullptr));

      // This is the canonical package archive name that we expect dist to
      // produce.
      //
      path a (dr / p.name.string () + '-' + p.version + ".tar.gz");
      path c (a + ".sha256");

      if (!exists (a))
        fail << "package distribution did not produce expected archive " << a;

      if (!exists (c))
        fail << "package distribution did not produce expected checksum " << c;

      // Verify that archive name/content all match and while at it extract
      // its manifest.
      //
      fdpipe pipe (open_pipe ()); // Text mode seems appropriate.

      // Pass the --deep option to make sure that the *-file manifest values
      // are resolvable, so rep-create will not fail due to this package
      // down the road.
      //
      process pr (start_bpkg (2    /* verbosity */,
                              o,
                              pipe /* stdout */,
                              2    /* stderr */,
                              "pkg-verify",
                              "--deep",
                              "--manifest",
                              a));

      // Shouldn't throw, unless something is severely damaged.
      //
      pipe.out.close ();

      bool io (false);
      try
      {
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
    if (ctrl && git_repo)
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
        auto_fd null (q ? open_null () : auto_fd ());

        process pr (start_git (git_ver,
                               true                /* system */,
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
        run_git (git_ver,
                 true /* system */,
                 prj,
                 "worktree",
                 "prune",
                 verb > 2 ? "-v" :
                 nullptr);
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
                                   false /* system */,
                                   prj,
                                   false /* ignore_error */,
                                   "branch",
                                   "--list",
                                   "build2-control"));

      // @@ Should we allow using the remote name other than origin (here and
      //    everywhere) via the --remote option or smth? Maybe later.
      //
      bool remote_exists (git_line (git_ver,
                                    false /* system */,
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
          auto_fd null (open_null ());
          fdpipe pipe (open_pipe ());

          process pr (start_git (git_ver,
                                 true  /* system */,
                                 prj,
                                 null  /* stdin  */,
                                 pipe  /* stdout */,
                                 2     /* stderr */,
                                 "hash-object",
                                 "-wt", "tree",
                                 "--stdin"));

          optional<string> tree (git_line (move (pr), move (pipe), false));

          if (!tree)
            fail << "unable to create git tree object for build2-control";

          // Create the (empty) root commit.
          //
          optional<string> commit (git_line (git_ver,
                                             true /* system */,
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
                   true /* system */,
                   prj,
                   "update-ref",
                   "refs/heads/build2-control",
                   *commit,
                   ""   /* oldvalue */);

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
                   true /* system */,
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
        // the user to deal with.
        //
        if (local_exists && remote_exists)
          run_git (git_ver,
                   true /* system */,
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
            fail << "unable to write to " << mf << ": " << e;
          }

          run_git (git_ver,
                   true /* system */,
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
            return p.name.string () + '/' + p.version;
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
                   true /* system */,
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
                           true /* system */,
                           prj,
                           "branch",
                           verb < 2 ? "-q" : nullptr,
                           "-D",
                           "build2-control");
                }
                else
                  run_git (git_ver,
                           true /* system */,
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

        if ((verb && !o.no_progress ()) || o.progress ())
          text << "pushing branch build2-control";

        git_push (o,
                  wd,
                  (!remote_exists
                   ? cstrings ({"--set-upstream", "origin", "build2-control"})
                   : cstrings ()));
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
      if ((verb && !o.no_progress ()) || o.progress ())
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
        params.push_back ({parameter::text, "control", *ctrl});

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

    if (o.forward ())
    {
      if (const char* n = (o.config_name_specified () ? "@<cfg-name>" :
                           o.config_id_specified ()   ? "--config-id" :
                           o.config_specified ()      ? "--config|-c" :
                           o.all ()                   ? "--all|-a"    : nullptr))
        fail << n << " specified together with --forward";
    }

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
    package_locations& pkgs (pp.packages);

    // If we are submitting multiple packages, verify they are from the same
    // project (or different projects are specified explicitly). While
    // bdep-new automatically adds the project manifest value to each package
    // it creates, people often cobble stuff up using copy and paste and
    // naturally don't bother marking all the packages as belonging to the
    // same project.
    //
    // If we have the project specified explicitly, we assume the user knows
    // what they are doing (this is a way to suppress this check).
    //
    if (pkgs.size () > 1)
    {
      // First see how many packages don't have explicit project. Also, while
      // at it, get the name of the first such packages.
      //
      size_t n (0);
      const package_name* pn (nullptr);

      for (const package_location& pl: pkgs)
      {
        if (!pl.project)
        {
          if (n++ == 0)
            pn = &pl.name;
        }
      }

      diag_record dr;
      if (n == 0)
        ;
      else if (n == 1)
      {
        // We have one package without explicit project.
        //
        // Verify all other packages use its name as a project.
        //
        for (const package_location& pl: pkgs)
        {
          if (pl.project && *pl.project != *pn)
          {
            if (dr.empty ())
              dr << fail << "packages belong to different projects" <<
                info << "package " << *pn << " implicitly belongs to project "
                 << *pn;

            dr << info << "package " << pl.name << " belongs to project "
               << *pl.project;
          }
        }

        if (!dr.empty ())
          dr << info << "consider explicitly specifying project for package "
             << *pn << " in its manifest or"
             << info << "consider changing project in other packages to "
             << *pn;
      }
      else
      {
        // We have multiple packages without explicit project.
        //
        // The typical setup is the main package and the project with the same
        // name (see bdep-new). Let's see if that's what we got so that we can
        // suggest the project name. We only do that if there are no packages
        // with explicit project.
        //
        pn = nullptr;
        if (n == pkgs.size ())
        {
          string p (prj.leaf ().string ());

          for (const package_location& pl: pkgs)
          {
            if (pl.name == p && (!pl.project || *pl.project == pl.name))
            {
              pn = &pl.name;
              break;
            }
          }
        }

        for (const package_location& pl: pkgs)
        {
          if (!pl.project && (pn == nullptr || pl.name != *pn))
          {
            if (dr.empty ())
            {
              dr << fail << "packages belong to different projects";

              if (pn != nullptr)
                dr << info << "package " << *pn << " belongs to project "
                   << *pn;
            }

            dr << info << "package " << pl.name << " implicitly belongs to "
               << "project " << pl.name;
          }
        }

        if (!dr.empty ())
        {
          dr << info << "consider explicitly specifying project ";
          if (pn != nullptr) dr << *pn << " ";
          dr << "for these packages in their manifests";
        }
      }

      if (!dr.empty ())
        dr << info << "consider resolving this issue by releasing a revision";
    }

    // Collect directories to distribute the packages in. In the forward mode
    // the packages are distributed in the package's (forwarded) source
    // directories and in their configuration directories otherwise.
    //
    dir_paths dist_dirs;

    if (o.forward ())
    {
      // Note: in this case we don't even open the database.
      //
      dir_paths cfgs; // Configuration directories to sync.

      for (const package_location& pl: pkgs)
      {
        dir_path d (prj / pl.path);

        package_info pi (package_b_info (o, d, b_info_flags::none));

        if (pi.src_root == pi.out_root)
          fail << "package " << pl.name << " source directory is not forwarded" <<
            info << "package source directory is " << d;

        dist_dirs.push_back (move (d));

        // If the forwarded configuration is amalgamated and this amalgamation
        // is a bpkg configuration where some packages are bdep-initialized,
        // then collect it for pre-sync.
        //
        if (!pi.amalgamation.empty ())
        {
          // Get the configuration root.
          //
          (pi.out_root /= pi.amalgamation).normalize ();

          if (exists (dir_path (pi.out_root) /= bpkg_dir) &&
              !configuration_projects (o,
                                       pi.out_root,
                                       dir_path () /* prj */).empty ())
          {
            if (find (cfgs.begin (), cfgs.end (), pi.out_root) == cfgs.end ())
              cfgs.push_back (move (pi.out_root));
          }
        }
      }

      // Pre-sync the configurations to avoid triggering the build system hook
      // (see sync for details).
      //
      cmd_sync_implicit (o, cfgs);
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

      // Configurations to sync.
      //
      // We could probably unify syncing configuration directories with the
      // forward mode, however configuration name-based progress indication
      // feels preferable for the common case.
      //
      configurations scs;

      // Besides collecting the package directories and configurations to
      // sync, also verify that for each package being published only one
      // configuration, it is initialized in, is specified.
      //
      for (const package_location& p: pkgs)
      {
        shared_ptr<configuration> pc;

        for (const shared_ptr<configuration>& c: cfgs.first)
        {
          if (find_if (c->packages.begin (),
                       c->packages.end (),
                       [&p] (const package_state& s)
                       {
                         return p.name == s.name;
                       }) != c->packages.end ())
          {
            if (pc != nullptr)
              fail << "package " << p.name << " is initialized in multiple "
                   << "specified configurations" <<
                info << *pc <<
                info << *c;

            pc = c;
          }
        }

        assert (pc != nullptr); // Wouldn't be here otherwise.

        dist_dirs.push_back (dir_path (pc->path) /= p.name.string ());

        if (find (scs.begin (), scs.end (), pc) == scs.end ())
          scs.push_back (move (pc));
      }

      cmd_sync (o, prj, scs, true /* implicit */);
    }

    return cmd_publish (o, prj, move (pkgs), move (dist_dirs));
  }
}
