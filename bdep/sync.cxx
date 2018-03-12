// file      : bdep/sync.cxx -*- C++ -*-
// copyright : Copyright (c) 2014-2017 Code Synthesis Ltd
// license   : MIT; see accompanying LICENSE file

#include <bdep/sync.hxx>

#include <bdep/database.hxx>
#include <bdep/diagnostics.hxx>

using namespace std;

namespace bdep
{
  void
  cmd_sync (const common_options& co,
            const dir_path& prj,
            const shared_ptr<configuration>& c)
  {
    assert (!c->packages.empty ());

    // Prepare the pkg-spec.
    //
    string spec;
    for (const package_state& p: c->packages)
    {
      if (!spec.empty ())
        spec += ',';

      spec += p.name;
    }

    spec += '@';
    spec += prj.string ();

    run_bpkg (co,
              "build",
              "-d", c->path,
              //"--fetch-shallow",
              "--configure-only",
              //"--keep-out",
              spec);
  }

  int
  cmd_sync (const cmd_sync_options& o, cli::scanner&)
  {
    tracer trace ("sync");

    // We could be running from a package directory (or the user specified one
    // with -d) that has not been init'ed in this configuration. We want to
    // diagnose that since such a package will not be present in the bpkg
    // configuration. But if we are running from the project, then we don't
    // want to treat all the available packages as specified by the user (thus
    // load_packages=false).
    //
    project_packages pp (
      find_project_packages (o,
                             false /* ignore_packages */,
                             false /* load_packages */));

    const dir_path& prj (pp.project);

    database db (open (prj, trace));

    transaction t (db.begin ());
    configurations cfgs (find_configurations (prj, t, o));
    t.commit ();

    // If specified, verify packages are present in each configuration.
    //
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

    // Synchronize each configuration skipping empty ones.
    //
    for (const shared_ptr<configuration>& c: cfgs)
    {
      if (c->packages.empty ())
      {
        if (verb)
          info << "skipping empty configuration " << *c;

        continue;
      }

      cmd_sync (o, prj, c);
    }

    return 0;
  }
}
