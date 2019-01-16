// file      : bdep/new-types.hxx -*- C++ -*-
// copyright : Copyright (c) 2014-2019 Code Synthesis Ltd
// license   : MIT; see accompanying LICENSE file

#ifndef BDEP_NEW_TYPES_HXX
#define BDEP_NEW_TYPES_HXX

#include <bdep/types.hxx>

namespace bdep
{
  // We could have defined cmd_new_*_options in a separate .cli file, include
  // that here, and so on. Or we can abuse templates and keep everything
  // together.

  // --type
  //
  class cmd_new_exe_options;
  class cmd_new_lib_options;
  class cmd_new_bare_options;
  class cmd_new_empty_options;

  template <typename EXE   = cmd_new_exe_options,
            typename LIB   = cmd_new_lib_options,
            typename BARE  = cmd_new_bare_options,
            typename EMPTY = cmd_new_empty_options>
  struct cmd_new_type_template
  {
    enum type_type {exe, lib, bare, empty} type;

    operator type_type () const {return type;}

    union
    {
      EXE   exe_opt;
      LIB   lib_opt;
      BARE  bare_opt;
      EMPTY empty_opt;
    };

    // Default is exe with no options.
    //
    cmd_new_type_template (): type (exe) {exe_opt = EXE ();}

    friend ostream&
    operator<< (ostream& os, const cmd_new_type_template& t)
    {
      using type = cmd_new_type_template;

      switch (t)
      {
      case type::exe:   return os << "executable";
      case type::lib:   return os << "library";
      case type::bare:  return os << "bare";
      case type::empty: return os << "empty";
      }

      return os;
    }
  };

  using cmd_new_type = cmd_new_type_template<>;

  // --lang
  //
  class cmd_new_c_options;
  class cmd_new_cxx_options;

  template <typename C   = cmd_new_c_options,
            typename CXX = cmd_new_cxx_options>
  struct cmd_new_lang_template
  {
    enum lang_type {c, cxx} lang;

    operator lang_type () const {return lang;}

    union
    {
      C     c_opt;
      CXX cxx_opt;
    };

    // Default is C++ with no options.
    //
    cmd_new_lang_template (): lang (cxx) {cxx_opt = CXX ();}
  };

  using cmd_new_lang = cmd_new_lang_template<>;

  // --vcs
  //
  class cmd_new_git_options;
  class cmd_new_none_options;

  template <typename GIT  = cmd_new_git_options,
            typename NONE = cmd_new_none_options>
  struct cmd_new_vcs_template
  {
    enum vcs_type {git, none} vcs;

    operator vcs_type () const {return vcs;}

    union
    {
      GIT  git_opt;
      NONE none_opt;
    };

    // Default is git with no options.
    //
    cmd_new_vcs_template (): vcs (git) {git_opt = GIT ();}
  };

  using cmd_new_vcs = cmd_new_vcs_template<>;
}

#endif // BDEP_NEW_TYPES_HXX
