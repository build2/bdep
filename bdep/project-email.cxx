// file      : bdep/project-email.cxx -*- C++ -*-
// copyright : Copyright (c) 2014-2018 Code Synthesis Ltd
// license   : MIT; see accompanying LICENSE file

#include <bdep/project-email.hxx>

#include <bdep/git.hxx>
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
    if (git (prj))
    {
      // In git the author email can be specified with the GIT_AUTHOR_EMAIL
      // environment variable after which things fall back to the committer
      // (GIT_COMMITTER_EMAIL and then the user.email git-config value). The
      // resolved value can be queried with the GIT_AUTHOR_IDENT logical
      // variable.
      //
      if (optional<string> l = git_line (prj,
                                         true /* ignore_error */,
                                         "var", "GIT_AUTHOR_IDENT"))
      {
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
        size_t p1, p2;

        if ((p2 = l->rfind ('>'    )) == string::npos ||
            (p1 = l->rfind ('<', p2)) == string::npos)
          fail << "no email in GIT_AUTHOR_IDENT" << endf;

        if (++p1 != p2)
          return string (*l, p1, p2 - p1);
      }
    }

    if ((r = getenv ("EMAIL")))
      return r;

    return r;
  }
}
