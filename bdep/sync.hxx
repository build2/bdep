// file      : bdep/sync.hxx -*- C++ -*-
// copyright : Copyright (c) 2014-2017 Code Synthesis Ltd
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

  int
  cmd_sync (cmd_sync_options&&, cli::group_scanner& args);
}

#endif // BDEP_SYNC_HXX
