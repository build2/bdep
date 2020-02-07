// file      : bdep/config.hxx -*- C++ -*-
// license   : MIT; see accompanying LICENSE file

#ifndef BDEP_CONFIG_HXX
#define BDEP_CONFIG_HXX

#include <bdep/types.hxx>
#include <bdep/utility.hxx>

#include <bdep/project.hxx>
#include <bdep/config-options.hxx>

namespace bdep
{
  shared_ptr<configuration>
  cmd_config_add (const configuration_add_options&,
                  const dir_path&           prj,
                  const package_locations&,
                  database&,
                  dir_path                  path,
                  optional<string>          name,
                  optional<uint64_t>        id = nullopt,
                  const char*               what = "added");

  shared_ptr<configuration>
  cmd_config_create (const common_options&,
                     const configuration_add_options&,
                     const dir_path&           prj,
                     const package_locations&,
                     database&,
                     dir_path                  path,
                     const strings&            args,
                     optional<string>          name,
                     optional<uint64_t>        id = nullopt);

  int
  cmd_config (cmd_config_options&&, cli::scanner& args);

  // Validate returning one of the options or NULL if none specified.
  //
  const char*
  cmd_config_validate_add (const configuration_add_options&);

  // Validate setting configuration name and/or id if specified.
  //
  void
  cmd_config_validate_add (const configuration_name_options&,
                           const char* what,
                           optional<string>& name,
                           optional<uint64_t>& id);

  default_options_files
  options_files (const char* cmd,
                 const cmd_config_options&,
                 const strings& args);

  cmd_config_options
  merge_options (const default_options<cmd_config_options>&,
                 const cmd_config_options&);
}

#endif // BDEP_CONFIG_HXX
