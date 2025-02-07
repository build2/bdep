// -*- C++ -*-
//
// This file was generated by CLI, a command line interface
// compiler for C++.
//

#ifndef BDEP_SYNC_OPTIONS_HXX
#define BDEP_SYNC_OPTIONS_HXX

// Begin prologue.
//
//
// End prologue.

#include <bdep/project-options.hxx>

namespace bdep
{
  class cmd_sync_options: public ::bdep::project_options
  {
    public:
    cmd_sync_options ();

    // Return true if anything has been parsed.
    //
    bool
    parse (int& argc,
           char** argv,
           bool erase = false,
           ::bdep::cli::unknown_mode option = ::bdep::cli::unknown_mode::fail,
           ::bdep::cli::unknown_mode argument = ::bdep::cli::unknown_mode::stop);

    bool
    parse (int start,
           int& argc,
           char** argv,
           bool erase = false,
           ::bdep::cli::unknown_mode option = ::bdep::cli::unknown_mode::fail,
           ::bdep::cli::unknown_mode argument = ::bdep::cli::unknown_mode::stop);

    bool
    parse (int& argc,
           char** argv,
           int& end,
           bool erase = false,
           ::bdep::cli::unknown_mode option = ::bdep::cli::unknown_mode::fail,
           ::bdep::cli::unknown_mode argument = ::bdep::cli::unknown_mode::stop);

    bool
    parse (int start,
           int& argc,
           char** argv,
           int& end,
           bool erase = false,
           ::bdep::cli::unknown_mode option = ::bdep::cli::unknown_mode::fail,
           ::bdep::cli::unknown_mode argument = ::bdep::cli::unknown_mode::stop);

    bool
    parse (::bdep::cli::scanner&,
           ::bdep::cli::unknown_mode option = ::bdep::cli::unknown_mode::fail,
           ::bdep::cli::unknown_mode argument = ::bdep::cli::unknown_mode::stop);

    // Merge options from the specified instance appending/overriding
    // them as if they appeared after options in this instance.
    //
    void
    merge (const cmd_sync_options&);

    // Option accessors and modifiers.
    //
    const bool&
    upgrade () const;

    bool&
    upgrade ();

    void
    upgrade (const bool&);

    const bool&
    patch () const;

    bool&
    patch ();

    void
    patch (const bool&);

    const bool&
    immediate () const;

    bool&
    immediate ();

    void
    immediate (const bool&);

    const bool&
    recursive () const;

    bool&
    recursive ();

    void
    recursive (const bool&);

    const bool&
    yes () const;

    bool&
    yes ();

    void
    yes (const bool&);

    const bool&
    disfigure () const;

    bool&
    disfigure ();

    void
    disfigure (const bool&);

    const bool&
    fetch () const;

    bool&
    fetch ();

    void
    fetch (const bool&);

    const bool&
    fetch_full () const;

    bool&
    fetch_full ();

    void
    fetch_full (const bool&);

    const bool&
    sys_no_query () const;

    bool&
    sys_no_query ();

    void
    sys_no_query (const bool&);

    const bool&
    sys_install () const;

    bool&
    sys_install ();

    void
    sys_install (const bool&);

    const bool&
    sys_no_fetch () const;

    bool&
    sys_no_fetch ();

    void
    sys_no_fetch (const bool&);

    const bool&
    sys_no_stub () const;

    bool&
    sys_no_stub ();

    void
    sys_no_stub (const bool&);

    const bool&
    sys_yes () const;

    bool&
    sys_yes ();

    void
    sys_yes (const bool&);

    const string&
    sys_sudo () const;

    string&
    sys_sudo ();

    void
    sys_sudo (const string&);

    bool
    sys_sudo_specified () const;

    void
    sys_sudo_specified (bool);

    const bool&
    create_host_config () const;

    bool&
    create_host_config ();

    void
    create_host_config (const bool&);

    const bool&
    create_build2_config () const;

    bool&
    create_build2_config ();

    void
    create_build2_config (const bool&);

    const bool&
    implicit () const;

    bool&
    implicit ();

    void
    implicit (const bool&);

    const uint16_t&
    hook () const;

    uint16_t&
    hook ();

    void
    hook (const uint16_t&);

    bool
    hook_specified () const;

    void
    hook_specified (bool);

    // Print usage information.
    //
    static ::bdep::cli::usage_para
    print_usage (::std::ostream&,
                 ::bdep::cli::usage_para = ::bdep::cli::usage_para::none);

    // Option description.
    //
    static const ::bdep::cli::options&
    description ();

    // Implementation details.
    //
    protected:
    friend struct _cli_cmd_sync_options_desc_type;

    static void
    fill (::bdep::cli::options&);

    bool
    _parse (const char*, ::bdep::cli::scanner&);

    private:
    bool
    _parse (::bdep::cli::scanner&,
            ::bdep::cli::unknown_mode option,
            ::bdep::cli::unknown_mode argument);

    public:
    bool upgrade_;
    bool patch_;
    bool immediate_;
    bool recursive_;
    bool yes_;
    bool disfigure_;
    bool fetch_;
    bool fetch_full_;
    bool sys_no_query_;
    bool sys_install_;
    bool sys_no_fetch_;
    bool sys_no_stub_;
    bool sys_yes_;
    string sys_sudo_;
    bool sys_sudo_specified_;
    bool create_host_config_;
    bool create_build2_config_;
    bool implicit_;
    uint16_t hook_;
    bool hook_specified_;
  };

  class cmd_sync_pkg_options
  {
    public:
    cmd_sync_pkg_options ();

    // Return true if anything has been parsed.
    //
    bool
    parse (int& argc,
           char** argv,
           bool erase = false,
           ::bdep::cli::unknown_mode option = ::bdep::cli::unknown_mode::fail,
           ::bdep::cli::unknown_mode argument = ::bdep::cli::unknown_mode::stop);

    bool
    parse (int start,
           int& argc,
           char** argv,
           bool erase = false,
           ::bdep::cli::unknown_mode option = ::bdep::cli::unknown_mode::fail,
           ::bdep::cli::unknown_mode argument = ::bdep::cli::unknown_mode::stop);

    bool
    parse (int& argc,
           char** argv,
           int& end,
           bool erase = false,
           ::bdep::cli::unknown_mode option = ::bdep::cli::unknown_mode::fail,
           ::bdep::cli::unknown_mode argument = ::bdep::cli::unknown_mode::stop);

    bool
    parse (int start,
           int& argc,
           char** argv,
           int& end,
           bool erase = false,
           ::bdep::cli::unknown_mode option = ::bdep::cli::unknown_mode::fail,
           ::bdep::cli::unknown_mode argument = ::bdep::cli::unknown_mode::stop);

    bool
    parse (::bdep::cli::scanner&,
           ::bdep::cli::unknown_mode option = ::bdep::cli::unknown_mode::fail,
           ::bdep::cli::unknown_mode argument = ::bdep::cli::unknown_mode::stop);

    // Merge options from the specified instance appending/overriding
    // them as if they appeared after options in this instance.
    //
    void
    merge (const cmd_sync_pkg_options&);

    // Option accessors and modifiers.
    //
    const vector<string>&
    config_name () const;

    vector<string>&
    config_name ();

    void
    config_name (const vector<string>&);

    bool
    config_name_specified () const;

    void
    config_name_specified (bool);

    const vector<uint64_t>&
    config_id () const;

    vector<uint64_t>&
    config_id ();

    void
    config_id (const vector<uint64_t>&);

    bool
    config_id_specified () const;

    void
    config_id_specified (bool);

    const vector<dir_path>&
    config () const;

    vector<dir_path>&
    config ();

    void
    config (const vector<dir_path>&);

    bool
    config_specified () const;

    void
    config_specified (bool);

    // Print usage information.
    //
    static ::bdep::cli::usage_para
    print_usage (::std::ostream&,
                 ::bdep::cli::usage_para = ::bdep::cli::usage_para::none);

    // Option description.
    //
    static const ::bdep::cli::options&
    description ();

    // Implementation details.
    //
    protected:
    friend struct _cli_cmd_sync_pkg_options_desc_type;

    static void
    fill (::bdep::cli::options&);

    bool
    _parse (const char*, ::bdep::cli::scanner&);

    private:
    bool
    _parse (::bdep::cli::scanner&,
            ::bdep::cli::unknown_mode option,
            ::bdep::cli::unknown_mode argument);

    public:
    vector<string> config_name_;
    bool config_name_specified_;
    vector<uint64_t> config_id_;
    bool config_id_specified_;
    vector<dir_path> config_;
    bool config_specified_;
  };
}

// Print page usage information.
//
namespace bdep
{
  ::bdep::cli::usage_para
  print_bdep_sync_usage (::std::ostream&,
                         ::bdep::cli::usage_para = ::bdep::cli::usage_para::none);
}

#include <bdep/sync-options.ixx>

// Begin epilogue.
//
//
// End epilogue.

#endif // BDEP_SYNC_OPTIONS_HXX
