// file      : bdep/project-email.cxx -*- C++ -*-
// copyright : Copyright (c) 2014-2018 Code Synthesis Ltd
// license   : MIT; see accompanying LICENSE file

#include <bdep/project-email.hxx>

#include <libbutl/filesystem.mxx>

#include <bdep/diagnostics.hxx>

using namespace butl;

namespace bdep
{
  optional<string>
  project_email (const dir_path& prj)
  {
    optional<string> r;

    // The search order is as follows:
    //
    // BDEP_EMAIL
    // <VCS>
    // EMAIL
    //
    if ((r = getenv ("BDEP_EMAIL")))
      return r;

    // See if this is a VCS repository we recognize.
    //

    // .git can be either a directory or a file in case of a submodule.
    //
    if (entry_exists (prj / ".git",
                      true /* follow_symlinks */,
                      true /* ignore_errors */))
    {
      // In git the author email can be specified with the GIT_AUTHOR_EMAIL
      // environment variable after which things fall back to the committer
      // (GIT_COMMITTER_EMAIL and then the user.email git-config value). The
      // resolved value can be queried with the GIT_AUTHOR_IDENT logical
      // variable.
      //
      process pr;
      bool io (false);
      try
      {
        fdpipe pipe (fdopen_pipe ());

        // If git cannot determine the author name/email, it fails verbosely
        // so we suppress all diagnostics.
        //
        pr = start (0         /* stdin  */,
                    pipe      /* stdout */,
                    fdnull () /* stderr */,
                    "git",
                    "-C", prj,
                    "var",
                    "GIT_AUTHOR_IDENT");

        pipe.out.close ();
        ifdstream is (move (pipe.in), ifdstream::badbit);

        // The output should be a single line in this form:
        //
        // NAME <EMAIL> TIME ZONE
        //
        // For example:
        //
        // John Doe <john@example.org> 1530517726 +0200
        //
        // The <> delimiters are there even if the email is empty so we use
        // them as anchors.
        //
        string l;
        if (!eof (getline (is, l)))
        {
          size_t p1, p2;

          if ((p2 = l.rfind ('>'    )) == string::npos ||
              (p1 = l.rfind ('<', p2)) == string::npos)
            fail << "no email in git-var output" << endf;

          if (++p1 != p2)
            r = string (l, p1, p2 - p1);
        }

        is.close (); // Detect errors.
      }
      catch (const io_error&)
      {
        io = true; // Presumably git failed so check that first.
      }

      if (!pr.wait ())
      {
        const process_exit& e (*pr.exit);

        if (!e.normal ())
          fail << "process git " << e;

        r = nullopt;
      }
      else if (io)
        fail << "unable to read git-var output";

      if (r)
        return r;
    }

    if ((r = getenv ("EMAIL")))
      return r;

    return r;
  }
}
