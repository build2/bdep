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
  void
  cmd_sync (const common_options&,
            const dir_path& prj,
            const shared_ptr<configuration>&);

  int
  cmd_sync (const cmd_sync_options&, cli::scanner& args);
}

#endif // BDEP_SYNC_HXX
