// -*- C++ -*-
//
// This file was generated by CLI, a command line interface
// compiler for C++.
//

#ifndef BDEP_NEW_OPTIONS_HXX
#define BDEP_NEW_OPTIONS_HXX

// Begin prologue.
//
//
// End prologue.

#include <bdep/project-options.hxx>

#include <bdep/new-types.hxx>

namespace bdep
{
  class cmd_new_c_options
  {
    public:
    cmd_new_c_options ();

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
    merge (const cmd_new_c_options&);

    // Option accessors and modifiers.
    //
    const bool&
    cpp () const;

    bool&
    cpp ();

    void
    cpp (const bool&);

    const string&
    hxx () const;

    string&
    hxx ();

    void
    hxx (const string&);

    bool
    hxx_specified () const;

    void
    hxx_specified (bool);

    const string&
    cxx () const;

    string&
    cxx ();

    void
    cxx (const string&);

    bool
    cxx_specified () const;

    void
    cxx_specified (bool);

    const string&
    ixx () const;

    string&
    ixx ();

    void
    ixx (const string&);

    bool
    ixx_specified () const;

    void
    ixx_specified (bool);

    const string&
    txx () const;

    string&
    txx ();

    void
    txx (const string&);

    bool
    txx_specified () const;

    void
    txx_specified (bool);

    const string&
    mxx () const;

    string&
    mxx ();

    void
    mxx (const string&);

    bool
    mxx_specified () const;

    void
    mxx_specified (bool);

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
    friend struct _cli_cmd_new_c_options_desc_type;

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
    bool cpp_;
    string hxx_;
    bool hxx_specified_;
    string cxx_;
    bool cxx_specified_;
    string ixx_;
    bool ixx_specified_;
    string txx_;
    bool txx_specified_;
    string mxx_;
    bool mxx_specified_;
  };

  class cmd_new_cxx_options
  {
    public:
    cmd_new_cxx_options ();

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
    merge (const cmd_new_cxx_options&);

    // Option accessors and modifiers.
    //
    const bool&
    cpp () const;

    bool&
    cpp ();

    void
    cpp (const bool&);

    const string&
    extension () const;

    string&
    extension ();

    void
    extension (const string&);

    bool
    extension_specified () const;

    void
    extension_specified (bool);

    const string&
    hxx () const;

    string&
    hxx ();

    void
    hxx (const string&);

    bool
    hxx_specified () const;

    void
    hxx_specified (bool);

    const string&
    cxx () const;

    string&
    cxx ();

    void
    cxx (const string&);

    bool
    cxx_specified () const;

    void
    cxx_specified (bool);

    const string&
    ixx () const;

    string&
    ixx ();

    void
    ixx (const string&);

    bool
    ixx_specified () const;

    void
    ixx_specified (bool);

    const string&
    txx () const;

    string&
    txx ();

    void
    txx (const string&);

    bool
    txx_specified () const;

    void
    txx_specified (bool);

    const string&
    mxx () const;

    string&
    mxx ();

    void
    mxx (const string&);

    bool
    mxx_specified () const;

    void
    mxx_specified (bool);

    const bool&
    c () const;

    bool&
    c ();

    void
    c (const bool&);

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
    friend struct _cli_cmd_new_cxx_options_desc_type;

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
    bool cpp_;
    string extension_;
    bool extension_specified_;
    string hxx_;
    bool hxx_specified_;
    string cxx_;
    bool cxx_specified_;
    string ixx_;
    bool ixx_specified_;
    string txx_;
    bool txx_specified_;
    string mxx_;
    bool mxx_specified_;
    bool c_;
  };

  class cmd_new_exe_options
  {
    public:
    cmd_new_exe_options ();

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
    merge (const cmd_new_exe_options&);

    // Option accessors and modifiers.
    //
    const bool&
    no_tests () const;

    bool&
    no_tests ();

    void
    no_tests (const bool&);

    const bool&
    unit_tests () const;

    bool&
    unit_tests ();

    void
    unit_tests (const bool&);

    const bool&
    no_install () const;

    bool&
    no_install ();

    void
    no_install (const bool&);

    const bool&
    export_stub () const;

    bool&
    export_stub ();

    void
    export_stub (const bool&);

    const dir_path&
    prefix () const;

    dir_path&
    prefix ();

    void
    prefix (const dir_path&);

    bool
    prefix_specified () const;

    void
    prefix_specified (bool);

    const dir_path&
    subdir () const;

    dir_path&
    subdir ();

    void
    subdir (const dir_path&);

    bool
    subdir_specified () const;

    void
    subdir_specified (bool);

    const bool&
    no_subdir () const;

    bool&
    no_subdir ();

    void
    no_subdir (const bool&);

    const bool&
    buildfile_in_prefix () const;

    bool&
    buildfile_in_prefix ();

    void
    buildfile_in_prefix (const bool&);

    const bool&
    third_party () const;

    bool&
    third_party ();

    void
    third_party (const bool&);

    const string&
    license () const;

    string&
    license ();

    void
    license (const string&);

    bool
    license_specified () const;

    void
    license_specified (bool);

    const bool&
    no_readme () const;

    bool&
    no_readme ();

    void
    no_readme (const bool&);

    const bool&
    no_package_readme () const;

    bool&
    no_package_readme ();

    void
    no_package_readme (const bool&);

    const bool&
    alt_naming () const;

    bool&
    alt_naming ();

    void
    alt_naming (const bool&);

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
    friend struct _cli_cmd_new_exe_options_desc_type;

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
    bool no_tests_;
    bool unit_tests_;
    bool no_install_;
    bool export_stub_;
    dir_path prefix_;
    bool prefix_specified_;
    dir_path subdir_;
    bool subdir_specified_;
    bool no_subdir_;
    bool buildfile_in_prefix_;
    bool third_party_;
    string license_;
    bool license_specified_;
    bool no_readme_;
    bool no_package_readme_;
    bool alt_naming_;
  };

  class cmd_new_lib_options
  {
    public:
    cmd_new_lib_options ();

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
    merge (const cmd_new_lib_options&);

    // Option accessors and modifiers.
    //
    const bool&
    binless () const;

    bool&
    binless ();

    void
    binless (const bool&);

    const bool&
    no_tests () const;

    bool&
    no_tests ();

    void
    no_tests (const bool&);

    const bool&
    unit_tests () const;

    bool&
    unit_tests ();

    void
    unit_tests (const bool&);

    const bool&
    no_install () const;

    bool&
    no_install ();

    void
    no_install (const bool&);

    const bool&
    no_version () const;

    bool&
    no_version ();

    void
    no_version (const bool&);

    const bool&
    no_symexport () const;

    bool&
    no_symexport ();

    void
    no_symexport (const bool&);

    const bool&
    auto_symexport () const;

    bool&
    auto_symexport ();

    void
    auto_symexport (const bool&);

    const dir_path&
    prefix_include () const;

    dir_path&
    prefix_include ();

    void
    prefix_include (const dir_path&);

    bool
    prefix_include_specified () const;

    void
    prefix_include_specified (bool);

    const dir_path&
    prefix_source () const;

    dir_path&
    prefix_source ();

    void
    prefix_source (const dir_path&);

    bool
    prefix_source_specified () const;

    void
    prefix_source_specified (bool);

    const dir_path&
    prefix () const;

    dir_path&
    prefix ();

    void
    prefix (const dir_path&);

    bool
    prefix_specified () const;

    void
    prefix_specified (bool);

    const bool&
    split () const;

    bool&
    split ();

    void
    split (const bool&);

    const dir_path&
    subdir () const;

    dir_path&
    subdir ();

    void
    subdir (const dir_path&);

    bool
    subdir_specified () const;

    void
    subdir_specified (bool);

    const bool&
    no_subdir_include () const;

    bool&
    no_subdir_include ();

    void
    no_subdir_include (const bool&);

    const bool&
    no_subdir_source () const;

    bool&
    no_subdir_source ();

    void
    no_subdir_source (const bool&);

    const bool&
    no_subdir () const;

    bool&
    no_subdir ();

    void
    no_subdir (const bool&);

    const bool&
    buildfile_in_prefix () const;

    bool&
    buildfile_in_prefix ();

    void
    buildfile_in_prefix (const bool&);

    const bool&
    third_party () const;

    bool&
    third_party ();

    void
    third_party (const bool&);

    const string&
    license () const;

    string&
    license ();

    void
    license (const string&);

    bool
    license_specified () const;

    void
    license_specified (bool);

    const bool&
    no_readme () const;

    bool&
    no_readme ();

    void
    no_readme (const bool&);

    const bool&
    no_package_readme () const;

    bool&
    no_package_readme ();

    void
    no_package_readme (const bool&);

    const bool&
    alt_naming () const;

    bool&
    alt_naming ();

    void
    alt_naming (const bool&);

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
    friend struct _cli_cmd_new_lib_options_desc_type;

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
    bool binless_;
    bool no_tests_;
    bool unit_tests_;
    bool no_install_;
    bool no_version_;
    bool no_symexport_;
    bool auto_symexport_;
    dir_path prefix_include_;
    bool prefix_include_specified_;
    dir_path prefix_source_;
    bool prefix_source_specified_;
    dir_path prefix_;
    bool prefix_specified_;
    bool split_;
    dir_path subdir_;
    bool subdir_specified_;
    bool no_subdir_include_;
    bool no_subdir_source_;
    bool no_subdir_;
    bool buildfile_in_prefix_;
    bool third_party_;
    string license_;
    bool license_specified_;
    bool no_readme_;
    bool no_package_readme_;
    bool alt_naming_;
  };

  class cmd_new_bare_options
  {
    public:
    cmd_new_bare_options ();

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
    merge (const cmd_new_bare_options&);

    // Option accessors and modifiers.
    //
    const bool&
    no_tests () const;

    bool&
    no_tests ();

    void
    no_tests (const bool&);

    const bool&
    no_install () const;

    bool&
    no_install ();

    void
    no_install (const bool&);

    const string&
    license () const;

    string&
    license ();

    void
    license (const string&);

    bool
    license_specified () const;

    void
    license_specified (bool);

    const bool&
    no_readme () const;

    bool&
    no_readme ();

    void
    no_readme (const bool&);

    const bool&
    alt_naming () const;

    bool&
    alt_naming ();

    void
    alt_naming (const bool&);

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
    friend struct _cli_cmd_new_bare_options_desc_type;

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
    bool no_tests_;
    bool no_install_;
    string license_;
    bool license_specified_;
    bool no_readme_;
    bool alt_naming_;
  };

  class cmd_new_empty_options
  {
    public:
    cmd_new_empty_options ();

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
    merge (const cmd_new_empty_options&);

    // Option accessors and modifiers.
    //
    const bool&
    third_party () const;

    bool&
    third_party ();

    void
    third_party (const bool&);

    const bool&
    no_readme () const;

    bool&
    no_readme ();

    void
    no_readme (const bool&);

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
    friend struct _cli_cmd_new_empty_options_desc_type;

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
    bool third_party_;
    bool no_readme_;
  };

  class cmd_new_git_options
  {
    public:
    cmd_new_git_options ();

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
    merge (const cmd_new_git_options&);

    // Option accessors and modifiers.
    //
    const string&
    branch () const;

    string&
    branch ();

    void
    branch (const string&);

    bool
    branch_specified () const;

    void
    branch_specified (bool);

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
    friend struct _cli_cmd_new_git_options_desc_type;

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
    string branch_;
    bool branch_specified_;
  };

  class cmd_new_none_options
  {
    public:
    cmd_new_none_options ();

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
    merge (const cmd_new_none_options&);

    // Option accessors and modifiers.
    //
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
    friend struct _cli_cmd_new_none_options_desc_type;

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
  };

  class cmd_new_options: public ::bdep::configuration_add_options,
    public ::bdep::configuration_name_options
  {
    public:
    cmd_new_options ();

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
    merge (const cmd_new_options&);

    // Option accessors and modifiers.
    //
    const bool&
    no_init () const;

    bool&
    no_init ();

    void
    no_init (const bool&);

    const bool&
    package () const;

    bool&
    package ();

    void
    package (const bool&);

    const bool&
    source () const;

    bool&
    source ();

    void
    source (const bool&);

    const dir_path&
    output_dir () const;

    dir_path&
    output_dir ();

    void
    output_dir (const dir_path&);

    bool
    output_dir_specified () const;

    void
    output_dir_specified (bool);

    const dir_path&
    directory () const;

    dir_path&
    directory ();

    void
    directory (const dir_path&);

    bool
    directory_specified () const;

    void
    directory_specified (bool);

    const cmd_new_type&
    type () const;

    cmd_new_type&
    type ();

    void
    type (const cmd_new_type&);

    bool
    type_specified () const;

    void
    type_specified (bool);

    const cmd_new_lang&
    lang () const;

    cmd_new_lang&
    lang ();

    void
    lang (const cmd_new_lang&);

    bool
    lang_specified () const;

    void
    lang_specified (bool);

    const cmd_new_vcs&
    vcs () const;

    cmd_new_vcs&
    vcs ();

    void
    vcs (const cmd_new_vcs&);

    bool
    vcs_specified () const;

    void
    vcs_specified (bool);

    const strings&
    pre_hook () const;

    strings&
    pre_hook ();

    void
    pre_hook (const strings&);

    bool
    pre_hook_specified () const;

    void
    pre_hook_specified (bool);

    const strings&
    post_hook () const;

    strings&
    post_hook ();

    void
    post_hook (const strings&);

    bool
    post_hook_specified () const;

    void
    post_hook_specified (bool);

    const bool&
    no_amalgamation () const;

    bool&
    no_amalgamation ();

    void
    no_amalgamation (const bool&);

    const bool&
    no_checks () const;

    bool&
    no_checks ();

    void
    no_checks (const bool&);

    const dir_path&
    config_add () const;

    dir_path&
    config_add ();

    void
    config_add (const dir_path&);

    bool
    config_add_specified () const;

    void
    config_add_specified (bool);

    const dir_path&
    config_create () const;

    dir_path&
    config_create ();

    void
    config_create (const dir_path&);

    bool
    config_create_specified () const;

    void
    config_create_specified (bool);

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
    friend struct _cli_cmd_new_options_desc_type;

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
    bool no_init_;
    bool package_;
    bool source_;
    dir_path output_dir_;
    bool output_dir_specified_;
    dir_path directory_;
    bool directory_specified_;
    cmd_new_type type_;
    bool type_specified_;
    cmd_new_lang lang_;
    bool lang_specified_;
    cmd_new_vcs vcs_;
    bool vcs_specified_;
    strings pre_hook_;
    bool pre_hook_specified_;
    strings post_hook_;
    bool post_hook_specified_;
    bool no_amalgamation_;
    bool no_checks_;
    dir_path config_add_;
    bool config_add_specified_;
    dir_path config_create_;
    bool config_create_specified_;
  };
}

// Print page usage information.
//
namespace bdep
{
  ::bdep::cli::usage_para
  print_bdep_new_usage (::std::ostream&,
                        ::bdep::cli::usage_para = ::bdep::cli::usage_para::none);
}

#include <bdep/new-options.ixx>

// Begin epilogue.
//
//
// End epilogue.

#endif // BDEP_NEW_OPTIONS_HXX
