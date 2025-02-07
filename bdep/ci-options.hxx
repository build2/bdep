// -*- C++ -*-
//
// This file was generated by CLI, a command line interface
// compiler for C++.
//

#ifndef BDEP_CI_OPTIONS_HXX
#define BDEP_CI_OPTIONS_HXX

// Begin prologue.
//
//
// End prologue.

#include <bdep/project-options.hxx>

#include <bdep/ci-types.hxx>

namespace bdep
{
  class cmd_ci_options: public ::bdep::project_options
  {
    public:
    cmd_ci_options ();

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
    merge (const cmd_ci_options&);

    // Option accessors and modifiers.
    //
    const bool&
    yes () const;

    bool&
    yes ();

    void
    yes (const bool&);

    const string&
    interactive () const;

    string&
    interactive ();

    void
    interactive (const string&);

    bool
    interactive_specified () const;

    void
    interactive_specified (bool);

    const url&
    server () const;

    url&
    server ();

    void
    server (const url&);

    bool
    server_specified () const;

    void
    server_specified (bool);

    const url&
    repository () const;

    url&
    repository ();

    void
    repository (const url&);

    bool
    repository_specified () const;

    void
    repository_specified (bool);

    const strings&
    override () const;

    strings&
    override ();

    void
    override (const strings&);

    bool
    override_specified () const;

    void
    override_specified (bool);

    const path&
    overrides_file () const;

    path&
    overrides_file ();

    void
    overrides_file (const path&);

    bool
    overrides_file_specified () const;

    void
    overrides_file_specified (bool);

    const strings&
    builds () const;

    strings&
    builds ();

    void
    builds (const strings&);

    bool
    builds_specified () const;

    void
    builds_specified (bool);

    const strings&
    build_config () const;

    strings&
    build_config ();

    void
    build_config (const strings&);

    bool
    build_config_specified () const;

    void
    build_config_specified (bool);

    const strings&
    target_config () const;

    strings&
    target_config ();

    void
    target_config (const strings&);

    bool
    target_config_specified () const;

    void
    target_config_specified (bool);

    const strings&
    package_config () const;

    strings&
    package_config ();

    void
    package_config (const strings&);

    bool
    package_config_specified () const;

    void
    package_config_specified (bool);

    const string&
    build_email () const;

    string&
    build_email ();

    void
    build_email (const string&);

    bool
    build_email_specified () const;

    void
    build_email_specified (bool);

    const cmd_ci_override&
    overrides () const;

    cmd_ci_override&
    overrides ();

    void
    overrides (const cmd_ci_override&);

    bool
    overrides_specified () const;

    void
    overrides_specified (bool);

    const string&
    simulate () const;

    string&
    simulate ();

    void
    simulate (const string&);

    bool
    simulate_specified () const;

    void
    simulate_specified (bool);

    const bool&
    forward () const;

    bool&
    forward ();

    void
    forward (const bool&);

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
    friend struct _cli_cmd_ci_options_desc_type;

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
    bool yes_;
    string interactive_;
    bool interactive_specified_;
    url server_;
    bool server_specified_;
    url repository_;
    bool repository_specified_;
    strings override_;
    bool override_specified_;
    path overrides_file_;
    bool overrides_file_specified_;
    strings builds_;
    bool builds_specified_;
    strings build_config_;
    bool build_config_specified_;
    strings target_config_;
    bool target_config_specified_;
    strings package_config_;
    bool package_config_specified_;
    string build_email_;
    bool build_email_specified_;
    cmd_ci_override overrides_;
    bool overrides_specified_;
    string simulate_;
    bool simulate_specified_;
    bool forward_;
  };
}

// Print page usage information.
//
namespace bdep
{
  ::bdep::cli::usage_para
  print_bdep_ci_usage (::std::ostream&,
                       ::bdep::cli::usage_para = ::bdep::cli::usage_para::none);
}

#include <bdep/ci-options.ixx>

// Begin epilogue.
//
//
// End epilogue.

#endif // BDEP_CI_OPTIONS_HXX
