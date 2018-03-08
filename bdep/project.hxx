// file      : bdep/project.hxx -*- C++ -*-
// copyright : Copyright (c) 2014-2017 Code Synthesis Ltd
// license   : MIT; see accompanying LICENSE file

#ifndef BDEP_PROJECT_HXX
#define BDEP_PROJECT_HXX

#include <bdep/types.hxx>
#include <bdep/utility.hxx>

#include <bdep/project-options.hxx>

namespace bdep
{
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
