// file      : bdep/diagnostics.cxx -*- C++ -*-
// copyright : Copyright (c) 2014-2017 Code Synthesis Ltd
// license   : MIT; see accompanying LICENSE file

#include <bdep/diagnostics.hxx>

#include <libbutl/process.mxx>
#include <libbutl/process-io.mxx> // operator<<(ostream, process_arg)

#include <bdep/utility.hxx>

using namespace std;
using namespace butl;

namespace bdep
{
  // print_process
  //
  void
  print_process (const char* const args[], size_t n)
  {
    diag_record r (text);
    print_process (r, args, n);
  }

  void
  print_process (diag_record& r, const char* const args[], size_t n)
  {
    r << process_args {args, n};
  }

  // Diagnostics verbosity level.
  //
  uint16_t verb;

  // Diagnostic facility, project specifics.
  //

  void simple_prologue_base::
  operator() (const diag_record& r) const
  {
    if (type_ != nullptr)
      r << type_ << ": ";

    if (name_ != nullptr)
      r << name_ << ": ";
  }

  void location_prologue_base::
  operator() (const diag_record& r) const
  {
    r << loc_.file << ':' << loc_.line << ':' << loc_.column << ": ";

    if (type_ != nullptr)
      r << type_ << ": ";

    if (name_ != nullptr)
      r << name_ << ": ";
  }

  // tracer
  //
  void tracer::
  operator() (const char* const args[], size_t n) const
  {
    if (verb >= 3)
    {
      diag_record dr (*this);
      print_process (dr, args, n);
    }
  }

  const basic_mark error ("error");
  const basic_mark warn  ("warning");
  const basic_mark info  ("info");
  const basic_mark text  (nullptr);
  const fail_mark  fail  ("error");
  const fail_end   endf;
}
