// file      : bdep/release.cxx -*- C++ -*-
// copyright : Copyright (c) 2014-2019 Code Synthesis Ltd
// license   : MIT; see accompanying LICENSE file

#include <bdep/release.hxx>

#include <libbutl/manifest-types.mxx>    // manifest_name_value
#include <libbutl/manifest-rewriter.mxx>

#include <libbpkg/manifest.hxx>

#include <bdep/git.hxx>
#include <bdep/project.hxx>
#include <bdep/diagnostics.hxx>

using namespace std;
using namespace butl;

namespace bdep
{
  // Note: the git_status() function, that we use, requires git 2.11.0 or
  // higher.
  //
  static const semantic_version git_ver {2, 11, 0};

  // Project/package information relevant to what we are doing here.
  //
  // Absent optional values mean the corresponding step does not make sense in
  // this mode or it was suppressed with one of the --no-* options.
  //
  // Note that while the release versions can vary in revisions, the open
  // version is always the same.
  //
  namespace
  {
    struct package
    {
      package_name name;
      path         manifest; // Absolute path.

      // File position of the version value for manifest rewriting.
      //
      manifest_name_value version_pos;

      standard_version           current_version;
      optional<standard_version> release_version;
    };

    struct project
    {
      dir_path        path;
      vector<package> packages;

      optional<standard_version> open_version;

      optional<string> tag;
      bool             replace_tag = false;
    };

    using status = git_repository_status;
  }

  // The plan_*() functions calculate and set all the new values in the passed
  // project but don't apply any changes.
  //
  static void
  plan_tag (const cmd_release_options& o, project& prj, const status& st)
  {
    // Note: not forcible. While there is nothing wrong with having
    // uncommitted changes while tagging, in our case we may end up with a
    // wrong tag if the version in the (modified) manifest does not correspond
    // to the latest commit.
    //
    // Note that if we are called as part of releasing a new revision, then
    // the project may have some changes staged (see plan_revision() for
    // details).
    //
    if ((st.staged && !o.revision ()) || st.unstaged)
      fail << "project directory has uncommitted changes" <<
        info << "run 'git status' for details" <<
        info << "use 'git stash' to temporarily hide the changes";

    // All the versions are the same sans the revision. Note that our version
    // can be either current (--tag mode) or release (part of the release).
    //
    const package& pkg (prj.packages.front ()); // Exemplar package.
    const standard_version& cv (pkg.release_version
                                ? *pkg.release_version
                                :  pkg.current_version);

    // It could be desirable to tag a snapshot for some kind of a pseudo-
    // release. For example, tag a snapshot for QA or some such. Note: can't
    // happen as part of a release or revision.
    //
    if (cv.snapshot () && o.force ().find ("snapshot") == o.force ().end ())
      fail << "current version " << cv << " is a snapshot"<<
        info << "use --force=snapshot to tag anyway";

    // Canonical version tag without epoch or revision.
    //
    // For tagging a snapshot we need to use the actual package version
    // (replacing .z with the actual snapshot information). Note: for
    // snapshots we are always tagging the current version (see above).
    //
    const standard_version& v (
      cv.latest_snapshot ()
      ? package_version (o, pkg.manifest.directory ())
      : cv);

    prj.tag = "v" + v.string_project ();

    // Replace the existing tag only if this is a revision.
    //
    prj.replace_tag = (cv.revision != 0);
  }

  static void
  plan_open (const cmd_release_options& o, project& prj, const status& st)
  {
    // There could be changes already added to the index but otherwise the
    // repository should be clean.
    //
    // Note: not forcible. Generally, we don't touch unstanged changes (which
    // include untracked files).
    //
    if (st.unstaged && !o.no_commit ())
      fail << "project directory has unstaged changes" <<
        info << "run 'git status' for details" <<
        info << "use 'git add' to add the changes to this commit" <<
        info << "use 'git stash' to temporarily hide the changes";

    // All the versions are the same sans the revision. Note that our version
    // can be either current (--open mode) or release (part of the release).
    //
    const package& pkg (prj.packages.front ()); // Exemplar package.
    const standard_version& cv (pkg.release_version
                                ? *pkg.release_version
                                :  pkg.current_version);

    // Change the version to the next snapshot. Similar to the release part,
    // here we have sensible defaults as well as user input:
    //
    // 1.2.0     -> 1.3.0-a.0.z
    //           -> 1.2.1-a.0.z  --open-patch
    //           -> 2.0.0-a.0.z  --open-major
    //
    // 1.2.3     -> 1.2.4-a.0.z  (assuming bugfix branch)
    //           -> 1.3.0-a.0.z  --open-minor
    //           -> 2.0.0-a.0.z  --open-major
    //
    // 1.2.0-a.1 -> 1.2.0-a.1.z
    //           -> 1.2.0-b.0.z  --open-beta
    //
    // 1.2.0-b.1 -> 1.2.0-b.1.z
    //
    // Note that there is no --open-alpha since that's either the default or
    // would be invalid (jumping backwards).
    //
    auto& ov (prj.open_version);

    auto make_snapshot = [&cv] (uint16_t major,
                                uint16_t minor,
                                uint16_t patch,
                                uint16_t pre_release = 0 /* a.0 */)
    {
      return standard_version (cv.epoch,
                               major,
                               minor,
                               patch,
                               pre_release,
                               standard_version::latest_sn,
                               "" /* snapshot_id */);
    };

    // Note: not forcible. The following (till the end of the function) checks
    // prevent skipping a release. Hard to think of a use case but we probably
    // could allow this as it doesn't break anything (released versions need
    // not be contiguous). Let's, however, postpone this until a real use
    // case comes up in order not to complicate things needlessly.
    //
    if (cv.snapshot ())
    {
      // The cv variable refers to the current version since the release
      // version cannot be a snapshot.
      //
      assert (!pkg.release_version);

      fail << "current version " << cv << " is a snapshot";
    }
    else if (cv.alpha ())
    {
      if (const char* n = (o.open_patch () ? "--open-patch" :
                           o.open_minor () ? "--open-minor" :
                           o.open_major () ? "--open-major" : nullptr))
        fail << n << " specified for alpha current version " << cv;

      ov = make_snapshot (cv.major (),
                          cv.minor (),
                          cv.patch (),
                          o.open_beta () ? 500 : *cv.pre_release ());
    }
    else if (cv.beta ())
    {
      if (const char* n = (o.open_patch () ? "--open-patch" :
                           o.open_minor () ? "--open-minor" :
                           o.open_major () ? "--open-major" : nullptr))
        fail << n << " specified for beta current version " << cv;

      ov = make_snapshot (cv.major (),
                          cv.minor (),
                          cv.patch (),
                          *cv.pre_release ());
    }
    else
    {
      if (const char* n = (o.open_beta () ? "--open-beta" : nullptr))
        fail << n << " specified for final current version " << cv;

      uint16_t mj (cv.major ());
      uint16_t mi (cv.minor ());
      uint16_t pa (cv.patch ());

      if      (o.open_major ()) {mj++; mi =  pa = 0;}
      else if (o.open_minor ()) {      mi++; pa = 0;}
      else if (o.open_patch ()) {            pa++;  }
      else if (pa == 0)         {      mi++;        } // Default open minor.
      else                      {            pa++;  } // Default open patch.

      ov = make_snapshot (mj, mi, pa);
    }
  }

  static void
  plan_version (const cmd_release_options& o, project& prj, const status& st)
  {
    // There could be changes already added to the index but otherwise the
    // repository should be clean.
    //
    // Note: not forcible. Generally, we don't touch unstanged changes (which
    // include untracked files).
    //
    if (st.unstaged && !o.no_commit ())
      fail << "project directory has unstaged changes" <<
        info << "run 'git status' for details" <<
        info << "use 'git add' to add the changes to this commit" <<
        info << "use 'git stash' to temporarily hide the changes";

    // All the current versions are the same sans the revision.
    //
    const standard_version& cv (prj.packages.front ().current_version);

    // Change the release version to the next (pre-)release.
    //
    // Note: not forcible. The following (till the end of the function) checks
    // prevent skipping a release. The same reasoning as in plan_open().
    //
    standard_version rv;
    if (cv.snapshot ())
    {
      // To recap, a snapshot is expressed as a version *after* the previous
      // pre-release or after an imaginary a.0, if it was the final release.
      // Which means we cannot know for certain what the next (pre-)release
      // version should be. However, the final release seems like a reasonable
      // default. Consider:
      //
      //   1.2.3-a.0.z -> 1.2.3  (without     pre-release)
      //   1.2.3-a.2.z -> 1.2.3  (a.2 is last pre-release)
      //   1.2.3-b.1.z -> 1.2.3  (b.1 is last pre-release)
      //
      // And the alternatives would be to release an alpha, a beta, or to jump
      // to the next minor (if we opened with a patch increment) or major (if
      // we opened with a minor or patch increment) version.
      //
      // Note that there is no --patch since we cannot jump to patch.
      //
      uint16_t mj (cv.major ());
      uint16_t mi (cv.minor ());
      uint16_t pa (cv.patch ());
      uint16_t pr (*cv.pre_release ());

      if      (o.major ()) {mj++; mi =  pa = pr = 0;}
      else if (o.minor ()) {      mi++; pa = pr = 0;}
      else if (o.beta ())
      {
        pr = (cv.beta () ? pr : 500) + 1; // Next/first beta.
      }
      else if (o.alpha ())
      {
        if (cv.beta ())
          fail << "--alpha specified for beta current version " << cv;

        pr++; // Next alpha.
      }
      else
        pr = 0; // Final.

      rv = standard_version (cv.epoch, mj, mi, pa, pr);
    }
    else if (cv.pre_release ())
    {
      // Releasing from alpha/beta. For example, alpha/beta becomes the final
      // release without going through a snapshot.
      //
      if (const char* n = (o.minor () ? "--minor" :
                           o.major () ? "--major" : nullptr))
        fail << n << " specified for " << (cv.beta () ? "beta" : "alpha")
             << " current version " << cv;

      uint16_t pr (*cv.pre_release ());

      if (o.beta ())
      {
        pr = (cv.beta () ? pr : 500) + 1; // Next/first beta.
      }
      else if (o.alpha ())
      {
        if (cv.beta ())
          fail << "--alpha specified for beta current version " << cv;

        pr++; // Next alpha.
      }
      else
        pr = 0; // Final.

      rv = standard_version (cv.epoch,
                             cv.major (),
                             cv.minor (),
                             cv.patch (),
                             pr);
    }
    else
      fail << "current version " << cv << " is not a snapshot or pre-release";

    // Update the version in each package.
    //
    for (package& p: prj.packages)
      p.release_version = rv;

    // If we are not committing, then the rest doesn't apply.
    //
    if (o.no_commit ())
      return;

    if (!o.no_tag ())
      plan_tag (o, prj, st);

    if (!o.no_open ())
      plan_open (o, prj, st);
  }

  static void
  plan_revision (const cmd_release_options& o, project& prj, const status& st)
  {
    // There must be changes already added to the index but otherwise the
    // repository should be clean.
    //
    // Note: not forcible. Generally, we don't touch unstanged changes (which
    // include untracked files).
    //
    if (st.unstaged && !o.no_commit ())
      fail << "project directory has unstaged changes" <<
        info << "run 'git status' for details" <<
        info << "use 'git add' to add the changes to this commit" <<
        info << "use 'git stash' to temporarily hide the changes";

    // Note: the use-case for forcing would be to create a commit to be
    // squashed later.
    //
    // Also, don't complain if we are not committing.
    //
    if (!st.staged      &&
        !o.no_commit () &&
        o.force ().find ("unchanged") == o.force ().end ())
      fail << "project directory has no staged changes" <<
        info << "revision increment must be committed together with "
           << "associated changes" <<
        info << "use --force=unchanged to release anyway";

    // All the current versions are the same sans the revision.
    //
    const standard_version& cv (prj.packages.front ().current_version);

    // Note: not forcible. Doesn't seem to make sense to release a revision
    // for snapshot (just release another snapshot).
    //
    if (cv.snapshot ())
      fail << "current version " << cv << " is a snapshot";

    // Increment the revision in each package manifest.
    //
    for (package& p: prj.packages)
    {
      p.release_version = p.current_version;
      p.release_version->revision++;
    }

    // If we are not committing, then the rest doesn't apply.
    //
    if (o.no_commit ())
      return;

    if (!o.no_tag ())
      plan_tag (o, prj, st);
  }

  int
  cmd_release (const cmd_release_options& o, cli::scanner&)
  {
    // Detect options incompatibility going through the groups of mutually
    // exclusive options. Also make sure that options make sense for the
    // current mode (releasing, revising, etc.) by pre-setting an incompatible
    // mode option (mopt) as the first group members.
    //
    {
      // Points to the group option, if any is specified and NULL otherwise.
      //
      const char* gopt (nullptr);

      // If an option is specified on the command line, then check that no
      // group option have been specified yet and set it, if that's the case,
      // or fail otherwise.
      //
      auto verify = [&gopt] (const char* opt, bool specified)
      {
        if (specified)
        {
          if (gopt == nullptr)
            gopt = opt;
          else
            fail << "both " << gopt << " and " << opt << " specified";
        }
      };

      // Check the mode options.
      //
      verify ("--revision", o.revision ());
      verify ("--open",     o.open ());
      verify ("--tag",      o.tag ());

      // The current mode option (--revision, --open, --tag, or NULL for the
      // releasing mode).
      //
      // Prior to verifying an option group we will be setting gopt to the
      // current mode option if the group options are meaningless for this
      // mode or to NULL otherwise.
      //
      const char* mopt (gopt);

      // The following (mutually exclusive) options are only meaningful for
      // the releasing mode.
      //
      gopt = mopt;
      verify ("--alpha", o.alpha ());
      verify ("--beta",  o.beta ());
      verify ("--minor", o.minor ());
      verify ("--major", o.major ());

      // The following option is only meaningful for the releasing mode.
      //
      gopt = mopt;
      verify ("--no-open", o.no_open ());

      // The following option is only meaningful for the releasing and
      // revising modes.
      //
      gopt = o.revision () ? nullptr : mopt;
      verify ("--no-tag",  o.no_tag ());

      // The following option is only meaningful for the releasing, revising,
      // and opening modes.
      //
      gopt = o.revision () || o.open () ? nullptr : mopt;
      verify ("--no-commit",  o.no_commit ());

      // The following (mutually exclusive) options are only meaningful for
      // the releasing and opening modes.
      //
      gopt = o.open () ? nullptr : mopt;
      verify ("--open-beta",  o.open_beta ());
      verify ("--open-patch", o.open_patch ());
      verify ("--open-minor", o.open_minor ());
      verify ("--open-major", o.open_major ());
      verify ("--no-open",    o.no_open ());    // Releasing only (see above).

      // There is no sense to push without committing the version change first.
      //
      gopt = nullptr;
      verify ("--push",      o.push ());       // Meaningful for all modes.
      verify ("--no-commit", o.no_commit ());  // See above for modes.
    }

    // Fully parse package manifest verifying it is valid and returning the
    // position of the version value. In a sense we are publishing (by
    // tagging) to a version control-based repository and it makes sense to
    // ensure the repository will not be broken, similar to how we do it in
    // publish.
    //
    auto parse_manifest = [] (const path& f)
    {
      manifest_name_value r;

      auto m (bdep::parse_manifest<bpkg::package_manifest> (
        f,
        "package",
        false /* ignore_unknown */,
        [&r] (manifest_name_value& nv)
        {
          if (nv.name == "version")
            r = nv;

          return true;
        }));

      //
      // Validate the *-file manifest values expansion.
      //
      // Note: not forcible. Allowing releasing broken packages we are just
      // asking for trouble and long mailing list threads.
      //
      m.load_files ([&f] (const string& n, const path& p)
      {
        path vf (f.directory () / p);

        try
        {
          ifdstream is (vf);
          string s (is.read_text ());

          if (s.empty ())
            fail << n << " manifest value in " << f << " references empty "
                 << "file " << vf;

          return s;
        }
        catch (const io_error& e)
        {
          fail << "unable to read " << vf << " referenced by " << n
               << " manifest value in " << f << ": " << e << endf;
        }
      });

      assert (!r.empty ());
      r.value = m.version.string (); // For good measure.
      return r;
    };

    // Collect project/package information.
    //
    project prj;
    {
      // We publish all the packages in the project. We could have required a
      // configuration and verified that they are all initialized, similar to
      // publish. But seeing that we don't need the configuration (unlike
      // publish), this feels like an unnecessary complication. We also don't
      // pre-sync for the same reasons.
      //
      // Seeing that we are tagging the entire repository, force the
      // collection of all the packages even if the current working directory
      // is a package. But allow explicit package directory specification as
      // the "I know what I am doing" mode (e.g., to sidestep the same version
      // restriction).
      //
      package_locations pls;

      if (o.directory_specified ())
      {
        project_packages pp (
          find_project_packages (o.directory (),
                                 false /* ignore_packages */,
                                 true  /* load_packages   */));
        prj.path = move (pp.project);
        pls = move (pp.packages);
      }
      else
      {
        prj.path = find_project (o.directory ());
        pls = load_packages (prj.path);
      }

      for (package_location& pl: pls)
      {
        // Parse each manifest extracting name, version, position, etc.
        //
        path f (prj.path / pl.path / manifest_file);
        manifest_name_value vv (parse_manifest (f));

        package_name n (move (pl.name));
        standard_version v;
        try
        {
          // Allow stubs only in the --revision mode.
          //
          standard_version::flags f (standard_version::none);

          if (o.revision ())
            f |= standard_version::allow_stub;

          v = standard_version (vv.value, f);
        }
        catch (const invalid_argument&)
        {
          fail << "current package " << n << " version " << vv.value
               << " is not standard";
        }

        prj.packages.push_back (
          package {move (n),
                   move (f),
                   move (vv),
                   move (v),
                   nullopt /* release_version */});
      }
    }

    // Verify all the packages have the same version. This is the only
    // arrangement we currently (and probably ever) support. The immediate
    // problem with supporting different versions (besides the extra
    // complexity, of course) is tagging. But since our tags don't include
    // revisions, we do allow variations in that.
    //
    // While at it, notice if we will end up with different revisions which
    // we use below when forming the commit message.
    //
    bool multi_rev (false);
    {
      const package& f (prj.packages.front ());

      for (const package& p: prj.packages)
      {
        const auto& fv (f.current_version);
        const auto& pv (p.current_version);

        if (fv.compare (pv, true /* ignore_revision */) != 0)
        {
          fail << "different current package versions" <<
            info << "package " << f.name << " version " << fv <<
            info << "package " << p.name << " version " << pv;
        }

        multi_rev = multi_rev || fv.revision != pv.revision;
      }

      multi_rev = multi_rev && o.revision ();
    }

    // Plan the changes.
    //
    status st (git_status (prj.path));

    const char* mode;
    if      (o.revision ()) {plan_revision (o, prj, st); mode = "revising";}
    else if (o.open     ()) {plan_open     (o, prj, st); mode = "opening";}
    else if (o.tag      ()) {plan_tag      (o, prj, st); mode = "tagging";}
    else                    {plan_version  (o, prj, st); mode = "releasing";}

    const package& pkg (prj.packages.front ()); // Exemplar package.

    bool commit (!o.no_commit () && (pkg.release_version || prj.open_version));
    bool push (o.push ());

    if (push && st.upstream.empty ())
      fail << "no upstream branch set for local branch '" << st.branch << "'";

    // Print the plan and ask for confirmation.
    //
    if (!o.yes ())
    {
      diag_record dr (text);

      dr << mode << ":" << '\n';

      for (const package& p: prj.packages)
      {
        dr << "  package: " << p.name    << '\n'
           << "  current: " << p.current_version << '\n';

        if (p.release_version)
          dr << "  release: " << *p.release_version << '\n';

        if (prj.open_version)
          dr << "  open:    " << *prj.open_version << '\n';

        // If printing multiple packages, separate them with a blank line.
        //
        if (prj.packages.size () > 1)
          dr << '\n';
      }

      if (!o.tag ())
        dr << "  commit:  " << (commit ? "yes" : "no") << '\n';

      if (!o.open ()) // Does not make sense in the open mode.
        dr << "  tag:     " << (prj.tag ? prj.tag->c_str () : "no") << '\n';

      dr << "  push:    " << (push ? st.upstream.c_str () : "no");

      dr.flush ();

      if (!yn_prompt ("continue? [y/n]"))
        return 1;
    }

    // Stage and commit the project changes.
    //
    auto commit_project = [&prj] (const string& msg)
    {
      // We shouldn't have any untracked files or unstaged changes other than
      // our modifications, so -a is good enough.
      //
      run_git (git_ver,
               prj.path,
               "commit",
               verb < 1 ? "-q" : verb >= 2 ? "-v" : nullptr,
               "-a",
               "-m", msg);
    };

    // Release.
    //
    if (pkg.release_version)
    {
      // Rewrite each package manifest.
      //
      for (package& p: prj.packages)
      {
        try
        {
          manifest_name_value& vv (p.version_pos);

          // Rewrite the version.
          //
          {
            manifest_rewriter rw (p.manifest);
            vv.value = p.release_version->string ();
            rw.replace (vv);
          }

          // If we also need to open the next development cycle, update the
          // version position for the subsequent manifest rewrite.
          //
          if (prj.open_version)
            vv = parse_manifest (p.manifest);
        }
        // The IO failure is unlikely to happen (as we have already read the
        // manifests) but still possible (write permission is denied, device
        // is full, etc.). In this case we may potentially leave the project
        // in an inconsistent state as some of the package manifests could
        // have already been rewritten. As a result, the subsequent
        // bdep-release may fail due to unstaged changes.
        //
        catch (const io_error& e)
        {
          fail << "unable to read/write " << p.manifest << ": " << e <<
            info << "use 'git checkout' to revert any changes and try again";
        }
      }

      // If not committing, then we are done.
      //
      // In this case it would have been nice to pre-populate the commit
      // message but there doesn't seem to be a way to do that. In particular,
      // writing to .git/COMMIT_EDITMSG does not work (existing content is
      // discarded).
      //
      if (!commit)
        return 0;

      // Commit the manifest rewrites.
      //
      // If we are releasing multiple revisions, then list every package in
      // the form:
      //
      // Release versions hello/0.1.0+1, libhello/0.1.0+2
      //
      string m;
      if (multi_rev)
      {
        for (const package& p: prj.packages)
        {
          m += m.empty () ? "Release versions " :  ", ";
          m += p.name.string ();
          m += '/';
          m += p.release_version->string ();
        }
      }
      else
        m = "Release version " + pkg.release_version->string ();

      commit_project (m);
    }

    // Tag.
    //
    if (prj.tag)
    {
      // Note that our version can be either current (--tag mode) or release
      // (part of the release).
      //
      const standard_version& cv (pkg.release_version
                                  ? *pkg.release_version
                                  :  pkg.current_version);

      // Note that the tag may exist both locally and remotely or only in one
      // of the places. The remote case is handled by push.
      //
      // The git-tag command with -f will replace the existing tag but may
      // print to stdout (which we redirect to stderr) something like:
      //
      // Updated tag 'v0.1.0' (was 8f689ec)
      //
      run_git (git_ver,
               prj.path,
               "tag",
               prj.replace_tag ? "-f" : nullptr,
               "-a", *prj.tag,
               "-m", "Tag version " + cv.string ());
    }

    // Open.
    //
    if (prj.open_version)
    {
      string ov (prj.open_version->string ());

      // Rewrite each package manifest (similar code to above).
      //
      for (package& p: prj.packages)
      {
        try
        {
          manifest_rewriter rw (p.manifest);
          p.version_pos.value = ov;
          rw.replace (p.version_pos);
        }
        catch (const io_error& e)
        {
          // If we are releasing, then the release/revision version have
          // already been written to the manifests and the changes have been
          // committed. Thus, the user should re-try with the --open option
          // in this case.
          //
          fail << "unable to read/write " << p.manifest << ": " << e <<
            info << "use 'git checkout' to revert any changes and try again"
               << (pkg.release_version ? " with --open" : "");
        }
      }

      if (!commit)
        return 0;

      // Commit the manifest rewrites.
      //
      commit_project ("Change version to " + ov);
    }

    if (push)
    {
      // It would have been nice to push commits and tags using just the
      // --follow-tags option. However, this doesn't work if we need to
      // replace the tag in the remote repository. Thus, we specify the
      // repository and refspecs explicitly.
      //
      string tagspec;

      if (prj.tag)
      {
        // Force update of the remote tag, if required.
        //
        if (prj.replace_tag)
          tagspec += '+';

        tagspec += "refs/tags/";
        tagspec += *prj.tag;
      }

      // Upstream is normally in the <remote>/<branch> form, for example
      // 'origin/master'.
      //
      string remote;
      string brspec;
      {
        size_t p (path::traits::rfind_separator (st.upstream));

        if (p == string::npos)
          fail << "unable to extract remote from '" << st.upstream << "'";

        remote  = string (st.upstream, 0, p);
        brspec  = st.branch + ':' + string (st.upstream, p + 1);
      }

      if (verb && !o.no_progress ())
      {
        diag_record dr (text);
        dr << "pushing branch " << st.branch;

        if (prj.tag)
          dr << ", tag " << *prj.tag;
      }

      git_push (o,
                prj.path,
                remote,
                brspec,
                !tagspec.empty () ? tagspec.c_str () : nullptr);
    }

    return 0;
  }
}
