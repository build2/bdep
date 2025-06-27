// file      : bdep/update.hxx -*- C++ -*-
// license   : MIT; see accompanying LICENSE file

#ifndef BDEP_UPDATE_HXX
#define BDEP_UPDATE_HXX

#include <bdep/types.hxx>
#include <bdep/utility.hxx>

#include <bdep/build.hxx>
#include <bdep/update-options.hxx>

namespace bdep
{
  inline void
  cmd_update (const cmd_update_options& o,
              const shared_ptr<configuration>& c,
              const cstrings& pkgs,
              const strings& cfg_vars)
  {
    run_bpkg (2,
              o,
              "update",
              "-d", c->path,
              (o.immediate () ? "--immediate" :
               o.recursive () ? "--recursive" : nullptr),
              (o.jobs_specified ()
               ? strings ({"-j", to_string (o.jobs ())})
               : strings ()),
              cfg_vars,
              pkgs);
  }

  inline int
  cmd_update (const cmd_update_options& o, cli::scanner& args)
  {
    if (o.immediate () && o.recursive ())
      fail << "both --immediate|-i and --recursive|-r specified";

    return cmd_build (o, &cmd_update, args);
  }
}

#endif // BDEP_UPDATE_HXX
