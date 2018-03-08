// file      : bdep/project.cxx -*- C++ -*-
// copyright : Copyright (c) 2014-2017 Code Synthesis Ltd
// license   : MIT; see accompanying LICENSE file

#include <bdep/project.hxx>

#include <libbpkg/manifest.hxx>

#include <bdep/diagnostics.hxx>

using namespace std;

namespace bdep
{
  // Given a directory which can a project root, a package root, or one of
  // their subdirectories, return the absolute project (first) and relative
  // package (second) directories. The package directory may be absent if the
  // given directory is not within a package root or empty if the project and
  // package roots are the same.
  //
  struct project_package
  {
    dir_path           project;
    optional<dir_path> package;
  };

  static project_package
  find_project_packages (const dir_path& start)
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
      if (exists (d / manifest_file, true))
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

      // Check for configurations.manifest first since a simple project will
      // have no packages.manifest
      //
      if (exists (d / configurations_file, true) ||
          exists (d / packages_file, true))
      {
        prj = move (d);
        break;
      }
    }

    if (prj.empty ())
    {
      if (!pkg)
        fail << start << " is no a (sub)directory of a package or project";

      // Project and package are the same.
      //
      prj = move (*pkg);
      pkg = dir_path ();
    }
    else if (pkg)
      pkg = pkg->leaf (prj);

    return project_package {move (prj), move (pkg)};
  }

  project_packages
  find_project_packages (const project_options& po, bool ignore_packages)
  {
    project_packages r;

    if (po.directory_specified ())
    {
      for (const dir_path& d: po.directory ())
      {
        project_package p (find_project_packages (d));

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
          if (find (r.packages.begin (),
                    r.packages.end (),
                    *p.package) == r.packages.end ())
          {
            r.packages.push_back (move (*p.package));
          }
        }
      }
    }
    else
    {
      project_package p (find_project_packages (path::current_directory ()));

      r.project = move (p.project);

      if (!ignore_packages && p.package)
      {
        r.packages.push_back (move (*p.package));
      }
    }

    if (!ignore_packages)
    {
      // If exists, load packages.manifest from the project root and either
      // verify that the discovered packages are in it or, if nothing was
      // discovered, use it as the source for the package list.
      //
      path f (r.project / packages_file);

      if (exists (f))
      {
        using bpkg::package_manifest;
        using bpkg::dir_package_manifests;

        auto ms (parse_manifest<dir_package_manifests> (f, "packages"));

        // While an empty repository is legal, in our case it doesn't make
        // much sense and will just further complicate things.
        //
        if (ms.empty ())
          fail << "no packages listed in " << f;

        // Convert the package location from POSIX to the host form and make
        // sure the current directory is represented as an empty path.
        //
        auto location = [] (const package_manifest& m)
        {
          assert (m.location);
          dir_path d (path_cast<dir_path> (*m.location));
          d.normalize (false /* actualize */, true /* cur_empty */);
          return d;
        };

        if (r.packages.empty ())
        {
          for (package_manifest& m: ms)
            r.packages.push_back (location (m));
        }
        else
        {
          // It could be costly to normalize the location for each
          // comparison. We, however, do not expect more than a handful of
          // packages so we are probably ok.
          //
          for (const dir_path& pd: r.packages)
          {
            if (find_if (ms.begin (),
                         ms.end (),
                         [&pd, &location] (const package_manifest& m)
                         {
                           return pd == location (m);
                         }) == ms.end ())
            {
              fail << "package directory " << pd << " not listed in " << f;
            }
          }
        }
      }
      else
      {
        // If packages.manifest does not exist, then this must be a simple
        // project.
        //
        assert (r.packages.size () == 1 && r.packages[0].empty ());
      }
    }

    return r;
  }
}
