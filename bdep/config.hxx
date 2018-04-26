// file      : bdep/config.hxx -*- C++ -*-
// copyright : Copyright (c) 2014-2017 Code Synthesis Ltd
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
  cmd_config_add (const dir_path&    prj,
                  database&,
                  dir_path           path,
                  optional<string>   name,
                  optional<bool>     default_ = nullopt,
                  optional<bool>     forward  = nullopt,
                  optional<uint64_t> id = nullopt,
                  const char*        what = "added");

  shared_ptr<configuration>
  cmd_config_create (const common_options&,
                     const dir_path&        prj,
                     database&,
                     dir_path               path,
                     cli::scanner&          args,
                     optional<string>       name,
                     optional<bool>         default_ = nullopt,
                     optional<bool>         forward  = nullopt,
                     optional<uint64_t>     id = nullopt);

  int
  cmd_config (const cmd_config_options&, cli::scanner& args);

  // Validate returning one of the options or NULL if none specified.
  //
  const char*
  cmd_config_validate_add (const configuration_add_options&);
}

#endif // BDEP_CONFIG_HXX
