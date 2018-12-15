// file      : bdep/project.cxx -*- C++ -*-
// copyright : Copyright (c) 2014-2018 Code Synthesis Ltd
// license   : MIT; see accompanying LICENSE file

#include <bdep/project.hxx>
#include <bdep/project-odb.hxx>

#include <libbpkg/manifest.hxx>

#include <bdep/database.hxx>
#include <bdep/diagnostics.hxx>

using namespace std;

namespace bdep
{
  configurations
  find_configurations (const project_options& po,
                       const dir_path& prj,
                       transaction& t,
                       bool fallback_default,
                       bool validate)
  {
    configurations r;

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

    // @<cfg-name>
    //
    if (po.config_name_specified ())
    {
      for (const string& n: po.config_name ())
      {
        if (auto c = db.query_one<configuration> (query::name == n))
          add (move (c));
        else
          fail << "no configuration name '" << n << "' in project " << prj;
      }
    }

    // --config <cfg-dir>
    //
    if (po.config_specified ())
    {
      for (dir_path d: po.config ())
      {
        d.complete ();
        d.normalize ();

        if (auto c = db.query_one<configuration> (query::path == d.string ()))
          add (move (c));
        else
          fail << "no configuration directory " << d << " in project " << prj;
      }
    }

    // --config-id <cfg-num>
    //
    if (po.config_id_specified ())
    {
      for (uint64_t id: po.config_id ())
      {
        if (auto c = db.find<configuration> (id))
          add (move (c));
        else
          fail << "no configuration id " << id << " in project " << prj;
      }
    }

    // --all
    //
    if (po.all ())
    {
      for (auto c: pointer_result (db.query<configuration> ()))
        add (move (c));

      if (r.empty ())
        fail << "no existing configurations";
    }

    // default
    //
    if (r.empty ())
    {
      if (fallback_default)
      {
        if (auto c = db.query_one<configuration> (query::default_))
          add (move (c));
        else
          fail << "no default configuration in project " << prj <<
            info << "use (@<cfg-name> | --config|-c <cfg-dir> | --all|-a) to "
               << "specify configuration explicitly";
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

    return r;
  }

  project_package
  find_project_package (const dir_path& start, bool ignore_nf)
  {
    dir_path prj;
    optional<dir_path> pkg;

    dir_path d (start);
    d.complete ();
    d.normalize ();
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
    }

    if (prj.empty ())
    {
      if (!pkg)
      {
        if (ignore_nf)
          return project_package ();

        fail << start << " is not a (sub)directory of a package or project";
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
    // Load each package's manifest and obtain its name (name is normally the
    // first value so we could optimize this, if necessary).
    //
    for (package_location& pl: pls)
    {
      path f (prj / pl.path / manifest_file);
      auto m (parse_manifest<bpkg::package_manifest> (f, "package"));
      pl.name = move (m.name);
      pl.project = move (m.project);
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
                         bool load_packages)
  {
    project_packages r;

    if (!dirs.empty ())
    {
      for (const dir_path& d: dirs)
      {
        project_package p (find_project_package (d));

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
      project_package p (find_project_package (path::current_directory ()));

      r.project = move (p.project);

      if (!ignore_packages && p.package)
      {
        // Name/project is to be extracted later.
        //
        r.packages.push_back (
          package_location {package_name (), nullopt, *p.package});
      }
    }

    if (!ignore_packages)
    {
      // Load the package locations and either verify that the discovered
      // packages are in it or, if nothing was discovered, use it as the
      // source for the package list.
      //
      package_locations pls (load_package_locations (r.project));

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
                           const configurations& cfgs)
  {
    for (const shared_ptr<configuration>& c: cfgs)
    {
      for (const package_location& p: pp.packages)
      {
        if (find_if (c->packages.begin (),
                     c->packages.end (),
                     [&p] (const package_state& s)
                     {
                       return p.name == s.name;
                     }) == c->packages.end ())
        {
          fail << "package " << p.name << " is not initialized "
               << "in configuration " << *c;
        }
      }
    }
  }

  standard_version
  package_version (const common_options& o,
                   const dir_path& cfg,
                   const package_name& p)
  {
    // We could have used bpkg-pkg-status but then we would have to deal with
    // iterations. So we use the build system's info meta-operation directly.
    //
    string v;
    {
      process pr;
      bool io (false);
      try
      {
        fdpipe pipe (fdopen_pipe ()); // Text mode seems appropriate.

        // Note: the package directory inside the configuration is a bit of an
        // assumption.
        //
        pr = start_b (
          o,
          pipe /* stdout */,
          2    /* stderr */,
          "info:", (dir_path (cfg) /= p.string ()).representation ());

        pipe.out.close ();
        ifdstream is (move (pipe.in), fdstream_mode::skip, ifdstream::badbit);

        for (string l; !eof (getline (is, l)); )
        {
          // Verify the name for good measure (comes before version).
          //
          if (l.compare (0, 9, "project: ") == 0)
          {
            if (l.compare (9, string::npos, p.string ()) != 0)
              fail << "name mismatch for package " << p;
          }
          else if (l.compare (0, 9, "version: ") == 0)
          {
            v = string (l, 9);
            break;
          }
        }

        is.close (); // Detect errors.
      }
      catch (const io_error&)
      {
        // Presumably the child process failed and issued diagnostics so let
        // finish_b() try to deal with that first.
        //
        io = true;
      }

      finish_b (o, pr, io);
    }

    try
    {
      return standard_version (v);
    }
    catch (const invalid_argument& e)
    {
      fail << "invalid package " << p << " version " << v << ": " << e << endf;
    }
  }
}
