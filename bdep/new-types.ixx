// file      : bdep/new-types.ixx -*- C++ -*-
// copyright : Copyright (c) 2014-2019 Code Synthesis Ltd
// license   : MIT; see accompanying LICENSE file

#include <bdep/utility.hxx> // move()

namespace bdep
{
  template <typename C, typename CXX>
  inline cmd_new_lang_template<C, CXX>::
  ~cmd_new_lang_template ()
  {
    if (lang == c)
      c_opt.~C ();
    else
      cxx_opt.~CXX ();
  }

  template <typename C, typename CXX>
  inline cmd_new_lang_template<C, CXX>::
  cmd_new_lang_template (cmd_new_lang_template&& l): lang (l.lang)
  {
    if (lang == c)
      new (&c_opt) C (move (l.c_opt));
    else
      new (&cxx_opt) CXX (move (l.cxx_opt));
  }

  template <typename C, typename CXX>
  inline cmd_new_lang_template<C, CXX>::
  cmd_new_lang_template (const cmd_new_lang_template& l): lang (l.lang)
  {
    if (lang == c)
      new (&c_opt) C (l.c_opt);
    else
      new (&cxx_opt) CXX (l.cxx_opt);
  }

  template <typename C, typename CXX>
  inline cmd_new_lang_template<C, CXX>& cmd_new_lang_template<C, CXX>::
  operator= (cmd_new_lang_template&& l)
  {
    if (this != &l)
    {
      this->~cmd_new_lang_template<C, CXX> ();

      // Assume noexcept move-construction.
      //
      new (this) cmd_new_lang_template<C, CXX> (move (l));
    }

    return *this;
  }

  template <typename C, typename CXX>
  inline cmd_new_lang_template<C, CXX>& cmd_new_lang_template<C, CXX>::
  operator= (const cmd_new_lang_template& l)
  {
    if (this != &l)
      *this = cmd_new_lang_template<C, CXX> (l); // Reduce to move-assignment.

    return *this;
  }
}
