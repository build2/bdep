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
      ss.begin_object ();
      ss.member_name ("configuration", false /* check */);
      ss.begin_object ();
      ss.member ("id", *c->id);
      ss.member ("path", c->path.string ());

      if (c->name)
        ss.member ("name", *c->name);

      ss.end_object ();

      // Collect the initialized packages to print, unless the dependency
      // packages are specified.
      //
      strings pkgs;

      if (dep_pkgs.empty ())
        pkgs = config_packages (*c, prj_pkgs.packages);

      // Statuses of the project packages or their dependencies represented as
      // a JSON array. Will be used as a pre-serialized value for the packages
      // member of the configuration packages status object.
      //
      string ps;

      // If we print statuses of the dependency packages, then retrieve them
      // all as bpkg-status' stdout. Otherwise, retrieve the initialized
      // packages statuses, if any, as bpkg-status' stdout and append statuses
      // of uninitialized packages, if any.
      //
      const vector<package_state>& cpkgs (c->packages);
      if (!cpkgs.empty () && (!pkgs.empty () || !dep_pkgs.empty ()))
      {
        const dir_path& prj (prj_pkgs.project);

        bool fetch (o.fetch () || o.fetch_full ());

        if (fetch)
          cmd_fetch (o, prj, c, o.fetch_full ());

        // Status for either packages or their dependencies must be printed,
        // but not for both.
        //
        assert (pkgs.empty () == !dep_pkgs.empty ());

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

        // While at it, let's verify that the output looks like a JSON array,
        // since we will rely on the presence of the framing brackets below.
        //
        if (ps.empty () || ps.front () != '[' || ps.back () != ']')
          fail << "invalid bpkg-status output:\n" << ps;
      }

      // Append the uninitialized packages statuses to the JSON array, unless
      // we are printing the dependency packages.
      //
      if (dep_pkgs.empty ())
      {
        // If the initialized packages are present and so the array
        // representation is not empty, then unwrap the statuses removing the
        // framing brackets before appending the uninitialized packages
        // statuses and wrap them all back afterwards. Let's do it lazily.
        //
        bool first (true);

        for (const package_location& l: prj_pkgs.packages)
        {
          if (find_if (cpkgs.begin (),
                       cpkgs.end (),
                       [&l] (const package_state& p)
                       {
                         return p.name == l.name;
                       }) == cpkgs.end ()) // Unintialized?
          {
            // Unwrap the array representation, if present, on the first
            // uninitialized package.
            //
            if (first)
            {
              first = false;

              if (!ps.empty ())
              {
                size_t b (ps.find_first_not_of ("[\n"));
                size_t e (ps.find_last_not_of ("\n]"));

                // We would fail earlier otherwise.
                //
                assert (b != string::npos && e != string::npos);

                ps = string (ps, b, e + 1 - b);
              }
            }

            if (!ps.empty ())
              ps += ",\n";

            // Note: here we rely on the fact that the package name doesn't
            // need escaping.
            //
            ps += "  {\n"
                  "    \"name\": \"" + l.name.string () + "\",\n"
                  "    \"status\": \"uninitialized\"\n"
                  "  }";
          }
        }

        // Wrap the array if any uninitialized packages statuses have been
        // added.
        //
        if (!first)
          ps = "[\n" + ps + "\n]";
      }

      // Note that we can end up with an empty statuses representation (for
      // example, when query the status of a dependency package in the empty
      // configuration).
      //
      if (!ps.empty ())
      {
        ss.member_name ("packages", false /* check */);
        ss.value_json_text (ps);
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

    bool json (o.stdout_format () == stdout_format::json);

    // The same ignore/load story as in sync for the lines format.
    //
    // For the JSON format we load all the project packages, unless they are
    // specified explicitly, and allow the empty projects. Note that in
    // contrast to the lines format we print statuses of uninitialized
    // packages and empty configurations.
    //
    project_packages pp (
      find_project_packages (o,
                             !dep_pkgs.empty () /* ignore_packages */,
                             json               /* load_packages */,
                             json               /* allow_empty */));

    const dir_path& prj (pp.project);

    database db (open (prj, trace));

    configurations cfgs;
    {
      transaction t (db.begin ());

      // If --all|-a is specified and the project has no associated
      // configurations let's print an empty array of configurations for the
      // JSON format instead of failing (as we do for the lines format).
      //
      pair<configurations, bool> cs (
        find_configurations (o,
                             prj,
                             t,
                             true /* fallback_default */,
                             true /* validate */,
                             json /* allow_none */));

      t.commit ();

      // If specified, verify packages are present in at least one
      // configuration. But not for the JSON format where we also print
      // statuses of uninitialized packages.
      //
      if (!pp.packages.empty () && !json)
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
