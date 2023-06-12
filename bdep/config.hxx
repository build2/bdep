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
  // If type is nullopt, then assume that an existing configuration is being
  // added. If that's the case, query the bpkg configuration type and links
  // and warn the user if any host or build2 configurations are linked, unless
  // they are already associated with the project. If dry_run is true, then
  // make sure the configuration can be added without path/name/id clash but
  // don't actually add anything, returning NULL.
  //
  shared_ptr<configuration>
  cmd_config_add (const common_options&,
                  const configuration_add_options&,
                  const dir_path&           prj,
                  const package_locations&,
                  database&,
                  dir_path                  path,
                  optional<string>          name,
                  optional<string>          type,
                  optional<uint64_t>        id = nullopt,
                  const char*               what = "added",
                  bool                      dry_run = false);

  // Configuration directory should exist and its path should be absolute and
  // normalized.
  //
  shared_ptr<configuration>
  cmd_config_add (const dir_path&         prj,
                  transaction&,
                  const dir_path&         path,
                  const optional<string>& name,
                  string                  type,
                  bool                    default_ = true,
                  bool                    forward = true,
                  bool                    auto_sync = true,
                  optional<uint64_t>      id = nullopt,
                  bool                    dry_run = false);

  void
  cmd_config_add_print (diag_record&,
                        const dir_path&         prj,
                        const dir_path&,
                        const optional<string>& name,
                        bool                    default_ = true,
                        bool                    forward = true,
                        bool                    auto_sync = true);

  shared_ptr<configuration>
  cmd_config_create (const common_options&,
                     const configuration_add_options&,
                     const dir_path&           prj,
                     const package_locations&,
                     database&,
                     dir_path                  path,
                     const strings&            args,
                     optional<string>          name,
                     string                    type,
                     optional<uint64_t>        id = nullopt);

  // Configuration directory path should be absolute and normalized.
  //
  shared_ptr<configuration>
  cmd_config_create (const common_options&,
                     const dir_path&         prj,
                     transaction&,
                     const dir_path&         path,
                     const optional<string>& name,
                     string                  type,
                     bool                    default_ = true,
                     bool                    forward = true,
                     bool                    auto_sync = true,
                     bool                    existing = false,
                     bool                    wipe = false,
                     const strings&          args = {},
                     optional<uint64_t>      id = nullopt);

  void
  cmd_config_create_print (diag_record&,
                           const dir_path&         prj,
                           const dir_path&,
                           const optional<string>& name,
                           const string&           type,
                           bool                    default_ = true,
                           bool                    forward = true,
                           bool                    auto_sync = true,
                           const strings&          args = {});

  void
  cmd_config_link (const common_options&,
                   const shared_ptr<configuration>&,
                   const shared_ptr<configuration>&);

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
