// file      : bdep/value-traits.hxx -*- C++ -*-
// license   : MIT; see accompanying LICENSE file

#ifndef BDEP_VALUE_TRAITS_HXX
#define BDEP_VALUE_TRAITS_HXX

#include <string>
#include <cstddef> // size_t
#include <utility> // move()

#include <odb/sqlite/traits.hxx>

#include <libbpkg/package-name.hxx>

namespace odb
{
  namespace sqlite
  {
    template <>
    class value_traits<bpkg::package_name, id_text>:
      value_traits<std::string, id_text>
    {
    public:
      using value_type = bpkg::package_name;
      using query_type = bpkg::package_name;
      using image_type = details::buffer;

      using base_type = value_traits<std::string, id_text>;

      static void
      set_value (value_type& v,
                 const details::buffer& b,
                 std::size_t n,
                 bool is_null)
      {
        std::string s;
        base_type::set_value (s, b, n, is_null);
        v = !s.empty () ? value_type (std::move (s)) : value_type ();
      }

      static void
      set_image (details::buffer& b,
                 std::size_t& n,
                 bool& is_null,
                 const value_type& v)
      {
        base_type::set_image (b, n, is_null, v.string ());
      }
    };
  };
}

#endif // BDEP_WRAPPER_TRAITS_HXX
