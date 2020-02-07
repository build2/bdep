// file      : bdep/fetch.hxx -*- C++ -*-
// license   : MIT; see accompanying LICENSE file

#ifndef BDEP_FETCH_HXX
#define BDEP_FETCH_HXX

#include <bdep/types.hxx>
#include <bdep/utility.hxx>

#include <bdep/project.hxx>
#include <bdep/fetch-options.hxx>

namespace bdep
{
  void
  cmd_fetch (const common_options&,
             const dir_path& prj,
             const shared_ptr<configuration>&,
             bool full);

  int
  cmd_fetch (const cmd_fetch_options&, cli::scanner& args);
}

#endif // BDEP_FETCH_HXX
