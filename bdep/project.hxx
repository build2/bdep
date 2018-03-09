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
  using optional_string = optional<string>;
  using optional_dir_path = optional<dir_path>;

  #pragma db map type(dir_path) as(string) \
    to((?).string ()) from(bdep::dir_path (?))

  #pragma db map type(optional_dir_path) as(bdep::optional_string) \
    to((?) ? (?)->string () : bdep::optional_string ())            \
    from((?) ? bdep::dir_path (*(?)) : bdep::optional_dir_path ())

  //@@ TODO: do we need session/shared_ptr?

  #pragma db object pointer(shared_ptr) session
  class configuration
  {
  public:

    // The configuration stores an absolute and normalized path to the bpkg
    // configuration. It may also have a name. A configuration can be moved or
    // renamed so we also have the id (it is NULL'able so that we can assign
    // fixed configuration ids, for example, in configurations.manifest).
    //
    // We also store a version of the configuration path relative to the
    // project directory so that we can automatically handle moving of the
    // project and its configurations as a bundle (note that the dir:
    // repository path in the configuration will have to be adjust as well).
    // Since it is not always possible to derive a relative path, it is
    // optional.
    //
    optional<uint64_t> id;
    optional<string>   name;
    dir_path           path;
    optional<dir_path> relative_path;

    bool default_;

    // Database mapping.
    //
    #pragma db member(id) id auto
    #pragma db member(name) unique
    #pragma db member(path) unique

    // Make path comparison case-insensitive for certain platforms.
    //
    // It would have been nice to do something like this but we can't: the
    // options are stored in the changelog and this will render changelog
    // platform-specific. So instead we tweak the scheme at runtime after
    // creating the database.
    //
    // #ifdef _WIN32
    //   #pragma db member(path)          options("COLLATE NOCASE")
    //   #pragma db member(relative_path) options("COLLATE NOCASE")
    // #endif

  private:
    friend class odb::access;
    configuration () = default;
  };

  #pragma db view object(configuration)
  struct configuration_count
  {
    #pragma db column("COUNT(*)")
    size_t result;

    operator size_t () const {return result;}
  };

  // Given the project directory, database, and options resolve all the
  // mentioned configurations or find the default configuration if none were
  // mentioned.
  //
  using configurations = vector<shared_ptr<configuration>>;

  configurations
  find_configurations (const dir_path& prj,
                       transaction&,
                       const project_options&);

  // Given the project options (and CWD) locate the packages and their
  // project. The result is the absolute and normalized project directory and
  // a vector of relative (to the project directory) package directories
  // (which will be empty if ignore_packages is true).
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
