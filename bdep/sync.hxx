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
  void
  cmd_sync (const common_options&,
            const dir_path& prj,
            const shared_ptr<configuration>&,
            const strings& pkg_args,
            bool implicit,
            bool fetch = true,
            bool yes = true,
            bool name_cfg = false);

  // As above but perform an implicit sync without a configuration object
  // (i.e., as if from the hook).
  //
  void
  cmd_sync_implicit (const common_options&,
                     const dir_path& cfg,
                     bool fetch = true,
                     bool yes = true,
                     bool name_cfg = true);

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
