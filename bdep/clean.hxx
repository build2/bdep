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
              (o.jobs_specified ()
               ? strings ({"-j", to_string (o.jobs ())})
               : strings ()),
              "clean",
              "-d", c->path,
              (o.immediate () ? "--immediate" :
               o.recursive () ? "--recursive" : nullptr),
              cfg_vars,
              pkgs);
  }

  inline int
  cmd_clean (const cmd_clean_options& o, cli::scanner& args)
  {
    return cmd_build (o, &cmd_clean, args);
  }
}

#endif // BDEP_CLEAN_HXX
