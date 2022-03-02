// file      : bdep/status.cxx -*- C++ -*-
// license   : MIT; see accompanying LICENSE file

#include <bdep/status.hxx>

#include <iostream> // cout

#include <libbutl/json/serializer.hxx>

#include <bdep/project.hxx>
#include <bdep/database.hxx>
#include <bdep/diagnostics.hxx>

#include <bdep/fetch.hxx>

using namespace std;

namespace bdep
{
  // If the specified package list is not empty, then return only those
  // packages which are initialized in the specified configuration. Otherwise,
  // return all packages that have been initialized in this configuration.
  //
  static strings
  config_packages (const configuration& cfg, const package_locations& pkgs)
  {
    strings r;

    bool all (pkgs.empty ());
    for (const package_state& s: cfg.packages)
    {
      if (all ||
          find_if (pkgs.begin (),
                   pkgs.end (),
                   [&s] (const package_location& p)
                   {
                     return p.name == s.name;
                   }) != pkgs.end ())
        r.push_back (s.name.string ());
    }

    return r;
  }

  static process
  start_bpkg_status (const cmd_status_options& o,
                     int out,
                     const dir_path& prj,
                     const dir_path& cfg,
                     const strings& pkgs,
                     bool fetch,
                     const char* format)
  {
    // Shallow fetch the project to make sure we show latest iterations and
    // pick up any new repositories.
    //
    // We do it in a separate command for the same reason as in sync.
    //
    if (fetch)
      run_bpkg (3,
                o,
                "fetch",
                "-d", cfg,
                "--shallow",
                repository_name (prj));

    // Don't show the hold status since the only packages that will normally
    // be held are the project's. But do show dependency constraints.
    //
    return start_bpkg (2 /* verbosity */,
                       o,
                       out,
                       2 /* stderr */,
                       "status",
                       "-d", cfg,
                       "--no-hold",
                       "--constraint",
                       (o.old_available () ? "--old-available" : nullptr),
                       (o.immediate () ? "--immediate" :
                        o.recursive () ? "--recursive" : nullptr),
                       "--stdout-format", format,
                       pkgs);
  }

  static void
  cmd_status_lines (const cmd_status_options& o,
                    const project_packages& prj_pkgs,
                    const configurations& cfgs,
                    const strings& dep_pkgs)
  {
    tracer trace ("status_lines");

    // Print status in each configuration, skipping fetching repositories in
    // those where no package statuses needs to be printed.
    //
    bool first (true);
    for (const shared_ptr<configuration>& c: cfgs)
    {
      // Collect the packages to print, unless the dependency packages are
      // specified.
      //
      strings pkgs;

      if (dep_pkgs.empty ())
        pkgs = config_packages (*c, prj_pkgs.packages);

      // If we are printing multiple configurations, separate them with a
      // blank line and print the configuration name/directory.
      //
      if (verb && cfgs.size () > 1)
      {
        cout << (first ? "" : "\n")
             << "in configuration " << *c << ':' << endl;

        first = false;
      }

      if (!c->packages.empty () && (!pkgs.empty () || !dep_pkgs.empty ()))
      {
        const dir_path& prj (prj_pkgs.project);

        bool fetch (o.fetch () || o.fetch_full ());

        if (fetch)
          cmd_fetch (o, prj, c, o.fetch_full ());

        // Status for either packages or their dependencies must be printed,
        // but not for both.
        //
        assert (pkgs.empty () == !dep_pkgs.empty ());

        process pr (start_bpkg_status (o,
                                       1 /* stdout */,
                                       prj,
                                       c->path,
                                       !pkgs.empty () ? pkgs : dep_pkgs,
                                       !fetch,
                                       "lines"));

        finish_bpkg (o, pr);
      }
      else
      {
        if (verb)
        {
          diag_record dr (info);

          if (c->packages.empty ())
            dr << "no packages ";
          else
            dr << "none of specified packages ";

          dr << "initialized in configuration " << *c << ", skipping";
        }
      }
    }
  }

  static void
  cmd_status_json (const cmd_status_options& o,
                   const project_packages& prj_pkgs,
                   const configurations& cfgs,
                   const strings& dep_pkgs)
  {
    tracer trace ("status_json");

    butl::json::stream_serializer ss (cout);

    ss.begin_array ();

    // Print status in each configuration, skipping fetching repositories in
    // those where no package statuses need to be printed.
    //
    for (const shared_ptr<configuration>& c: cfgs)
    {
      // Collect the packages to print, unless the dependency packages are
      // specified.
      //
      strings pkgs;

      if (dep_pkgs.empty ())
        pkgs = config_packages (*c, prj_pkgs.packages);

      ss.begin_object ();
      ss.member_name ("configuration");
      ss.begin_object ();
      ss.member ("id", *c->id);
      ss.member ("path", c->path.string ());

      if (c->name)
        ss.member ("name", *c->name);

      ss.end_object ();

      if (!c->packages.empty () && (!pkgs.empty () || !dep_pkgs.empty ()))
      {
        const dir_path& prj (prj_pkgs.project);

        bool fetch (o.fetch () || o.fetch_full ());

        if (fetch)
          cmd_fetch (o, prj, c, o.fetch_full ());

        // Status for either packages or their dependencies must be printed,
        // but not for both.
        //
        assert (pkgs.empty () == !dep_pkgs.empty ());

        // Save the JSON representation of package statuses into a string from
        // bpkg-status' stdout and use it as a pre-serialized value for the
        // packages member of the configuration packages status object.
        //
        string ps;

        fdpipe pipe (open_pipe ()); // Text mode seems appropriate.

        process pr (start_bpkg_status (o,
                                       pipe.out.get (),
                                       prj,
                                       c->path,
                                       !pkgs.empty () ? pkgs : dep_pkgs,
                                       !fetch,
                                       "json"));

        // Shouldn't throw, unless something is severely damaged.
        //
        pipe.out.close ();

        bool io (false);
        try
        {
          ifdstream is (move (pipe.in));
          ps = is.read_text ();
          is.close ();
        }
        catch (const io_error&)
        {
          // Presumably the child process failed and issued diagnostics so let
          // finish_bpkg() try to deal with that first.
          //
          io = true;
        }

        finish_bpkg (o, pr, io);

        // Trim the trailing newline, which must be present. Let's however
        // check that it is, for good measure.
        //
        if (!ps.empty () && ps.back () == '\n')
          ps.pop_back ();

        ss.member_name ("packages");
        ss.value_json_text (ps);
      }
      else
      {
        // Not that unlike in the lines output we don't tell the user that
        // there are no packages.
      }

      ss.end_object ();
    }

    ss.end_array ();
    cout << endl;
  }

  int
  cmd_status (const cmd_status_options& o, cli::scanner& args)
  {
    tracer trace ("status");

    if (o.immediate () && o.recursive ())
      fail << "both --immediate|-i and --recursive|-r specified";

    // We have two pretty different modes: project package status and
    // dependency package status (have arguments).
    //
    strings dep_pkgs;
    for (; args.more (); dep_pkgs.push_back (args.next ())) ;

    // The same ignore/load story as in sync.
    //
    project_packages pp (
      find_project_packages (o,
                             !dep_pkgs.empty () /* ignore_packages */,
                             false              /* load_packages   */));

    const dir_path& prj (pp.project);

    database db (open (prj, trace));

    configurations cfgs;
    {
      transaction t (db.begin ());
      pair<configurations, bool> cs (find_configurations (o, prj, t));
      t.commit ();

      // If specified, verify packages are present in at least one
      // configuration.
      //
      if (!pp.packages.empty ())
        verify_project_packages (pp, cs);

      cfgs = move (cs.first);
    }

    switch (o.stdout_format ())
    {
    case stdout_format::lines:
      {
        cmd_status_lines (o, pp, cfgs, dep_pkgs);
        break;
      }
    case stdout_format::json:
      {
        cmd_status_json (o, pp, cfgs, dep_pkgs);
        break;
      }
    }

    return 0;
  }
}
