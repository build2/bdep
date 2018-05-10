// file      : bdep/test.hxx -*- C++ -*-
// copyright : Copyright (c) 2014-2017 Code Synthesis Ltd
// license   : MIT; see accompanying LICENSE file

#ifndef BDEP_TEST_HXX
#define BDEP_TEST_HXX

#include <bdep/types.hxx>
#include <bdep/utility.hxx>

#include <bdep/build.hxx>
#include <bdep/test-options.hxx>

namespace bdep
{
  inline void
  cmd_test (const cmd_test_options& o,
            const shared_ptr<configuration>& c,
            const cstrings& pkgs,
            const strings& cfg_vars)
  {
    run_bpkg (2, o, "test", "-d", c->path, cfg_vars, pkgs);
  }

  inline int
  cmd_test (const cmd_test_options& o, cli::scanner& args)
  {
    return cmd_build (o, &cmd_test, args);
  }
}

#endif // BDEP_TEST_HXX
