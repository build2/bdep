// file      : bdep/project-author.cxx -*- C++ -*-
// license   : MIT; see accompanying LICENSE file

#include <bdep/project-author.hxx>

#include <bdep/git.hxx>
#include <bdep/diagnostics.hxx>

using namespace butl;

namespace bdep
{
  project_author
  find_project_author (const dir_path& prj)
  {
    project_author r;

    // The search order is as follows:
    //
    // BDEP_AUTHOR_(NAME|EMAIL)
    // <VCS>
    // (USER|EMAIL)
    //
    r.name  = getenv ("BDEP_AUTHOR_NAME");
    r.email = getenv ("BDEP_AUTHOR_EMAIL");

    if (!r.name || !r.email)
    {
      // See if this is a VCS repository we recognize.
      //
      if (git_repository (prj))
      {
        // In git the author name/email can be specified with the
        // GIT_AUTHOR_NAME/EMAIL environment variables after which things
        // fall back to the committer (GIT_COMMITTER_NAME/EMAIL and then
        // the user.name/email git-config value). The resolved value can
        // be queried with the GIT_AUTHOR_IDENT logical variable.
        //
        if (optional<string> l = git_line (semantic_version {2, 1, 0},
                                           true /* system */,
                                           prj,
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
            fail << "invalid GIT_AUTHOR_IDENT value '" << *l << "'" << endf;

          if (!r.name)
          {
            string n (*l, 0, p1);

            if (!trim (n).empty ())
              r.name = move (n);
          }

          if (!r.email)
          {
            ++p1;
            string e (*l, p1, p2 - p1);

            if (!trim (e).empty ())
              r.email = move (e);
          }
        }
      }
    }

    if (!r.name ) r.name  = getenv ("USER");
    if (!r.email) r.email = getenv ("EMAIL");

    return r;
  }
}
