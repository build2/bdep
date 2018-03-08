// file      : bdep/project.hxx -*- C++ -*-
// copyright : Copyright (c) 2014-2017 Code Synthesis Ltd
// license   : MIT; see accompanying LICENSE file

#ifndef BDEP_PROJECT_HXX
#define BDEP_PROJECT_HXX

#include <odb/core.hxx>

#include <bdep/types.hxx>
#include <bdep/utility.hxx>

#include <bdep/project-options.hxx>

#pragma db model version(1, 1, open)

// Prevent assert() macro expansion in get/set expressions. This should appear
// after all #include directives since the assert() macro is redefined in each
// <assert.h> inclusion.
//
#ifdef ODB_COMPILER
#  undef assert
#  define assert assert
void assert (int);
#endif

namespace bdep
{
  #pragma db map type(dir_path) as(string) \
    to((?).string ()) from(bdep::dir_path (?))

  //@@ TODO: do we need session/shared_ptr?

  #pragma db object pointer(shared_ptr) session
  class configuration
  {
  public:

    dir_path path; // @@ TODO: relative or absolute?
    string name;

    // Database mapping.
    //
    #pragma db member(name) id
    //#pragma db member(name) index

  private:
    friend class odb::access;
    configuration () = default;
  };

  // Given project_options (and CWD) locate the packages and their project.
  // The result is the absolute and normalized project directory and a vector
  // of relative (to the project directory) package directories (which will be
  // empty if ignore_packages is true).
  //
  // Note that if the package directory is the same as project, then the
  // package directory will be empty (and not ./).
  //
  struct project_packages
  {
    dir_path  project;
    dir_paths packages;
  };

  project_packages
  find_project_packages (const project_options&, bool ignore_packages = false);
}

#endif // BDEP_PROJECT_HXX
