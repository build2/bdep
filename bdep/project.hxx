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

  // State of a package in a configuration.
  //
  // Pretty much everything about the package can change (including location
  // within the project) with the only immutable data being its name (even the
  // version in the manifest could be bogus without the fix-up). As a result,
  // that't the only thing we store in the database with everything else (like
  // location) always loaded from the manifest on each run. We may, however,
  // need to store some additional state (like manifest hash to speed up noop
  // syncs) in the future.
  //
  #pragma db value
  struct package_state
  {
    string name;
  };

  // Configuration associated with a project.
  //
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

    // We made it a vector instead of set/map since we are unlikely to have
    // more than a handful of packages. We may, however, want to use a change-
    // tracking vector later (e.g., to optimize updates).
    //
    vector<package_state> packages;

    // Database mapping.
    //
    #pragma db member(id) id auto
    #pragma db member(name) unique
    #pragma db member(path) unique
    #pragma db member(packages) value_column("")

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

  // Print configuration name if available and path otherwise.
  //
  inline ostream&
  operator<< (ostream& os, const configuration& c)
  {
    if (c.name)
      os << '@' << *c.name;
    else
      os << c.path;

    return os;
  }

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
  // project. The result is an absolute and normalized project directory and a
  // vector of relative (to the project directory) package locations.
  //
  // If ignore_packages is true then ignore packages in case the resulting
  // vector will be empty. Otherwise, if load_packages is false, then don't
  // load all the available packages from packages.manifest if none were
  // found/specified.
  //
  // Note that if the package directory is the same as project, then the
  // package path will be empty (and not ./).
  //
  struct package_location
  {
    string   name;
    dir_path path;
  };

  using package_locations = vector<package_location>;

  struct project_packages
  {
    dir_path          project;
    package_locations packages;
  };

  project_packages
  find_project_packages (const project_options&,
                         bool ignore_packages = false,
                         bool load_packages = true);
}

#endif // BDEP_PROJECT_HXX
