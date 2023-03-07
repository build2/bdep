// file      : bdep/release.cxx -*- C++ -*-
// license   : MIT; see accompanying LICENSE file

#include <bdep/release.hxx>

#include <iostream> // cout

#include <libbutl/manifest-parser.hxx>
#include <libbutl/manifest-rewriter.hxx>

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

      // File positions of *-version values (upstream, <distribution>) for
      // warnings.
      //
      vector<manifest_name_value> other_version_pos;

      standard_version           current_version;
      optional<standard_version> release_version;
    };

    struct project
    {
      dir_path        path;
      vector<package> packages;

      optional<standard_version> open_version;

      optional<string> tag;

      struct current_tag
      {
        string name;
        cmd_release_current_tag action;
      };
      optional<current_tag> cur_tag; // Only present if tagging a revision.
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
      fail << "current version " << cv << " is a snapshot" <<
        info << "use --force=snapshot to tag anyway";

    // Canonical version tag without epoch or revision, unless we keep/remove
    // the current tag in the revising or tagging mode.
    //
    // For tagging a snapshot we need to use the actual package version
    // (replacing .z with the actual snapshot information). Note: for
    // snapshots we are always tagging the current version (see above).
    //
    const standard_version& v (
      cv.latest_snapshot ()
      ? package_version (o, pkg.manifest.directory ())
      : cv);

    auto vtag = [] (const standard_version& v, bool inc_rev)
    {
      return 'v' + v.string_project (inc_rev);
    };

    if (cv.revision == 0) // Tagging a release.
    {
      // Specifying --current-tag for the release tagging is meaningless.
      //
      if (o.current_tag_specified ())
        fail << "--current-tag specified for non-revision current version "
             << cv;

      prj.tag = vtag (v, false /* inc_rev */);
    }
    else                  // Tagging a revision.
    {
      // Note that using --current-tag for the same version inconsistently
      // either fails in the middle of the release (e.g. for update after
      // remove) or succeeds but with an undesirable result (e.g. update after
      // keep). Diagnosing such inconsistent use is quite complicated so let's
      // ignore it for now.
      //
      cmd_release_current_tag ct (o.current_tag ());

      // Include the revision into the tag, unless we update the current tag.
      //
      prj.tag = vtag (v, ct != cmd_release_current_tag::update /* inc_rev */);

      // Current tag version.
      //
      // Note that the current tag name is only used for removal but let's
      // always fill it in, for completeness.
      //
      standard_version ctv (v);
      ctv.revision = ct != cmd_release_current_tag::update
                     ? v.revision - 1
                     : 0;

      prj.cur_tag = project::current_tag {vtag (ctv, true /* inc_rev */), ct};
    }
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

    auto make_snapshot = [&cv] (uint32_t major,
                                uint32_t minor,
                                uint32_t patch,
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
    else if (o.open_base_specified ())
    {
      const string& vs (o.open_base ());

      try
      {
        standard_version v (vs);

        if (!v.release ())
          throw invalid_argument ("pre-release");

        if (v.revision != 0)
          throw invalid_argument ("contains revision");

        if (v <= cv)
          fail << "base version " << vs << " is less than or equal to "
               << "current version " << cv;

        ov = make_snapshot (v.major (), v.minor (), v.patch ());
      }
      catch (const invalid_argument& e)
      {
        fail << "invalid base version '" << vs << "': " << e;
      }
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

      uint32_t mj (cv.major ());
      uint32_t mi (cv.minor ());
      uint32_t pa (cv.patch ());

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
      uint32_t mj (cv.major ());
      uint32_t mi (cv.minor ());
      uint32_t pa (cv.patch ());
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
    // Also, don't complain if we are not committing or amending an existing
    // commit.
    //
    if (!st.staged      &&
        !o.no_commit () &&
        !o.amend ()     &&
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

      // Verify that an option is specified only if any of it's prerequisite
      // options is also specified.
      //
      auto require_any = [] (
        const char* opt, bool opt_specified,
        const small_vector<pair<const char*, bool>, 2>& prereqs)
      {
        assert (!prereqs.empty ());

        if (opt_specified)
        {
          for (const pair<const char*, bool>& p: prereqs)
          {
            // Bail out if a prerequisite is specified.
            //
            if (p.second)
              return;
          }

          diag_record dr (fail);
          dr << opt << " requires";

          for (size_t i (0); i != prereqs.size (); ++i)
          {
            if (i != 0)
            {
              if (prereqs.size () > 2)
                dr << ',';

              if (i == prereqs.size () - 1)
                dr << " or";
            }

            dr << ' ' << prereqs[i].first;
          }
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

      // The following option is only meaningful for the revising and tagging
      // modes.
      //
      require_any ("--current-tag", o.current_tag_specified (),
                   {{"--revision", o.revision ()}, {"--tag", o.tag ()}});

      // Specifying --current-tag is meaningless without tagging.
      //
      gopt = nullptr;
      verify ("--current-tag", o.current_tag_specified ());
      verify ("--no-tag",  o.no_tag ());

      // The following option is only meaningful for the releasing and
      // revising modes.
      //
      gopt = o.revision () ? nullptr : mopt;
      verify ("--no-tag",  o.no_tag ());

      // The following (mutually exclusive) options are only meaningful for
      // the releasing, revising, and opening modes.
      //
      gopt = o.revision () || o.open () ? nullptr : mopt;
      verify ("--no-commit",  o.no_commit ());
      verify ("--edit",       o.edit ());
      verify ("--no-edit",    o.no_edit ());

      // The following (mutually exclusive) options are only meaningful for
      // the releasing and opening modes.
      //
      gopt = o.open () ? nullptr : mopt;
      verify ("--open-beta",  o.open_beta ());
      verify ("--open-patch", o.open_patch ());
      verify ("--open-minor", o.open_minor ());
      verify ("--open-major", o.open_major ());
      verify ("--open-base",  o.open_base_specified ());
      verify ("--no-open",    o.no_open ()); // Releasing only (see above).

      // There is no sense to push without committing the version change first.
      // Meaningful for all modes.
      //
      gopt = nullptr;
      verify ("--push",      o.push ());
      verify ("--show-push", o.show_push ());
      verify ("--no-commit", o.no_commit ()); // Not for tagging (see above).

      // Verify the --amend and --squash options.
      //
      require_any ("--amend", o.amend (), {{"--revision", o.revision ()}});

      require_any ("--squash", o.squash_specified (),
                   {{"--amend", o.amend ()}});

      if (o.squash_specified () && o.squash () == 0)
        fail << "invalid --squash value: " << o.squash ();

      // There is no sense to amend without committing.
      //
      gopt = nullptr;
      verify ("--amend",     o.amend ());     // Revising only (see above).
      verify ("--no-commit", o.no_commit ()); // Not for tagging (see above).
    }

    // Fully parse package manifest verifying it is valid and returning the
    // position of the version value (first) and positions of *-version values
    // (second).
    //
    // In a sense we are publishing (by tagging) to a version control-based
    // repository and it makes sense to ensure the repository will not be
    // broken, similar to how we do it in publish.
    //
    auto parse_manifest = [&o] (const path& f)
    {
      using bpkg::version;
      using bpkg::package_manifest;

      if (!exists (f))
        fail << "package manifest file " << f << " does not exist";

      pair<manifest_name_value, vector<manifest_name_value>> r;

      try
      {
        ifdstream is (f);
        manifest_parser p (is,
                           f.string (),
                           [&r] (manifest_name_value& nv)
                           {
                             if (nv.name == "version")
                               r.first = nv;
                             else
                             {
                               size_t n (nv.name.size ());

                               if (n > 8 &&
                                   nv.name.compare (n - 8, 8, "-version") == 0)
                                 r.second.push_back (nv);
                             }

                             return true;
                           });

        dir_path d (f.directory ());

        // Parse the package manifest populating the version snapshot
        // information and completing dependency constraints. Save the
        // original version into the result.
        //
        // Note that the dependency constraint parsing may fail if the
        // dependent version it refers to is a latest snapshot (see
        // dependency_constraint for details). That's why the version
        // translation is required.
        //
        package_manifest m (p,
                            [&o, &d, &r] (version& v)
                            {
                              r.first.value = v.string (); // For good measure.
                              v = version (package_version (o, d).string ());
                            });

        // Validate the *-file manifest values expansion.
        //
        // Note: not forcible. Allowing releasing broken packages we are just
        // asking for trouble and long mailing list threads.
        //
        m.load_files ([&f, &d] (const string& n, const path& p)
                      {
                        path vf (d / p);

                        try
                        {
                          ifdstream is (vf);
                          string s (is.read_text ());

                          if (s.empty () && n != "build-file")
                            fail << n << " manifest value in " << f
                                 << " references empty file " << vf;

                          return s;
                        }
                        catch (const io_error& e)
                        {
                          fail << "unable to read " << vf << " referenced by "
                               << n << " manifest value in " << f << ": " << e
                               << endf;
                        }
                      });

        assert (!r.first.empty ());
        return r;
      }
      catch (const manifest_parsing& e)
      {
        fail << "invalid package manifest: " << f << ':'
             << e.line << ':' << e.column << ": " << e.description << endf;
      }
      catch (const io_error& e)
      {
        fail << "unable to read " << f << ": " << e << endf;
      }
    };

    // Collect project/package information.
    //
    project prj;
    {
      // We release all the packages in the project. We could have required a
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
        pair<manifest_name_value, vector<manifest_name_value>> r (
          parse_manifest (f));

        package_name n (move (pl.name));
        standard_version v;
        try
        {
          // Allow stubs only in the --revision and --tag modes.
          //
          standard_version::flags f (standard_version::none);

          if (o.revision () || o.tag ())
            f |= standard_version::allow_stub;

          v = standard_version (r.first.value, f);
        }
        catch (const invalid_argument& e)
        {
          fail << "current package " << n << " version '" << r.first.value
               << "' is not standard: " << e;
        }

        prj.packages.push_back (
          package {move (n),
                   move (f),
                   move (r.first),
                   move (r.second),
                   move (v),
                   nullopt /* release_version */});
      }
    }

    // Verify all the packages have the same version. This is the only
    // arrangement we currently (and probably ever) support. The immediate
    // problem with supporting different versions (besides the extra
    // complexity, of course) is tagging. We only allow variations in
    // revisions if the tag doesn't include the revision, which is not the
    // case when we keep/remove a current tag in the revising and tagging
    // modes.
    //
    // While at it, notice if we will end up with different revisions which
    // we use below when forming the commit message.
    //
    bool multi_rev (false);
    {
      const package& f (prj.packages.front ());

      bool ignore_revision (
        !((o.revision () || o.tag()) &&
          o.current_tag () != cmd_release_current_tag::update));

      for (const package& p: prj.packages)
      {
        const auto& fv (f.current_version);
        const auto& pv (p.current_version);

        if (fv.compare (pv, ignore_revision) != 0)
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

    // true - push, false - show push, nullopt - none of the above
    //
    optional<bool> push;
    if (o.push () || o.show_push ())
      push = o.push ();

    if (push && st.upstream.empty ())
      fail << "no upstream branch set for local branch '" << st.branch << "'";

    // Warn about other *-version values that have to be updated manually.
    //
    auto warn_other_versions = [&prj] (const char* what)
    {
      for (const package& p: prj.packages)
      {
        for (const manifest_name_value& v: p.other_version_pos)
        {
          location l (p.manifest.string (), v.value_line, v.value_column);
          warn (l) << v.name << " value " << v.value << " has to be "
                   << what << " manually, if necessary";
        }
      }
    };

    // Print the plan and ask for confirmation.
    //
    if (!o.yes ())
    {
      diag_record dr (text);

      dr << mode << ":" << '\n';

      for (const package& p: prj.packages)
      {
        dr << "  package: " << p.name            << '\n'
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
      {
        dr << "  tag:     " << (prj.tag ? prj.tag->c_str () : "no");

        const optional<project::current_tag>& ct (prj.cur_tag);

        if (ct)
        {
          if (ct->action == cmd_release_current_tag::update)
            dr << " (update)";
          else if (ct->action == cmd_release_current_tag::remove)
            dr << " (remove " << ct->name << ')';
        }

        dr << '\n';
      }

      dr << "  push:    " << (push ? st.upstream.c_str () : "no");

      if (push && !*push)
        dr << " (show)";

      dr.flush ();

      warn_other_versions ("changed");

      if (!yn_prompt ("continue? [y/n]"))
        return 1;
    }
    else
      warn_other_versions ("changed");

    // Stage the project changes and either commit them separately or amend
    // the latest commit. Open the commit messages in the editor if requested
    // and --no-edit is not specified or if --edit is specified. Fail if the
    // process terminated abnormally or with a non-zero status, unless
    // return_error is true in which case return the git process exit
    // information.
    //
    auto commit_project = [&o, &prj] (const strings& msgs,
                                      bool edit,
                                      bool amend = false,
                                      bool return_error = false)
    {
      assert (!msgs.empty ());

      cstrings msg_ops;
      for (const string& m: msgs)
      {
        msg_ops.push_back ("-m");
        msg_ops.push_back (m.c_str ());
      }

      // We shouldn't have any untracked files or unstaged changes other than
      // our modifications, so -a is good enough.
      //
      // We don't want git to print anything to stdout, so let's redirect it
      // to stderr for good measure, as git is known to print some
      // informational messages to stdout.
      //
      process pr (
        start_git (git_ver,
                   true /* system */,
                   prj.path,
                   0    /* stdin  */,
                   2    /* stdout */,
                   2    /* stderr */,
                   "commit",
                   verb < 1 ? "-q" : verb >= 2 ? "-v" : nullptr,
                   amend ? "--amend" : nullptr,
                   "-a",
                   (edit && !o.no_edit ()) || o.edit () ? "-e" : nullptr,
                   msg_ops));

      // Wait for the process termination.
      //
      if (return_error)
        pr.wait ();
      else
        finish_git (pr); // Fails on process error.

      assert (pr.exit);
      return move (*pr.exit);
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
          // version positions for the subsequent manifest rewrite.
          //
          if (prj.open_version)
          {
            pair<manifest_name_value, vector<manifest_name_value>> r (
              parse_manifest (p.manifest));

            vv = move (r.first);
            p.other_version_pos = move (r.second);
          }
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

      strings msgs ({move (m)});

      if (o.amend ())
      {
        // Collect the amended/squashed commit messages.
        //
        // This would fail if we try to squash initial commit. However, that
        // shouldn't be a problem since that would mean squashing the commit
        // for the first release that we are now revising, which is
        // meaningless.
        //
        optional<string> ms (
          git_string (git_ver,
                      false     /* system */,
                      prj.path,
                      false     /* ignore_error */,
                      "log",
                      "--format=%B",
                      "HEAD~" + to_string (o.squash ()) + "..HEAD"));

        if (!ms)
          fail << "unable to obtain commit messages for " << o.squash ()
               << " latest commit(s)";

        msgs.push_back (move (*ms));
      }

      // Note that we could handle the latest commit amendment (squash == 1)
      // here as well, but let's do it via the git-commit --amend option to
      // revert automatically in case of git terminating due to SIGINT, etc.
      //
      if (o.squash () > 1)
      {
        // Squash commits, reverting them into the index.
        //
        run_git (git_ver,
                 true /* system */,
                 prj.path,
                 "reset",
                  verb < 1 ? "-q" : nullptr,
                 "--soft",
                 "HEAD~" + to_string (o.squash ()));

        // Commit editing the message.
        //
        process_exit e (commit_project (msgs,
                                        true  /* edit */,
                                        false /* amend */,
                                        true  /* ignore_error */));

        // If git-commit terminated normally with non-zero status, for example
        // due to the empty commit message, then revert the above reset and
        // fail afterwards. If it terminated abnormally, we probably shouldn't
        // mess with it and leave things to the user to handle.
        //
        if (!e)
        {
          if (e.normal ())
          {
            run_git (git_ver,
                     true /* system */,
                     prj.path,
                     "reset",
                     verb < 1 ? "-q" : nullptr,
                     "--soft",
                     "HEAD@{1}"); // Previously checked out revision.

            throw failed (); // Assume the child issued diagnostics.
          }

          fail << "git " << e;
        }
      }
      else
      {
        // Commit or amend.
        //
        commit_project (msgs, st.staged || o.amend () /* edit */, o.amend ());
      }

      // The (possibly) staged changes are now committed.
      //
      st.staged = false;
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
      const optional<project::current_tag>& ct (prj.cur_tag);

      run_git (git_ver,
               true /* system */,
               prj.path,
               "tag",
               (ct && ct->action == cmd_release_current_tag::update
                ? "-f"
                : nullptr),
               "-a",
               "-m", "Tag version " + cv.string (),
               *prj.tag);

      if (ct && ct->action == cmd_release_current_tag::remove)
        run_git (git_ver,
                 true /* system */,
                 prj.path,
                 "tag",
                 "--delete",
                 ct->name);
    }

    // Open.
    //
    if (prj.open_version)
    {
      // Note: must be done before rewrite (which may change the locations;
      // unless we update the location after rewrite).
      //
      warn_other_versions ("opened");

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
      commit_project ({"Change version to " + ov}, st.staged);
    }

    if (push)
    {
      // It would have been nice to push commits and tags using just the
      // --follow-tags option. However, this doesn't work if we need to
      // replace or remove the tag in the remote repository. Thus, we specify
      // the repository and refspecs explicitly.
      //
      // Upstream is normally in the <remote>/<branch> form, for example
      // 'origin/master'. Note, however, that if any of these names contains
      // '/', then the split is ambiguous. Thus, we retrieve the remote name
      // via git-config and use it to also deduce the remote branch name from
      // the upstream branch.
      //
      string remote;
      string brspec;
      {
        // It's unlikely that the branch remote is configured globally, so we
        // use the bundled git.
        //
        optional<string> rem (git_line (git_ver,
                                        false /* system */,
                                        prj.path,
                                        false /* ignore_error */,
                                        "config",
                                        "branch." + st.branch + ".remote"));

        if (!rem)
          fail << "unable to obtain remote for '" << st.branch << "'";

        remote = move (*rem);

        // Push the branch if the mode is other than tagging (and so the
        // version change is committed) or the local branch is ahead (probably
        // due to the previous command run without --push).
        //
        if (!o.tag () || st.ahead)
        {
          // Since we may print the push command, let's tidy it up a bit by
          // reducing <src>:<dst> to just <src> if the source and destination
          // branches are the same.
          //
          brspec = st.branch;

          // For good measure verify that the remote name is a prefix for the
          // upstream branch.
          //
          size_t n (remote.size ());
          if (st.upstream.compare (0, n, remote) != 0 ||
              !path_traits::is_separator (st.upstream[n]))
            fail << "remote '" << remote << "' is not a prefix for upstream "
                 << "branch '" << st.upstream << "'";

          string b (st.upstream, n + 1);
          if (b != st.branch)
          {
            brspec += ':';
            brspec += b;
          }
        }
      }

      small_vector<string, 2> tagspecs;

      if (prj.tag)
      {
        // Add the new or update an existing tag.
        //
        const optional<project::current_tag>& ct (prj.cur_tag);
        {
          string t;

          // Force update of the remote tag, if requested.
          //
          if (ct && ct->action == cmd_release_current_tag::update)
            t += '+';

          t += "refs/tags/";
          t += *prj.tag;

          tagspecs.push_back (move (t));
        }

        // Remove the current tag, if requested.
        //
        if (ct && ct->action == cmd_release_current_tag::remove)
          tagspecs.push_back (":refs/tags/" + ct->name);
      }

      // There should always be something to push, since we are either tagging
      // or committing the version change (or both).
      //
      assert (!brspec.empty () || !tagspecs.empty ());

      if (*push)
      {
        if ((verb && !o.no_progress ()) || o.progress ())
        {
          diag_record dr (text);
          dr << "pushing";

          if (!brspec.empty ())
            dr << " branch " << st.branch;

          if (prj.tag)
          {
            if (!brspec.empty ())
              dr << ',';

            dr << " tag " << *prj.tag;
          }
        }

        git_push (o,
                  prj.path,
                  remote,
                  !brspec.empty ()  ? brspec.c_str ()  : nullptr,
                  tagspecs);
      }
      else
      {
        // While normally the command will be run by the user, it's possible
        // this will be scripted in some way and the script may want to
        // extract the push command to run later. So, for generality and
        // consistency with other similar situations, we print the command
        // to stdout.
        //
        cout << "git ";

        // Check if CWD is the project root and add -C if it's not.
        //
        if (prj.path != current_directory())
        {
          // Quote the directory if it contains spaces.
          //
          const string& s (prj.path.string ());
          const string& d (s.find (' ') == string::npos ? s : '"' + s + '"');
          cout << "-C " << d << ' ';
        }

        // Note: none of the remote, branch, or tag may contain spaces.
        //
        cout << "push " << remote;

        if (!brspec.empty ())
          cout << ' ' << brspec;

        for (const string& t: tagspecs)
          cout << ' ' << t;

        cout << endl;
      }
    }

    return 0;
  }

  default_options_files
  options_files (const char*, const cmd_release_options& o, const strings&)
  {
    // NOTE: remember to update the documentation if changing anything here.

    // bdep.options
    // bdep-release.options
    // bdep-release-{version|revision|open|tag}.options

    default_options_files r {
      {path ("bdep.options"), path ("bdep-release.options")},
      find_project (o.directory ())};

    // Add the mode-specific options file.
    //
    r.files.push_back (path (o.revision () ? "bdep-release-revision.options" :
                             o.open     () ? "bdep-release-open.options"     :
                             o.tag      () ? "bdep-release-tag.options"      :
                                             "bdep-release-version.options"));

    return r;
  }

  cmd_release_options
  merge_options (const default_options<cmd_release_options>& defs,
                 const cmd_release_options& cmd)
  {
    // NOTE: remember to update the documentation if changing anything here.

    return merge_default_options (
      defs,
      cmd,
      [] (const default_options_entry<cmd_release_options>& e,
          const cmd_release_options&)
      {
        const cmd_release_options& o (e.options);

        auto forbid = [&e] (const char* opt, bool specified)
        {
          if (specified)
            fail (e.file) << opt << " in default options file";
        };

        forbid ("--directory|-d", o.directory_specified ());
        forbid ("--revision",     o.revision ());
        forbid ("--open",         o.open ());
        forbid ("--tag",          o.tag ());
      });
  }
}
