// file      : bdep/types.hxx -*- C++ -*-
// copyright : Copyright (c) 2014-2017 Code Synthesis Ltd
// license   : MIT; see accompanying LICENSE file

#ifndef BDEP_TYPES_HXX
#define BDEP_TYPES_HXX

#include <vector>
#include <string>
#include <memory>        // unique_ptr, shared_ptr
#include <utility>       // pair
#include <cstddef>       // size_t, nullptr_t
#include <cstdint>       // uint{8,16,32,64}_t
#include <istream>
#include <ostream>
#include <functional>    // function, reference_wrapper

#include <ios>           // ios_base::failure
#include <exception>     // exception
#include <stdexcept>     // logic_error, invalid_argument, runtime_error
#include <system_error>

#include <libbutl/path.mxx>
#include <libbutl/optional.mxx>
#include <libbutl/fdstream.mxx>

namespace bdep
{
  // Commonly-used types.
  //
  using std::uint8_t;
  using std::uint16_t;
  using std::uint32_t;
  using std::uint64_t;

  using std::size_t;
  using std::nullptr_t;

  using std::pair;
  using std::string;
  using std::function;
  using std::reference_wrapper;

  using std::unique_ptr;
  using std::shared_ptr;
  using std::weak_ptr;

  using std::vector;

  using strings = vector<string>;
  using cstrings = vector<const char*>;

  using std::istream;
  using std::ostream;

  // Exceptions. While <exception> is included, there is no using for
  // std::exception -- use qualified.
  //
  using std::logic_error;
  using std::invalid_argument;
  using std::runtime_error;
  using std::system_error;
  using io_error = std::ios_base::failure;

  // <libbutl/optional.mxx>
  //
  using butl::optional;
  using butl::nullopt;

  // <libbutl/path.mxx>
  //
  using butl::path;
  using butl::dir_path;
  using butl::basic_path;
  using butl::invalid_path;

  using butl::path_cast;

  using paths = std::vector<path>;
  using dir_paths = std::vector<dir_path>;

  // <libbutl/fdstream.mxx>
  //
  using butl::auto_fd;
  using butl::fdpipe;
  using butl::ifdstream;
  using butl::ofdstream;
  using butl::fdstream_mode;
}

// In order to be found (via ADL) these have to be either in std:: or in
// butl::. The latter is bad idea since libbutl includes the default
// implementation.
//
namespace std
{
  // Custom path printing (canonicalized, with trailing slash for directories).
  //
  inline ostream&
  operator<< (ostream& os, const ::butl::path& p)
  {
    string r (p.representation ());
    ::butl::path::traits::canonicalize (r);
    return os << r;
  }
}

#endif // BDEP_TYPES_HXX
