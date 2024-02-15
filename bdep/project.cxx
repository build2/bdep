// file      : bdep/project.cxx -*- C++ -*-
// license   : MIT; see accompanying LICENSE file

#include <bdep/project.hxx>
#include <bdep/project-odb.hxx>

#include <libbutl/manifest-parser.hxx>

#include <libbpkg/manifest.hxx>

#include <bdep/database.hxx>
#include <bdep/diagnostics.hxx>

using namespace std;
using namespace butl;

namespace bdep
{
  pair<configurations, bool>
  find_configurations (const project_options& po,
                       const dir_path& prj,
                       transaction& t,
                       bool fallback_default,
                       bool validate,
                       bool allow_none)
  {
    configurations r;
    bool fallback (false);

    // Weed out duplicates.
    //
    auto add = [&r] (shared_ptr<configuration> c)
    {
      if (find_if (r.begin (),
                   r.end (),
                   [&c] (const shared_ptr<configuration>& e)
                   {
                     return *c->id == *e->id;
                   }) == r.end ())
        r.push_back (move (c));
    };

    database& db (t.database ());
    using query = bdep::query<configuration>;

    {
      // @<cfg-name>
      //
      const vector<pair<string, size_t>>& ns (po.config_name ());
      auto ni (ns.begin ());
      auto ne (ns.end ());

      // --config <cfg-dir>
      //
      const vector<pair<dir_path, size_t>>& ds (po.config ());
      auto di (ds.begin ());
      auto de (ds.end ());

      // --config-id <cfg-num>
      //
      const vector<pair<uint64_t, size_t>>& is (po.config_id ());
      auto ii (is.begin ());
      auto ie (is.end ());

      // Return true if the first options range is not empty and position of
      // its leftmost option is less than positions in two other option
      // ranges.
      //
      auto lt = [] (auto oi1, auto oe1, auto oi2, auto oe2, auto oi3, auto oe3)
      {
        return oi1 != oe1 &&
               (oi2 == oe2 || oi2->second > oi1->second) &&
               (oi3 == oe3 || oi3->second > oi1->second);
      };

      while (ni != ne || di != de || ii != ie)
      {
        if (lt (ni, ne, di, de, ii, ie))
        {
          const string& n (ni->first);

          if (auto c = db.query_one<configuration> (query::name == n))
            add (move (c));
          else
            fail << "no configuration name '" << n << "' in project " << prj;

          ++ni;
        }
        else if (lt (di, de, ii, ie, ni, ne))
        {
          dir_path d (di->first);

          normalize (d, "configuration directory");

          if (auto c = db.query_one<configuration> (query::path ==
                                                    d.string ()))
            add (move (c));
          else
            fail << "no configuration directory " << d << " in project "
                 << prj;

          ++di;
        }
        else if (lt (ii, ie, ni, ne, di, de))
        {
          uint64_t id (ii->first);

          if (auto c = db.find<configuration> (id))
            add (move (c));
          else
            fail << "no configuration id " << id << " in project " << prj;

          ++ii;
        }
        else
          assert (false);
      }
    }

    // --all
    //
    // Add configurations unordered.
    //
    if (po.all ())
    {
      for (auto c: pointer_result (db.query<configuration> ()))
        add (move (c));

      if (r.empty ())
      {
        if (allow_none)
          return make_pair (move (r), false /* fallback */);

        fail << "no existing configurations";
      }
    }

    // default
    //
    // Add configurations unordered.
    //
    if (r.empty ())
    {
      if (fallback_default)
      {
        for (shared_ptr<configuration> c:
               pointer_result (db.query<configuration> (query::default_)))
          add (move (c));

        if (r.empty ())
          fail << "no default configuration in project " << prj <<
            info << "use (@<cfg-name> | --config|-c <cfg-dir> | --all|-a) to "
                 << "specify configuration explicitly";

        fallback = true;
      }
      else
        fail << "no configurations specified";
    }

    // Validate all the returned configuration directories are still there.
    //
    if (validate)
    {
      for (const shared_ptr<configuration>& c: r)
      {
        if (!exists (c->path))
          fail << "configuration directory " << c->path << " no longer exists" <<
            info << "use config move command if it has been moved/renamed" <<
            info << "use config remove commands if it has been removed";
      }
    }

    return make_pair (move (r), fallback);
  }

  project_package
  find_project_package (const dir_path& start,
                        bool allow_subdir,
                        bool ignore_nf)
  {
    dir_path prj;
    optional<dir_path> pkg;

    dir_path d (normalize (start, "project directory"));
    for (; !d.empty (); d = d.directory ())
    {
      // Ignore errors when checking for file existence since we may be
      // iterating over directories past any reasonable project boundaries.
      //
      bool p (exists (d / manifest_file, true));
      if (p)
      {
        if (pkg)
        {
          fail << "multiple package manifests between " << start
               << " and project root" <<
            info << "first  manifest is in " << *pkg <<
            info << "second manifest is in " << d;
        }

        pkg = d;

        // Fall through (can also be the project root).
      }

      // Check for the database file first since an (initialized) simple
      // project most likely won't have any *.manifest files.
      //
      if (exists (d / bdep_file, true)     ||
          exists (d / packages_file, true) ||
          //
          // We should only consider {repositories,configurations}.manifest if
          // we have either packages.manifest or manifest (i.e., this is a
          // valid project root).
          //
          (p && (exists (d / repositories_file, true) ||
                 exists (d / configurations_file, true))))
      {
        prj = move (d);
        break;
      }

      // If this directory is not a package nor project root and search from
      // within their subdirs is disallowed, then bail out to fail.
      //
      if (!allow_subdir && !pkg)
        break;
    }

    if (prj.empty ())
    {
      if (!pkg)
      {
        if (ignore_nf)
          return project_package ();

        fail << start << " is not a "
             << (allow_subdir
                 ? "(sub)directory of a package or project"
                 : "package or project directory");
      }

      // Project and package are the same.
      //
      prj = move (*pkg);
      pkg = dir_path ();
    }
    else if (pkg)
      pkg = pkg->leaf (prj);

    return project_package {move (prj), move (pkg)};
  }

  static package_locations
  load_package_locations (const dir_path& prj, bool allow_empty = false)
  {
    package_locations pls;

    // If exists, load packages.manifest from the project root. Otherwise,
    // this must be a simple, single-package project.
    //
    path f (prj / packages_file);

    if (exists (f))
    {
      using bpkg::package_manifest;
      using bpkg::dir_package_manifests;

      auto ms (parse_manifest<dir_package_manifests> (f, "packages"));

      // While an empty repository is legal, in our case it doesn't make much
      // sense and will just further complicate things.
      //
      if (ms.empty () && !allow_empty)
        fail << "no packages listed in " << f;

      for (package_manifest& m: ms)
      {
        // Convert the package location from POSIX to the host form and make
        // sure the current directory is represented as an empty path.
        //
        assert (m.location);
        dir_path d (path_cast<dir_path> (move (*m.location)));
        d.normalize (false /* actualize */, true /* cur_empty */);

        pls.push_back (package_location {package_name (), nullopt, move (d)});
      }
    }
    else if (exists (prj / manifest_file))
    {
      pls.push_back (package_location {package_name (), nullopt, dir_path ()});
    }
    else if (!allow_empty)
      fail << "no packages in project " << prj;

    return pls;
  }

  static void
  load_package_names (const dir_path& prj, package_locations& pls)
  {
    // Load each package's manifest and obtain its name and project (they are
    // normally at the beginning of the manifest so we could optimize this, if
    // necessary).
    //
    for (package_location& pl: pls)
    {
      path f (prj / pl.path / manifest_file);

      if (!exists (f))
        fail << "package manifest file " << f << " does not exist";

      try
      {
        ifdstream is (f);
        manifest_parser p (is, f.string ());

        bpkg::package_manifest m (p,
                                  false /* ignore_unknown */,
                                  false /* complete_depends */);

        pl.name = move (m.name);
        pl.project = move (m.project);
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
    }
  }

  package_locations
  load_packages (const dir_path& prj, bool allow_empty)
  {
    package_locations pls (load_package_locations (prj, allow_empty));
    load_package_names (prj, pls);
    return pls;
  }

  project_packages
  find_project_packages (const dir_paths& dirs,
                         bool ignore_packages,
                         bool load_packages,
                         bool allow_empty)
  {
    project_packages r;

    if (!dirs.empty ())
    {
      for (const dir_path& d: dirs)
      {
        if (!exists (d))
          fail << "project/package directory " << d << " does not exist";

        project_package p (
          find_project_package (d, false /* allow_subdir */));

        // We only work on one project at a time.
        //
        if (r.project.empty ())
        {
          r.project = move (p.project);
        }
        else if (r.project != p.project)
        {
          fail << "multiple project directories specified" <<
            info << r.project <<
            info << p.project;
        }

        if (!ignore_packages && p.package)
        {
          // Suppress duplicate packages.
          //
          if (find_if (r.packages.begin (),
                       r.packages.end (),
                       [&p] (const package_location& pl)
                       {
                         return *p.package == pl.path;
                       }) == r.packages.end ())
          {
            // Name/project is to be extracted later.
            //
            r.packages.push_back (
              package_location {package_name (), nullopt, move (*p.package)});
          }
        }
      }
    }
    else
    {
      project_package p (
        find_project_package (current_directory (), true /* allow_subdir */));

      r.project = move (p.project);

      if (!ignore_packages && p.package)
      {
        // Name/project is to be extracted later.
        //
        r.packages.push_back (
          package_location {package_name (), nullopt, move (*p.package)});
      }
    }

    if (!ignore_packages)
    {
      // Load the package locations and either verify that the discovered
      // packages are in it or, if nothing was discovered, use it as the
      // source for the package list.
      //
      package_locations pls (load_package_locations (r.project, allow_empty));

      if (!r.packages.empty ())
      {
        for (const package_location& x: r.packages)
        {
          if (find_if (pls.begin (),
                       pls.end (),
                       [&x] (const package_location& y)
                       {
                         return x.path == y.path;
                       }) == pls.end ())
          {
            fail << "package directory " << x.path << " is not listed in "
                 << r.project;
          }
        }
      }
      else if (load_packages)
      {
        // Names to be extracted later.
        //
        r.packages = move (pls);
      }

      if (!r.packages.empty ())
        load_package_names (r.project, r.packages);
    }

    return r;
  }

  void
  verify_project_packages (const project_packages& pp,
                           const pair<configurations, bool>& cfgs)
  {
    for (const package_location& p: pp.packages)
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
          init = true;
          break;
        }
      }

      if (!init)
      {
        diag_record dr (fail);

        dr << "package " << p.name << " is not initialized in ";

        if (cfgs.second)
          dr << "any default configuration(s)";
        else if (cfgs.first.size () == 1)
          dr << "configuration " << *cfgs.first.front ();
        else
          dr << "any specified configurations";
      }
    }
  }

  package_info
  package_b_info (const common_options& o, const dir_path& d, b_info_flags fl)
  {
    try
    {
      return b_info (d,
                     fl,
                     verb,
                     [] (const char* const args[], size_t n)
                     {
                       if (verb >= 3)
                         print_process (args, n);
                     },
                     path (name_b (o)),
                     exec_dir,
                     o.build_option ());
    }
    catch (const b_error& e)
    {
      if (e.normal ())
        throw failed (); // Assume the build2 process issued diagnostics.

      fail << "unable to obtain project info for package directory " << d
           << ": " << e << endf;
    }
  }

  standard_version
  package_version (const common_options& o, const dir_path& d)
  {
    package_info pi (package_b_info (o, d, b_info_flags::none));

    if (pi.version.empty ())
      fail << "package in directory " << d << " does not use standard version" <<
        info << "perhaps the package does not load the version module?";

    return move (pi.version);
  }

  standard_version
  package_version (const common_options& o,
                   const dir_path& cfg,
                   const package_name& p)
  {
    // We could have used bpkg-pkg-status but then we would have to deal with
    // iterations. So we use the build system's info meta-operation directly.
    //
    // Note: the package directory inside the configuration is a bit of an
    // assumption.
    //
    package_info pi (package_b_info (o,
                                     (dir_path (cfg) /= p.string ()),
                                     b_info_flags::none));
    verify_package_info (pi, p);

    return move (pi.version);
  }

  void
  verify_package_info (const package_info& pi, const package_name& p)
  {
    if (pi.project != p)
      fail << "name mismatch for package " << p;

    if (pi.version.empty ())
      fail << "package " << p << " does not use standard version" <<
        info << "perhaps the package does not load the version module?";
  }
}
