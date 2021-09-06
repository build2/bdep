// file      : bdep/sync.hxx -*- C++ -*-
// license   : MIT; see accompanying LICENSE file

#ifndef BDEP_SYNC_HXX
#define BDEP_SYNC_HXX

#include <bdep/types.hxx>
#include <bdep/utility.hxx>

#include <bdep/project.hxx>
#include <bdep/sync-options.hxx>

namespace bdep
{
  // The optional pkg_args are the additional dependency packages and/or
  // configuration variables to pass to bpkg-pkg-build (see bdep-init).
  //
  // If fetch is false, don't perform a (shallow) fetch of the project
  // repository. If yes is false, then don't suppress bpkg prompts. If
  // name_cfg is true then include the configuration name/directory into
  // progress.
  //
  // Before automatically creating a configuration for a build-time dependency
  // and associating it with the project(s), the user is prompted unless the
  // respective create_*_config argument is true.
  //
  // If the transaction is already started on the project's database, then it
  // must also be passed to the function along with a pointer to the
  // configuration paths/types list. If the function will be associating
  // build-time dependency configurations with the project, it will do it as
  // part of this transaction and will also save the created dependency
  // configurations into this list. If this transaction is rolled back for any
  // reason (for example, because the failed exception is thrown by cmd_sync()
  // call), then the caller must start the new transaction and re-associate
  // the created configurations with the project using the configuration types
  // also as their names.
  //
  void
  cmd_sync (const common_options&,
            const dir_path& prj,
            const shared_ptr<configuration>&,
            bool implicit,
            const strings& pkg_args = strings (),
            bool fetch = true,
            bool yes = true,
            bool name_cfg = false,
            bool create_host_config = false,
            bool create_build2_config = false,
            transaction* = nullptr,
            vector<pair<dir_path, string>>* created_cfgs = nullptr);

  // As above but perform an implicit sync without a configuration object
  // (i.e., as if from the hook).
  //
  void
  cmd_sync_implicit (const common_options&,
                     const dir_path& cfg,
                     bool fetch = true,
                     bool yes = true,
                     bool name_cfg = true,
                     bool create_host_config = false,
                     bool create_build2_config = false);

  int
  cmd_sync (cmd_sync_options&&, cli::group_scanner& args);

  // Return the list of additional (to prj, if not empty) projects that are
  // using this configuration.
  //
  dir_paths
  configuration_projects (const common_options& co,
                          const dir_path& cfg,
                          const dir_path& prj = dir_path ());

  default_options_files
  options_files (const char* cmd,
                 const cmd_sync_options&,
                 const strings& args);

  cmd_sync_options
  merge_options (const default_options<cmd_sync_options>&,
                 const cmd_sync_options&);

  // Note that the hook is installed into the bpkg-created configuration which
  // always uses the standard build file/directory naming scheme.
  //
  extern const path hook_file; // build/bootstrap/pre-bdep-sync.build
}

#endif // BDEP_SYNC_HXX
