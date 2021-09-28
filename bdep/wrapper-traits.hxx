// file      : bdep/wrapper-traits.hxx -*- C++ -*-
// license   : MIT; see accompanying LICENSE file

#ifndef BDEP_WRAPPER_TRAITS_HXX
#define BDEP_WRAPPER_TRAITS_HXX

#include <cstdint>

#include <libbutl/optional.hxx>

#include <odb/wrapper-traits.hxx>

namespace odb
{
  template <typename T>
  class wrapper_traits<butl::optional<T>>
  {
  public:
    typedef T wrapped_type;
    typedef butl::optional<T> wrapper_type;

    // T can be const.
    //
    typedef
    typename odb::details::meta::remove_const<T>::result
    unrestricted_wrapped_type;

    static const bool null_handler = true;
    static const bool null_default = true;

    static bool
    get_null (const wrapper_type& o)
    {
      return !o;
    }

    static void
    set_null (wrapper_type& o)
    {
      o = butl::nullopt;
    }

    static const wrapped_type&
    get_ref (const wrapper_type& o)
    {
      return *o;
    }

    static unrestricted_wrapped_type&
    set_ref (wrapper_type& o)
    {
      if (!o)
        o = unrestricted_wrapped_type ();

      return const_cast<unrestricted_wrapped_type&> (*o);
    }
  };

  // These magic incantations are necessary to get portable generated
  // code (without these because of the way GCC works uint64_t will be
  // spelled as unsigned long which will break on Windows).
  //
  using optional_uint64_t = butl::optional<std::uint64_t>;
  using optional_uint64_traits = odb::wrapper_traits<optional_uint64_t>;
#ifdef ODB_COMPILER
  template class odb::wrapper_traits<optional_uint64_t>;
#endif
}

#endif // BDEP_WRAPPER_TRAITS_HXX
