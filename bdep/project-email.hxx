// file      : bdep/project-email.hxx -*- C++ -*-
// copyright : Copyright (c) 2014-2018 Code Synthesis Ltd
// license   : MIT; see accompanying LICENSE file

#ifndef BDEP_PROJECT_EMAIL_HXX
#define BDEP_PROJECT_EMAIL_HXX

#include <bdep/types.hxx>
#include <bdep/utility.hxx>

namespace bdep
{
  // Given a project directly, try to discover the author's email address
  // using the environment and, if project looks like a repository, its VCS.
  //
  // Note: if using this function in a command, don't forget to update its
  // ENVIRONMENT section to mention BDEP_EMAIL.
  //
  optional<string>
  project_email (const dir_path&);
}

#endif // BDEP_PROJECT_EMAIL_HXX
