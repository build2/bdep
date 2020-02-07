// file      : bdep/project-author.hxx -*- C++ -*-
// license   : MIT; see accompanying LICENSE file

#ifndef BDEP_PROJECT_AUTHOR_HXX
#define BDEP_PROJECT_AUTHOR_HXX

#include <bdep/types.hxx>
#include <bdep/utility.hxx>

namespace bdep
{
  // Given a project directly, try to discover the author's name/email address
  // using the environment and, if project looks like a repository, its VCS.
  //
  // Note: if using this function in a command, don't forget to update its
  // ENVIRONMENT section to mention BDEP_AUTHOR_{NAME,EMAIL}.
  //
  struct project_author
  {
    optional<string> name;
    optional<string> email;
  };

  project_author
  find_project_author (const dir_path&);

  inline optional<string>
  find_project_author_name (const dir_path& d)
  {
    return find_project_author (d).name;
  }

  inline optional<string>
  find_project_author_email (const dir_path& d)
  {
    return find_project_author (d).email;
  }
}

#endif // BDEP_PROJECT_AUTHOR_HXX
