// file      : bdep/init.hxx -*- C++ -*-
// copyright : Copyright (c) 2014-2017 Code Synthesis Ltd
// license   : MIT; see accompanying LICENSE file

#ifndef BDEP_INIT_HXX
#define BDEP_INIT_HXX

#include <bdep/types.hxx>
#include <bdep/utility.hxx>

#include <bdep/project.hxx>
#include <bdep/init-options.hxx>

namespace bdep
{
  // Handle --config-create/add.
  //
  shared_ptr<configuration>
  cmd_init_config (const configuration_name_options&,
                   const dir_path& prj,
                   database&,
                   const dir_path& cfg,
                   cli::scanner& cfg_args,
                   bool config_add_specified,
                   bool config_create_specified,
                   optional<bool> config_default,
                   optional<bool> config_forward);

  // Initialize each package in each configuration skipping those that are
  // already initialized. Then synchronize each configuration.
  //
  void
  cmd_init (const common_options&,
            const dir_path& prj,
            database&,
            const configurations&,
            const package_locations&);

  int
  cmd_init (const cmd_init_options&, cli::scanner& args);
}

#endif // BDEP_INIT_HXX
