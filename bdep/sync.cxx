// file      : bdep/sync.cxx -*- C++ -*-
// copyright : Copyright (c) 2014-2017 Code Synthesis Ltd
// license   : MIT; see accompanying LICENSE file

#include <bdep/sync.hxx>

#include <bdep/database.hxx>
#include <bdep/diagnostics.hxx>

#include <bdep/fetch.hxx>

using namespace std;

namespace bdep
{
  void
  cmd_sync (const common_options& co,
            const dir_path& prj,
            const shared_ptr<configuration>& c,
            bool fetch)
  {
    assert (!c->packages.empty ());

    // Do a separate fetch instead of letting pkg-build do it. This way we get
    // better control of the diagnostics (no "fetching ..." for the project
    // itself). We also make sure that if the user specifies a repository for
    // a dependency to upgrade, then that repository is listed as part of the
    // project prerequisites/complements. Or, in other words, we only want to
    // allow specifying the location as a proxy for specifying version (i.e.,
    // "I want this dependency upgraded to the latest version available from
    // this repository").
    //
    // We also use the repository name rather than then location as a sanity
    // check (the repository must have been added as part of init).
    //
    if (fetch)
      run_bpkg (co,
                "fetch",
                "-d", c->path,
                "--shallow",
                "dir:" + prj.string ());

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
              "--no-fetch",
              "--configure-only",
              "--keep-out",
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
    if (!pp.packages.empty ())
      verify_project_packages (pp, cfgs);

    // Synchronize each configuration skipping empty ones.
    //
    bool first (true);
    for (const shared_ptr<configuration>& c: cfgs)
    {
      if (c->packages.empty ())
      {
        if (verb)
          info << "skipping empty configuration " << *c;

        continue;
      }

      // If we are synchronizing multiple configurations, separate them with a
      // blank line and print the configuration name/directory.
      //
      if (verb && cfgs.size () > 1)
      {
        text << (first ? "" : "\n")
             << "synchronizing with configuration " << *c;

        first = false;
      }

      bool fetch (o.fetch () || o.fetch_full ());

      if (fetch)
        cmd_fetch (o, prj, c, o.fetch_full ());

      // Don't re-fetch if we just fetched.
      //
      cmd_sync (o, prj, c, !fetch);
    }

    return 0;
  }
}
