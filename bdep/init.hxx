// file      : bdep/init.hxx -*- C++ -*-
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
                   const configuration_add_options&,
                   const dir_path& prj,
                   const package_locations&,
                   database&,
                   const dir_path& cfg,
                   const strings& cfg_args,
                   bool config_add_specified,
                   bool config_create_specified);

  // Initialize each package in each configuration skipping those that are
  // already initialized. Then synchronize each configuration unless sync
  // is false.
  //
  void
  cmd_init (const common_options&,
            const dir_path& prj,
            database&,
            const configurations&,
            const package_locations&,
            const strings& pkg_args,
            bool sync = true);

  int
  cmd_init (const cmd_init_options&, cli::group_scanner& args);

  default_options_files
  options_files (const char* cmd,
                 const cmd_init_options&,
                 const strings& args);

  cmd_init_options
  merge_options (const default_options<cmd_init_options>&,
                 const cmd_init_options&);
}

#endif // BDEP_INIT_HXX
