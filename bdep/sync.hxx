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
  // A RAII class that restores the original value of the BDEP_SYNCED_CONFIGS
  // environment variable on destruction.
  //
  class synced_configs_guard
  {
  public:
    optional<optional<string>> original; // Set to absent to disarm.

    synced_configs_guard () = default;
    synced_configs_guard (optional<string> o): original (move (o)) {}
    ~synced_configs_guard ();

    synced_configs_guard (synced_configs_guard&& r) noexcept
        : original (move (r.original))
    {
      r.original = nullopt;
    }

    synced_configs_guard (const synced_configs_guard&) = delete;

    synced_configs_guard& operator= (synced_configs_guard&&) = delete;
    synced_configs_guard& operator= (const synced_configs_guard&) = delete;
  };

  // The optional pkg_args are the additional dependency packages and/or
  // configuration variables to pass to bpkg-pkg-build (see bdep-init).
  //
  // If the origin project packages (prj_pkgs) are specified, then non-global
  // configuration variables are only applied to these packages.
  //
  // If fetch_full is not nullopt, perform a fetch of the project repository,
  // shallow if false and full if true. If yes is false, then don't suppress
  // bpkg prompts. If name_cfg is true then include the configuration
  // name/directory into progress.
  //
  // Before automatically creating a configuration for a build-time dependency
  // and associating it with the project(s), the user is prompted unless the
  // respective create_*_config argument is true.
  //
  // If the transaction is already started on the project's database, then it
  // must also be passed to the function along with a pointer to the
  // configuration paths/types list. If the function will be associating
  // build-time dependency configurations with the project, it will do it as
  // part of this transaction and will also save the created dependency
  // configurations into this list. If this transaction is rolled back for any
  // reason (for example, because the failed exception is thrown by cmd_sync()
  // call), then the caller must start the new transaction and re-associate
  // the created configurations with the project using the configuration types
  // also as their names.
  //
  struct sys_options
  {
    bool no_query = false;
    bool install  = false;
    bool no_fetch = false;
    bool no_stub  = false;
    bool yes      = false;
    optional<string> sudo;

    sys_options () = default;

    template <typename O>
    explicit
    sys_options (const O& o)
        : no_query (o.sys_no_query ()),
          install (o.sys_install ()),
          no_fetch (o.sys_no_fetch ()),
          no_stub (o.sys_no_stub ()),
          yes (o.sys_yes ()),
          sudo (o.sys_sudo_specified ()
                ? o.sys_sudo ()
                : optional<string> ()) {}
  };

  synced_configs_guard
  cmd_sync (const common_options&,
            const dir_path& prj,
            const shared_ptr<configuration>&,
            bool implicit,
            const strings& pkg_args = strings (),
            optional<bool> fetch_full = false, // Shallow fetch.
            bool yes = true,
            bool name_cfg = false,
            const package_locations& prj_pkgs = {},
            const sys_options& = sys_options (),
            bool create_host_config = false,
            bool create_build2_config = false,
            transaction* = nullptr,
            vector<pair<dir_path, string>>* created_cfgs = nullptr);

  // As above but sync multiple configurations. If some configurations belong
  // to the same cluster, then they are synced at once.
  //
  // Note: in the rest of cmd_sync() overloads, fetch is bool, with false
  // meaning do not fetch and true -- fetch shallow.
  //
  void
  cmd_sync (const common_options&,
            const dir_path& prj,
            const configurations&,
            bool implicit,
            const strings& pkg_args = strings (),
            bool fetch = true,
            bool yes = true,
            bool name_cfg = false,
            const package_locations& prj_pkgs = {},
            const sys_options& = sys_options (),
            bool create_host_config = false,
            bool create_build2_config = false);

  // As above but perform an implicit sync without a configuration object
  // (i.e., as if from the hook).
  //
  synced_configs_guard
  cmd_sync_implicit (const common_options&,
                     const dir_path& cfg,
                     bool fetch = true,
                     bool yes = true,
                     bool name_cfg = true,
                     const sys_options& = sys_options (),
                     bool create_host_config = false,
                     bool create_build2_config = false);

  // As above but sync multiple configurations. If some configurations belong
  // to the same cluster, then they are synced at once.
  //
  void
  cmd_sync_implicit (const common_options&,
                     const dir_paths& cfgs,
                     bool fetch = true,
                     bool yes = true,
                     bool name_cfg = true,
                     const sys_options& = sys_options (),
                     bool create_host_config = false,
                     bool create_build2_config = false);

  // As above but deinitialize the specified project packages in the specified
  // configuration instead of upgrading them.
  //
  // Specifically, in bpkg terms, unhold and deorphan these packages in the
  // specified configuration with their project directory repository being
  // masked.
  //
  // Note that it is not very likely but still possible that the
  // deinitialization of a package may end up with associating new
  // configurations with the project. Think of deorphaning a package, which
  // has been replaced with another version that got some new build-time
  // dependency.
  //
  void
  cmd_sync_deinit (const common_options&,
                   const dir_path& prj,
                   const shared_ptr<configuration>&,
                   const strings& pkgs,
                   transaction* = nullptr,
                   vector<pair<dir_path, string>>* created_cfgs = nullptr);

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
