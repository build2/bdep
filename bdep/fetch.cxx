// file      : bdep/fetch.cxx -*- C++ -*-
// copyright : Copyright (c) 2014-2017 Code Synthesis Ltd
// license   : MIT; see accompanying LICENSE file

#include <bdep/fetch.hxx>

#include <bdep/database.hxx>
#include <bdep/diagnostics.hxx>

using namespace std;

namespace bdep
{
  void
  cmd_fetch (const common_options& o,
             const dir_path& prj,
             const shared_ptr<configuration>& c,
             bool full)
  {
    // Let's use the repository name rather than the location as a sanity
    // check (the repository must have been added as part of init).
    //
    run_bpkg (o,
              "fetch",
              "-d", c->path,
              (full ? nullptr : ("dir:" + prj.string ()).c_str ()));
  }

  int
  cmd_fetch (const cmd_fetch_options& o, cli::scanner&)
  {
    tracer trace ("fetch");

    dir_path prj (
      find_project_packages (o, true /* ignore_packages */).project);

    database db (open (prj, trace));

    transaction t (db.begin ());
    configurations cfgs (find_configurations (prj, t, o));
    t.commit ();

    bool first (true);
    for (const shared_ptr<configuration>& c: cfgs)
    {
      if (c->packages.empty ())
      {
        info << "no packages initialized in configuration " << *c;
        continue;
      }

      // If we are fetching in multiple configurations, separate them with a
      // blank line and print the configuration name/directory.
      //
      if (verb && cfgs.size () > 1)
      {
        text << (first ? "" : "\n")
             << "fetching in configuration " << *c;

        first = false;
      }

      cmd_fetch (o, prj, c, o.full ());
    }

    return 0;
  }
}