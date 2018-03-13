// file      : bdep/new-types.hxx -*- C++ -*-
// copyright : Copyright (c) 2014-2017 Code Synthesis Ltd
// license   : MIT; see accompanying LICENSE file

#ifndef BDEP_NEW_TYPES_HXX
#define BDEP_NEW_TYPES_HXX

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

  template <typename EXE  = cmd_new_exe_options,
            typename LIB  = cmd_new_lib_options,
            typename BARE = cmd_new_bare_options>
  struct cmd_new_type_template
  {
    enum type_type {exe, lib, bare} type;

    operator type_type () const {return type;}

    union
    {
      EXE   exe_opt;
      LIB   lib_opt;
      BARE bare_opt;
    };

    // Default is bare with no options.
    //
    cmd_new_type_template (): type (bare) {bare_opt = BARE ();}
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
}

#endif // BDEP_NEW_TYPES_HXX
