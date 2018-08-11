// file      : bdep/git.cxx -*- C++ -*-
// copyright : Copyright (c) 2014-2018 Code Synthesis Ltd
// license   : MIT; see accompanying LICENSE file

#include <bdep/git.hxx>

#include <libbutl/git.mxx>

#include <bdep/diagnostics.hxx>

using namespace butl;

namespace bdep
{
  static optional<standard_version> git_ver;

  // On the first call check that git is at least of the specified minimum
  // supported version.
  //
  void
  git_check_version (const standard_version& min_ver)
  {
    if (!git_ver)
    {
      // Make sure that the getline() function call doesn't end up with an
      // infinite recursion.
      //
      git_ver = standard_version ();

      optional<string> s (git_line (min_ver,
                                    false /* ignore_error */,
                                    "--version"));

      if (!s || !(git_ver = git_version (*s)))
        fail << "unable to obtain git version";

      if (*git_ver < min_ver)
        fail << "unsupported git version " << *git_ver <<
          info << "minimum supported version is " << min_ver << endf;
    }
  }

  optional<string>
  git_line (process&& pr, fdpipe&& pipe, bool ie)
  {
    optional<string> r;

    bool io (false);
    try
    {
      pipe.out.close ();
      ifdstream is (move (pipe.in), fdstream_mode::skip, ifdstream::badbit);

      string l;
      if (!eof (getline (is, l)))
        r = move (l);

      is.close (); // Detect errors.
    }
    catch (const io_error&)
    {
      io = true; // Presumably git failed so check that first.
    }

    // Note: cannot use finish() since ignoring normal error.
    //
    if (!pr.wait ())
    {
      const process_exit& e (*pr.exit);

      if (!e.normal ())
        fail << "process git " << e;

      if (ie)
        r = nullopt;
      else
        throw failed (); // Assume git issued diagnostics.
    }
    else if (io)
      fail << "unable to read git output";

    return r;
  }
}
