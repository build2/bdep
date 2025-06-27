// file      : bdep/clean.hxx -*- C++ -*-
// license   : MIT; see accompanying LICENSE file

#ifndef BDEP_CLEAN_HXX
#define BDEP_CLEAN_HXX

#include <bdep/types.hxx>
#include <bdep/utility.hxx>

#include <bdep/build.hxx>
#include <bdep/clean-options.hxx>

namespace bdep
{
  inline void
  cmd_clean (const cmd_clean_options& o,
             const shared_ptr<configuration>& c,
             const cstrings& pkgs,
             const strings& cfg_vars)
  {
    run_bpkg (2,
              o,
              "clean",
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
  cmd_clean (const cmd_clean_options& o, cli::scanner& args)
  {
    if (o.immediate () && o.recursive ())
      fail << "both --immediate|-i and --recursive|-r specified";

    return cmd_build (o, &cmd_clean, args);
  }
}

#endif // BDEP_CLEAN_HXX
