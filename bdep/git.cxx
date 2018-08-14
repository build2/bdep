// file      : bdep/git.cxx -*- C++ -*-
// copyright : Copyright (c) 2014-2018 Code Synthesis Ltd
// license   : MIT; see accompanying LICENSE file

#include <bdep/git.hxx>

#include <libbutl/git.mxx>

#include <bdep/diagnostics.hxx>

using namespace butl;

namespace bdep
{
  static optional<semantic_version> git_ver;

  // Check that git is at least of the specified minimum supported version.
  //
  void
  git_check_version (const semantic_version& min_ver)
  {
    // Query and cache git version on the first call.
    //
    if (!git_ver)
    {
      // Make sure that the getline() function call doesn't end up with an
      // infinite recursion.
      //
      git_ver = semantic_version ();

      optional<string> s (git_line (*git_ver,
                                    false /* ignore_error */,
                                    "--version"));

      if (!s || !(git_ver = git_version (*s)))
        fail << "unable to obtain git version";
    }

    // Note that we don't expect the min_ver to contain the build component,
    // that doesn't matter functionality-wise for git.
    //
    if (*git_ver < min_ver)
      fail << "unsupported git version " << *git_ver <<
        info << "minimum supported version is " << min_ver << endf;
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
