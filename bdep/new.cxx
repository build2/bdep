// file      : bdep/new.cxx -*- C++ -*-
// copyright : Copyright (c) 2014-2019 Code Synthesis Ltd
// license   : MIT; see accompanying LICENSE file

#include <bdep/new.hxx>

#include <map>
#include <algorithm> // replace()

#include <libbutl/regex.mxx>
#include <libbutl/command.mxx>
#include <libbutl/project-name.mxx>

#include <bdep/git.hxx>
#include <bdep/project.hxx>
#include <bdep/project-author.hxx>
#include <bdep/database.hxx>
#include <bdep/diagnostics.hxx>

#include <bdep/init.hxx>
#include <bdep/config.hxx>

using namespace std;
using namespace butl;

namespace bdep
{
  // License id to full name map.
  //
  static const map<string, string, icase_compare_string> licenses = {
    {"MIT",               "MIT License"                            },
    {"BSD2",              "Simplified 2-clause BSD License"        },
    {"BSD3",              "New 3-clause BSD License"               },
    {"BSD4",              "Original 4-clause BSD License"          },
    {"GPLv2",             "GNU General Public License v2.0"        },
    {"GPLv3",             "GNU General Public License v3.0"        },
    {"LGPLv2",            "GNU Lesser General Public License v2.0" },
    {"LGPLv2.1",          "GNU Lesser General Public License v2.1" },
    {"LGPLv3",            "GNU Lesser General Public License v3.0" },
    {"AGPLv2",            "Affero General Public License v2.0"     },
    {"AGPLv3",            "GNU Affero General Public License v3.0" },
    {"ASLv1",             "Apache License v1.0"                    },
    {"ASLv1.1",           "Apache License v1.1"                    },
    {"ASLv2",             "Apache License v2.0"                    },
    {"MPLv2",             "Mozilla Public License v2.0"            },

    // Note: entries with empty full name are here to get canonical case.
    //
    {"public domain" ,    ""                                       },
    {"available source",  "Not free software/open source"          },
    {"proprietary",       ""                                       },
    {"TODO",              "License is not yet decided"             }};


  // Extract a license id from a license file returning an empty string if
  // it doesn't match any known license file signatures.
  //
  static string
  extract_license (const path& f)
  {
    // The overall plan is to read the license heading and then try to match
    // it against a bunch of regular expression.
    //
    // Some license headings are spread over multiple lines but all the files
    // that we have seen so far separate the heading from the license body
    // with a blank line, for example:
    //
    //                       Apache License
    //                   Version 2.0, January 2004
    //                http://www.apache.org/licenses/
    //
    //   TERMS AND CONDITIONS FOR USE, REPRODUCTION, AND DISTRIBUTION
    //   ....
    //
    // So what we are going to do is combine all the lines (trimmed and
    // separated with spaces) until the blank and run our regular expressions
    // on that. Note that it's possible we will end up with some non-heading
    // junk, as in the case of MPL:
    //
    //   Mozilla Public License Version 2.0
    //   ==================================
    //
    //   1. Definitions
    //   ...
    //
    string h;
    try
    {
      ifdstream is (f, ifdstream::badbit);

      for (string l; !eof (getline (is, l)); )
      {
        if (trim (l).empty ())
          break;

        if (!h.empty ())
          h += ' ';

        h += l;
      }
    }
    catch (const io_error& e)
    {
      fail << "unable to read " << f << ": " << e << endf;
    }

    if (h.empty ())
      return "";

    // We do case-insensitive first-only match ignoring the unmatched parts.
    //
    string r;
    auto test = [&h, &r] (const string& e, const string& f) -> bool
    {
      regex re (e, regex::ECMAScript | regex::icase);

      pair<string, bool> p (
        regex_replace_search (h,
                              re,
                              f,
                              regex_constants::format_first_only |
                              regex_constants::format_no_copy));

      if (p.second)
      {
        assert (!p.first.empty ());
        r = move (p.first);
      }

      return p.second;
    };

    // Note that some licenses (like ASL, MPL) always spell the minor verison,
    // even if it is there (unlike the GNU licenses). So for them we need to
    // ignore the zero minor component.
    //
    (test ("MIT License",                                         "MIT")     ||
     test ("BSD ([1234])-Clause License",                         "BSD$1")   ||
     test ("Apache License Version ([0-9]+(\\.[1-9])?)",          "ASLv$1")  ||
     test ("Mozilla Public License Version ([0-9]+(\\.[1-9])?)",  "MPLv$1")  ||
     test ("GNU GENERAL PUBLIC LICENSE Version ([0-9.]+)",        "GPLv$1")  ||
     test ("GNU LESSER GENERAL PUBLIC LICENSE Version ([0-9.]+)", "LGPLv$1") ||
     test ("GNU AFFERO GENERAL PUBLIC LICENSE Version ([0-9.]+)", "AGPLv$1") ||
     test ("public domain",                                 "public domain"));

    return r;
  }

  // Extract a summary line from a README.md file returning an empty string if
  // unable to. The second argument is the project name.
  //
  static string
  extract_summary (const path& f, const string& n)
  {
    // README.md created by popular hosting services (GitHub, GitLab) have the
    // following format (give or take a few blank lines in between):
    //
    // # <name>
    // <summary>
    //
    // Of course, the file could have been tweaked. In particular, one popular
    // alternative arrangement looks like this:
    //
    // # <name> - <summary>
    //
    // Let's start simple by only support the first version and maybe
    // extend/complicate things later.
    //
    try
    {
      ifdstream is (f, ifdstream::badbit);

      string l;
      auto next = [&is, &l] () -> bool
      {
        while (!eof (getline (is, l)) && trim (l).empty ())
          ;
        return !l.empty ();
      };

      if (next ())
      {
        if (icasecmp (l, "# " + n) == 0)
        {
          if (next ())
          {
            // Potential improvements:
            //
            // - Strip trailing period, if any.
            // - Get only the first sentence.
            //
            return l;
          }
        }
      }

      return "";
    }
    catch (const io_error& e)
    {
      fail << "unable to read " << f << ": " << e << endf;
    }
  }

  using type = cmd_new_type;
  using lang = cmd_new_lang;
  using vcs  = cmd_new_vcs;

  int
  cmd_new (cmd_new_options&& o, cli::group_scanner& args)
  {
    tracer trace ("new");

    // Validate options.
    //
    bool ca (o.config_add_specified ());
    bool cc (o.config_create_specified ());

    if (o.package () && o.subdirectory ())
      fail << "both --package and --subdirectory specified";

    const char* m (o.package ()      ? "--package"      :
                   o.subdirectory () ? "--subdirectory" : nullptr);

    if (m && o.no_init ())
      fail << "both --no-init and " << m << " specified";

    if (const char* n = (o.no_init () ? "--no-init" :
                         m            ? m           : nullptr))
    {
      if (ca) fail << "both " << n << " and --config-add specified";
      if (cc) fail << "both " << n << " and --config-create specified";
    }

    if (o.directory_specified () && !m)
      fail << "--directory|-d only valid with --package or --subdirectory";

    if (const char* n = cmd_config_validate_add (o))
    {
      if (!ca && !cc)
        fail << n << " specified without --config-(add|create)";

      if (o.existing () && !cc)
        fail << "--existing|-e specified without --config-create";

      if (o.wipe () && !cc)
        fail << "--wipe specified without --config-create";
    }

    // Validate type options.
    //
    const type& t (o.type ());

    // For a library source subdirectory (--subdirectory) we don't generate
    // the export stub, integration tests (because there is no export stub),
    // or the version header (because the project name used in the .in file
    // will most likely be wrong). All this seems reasonable for what this
    // mode is expected to be used ("end-product" kind of projects).
    //
    bool readme  (false); // !no-readme
    bool altn    (false); // alt-naming
    bool itest   (false); // !no-tests
    bool utest   (false); // unit-tests
    bool install (false); // !no-install
    bool ver     (false); // !no-version

    string license;
    bool   license_o (false);
    {
      bool pkg (o.package ());
      bool sub (o.subdirectory ());

      switch (t)
      {
      case type::exe:
        {
          readme  = !t.exe_opt.no_readme ()  && !sub;
          altn    =  t.exe_opt.alt_naming ();
          itest   = !t.exe_opt.no_tests ();
          utest   =  t.exe_opt.unit_tests ();
          install = !t.exe_opt.no_install ();

          if (!sub)
          {
            license   = t.exe_opt.license ();
            license_o = t.exe_opt.license_specified ();
          }
          break;
        }
      case type::lib:
        {
          readme  = !t.lib_opt.no_readme ()  && !sub;
          altn    =  t.lib_opt.alt_naming ();
          itest   = !t.lib_opt.no_tests ()   && !sub;
          utest   =  t.lib_opt.unit_tests ();
          install = !t.lib_opt.no_install ();
          ver     = !t.lib_opt.no_version () && !sub;

          if (!sub)
          {
            license   = t.lib_opt.license ();
            license_o = t.lib_opt.license_specified ();
          }
          break;
        }
      case type::bare:
        {
          if (sub)
            fail << "cannot create bare source subdirectory";

          readme  = !t.bare_opt.no_readme ();
          altn    =  t.bare_opt.alt_naming ();
          itest   = !t.bare_opt.no_tests ();
          install = !t.bare_opt.no_install ();

          if (!sub)
          {
            license   = t.bare_opt.license ();
            license_o = t.bare_opt.license_specified ();
          }
          break;
        }
      case type::empty:
        {
          if (const char* w = (sub ? "source subdirectory" :
                               pkg ? "package"             : nullptr))
            fail << "cannot create empty " << w;

          readme = !t.empty_opt.no_readme ();
          break;
        }
      }
    }

    // Standard/alternative build file/directory naming scheme.
    //
    const dir_path build_dir      (altn ? "build2"     : "build");
    const string   build_ext      (altn ? "build2"     : "build");
    const path     buildfile_file (altn ? "build2file" : "buildfile");

    // Validate language options.
    //
    const lang& l (o.lang ());

    switch (l)
    {
    case lang::c:
      {
        break;
      }
    case lang::cxx:
      {
        auto& o (l.cxx_opt);

        if (o.cpp () && o.extension_specified ())
          fail << "'extension' and 'cpp' are mutually exclusive c++ options";

        // Verify that none of the extensions are specified as empty, except
        // for hxx.
        //
        auto empty_ext = [] (const string& v, const char* o)
        {
          if (v.empty () || (v.size () == 1 && v[0] == '.'))
            fail << "empty extension specified with '" << o << "' c++ option";
        };

        if (o.extension_specified ()) empty_ext (o.extension (), "extension");

        if (o.cxx_specified ()) empty_ext (o.cxx (), "cxx");
        if (o.ixx_specified ()) empty_ext (o.ixx (), "ixx");
        if (o.txx_specified ()) empty_ext (o.txx (), "txx");
        if (o.mxx_specified ()) empty_ext (o.mxx (), "mxx");

        if (o.binless () && t != type::lib)
          fail << "'binless' is only valid for libraries";

        break;
      }
    }

    // Validate vcs options.
    //
    vcs  vc   (o.vcs ());
    bool vc_o (o.vcs_specified ());

    // Check if we have the argument (name). If not, then we use the specified
    // output or current working directory name.
    //
    string a;
    if (args.more ())
      a = args.next ();
    else
    {
      if (!o.output_dir_specified ())
      {
        // Reduce this case (for the logic that follows) to as-if the current
        // working directory was specified as the output directory.
        //
        o.output_dir (path::current_directory ());
        o.output_dir_specified (true);
      }

      a = o.output_dir ().leaf ().string ();
    }

    // If the project type is not empty then the project name is also a package
    // name. But even if it is empty, verify it is a valid package name since
    // it will most likely end up in the 'project' manifest value.
    //
    package_name pkgn;

    try
    {
      pkgn = package_name (move (a));
    }
    catch (const invalid_argument& e)
    {
      fail << "invalid " << (t == type::empty ? "project" : "package")
           << " name: " << e;
    }

    // Full package name vs base name (e.g., libhello in libhello.bash) vs the
    // name stem (e.g, hello in libhello).
    //
    // We use the full name in the manifest and the top-level directory, the
    // base name for inner filesystem directories and preprocessor macros,
    // while the (sanitized) stem for modules, namespaces, etc.
    //
    const string&   n (pkgn.string ());   // Full name.
    const string&   b (pkgn.base ());     // Base name.
    const string&   v (pkgn.variable ()); // Variable name.
    string          s (b);                // Name stem.
    {
      // Warn about the lib prefix unless we are creating a source
      // subdirectory, in which case the project is probably not meant to be a
      // package anyway.
      //
      bool w (!o.subdirectory ());

      switch (t)
      {
      case type::exe:
        {
          if (w && s.compare (0, 3, "lib") == 0)
            warn << "executable name starts with 'lib'";

          break;
        }
      case type::lib:
        {
          if (s.compare (0, 3, "lib") == 0)
          {
            s.erase (0, 3);

            if (w && s.empty ())
              fail << "empty library name stem in '" << b << "'";
          }
          else if (w)
            warn << "library name does not start with 'lib'";

          break;
        }
      case type::bare:
      case type::empty:
        break;
      }
    }

    // Sanitize the stem to be a valid language identifier.
    //
    string id;
    switch (l)
    {
    case lang::c:
    case lang::cxx:
      {
        id = sanitize_identifier (const_cast<const string&> (s));
        break;
      }
    }

    dir_path prj;           // Project.
    dir_path out;           // Project/package/subdirectory output directory.
    optional<dir_path> pkg; // Package relative to its project root.
    optional<dir_path> sub; // Source subdirectory relative to its
                            // project/package root.
    {
      // Figure the final output and tentative project directories.
      //
      if (o.package () || o.subdirectory ())
      {
        if (o.directory_specified ())
          prj = normalize (o.directory (), "project");
        else
          prj = current_directory ();

        out = o.output_dir_specified () ? o.output_dir () : prj / dir_path (n);
        normalize (out, "output");
      }
      else
      {
        out = o.output_dir_specified () ? o.output_dir () : dir_path (n);
        normalize (out, "output");
        prj = out;
      }

      // Get the actual project/package information as "seen" from the output
      // directory.
      //
      project_package pp (
        find_project_package (out, true /* ignore_not_found */));

      // Finalize the tentative project directory and do some sanity checks
      // (nested packages, etc; you would be surprised what people come up
      // with).
      //
      if (o.package ())
      {
        if (!o.no_checks ())
        {
          if (pp.project.empty ())
            warn << prj << " does not look like a project directory";
          else
          {
            if (pp.package)
              fail << "package directory " << out << " is inside another "
                   << "package directory " << pp.project / *pp.package <<
                info << "nested packages are not allowed";
          }
        }

        if (!pp.project.empty ())
        {
          if (prj != pp.project)
            prj = move (pp.project);
        }

        if (!out.sub (prj))
          fail << "package directory " << out << " is not a subdirectory of "
               << "project directory " << prj;

        pkg = out.leaf (prj);
      }
      else if (o.subdirectory ())
      {
        if (!o.no_checks ())
        {
          if (pp.project.empty () || !pp.package)
            warn << prj << " does not look like a package directory";
        }

        // Note: our prj should actually be the package (i.e., the build
        // system project root).
        //
        if (!pp.project.empty ())
        {
          dir_path pkg (move (pp.project));

          if (pp.package)
            pkg /= *pp.package;

          if (prj != pkg)
            prj = move (pkg);
        }

        if (!out.sub (prj))
          fail << "source subdirectory " << out << " is not a subdirectory of "
               << "package directory " << prj;

        // We use this information to form the include directories. The idea
        // is that if the user places the subdirectory somewhere deeper (say
        // into core/libfoo/), then we want the include directives to contain
        // the prefix from the project root (so it will be <core/libfoo/...>)
        // since all our buildfiles are hardwired with -I$src_root.
        //
        // Note also that a crafty user can adjust the prefix by picking the
        // appropriate --directory|-d (i.e., it can point somewhere deeper
        // than the project root). They will need to adjust their buildfiles,
        // however (or we could get smarter by finding the actual package root
        // and adding the difference to -I). Also, some other things, such as
        // the namespace, currently do not contain the prefix.
        //
        sub = out.leaf (prj);
      }
      else
      {
        if (!o.no_checks ())
        {
          if (!pp.project.empty ())
            fail << "project directory " << out << " is inside another "
                 << "project directory " << pp.project <<
              info << "nested projects are not allowed";
        }
      }

      // Create the output directory if it doesn't exit. Note that we are
      // ok with it already existing and containing some things; see below
      // for details.
      //
      if (!exists (out))
        mk_p (out);
    }

    // Run pre/post hooks.
    //
    auto run_hooks = [&prj, &sub, &pkg, &n, &b, &s, &t, &l, &vc, &out]
                     (const strings& hooks, const char* what)
    {
      command_substitution_map subs;
      strings                  vars;

      auto add_var = [&subs, &vars] (string name, string value)
      {
        vars.push_back ("BDEP_NEW_"                              +
                        ucase (const_cast<const string&> (name)) +
                        '='                                      +
                        value);

        subs[move (name)] = move (value);
      };

      add_var ("mode", sub ? "subdirectory" : pkg ? "package" : "project");
      add_var ("name", n);
      add_var ("base", b);
      add_var ("stem", s);
      add_var ("type", t.string ());
      add_var ("lang", l.string (true /* lower */));
      add_var ("vcs",  vc.string ());
      add_var ("root", prj.string ());

      // Note: out directory path is absolute and normalized.
      //
      optional<process_env> env (process_env (process_path (), out, vars));

      for (const string& cmd: hooks)
      {
        try
        {
          process_exit e (command_run (cmd,
                                       env,
                                       subs,
                                       '@',
                                       [] (const char* const args[], size_t n)
                                       {
                                         if (verb >= 2)
                                         {
                                           print_process (args, n);
                                         }
                                       }));

          if (!e)
          {
            if (e.normal ())
              throw failed (); // Assume the command issued diagnostics.

            fail << what << " hook '" << cmd << "' " << e;
          }
        }
        catch (const invalid_argument& e)
        {
          fail << "invalid " << what << " hook '" << cmd << "': " << e;
        }
        catch (const io_error& e)
        {
          fail << "unable to execute " << what << " hook '" << cmd << "': "
               << e;
        }
        // Also handles process_error exception (derived from system_error).
        //
        catch (const system_error& e)
        {
          fail << "unable to execute " << what << " hook '" << cmd << "': "
               << e;
        }
      }
    };

    // Run pre hooks.
    //
    if (o.pre_hook_specified ())
      run_hooks (o.pre_hook (), "pre");

    // Source directory relative to package root.
    //
    const dir_path& d (sub ? *sub : dir_path (b));

    // Check if certain things already exist.
    //
    optional<vcs::vcs_type> vc_e;      // Detected version control system.
    optional<string>        readme_e;  // Extracted summary line.
    optional<string>        license_e; // Extracted license id.
    {
      if (!sub)
      {
        if (!pkg)
        {
          if (git_repository (out))
            vc_e = vcs::git;
        }

        // @@ What if in the --package mode these files already exist but are
        //    not in the package but in the project root? This also goes back
        //    to an existing desire to somehow reuse project README.md/LICENSE
        //    in its packages (currently a reference from manifest out of
        //    package is illegal). Maybe via symlinks? We could probably even
        //    automatically find and symlink them?
        //
        path f;

        // README.md
        //
        if (exists ((f = out / "README.md")))
        {
          readme_e = extract_summary (f, n);

          if (readme_e->empty ())
            warn << "unable to extract project summary from " << f <<
              info << "using generic summary in manifest";
        }

        // LICENSE
        //
        if (exists ((f = out / "LICENSE")))
        {
          license_e = extract_license (f);

          if (license_e->empty () && !license_o)
            fail << "unable to guess project license from " << f <<
              info << "use license --type|-t sub-option to specify explicitly";
        }
      }

      // Merge option and existing values verifying that what already exists
      // does not conflict with what's requested.
      //
      if (vc_e)
      {
        if (!vc_o)
          vc = *vc_e;
        else if (*vc_e != vc)
          fail << "existing version control system does not match requested" <<
            info << "existing: " << *vc_e <<
            info << "requested: " << vc;
      }

      if (readme_e)
      {
        if (!readme)
          fail << "no-readme sub-option specified but README already exists";
      }

      if (license_e)
      {
        if (!license_o)
          license = *license_e;
        else if (icasecmp (*license_e, license) != 0)
          fail << "extracted license does not match requested" <<
            info << "extracted: " << *license_e <<
            info << "requested: " << license;
      }
    }

    // Initialize the version control system. Do it before writing anything
    // ourselves in case it fails. Also, the email discovery may do the VCS
    // detection.
    //
    if (!vc_e && !pkg && !sub)
    {
      switch (vc)
      {
      case vcs::git:  run ("git", "init", "-q", out); break;
      case vcs::none:                                 break;
      }
    }

    // We support creating a project that already contains some files provided
    // none of them conflict with what we are trying to create (with a few
    // exceptions such as LICENSE and README.md that are handled explicitly
    // plus packages.manifest to which we append last).
    //
    // While we could verify at the outset that none of the files we will be
    // creating exist, that would be quite unwieldy. So instead we are going
    // to fail as we go along but, in this case, also cleanup any files that
    // we have already created.
    //
    vector<auto_rmfile> rms;
    for (path cf;;) // Breakout loop with the current file being written.
    try
    {
      ofdstream os;
      auto open = [&cf, &os, &rms] (path f)
      {
        try
        {
          os.open (f, (fdopen_mode::out    |
                       fdopen_mode::create |
                       fdopen_mode::exclusive));
          cf = f;
          rms.push_back (auto_rmfile (move (f)));
        }
        catch (const io_error& e)
        {
          fail << "unable to create " << f << ": " << e;
        }
      };

      // .gitignore
      //
      // See also tests/.gitignore below.
      //
      if (!sub)
      {
        if (vc == vcs::git)
        {
          // Use POSIX directory separators here.
          //
          open (out / ".gitignore");
          if (!pkg)
            os << bdep_dir.posix_representation ()                     << endl
               <<                                                         endl
               << "# Local default options files."                     << endl
               << "#"                                                  << endl
               << ".build2/local/"                                     << endl
               <<                                                         endl;
          if (t != type::empty)
            os << "# Compiler/linker output."                          << endl
               << "#"                                                  << endl
               << "*.d"                                                << endl
               << "*.t"                                                << endl
               << "*.i"                                                << endl
               << "*.ii"                                               << endl
               << "*.o"                                                << endl
               << "*.obj"                                              << endl
               << "*.so"                                               << endl
               << "*.dll"                                              << endl
               << "*.a"                                                << endl
               << "*.lib"                                              << endl
               << "*.exp"                                              << endl
               << "*.pdb"                                              << endl
               << "*.ilk"                                              << endl
               << "*.exe"                                              << endl
               << "*.exe.dlls/"                                        << endl
               << "*.exe.manifest"                                     << endl
               << "*.pc"                                               << endl;
          os.close ();
        }
      }

      // repositories.manifest
      //
      if (!pkg && !sub)
      {
        open (out / "repositories.manifest");
        os << ": 1"                                                    << endl
           << "summary: " << n << " project repository"                << endl
           <<                                                             endl
           << "#:"                                                     << endl
           << "#role: prerequisite"                                    << endl
           << "#location: https://pkg.cppget.org/1/stable"             << endl
           << "#trust: ..."                                            << endl
           <<                                                             endl
           << "#:"                                                     << endl
           << "#role: prerequisite"                                    << endl
           << "#location: https://git.build2.org/hello/libhello.git"   << endl;
        os.close ();
      }

      // README.md
      //
      if (!readme_e && readme)
      {
        open (out / "README.md");
        switch (t)
        {
        case type::exe:
        case type::lib:
          {
            // @@ Maybe we should generate a "Hello, World" description and
            //    usage example as a guide, at least for a library?

            os << "# " << n                                            << endl
               <<                                                         endl
               << l << " " << t                                        << endl;
            break;
          }
        case type::bare:
        case type::empty:
          {
            os << "# " << n                                            << endl;
            break;
          }
        }
        os.close ();
      }

      if (t == type::empty)
        break; // Done.

      // manifest
      //
      if (!sub)
      {
        // Project name.
        //
        // If this is a package in a project (--package mode), then use the
        // project directory name as the project name. Otherwise, the project
        // name is the same as the package and is therefore omitted.
        //
        // In case of a library, we could have used either the full name or
        // the stem without the lib prefix. And it could go either way: if a
        // library is (likely to be) accompanied by an executable (or some
        // other extra packages), then its project should probably be the
        // stem. Otherwise, if it is a standalone library, then the full
        // library name is probably preferred. The stem also has another
        // problem: it could be an invalid project name. So using the full
        // name seems like a simpler and more robust approach.
        //
        // There was also an idea to warn if the project name ends with a
        // digit (think libfoo and libfoo2).
        //
        optional<project_name> pn;
        if (pkg)
        {
          string p (prj.leaf ().string ());

          if (p != n) // Omit if the same as the package name.
          {
            try
            {
              pn = project_name (move (p));
            }
            catch (const invalid_argument& e)
            {
              warn << "project name '" << p << "' is invalid: " << e <<
                info << "leaving the 'project' manifest value empty";

              pn = project_name ();
            }
          }
        }

        // Project email.
        //
        string pe;
        {
          optional<string> r (find_project_author_email (prj));
          pe = r ? move (*r) : "you@example.org";
        }

        // Full license name.
        //
        string ln;
        {
          auto i (licenses.find (license));
          if (i != licenses.end ())
          {
            ln = i->second;
            license = i->first; // Use canonical case.
          }
          else
          {
            if (icasecmp (license, "BSD") == 0)
              warn << "BSD license name is ambiguous" <<
                info << "consider using BSD3 for \"New 3-clause BSD License\"";
          }
        }

        open (out / "manifest");
        os << ": 1"                                                    << endl
           << "name: " << n                                            << endl
           << "version: 0.1.0-a.0.z"                                   << endl;
        if (pn)
          os << "project: " << *pn                                     << endl;
        if (readme_e && !readme_e->empty ())
          os << "summary: " << *readme_e                               << endl;
        else
          os << "summary: " << s << " " << l << " " << t               << endl;
        if (ln.empty ())
          os << "license: " << license                                 << endl;
        else
          os << "license: " << license << " ; " << ln << "."           << endl;
        if (readme)
          os << "description-file: README.md"                          << endl;
        os << "url: https://example.org/" << (pn ? pn->string () : n)  << endl
           << "email: " << pe                                          << endl
           << "depends: * build2 >= 0.11.0"                            << endl
           << "depends: * bpkg >= 0.11.0"                              << endl
           << "#depends: libhello ^1.0.0"                              << endl;
        os.close ();
      }

      string m;  // Language module.
      string x;  // Source target type.
      string h;  // Header target type.
      string hs; // All header-like target types.
      string xe; // Source file extension (including leading dot).
      string he; // Header file extension (including leading dot unless empty).

      // @@ In a modular project, mxx is probably more like hxx/cxx rather
      //    than ixx/txx.
      //
      optional<string> ie; // Inline file extension.
      optional<string> te; // Template file extension.
      optional<string> me; // Module interface extension.

      switch (l)
      {
      case lang::c:
        {
          m  = "c";
          x  = "c";
          h  = "h";
          hs = "h";
          xe = ".c";
          he = ".h";
          break;
        }
      case lang::cxx:
        {
          const auto& opt (l.cxx_opt);

          m  = "cxx";
          x  = "cxx";
          h  = "hxx";
          hs = "hxx";

          // Return the extension (v), if specified (s), derive the extension
          // from the pattern and type (t), or return the default (d), if
          // specified.
          //
          auto ext = [&opt] (bool s,
                             const string& v,
                             optional<char> t,
                             const char* d = nullptr) -> optional<string>
          {
            optional<string> r;

            if (s)
              r = v;
            else if (t && (opt.extension_specified () || opt.cpp ()))
            {
              string p (opt.extension_specified () ? opt.extension () :
                        opt.cpp ()                 ? "?pp"            : "");

              replace (p.begin (), p.end (), '?', *t);
              r = move (p);
            }
            else if (d != nullptr)
              r = d;

            // Add leading dot if absent.
            //
            if (r && !r->empty () && r->front () != '.')
              r = '.' + *r;

            return r;
          };

          xe = *ext (opt.cxx_specified (), opt.cxx (), 'c', "cxx");
          he = *ext (opt.hxx_specified (), opt.hxx (), 'h', "hxx");

          // We only want default .ixx/.txx/.mxx if the user didn't specify
          // any of the extension-related options explicitly.
          //
          bool d (!opt.cxx_specified () &&
                  !opt.hxx_specified () &&
                  !opt.ixx_specified () &&
                  !opt.txx_specified () &&
                  !opt.mxx_specified ());

          ie = ext (opt.ixx_specified (), opt.ixx (), 'i', d ? "ixx" : nullptr);
          te = ext (opt.txx_specified (), opt.txx (), 't', d ? "txx" : nullptr);

          // For now only include mxx in buildfiles if its extension was
          // explicitly specified with mxx=.
          //
          me = ext (opt.mxx_specified (), opt.mxx (), nullopt, nullptr);

          if (ie) hs += " ixx";
          if (te) hs += " txx";
          if (me) hs += " mxx";

          break;
        }
      }

      // Return the pointer to the extension suffix after the leading dot or
      // to the extension beginning if it is empty.
      //
      auto pure_ext = [] (const string& e)
      {
        assert (e.empty () || e[0] == '.');
        return e.c_str () + (e.empty () ? 0 : 1);
      };

      // build/
      //
      dir_path bd;
      if (!sub)
      {
        bd = out / build_dir;
        mk (bd);

        // build/bootstrap.build
        //
        open (bd / "bootstrap." + build_ext);
        os << "project = " << n                                        << endl;
        if (o.no_amalgamation ())
          os << "amalgamation = # Disabled."                           << endl;
        os <<                                                             endl
           << "using version"                                          << endl
           << "using config"                                           << endl;
        if (itest || utest)
          os << "using test"                                           << endl;
        if (install)
          os << "using install"                                        << endl;
        os << "using dist"                                             << endl;
        os.close ();

        // build/root.build
        //
        // Note: see also tests/build/root.build below.
        //
        open (bd / "root." + build_ext);

        switch (l)
        {
        case lang::c:
          {
            // @@ TODO: 'latest' in c.std.
            //
            // << "c.std = latest"                                     << endl
            // <<                                                         endl
            os << "using c"                                            << endl
               <<                                                         endl
               << "h{*}: extension = h"                                << endl
               << "c{*}: extension = c"                                << endl;
            break;
          }
        case lang::cxx:
          {
            os << "cxx.std = latest"                                   << endl
               <<                                                         endl
               << "using cxx"                                          << endl
               <<                                                         endl;

            if (me) os << "mxx{*}: extension = " << pure_ext (*me)     << endl;
            os         << "hxx{*}: extension = " << pure_ext (he)      << endl;
            if (ie) os << "ixx{*}: extension = " << pure_ext (*ie)     << endl;
            if (te) os << "txx{*}: extension = " << pure_ext (*te)     << endl;
            os         << "cxx{*}: extension = " << pure_ext (xe)      << endl;

            break;
          }
        }

        if ((itest || utest) && !m.empty ())
          os <<                                                           endl
             << "# The test target for cross-testing (running tests under Wine, etc)." << endl
             << "#"                                                    << endl
             << "test.target = $" << m << ".target"                    << endl;

        os.close ();

        // build/.gitignore
        //
        if (vc == vcs::git)
        {
          open (bd / ".gitignore");
          os << "config." << build_ext                                 << endl
             << "root/"                                                << endl
             << "bootstrap/"                                           << endl;
          os.close ();
        }
      }

      // buildfile
      //
      if (!sub)
      {
        open (out / buildfile_file);

        os << "./: {*/ -" << build_dir.posix_representation () << "}"  <<
          (readme ? " doc{README.md}" : "") << " manifest"             << endl;

        if (itest && install && t == type::lib) // Have tests/ subproject.
          os <<                                                           endl
             << "# Don't install tests."                               << endl
             << "#"                                                    << endl
             << "tests/: install = false"                              << endl;
        os.close ();
      }

      if (t == type::bare)
        break; // Done

      // <base>/ (source subdirectory).
      //
      const dir_path& sd (sub ? out : out / d);
      mk (sd);

      switch (t)
      {
      case type::exe:
        {
          switch (l)
          {
          case lang::c:
            {
              // <base>/<stem>.c
              //
              open (sd / s + ".c");
              os << "#include <stdio.h>"                               << endl
                 <<                                                       endl
                 << "int main (int argc, char *argv[])"                << endl
                 << "{"                                                << endl
                 << "  if (argc < 2)"                                  << endl
                 << "  {"                                              << endl
                 << "    fprintf (stderr, \"error: missing name\\n\");"<< endl
                 << "    return 1;"                                    << endl
                 << "  }"                                              << endl
                 <<                                                       endl
                 << "  printf (\"Hello, %s!\\n\", argv[1]);"           << endl
                 << "  return 0;"                                      << endl
                 << "}"                                                << endl;
              os.close ();

              break;
            }
          case lang::cxx:
            {
              // <base>/<stem>.<cxx-ext>
              //
              open (sd / s + xe);
              os << "#include <iostream>"                              << endl
                 <<                                                       endl
                 << "int main (int argc, char* argv[])"                << endl
                 << "{"                                                << endl
                 << "  using namespace std;"                           << endl
                 <<                                                       endl
                 << "  if (argc < 2)"                                  << endl
                 << "  {"                                              << endl
                 << "    cerr << \"error: missing name\" << endl;"     << endl
                 << "    return 1;"                                    << endl
                 << "  }"                                              << endl
                 <<                                                       endl
                 << "  cout << \"Hello, \" << argv[1] << '!' << endl;" << endl
                 << "}"                                                << endl;
              os.close ();

              break;
            }
          }

          // <base>/buildfile
          //
          open (sd / buildfile_file);
          os << "libs ="                                               << endl
             << "#import libs += libhello%lib{hello}"                  << endl
             <<                                                           endl;

          if (!utest)
            os << "exe{" << s << "}: "                                 <<
              "{" << hs << ' ' << x << "}{**} "                        <<
              "$libs"                                                  <<
              (itest ? " testscript" : "")                             << endl;
          else
          {
            os << "./: exe{" << s << "}: libue{" << s << "}: "         <<
              "{" << hs << ' ' << x << "}{** -**.test...} $libs"       << endl;

            if (itest)
              os << "exe{" << s << "}: testscript"                     << endl;

            os <<                                                         endl
               << "# Unit tests."                                      << endl
               << "#"                                                  << endl;

            if (install)
              os << "exe{*.test}:"                                     << endl
                 << "{"                                                << endl
                 << "  test = true"                                    << endl
                 << "  install = false"                                << endl
                 << "}"                                                << endl;
            else
              os << "exe{*.test}: test = true"                         << endl;

            os <<                                                         endl
               << "for t: " << x << "{**.test...}"                     << endl
               << "{"                                                  << endl
               << "  d = $directory($t)"                               << endl
               << "  n = $name($t)..."                                 << endl
               <<                                                         endl
               << "  ./: $d/exe{$n}: $t $d/{" << hs                    <<
              "}{+$n} $d/testscript{+$n}"                              << endl
               << "  $d/exe{$n}: libue{" << s << "}: bin.whole = false"<< endl
               << "}"                                                  << endl;
          }

          os <<                                                           endl
             << m << ".poptions =+ \"-I$out_root\" \"-I$src_root\""    << endl;
          os.close ();

          // <base>/.gitignore
          //
          if (vc == vcs::git)
          {
            open (sd / ".gitignore");
            os << s                                                    << endl;
            if (utest)
              os << "*.test"                                           << endl;
            if (itest || utest)
              os <<                                                       endl
                 << "# Testscript output directory (can be symlink)."  << endl
                 << "#"                                                << endl;
            if (itest)
              os << "test-" << s                                       << endl;
            if (utest)
              os << "test-*.test"                                      << endl;
            os.close ();
          }

          // <base>/testscript
          //
          if (itest)
          {
            open (sd / "testscript");
            os << ": basics"                                           << endl
               << ":"                                                  << endl
               << "$* 'World' >'Hello, World!'"                        << endl
               <<                                                         endl
               << ": missing-name"                                     << endl
               << ":"                                                  << endl
               << "$* 2>>EOE != 0"                                     << endl
               << "error: missing name"                                << endl
               << "EOE"                                                << endl;
            os.close ();
          }

          // <base>/<stem>.test.*
          //
          if (utest)
          {
            switch (l)
            {
            case lang::c:
              {
                // <base>/<stem>.test.c
                //
                open (sd / s + ".test.c");
                os << "#include <stdio.h>"                             << endl
                   << "#include <assert.h>"                            << endl
                   <<                                                     endl
                   << "int main ()"                                    << endl
                   << "{"                                              << endl
                   << "  return 0;"                                    << endl
                   << "}"                                              << endl;
                os.close ();

                break;
              }
            case lang::cxx:
              {
                // <base>/<stem>.test.<cxx-ext>
                //
                open (sd / s + ".test" + xe);
                os << "#include <cassert>"                             << endl
                   << "#include <iostream>"                            << endl
                   <<                                                     endl
                   << "int main ()"                                    << endl
                   << "{"                                              << endl
                   <<                                                     endl
                   << "}"                                              << endl;
                os.close ();

                break;
              }
            }
          }

          break;
        }
      case type::lib:
        {
          string ip (d.posix_representation ()); // Include prefix.

          // Macro prefix.
          //
          string mp (
            sanitize_identifier (ucase (const_cast<const string&> (ip))));

          // Strip the trailing underscore (produced from slash).
          //
          mp.pop_back ();

          string apih; // API header name.
          string exph; // Export header name (empty if binless).
          string verh; // Version header name.

          switch (l)
          {
          case lang::c:
            {
              apih = s + ".h";
              exph = "export.h";
              verh = ver ? "version.h" : string ();

              // <stem>.h
              //
              open (sd / apih);
              os << "#pragma once"                                     << endl
                 <<                                                       endl
                 << "#include <stdio.h>"                               << endl
                 <<                                                       endl
                 << "#include <" << ip << exph << ">"                  << endl
                 <<                                                       endl
                 << "/* Print a greeting for the specified name into the specified"   << endl
                 << " * stream. On success, return the number of characters printed." << endl
                 << " * On failure, set errno and return a negative value."           << endl
                 << " */"                                                             << endl
                 << mp << "_SYMEXPORT int"                             << endl
                 << "say_hello (FILE *, const char *name);"            << endl;
              os.close ();

              // <stem>.c
              //
              open (sd / s + ".c");
              os << "#include <" << ip << apih << ">"                  << endl
                 <<                                                       endl
                 << "#include <errno.h>"                               << endl
                 <<                                                       endl
                 << "int say_hello (FILE *f, const char* n)"           << endl
                 << "{"                                                << endl
                 << "  if (f == NULL || n == NULL || *n == '\\0')"     << endl
                 << "  {"                                              << endl
                 << "    errno = EINVAL;"                              << endl
                 << "    return -1;"                                   << endl
                 << "  }"                                              << endl
                 <<                                                       endl
                 << "  return fprintf (f, \"Hello, %s!\\n\", n);"      << endl
                 << "}"                                                << endl;
              os.close ();

              break;
            }
          case lang::cxx:
            if (l.cxx_opt.binless ())
            {
              apih = s + he;
              verh = ver ? "version" + he : string ();

              // <stem>[.<hxx-ext>]
              //
              open (sd / apih);
              os << "#pragma once"                                     << endl
                 <<                                                       endl
                 << "#include <string>"                                << endl
                 << "#include <ostream>"                               << endl
                 << "#include <stdexcept>"                             << endl
                 <<                                                       endl
                 << "namespace " << id                                 << endl
                 << "{"                                                << endl
                 << "  // Print a greeting for the specified name into the specified" << endl
                 << "  // stream. Throw std::invalid_argument if the name is empty."  << endl
                 << "  //"                                                            << endl
                 << "  inline void"                                    << endl
                 << "  say_hello (std::ostream& o, const std::string& name)" << endl
                 << "  {"                                              << endl
                 << "    using namespace std;"                         << endl
                 <<                                                       endl
                 << "    if (name.empty ())"                           << endl
                 << "      throw invalid_argument (\"empty name\");"   << endl
                 <<                                                       endl
                 << "    o << \"Hello, \" << name << '!' << endl;"     << endl
                 << "  }"                                              << endl
                 << "}"                                                << endl;
              os.close ();

              break;
            }
            else
            {
              apih = s + he;
              exph = "export" + he;
              verh = ver ? "version" + he : string ();

              // <stem>[.<hxx-ext>]
              //
              open (sd / apih);
              os << "#pragma once"                                     << endl
                 <<                                                       endl
                 << "#include <iosfwd>"                                << endl
                 << "#include <string>"                                << endl
                 <<                                                       endl
                 << "#include <" << ip << exph << ">"                  << endl
                 <<                                                       endl
                 << "namespace " << id                                 << endl
                 << "{"                                                << endl
                 << "  // Print a greeting for the specified name into the specified" << endl
                 << "  // stream. Throw std::invalid_argument if the name is empty."  << endl
                 << "  //"                                                            << endl
                 << "  " << mp << "_SYMEXPORT void"                    << endl
                 << "  say_hello (std::ostream&, const std::string& name);" << endl
                 << "}"                                                << endl;
              os.close ();

              // <stem>.<cxx-ext>
              //
              open (sd / s + xe);
              os << "#include <" << ip << apih << ">"                  << endl
                 <<                                                       endl
                 << "#include <ostream>"                               << endl
                 << "#include <stdexcept>"                             << endl
                 <<                                                       endl
                 << "using namespace std;"                             << endl
                 <<                                                       endl
                 << "namespace " << id                                 << endl
                 << "{"                                                << endl
                 << "  void say_hello (ostream& o, const string& n)"   << endl
                 << "  {"                                              << endl
                 << "    if (n.empty ())"                              << endl
                 << "      throw invalid_argument (\"empty name\");"   << endl
                 <<                                                       endl
                 << "    o << \"Hello, \" << n << '!' << endl;"        << endl
                 << "  }"                                              << endl
                 << "}"                                                << endl;
              os.close ();

              break;
            }
          }

          // export.h[??]
          //
          if (!exph.empty ())
          {
            open (sd / exph);
            os << "#pragma once"                                       << endl
               <<                                                         endl;
            if (l == lang::cxx)
            {
              os << "// Normally we don't export class templates (but do complete specializations)," << endl
                 << "// inline functions, and classes with only inline member functions. Exporting"  << endl
                 << "// classes that inherit from non-exported/imported bases (e.g., std::string)"   << endl
                 << "// will end up badly. The only known workarounds are to not inherit or to not"  << endl
                 << "// export. Also, MinGW GCC doesn't like seeing non-exported functions being"    << endl
                 << "// used before their inline definition. The workaround is to reorder code. In"  << endl
                 << "// the end it's all trial and error."                                           << endl
                 <<                                                                                     endl;
            }
            os << "#if defined(" << mp << "_STATIC)         // Using static."    << endl
               << "#  define " << mp << "_SYMEXPORT"                             << endl
               << "#elif defined(" << mp << "_STATIC_BUILD) // Building static." << endl
               << "#  define " << mp << "_SYMEXPORT"                             << endl
               << "#elif defined(" << mp << "_SHARED)       // Using shared."    << endl
               << "#  ifdef _WIN32"                                             << endl
               << "#    define " << mp << "_SYMEXPORT __declspec(dllimport)"     << endl
               << "#  else"                                                     << endl
               << "#    define " << mp << "_SYMEXPORT"                           << endl
               << "#  endif"                                                    << endl
               << "#elif defined(" << mp << "_SHARED_BUILD) // Building shared." << endl
               << "#  ifdef _WIN32"                                             << endl
               << "#    define " << mp << "_SYMEXPORT __declspec(dllexport)"     << endl
               << "#  else"                                                     << endl
               << "#    define " << mp << "_SYMEXPORT"                           << endl
               << "#  endif"                                                    << endl
               << "#else"                                                       << endl
               << "// If none of the above macros are defined, then we assume we are being used"  << endl
               << "// by some third-party build system that cannot/doesn't signal the library"    << endl
               << "// type. Note that this fallback works for both static and shared but in case" << endl
               << "// of shared will be sub-optimal compared to having dllimport."                << endl
               << "//"                                                                            << endl
               << "#  define " << mp << "_SYMEXPORT         // Using static or shared." << endl
               << "#endif"                                                             << endl;
            os.close ();
          }

          // version.h[??].in
          //
          if (ver)
          {
            open (sd / verh + ".in");

            os << "#pragma once"                                       << endl
               <<                                                         endl
               << "// The numeric version format is AAAAABBBBBCCCCCDDDE where:"<< endl
               << "//"                                                 << endl
               << "// AAAAA - major version number"                    << endl
               << "// BBBBB - minor version number"                    << endl
               << "// CCCCC - bugfix version number"                   << endl
               << "// DDD   - alpha / beta (DDD + 500) version number" << endl
               << "// E     - final (0) / snapshot (1)"                << endl
               << "//"                                                 << endl
               << "// When DDDE is not 0, 1 is subtracted from AAAAABBBBBCCCCC. For example:" << endl
               << "//"                                                 << endl
               << "// Version      AAAAABBBBBCCCCCDDDE"                << endl
               << "//"                                                 << endl
               << "// 0.1.0        0000000001000000000"                << endl
               << "// 0.1.2        0000000001000020000"                << endl
               << "// 1.2.3        0000100002000030000"                << endl
               << "// 2.2.0-a.1    0000200001999990010"                << endl
               << "// 3.0.0-b.2    0000299999999995020"                << endl
               << "// 2.2.0-a.1.z  0000200001999990011"                << endl
               << "//"                                                 << endl
               << "#define " << mp << "_VERSION       $" << v << ".version.project_number$ULL" << endl
               << "#define " << mp << "_VERSION_STR   \"$" << v << ".version.project$\""       << endl
               << "#define " << mp << "_VERSION_ID    \"$" << v << ".version.project_id$\""    << endl
               <<                                                                                  endl
               << "#define " << mp << "_VERSION_MAJOR $" << v << ".version.major$" << endl
               << "#define " << mp << "_VERSION_MINOR $" << v << ".version.minor$" << endl
               << "#define " << mp << "_VERSION_PATCH $" << v << ".version.patch$" << endl
               <<                                                                    endl
               << "#define " << mp << "_PRE_RELEASE   $" << v << ".version.pre_release$" << endl
               <<                                                                          endl
               << "#define " << mp << "_SNAPSHOT_SN   $" << v << ".version.snapshot_sn$ULL"<< endl
               << "#define " << mp << "_SNAPSHOT_ID   \"$" << v << ".version.snapshot_id$\"" << endl;
            os.close ();
          }

          bool binless (l == lang::cxx && l.cxx_opt.binless ());

          // buildfile
          //
          open (sd / buildfile_file);
          os << "int_libs = # Interface dependencies."                 << endl
             << "imp_libs = # Implementation dependencies."            << endl
             << "#import imp_libs += libhello%lib{hello}"              << endl
             <<                                                           endl;

          if (!utest)
          {
            os << "lib{" << s << "}: "                                 <<
              "{" << hs << (binless ? "" : ' ' + x) << "}{**";
            if (ver)
              os << " -version} " << h << "{version}";
            else
              os << "}";
            os << " $imp_libs $int_libs"                               << endl;
          }
          else
          {
            if (binless)
            {
              os << "./: lib{" << s << "}: "                           <<
                "{" << hs << "}{** -**.test...";
              if (ver)
                os << " -version} " << h << "{version} \\"             << endl
                   << " ";
              else
                os << "}";
              os << " $imp_libs $int_libs"                             << endl;
            }
            else
            {
              os << "./: lib{" << s << "}: libul{" << s << "}: "       <<
                "{" << hs << ' ' << x << "}{** -**.test...";
              if (ver)
                os << " -version} \\"                                  << endl
                   << "  " << h << "{version}";
              else
                os << "}";
              os << " $imp_libs $int_libs"                             << endl;
            }

            os <<                                                         endl
               << "# Unit tests."                                      << endl
               << "#"                                                  << endl;

            if (install)
              os << "exe{*.test}:"                                     << endl
                 << "{"                                                << endl
                 << "  test = true"                                    << endl
                 << "  install = false"                                << endl
                 << "}"                                                << endl;
            else
              os << "exe{*.test}: test = true"                         << endl;

            os <<                                                         endl
               << "for t: " << x << "{**.test...}"                     << endl
               << "{"                                                  << endl
               << "  d = $directory($t)"                               << endl
               << "  n = $name($t)..."                                 << endl
               <<                                                         endl
               << "  ./: $d/exe{$n}: $t $d/{" << hs << "}{+$n} $d/testscript{+$n}";

            if (binless)
              os << ' ' << "lib{" << s << "}"                          << endl;
            else
              os << '\n'
                 << "  $d/exe{$n}: libul{" << s << "}: bin.whole = false" << endl;

            os << "}"                                                  << endl;
          }

          if (ver)
            os <<                                                         endl
               << "# Include the generated version header into the distribution (so that we don't" << endl
               << "# pick up an installed one) and don't remove it when cleaning in src (so that"  << endl
               << "# clean results in a state identical to distributed)."                          << endl
               << "#"                                                                              << endl
               << h << "{version}: in{version} $src_root/manifest"     << endl
               << "{"                                                  << endl
               << "  dist  = true"                                     << endl
               << "  clean = ($src_root != $out_root)"                 << endl
               << "}"                                                  << endl;

          // Build.
          //
          os <<                                                           endl
             << "# Build options."                                     << endl
             << "#"                                                    << endl
             << m << ".poptions =+ \"-I$out_root\" \"-I$src_root\""    << endl;

          if (!binless)
            os <<                                                         endl
               << "obja{*}: " << m << ".poptions += -D" << mp << "_STATIC_BUILD" << endl
               << "objs{*}: " << m << ".poptions += -D" << mp << "_SHARED_BUILD" << endl;

          // Export.
          //
          os <<                                                           endl
             << "# Export options."                                    << endl
             << "#"                                                    << endl
             << "lib{" <<  s << "}:"                                   << endl
             << "{"                                                    << endl
             << "  " << m << ".export.poptions = \"-I$out_root\" \"-I$src_root\"" << endl
             << "  " << m << ".export.libs = $int_libs"                << endl
             << "}"                                                    << endl;

          if (!binless)
            os <<                                                         endl
               << "liba{" << s << "}: " << m << ".export.poptions += -D" << mp << "_STATIC" << endl
               << "libs{" << s << "}: " << m << ".export.poptions += -D" << mp << "_SHARED" << endl;

          // Library versioning.
          //
          if (!binless)
            os <<                                                         endl
               << "# For pre-releases use the complete version to make sure they cannot be used" << endl
               << "# in place of another pre-release or the final version. See the version module" << endl
               << "# for details on the version.* variable values."    << endl
               << "#"                                                                            << endl
               << "if $version.pre_release"                                                   << endl
               << "  lib{" << s << "}: bin.lib.version = @\"-$version.project_id\""           << endl
               << "else"                                                                      << endl
               << "  lib{" << s << "}: bin.lib.version = @\"-$version.major.$version.minor\"" << endl;

          // Installation.
          //
          if (install)
            os <<                                                         endl
               << "# Install into the " << ip << " subdirectory of, say, /usr/include/" << endl
               << "# recreating subdirectories."                                        << endl
               << "#"                                                                   << endl
               << "{" << hs << "}{*}:"                                 << endl
               << "{"                                                  << endl
               << "  install         = include/" << ip                 << endl
               << "  install.subdirs = true"                           << endl
               << "}"                                                  << endl;

          os.close ();

          // <base>/.gitignore
          //
          if (ver || utest)
          {
            if (vc == vcs::git)
            {
              open (sd / ".gitignore");
              if (ver)
                os << "# Generated version header."                    << endl
                   << "#"                                              << endl
                   << verh                                             << endl;
              if (utest)
                os <<                                        (ver ? "\n" : "")
                   << "# Unit test executables and Testscript output directories" << endl
                   << "# (can be symlinks)."                           << endl
                   << "#"                                              << endl
                   << "*.test"                                         << endl
                   << "test-*.test"                                    << endl;
              os.close ();
            }
          }

          // <base>/<stem>.test.*
          //
          if (utest)
          {
            switch (l)
            {
            case lang::c:
              {
                // <base>/<stem>.test.c
                //
                open (sd / s + ".test.c");
                os << "#include <stdio.h>"                             << endl
                   << "#include <assert.h>"                            << endl
                   <<                                                     endl
                   << "#include <" << ip << apih << ">"                << endl
                   <<                                                     endl
                   << "int main ()"                                    << endl
                   << "{"                                              << endl
                   << "  return 0;"                                    << endl
                   << "}"                                              << endl;
                os.close ();

                break;
              }
            case lang::cxx:
              {
                // <base>/<stem>.test.<cxx-ext>
                //
                open (sd / s + ".test" + xe);
                os << "#include <cassert>"                             << endl
                   << "#include <iostream>"                            << endl
                   <<                                                     endl
                   << "#include <" << ip << apih << ">"                << endl
                   <<                                                     endl
                   << "int main ()"                                    << endl
                   << "{"                                              << endl
                   <<                                                     endl
                   << "}"                                              << endl;
                os.close ();

                break;
              }
            }
          }

          // build/export.build
          //
          if (!sub)
          {
            open (bd / "export." + build_ext);
            os << "$out_root/"                                         << endl
               << "{"                                                  << endl
               << "  include " << ip                                   << endl
               << "}"                                                  << endl
               <<                                                         endl
               << "export $out_root/" << ip << "$import.target"        << endl;
            os.close ();
          }

          // tests/ (tests subproject).
          //
          if (!itest)
            break;

          dir_path td (dir_path (out) /= "tests");
          mk (td);

          // tests/build/
          //
          dir_path tbd (dir_path (td) / build_dir);
          mk (tbd);

          // tests/build/bootstrap.build
          //
          open (tbd / "bootstrap." + build_ext);
          os << "project = # Unnamed tests subproject."                << endl
             <<                                                           endl
             << "using config"                                         << endl
             << "using test"                                           << endl
             << "using dist"                                           << endl;
          os.close ();

          // tests/build/root.build
          //
          open (tbd / "root." + build_ext);
          switch (l)
          {
          case lang::c:
            {
              // @@ TODO: 'latest' in c.std.
              //
              os //<< "c.std = latest"                                 << endl
                //<<                                                      endl
                << "using c"                                           << endl
                <<                                                        endl
                << "h{*}: extension = h"                               << endl
                << "c{*}: extension = c"                               << endl;
              break;
            }
          case lang::cxx:
            {
              os << "cxx.std = latest"                                 << endl
                 <<                                                       endl
                 << "using cxx"                                        << endl
                 <<                                                       endl;

              if (me) os << "mxx{*}: extension = " << pure_ext (*me)   << endl;
              os         << "hxx{*}: extension = " << pure_ext (he)    << endl;
              if (ie) os << "ixx{*}: extension = " << pure_ext (*ie)   << endl;
              if (te) os << "txx{*}: extension = " << pure_ext (*te)   << endl;
              os         << "cxx{*}: extension = " << pure_ext (xe)    << endl;

              break;
            }
          }
          os <<                                                            endl
             << "# Every exe{} in this subproject is by default a test."<< endl
             << "#"                                                     << endl
             << "exe{*}: test = true"                                   << endl
             <<                                                            endl
             << "# The test target for cross-testing (running tests under Wine, etc)." << endl
             << "#"                                                     << endl
             << "test.target = $" << m << ".target"                     << endl;
          os.close ();

          // tests/build/.gitignore
          //
          if (vc == vcs::git)
          {
            open (tbd / ".gitignore");
            os << "config." << build_ext                               << endl
               << "root/"                                              << endl
               << "bootstrap/"                                         << endl;
            os.close ();
          }

          // tests/buildfile
          //
          open (td / buildfile_file);
          os << "./: {*/ -" << build_dir.posix_representation () << "}" << endl;
          os.close ();

          // tests/.gitignore
          //
          if (vc == vcs::git)
          {
            open (td / ".gitignore");
            os << "# Test executables."                                << endl
               << "#"                                                  << endl
               << "driver"                                             << endl
               <<                                                         endl
               << "# Testscript output directories (can be symlinks)." << endl
               << "#"                                                  << endl
               << "test"                                               << endl
               << "test-*"                                             << endl;
            os.close ();
          }

          // tests/basics/
          //
          td /= "basics";
          mk (td);

          switch (l)
          {
          case lang::c:
            {
              // It would have been nice if we could use something like
              // open_memstream() or fmemopen() but these are not portable.
              // So we resort to tmpfile(), which, turns out, is broken on
              // Windows (it may try to create a file in the root directory of
              // a drive and that may require elevated privileges). So we
              // provide our own implementation for that. Who thought writing
              // portable C could be so hard?

              // tests/basics/driver.c
              //
              open (td / "driver.c");
              os << "#include <stdio.h>"                               << endl
                 << "#include <errno.h>"                               << endl
                 << "#include <string.h>"                              << endl
                 << "#include <assert.h>"                              << endl
                 <<                                                       endl;
              if (ver)
                os << "#include <" << ip << verh << ">"                << endl;
              os << "#include <" << ip << apih << ">"                  << endl
                 <<                                                       endl
                 << "#ifdef _WIN32"                                    << endl
                 << "#define tmpfile mytmpfile"                        << endl
                 << "static FILE *mytmpfile ();"                       << endl
                 << "#endif"                                           << endl
                 <<                                                       endl
                 << "int main ()"                                      << endl
                 << "{"                                                << endl
                 << "  char b[256];"                                   << endl
                 <<                                                       endl
                 << "  /* Basics."                                     << endl
                 << "   */"                                            << endl
                 << "  {"                                              << endl
                 << "    FILE *o = tmpfile ();"                        << endl
                 << "    assert (say_hello (o, \"World\") > 0);"       << endl
                 << "    rewind (o);"                                  << endl
                 << "    assert (fread (b, 1, sizeof (b), o) == 14 &&" << endl
                 << "            strncmp (b, \"Hello, World!\\n\", 14) == 0);" << endl
                 << "    fclose (o);"                                  << endl
                 << "  }"                                              << endl
                 <<                                                       endl
                 << "  /* Empty name."                                 << endl
                 << "   */"                                            << endl
                 << "  {"                                              << endl
                 << "    FILE *o = tmpfile ();"                        << endl
                 << "    assert (say_hello (o, \"\") < 0 && errno == EINVAL);" << endl
                 << "    fclose (o);"                                  << endl
                 << "  }"                                              << endl
                 <<                                                       endl
                 << "  return 0;"                                      << endl
                 << "}"                                                << endl
                 <<                                                       endl
                 << "#ifdef _WIN32"                                    << endl
                 << "#include <windows.h>"                             << endl
                 << "#include <fcntl.h>"                               << endl
                 << "#include <io.h>"                                  << endl
                 <<                                                       endl
                 << "FILE *mytmpfile ()"                               << endl
                 << "{"                                                << endl
                 << "  char d[MAX_PATH + 1], p[MAX_PATH + 1];"         << endl
                 << "  if (GetTempPathA (sizeof (d), d) == 0 ||"       << endl
                 << "      GetTempFileNameA (d, \"tmp\", 0, p) == 0)"  << endl
                 << "    return NULL;"                                 << endl
                 <<                                                       endl
                 << "  HANDLE h = CreateFileA (p,"                     << endl
                 << "                          GENERIC_READ | GENERIC_WRITE," << endl
                 << "                          0,"                     << endl
                 << "                          NULL,"                  << endl
                 << "                          CREATE_ALWAYS,"         << endl
                 << "                          FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE," << endl
                 << "                          NULL);"                 << endl
                 << "  if (h == INVALID_HANDLE_VALUE)"                 << endl
                 << "    return NULL;"                                 << endl
                 <<                                                       endl
                 << "  int fd = _open_osfhandle ((intptr_t) h, _O_RDWR);" << endl
                 << "  if (fd == -1)"                                  << endl
                 << "    return NULL;"                                 << endl
                 <<                                                       endl
                 << "  FILE *f = _fdopen (fd, \"wb+\");"               << endl
                 << "  if (f == NULL)"                                 << endl
                 << "    _close (fd);"                                 << endl
                 <<                                                       endl
                 << "  return f;"                                      << endl
                 << "}"                                                << endl
                 << "#endif"                                           << endl;
              os.close ();

              break;
            }
          case lang::cxx:
            {
              // tests/basics/driver.<cxx-ext>
              //
              open (td / "driver" + xe);
              os << "#include <cassert>"                               << endl
                 << "#include <sstream>"                               << endl
                 << "#include <stdexcept>"                             << endl
                 <<                                                       endl;
              if (ver)
                os << "#include <" << ip << verh << ">"                << endl;
              os << "#include <" << ip << apih << ">"                  << endl
                 <<                                                       endl
                 << "int main ()"                                      << endl
                 << "{"                                                << endl
                 << "  using namespace std;"                           << endl
                 << "  using namespace " << id << ";"                  << endl
                 <<                                                       endl
                 << "  // Basics."                                     << endl
                 << "  //"                                             << endl
                 << "  {"                                              << endl
                 << "    ostringstream o;"                             << endl
                 << "    say_hello (o, \"World\");"                    << endl
                 << "    assert (o.str () == \"Hello, World!\\n\");"   << endl
                 << "  }"                                              << endl
                 <<                                                       endl
                 << "  // Empty name."                                 << endl
                 << "  //"                                             << endl
                 << "  try"                                            << endl
                 << "  {"                                              << endl
                 << "    ostringstream o;"                             << endl
                 << "    say_hello (o, \"\");"                         << endl
                 << "    assert (false);"                              << endl
                 << "  }"                                              << endl
                 << "  catch (const invalid_argument& e)"              << endl
                 << "  {"                                              << endl
                 << "    assert (e.what () == string (\"empty name\"));" << endl
                 << "  }"                                              << endl
                 << "}"                                                << endl;
              os.close ();

              break;
            }
          }

          // tests/basics/buildfile
          //
          open (td / buildfile_file);
          os << "import libs = " << n << "%lib{" << s << "}"           << endl
             <<                                                           endl
             << "exe{driver}: {" << hs << ' ' << x                     <<
            "}{**} $libs testscript{**}"                               << endl;
          // <<                                                         endl
          // << m << ".poptions =+ \"-I$out_root\" \"-I$src_root\""  << endl;
          os.close ();

          break;
        }
      case type::bare:
      case type::empty:
        {
          assert (false);
        }
      }

      break; // Done.
    }
    catch (const io_error& e)
    {
      fail << "unable to write " << cf << ": " << e;
    }

    // Cancel auto-removal of the files we have created.
    //
    for (auto& rm: rms)
      rm.cancel ();

    // packages.manifest
    //
    if (pkg)
    {
      path f (prj / "packages.manifest");
      bool e (exists (f));
      try
      {
        ofdstream os (f, (fdopen_mode::out    |
                          fdopen_mode::create |
                          fdopen_mode::append));
        os << (e ? ":" : ": 1")                                        << endl
           << "location: " << pkg->posix_representation ()             << endl;
        os.close ();
      }
      catch (const io_error& e)
      {
        fail << "unable to write " << f << ": " << e;
      }
    }

    // Run post hooks.
    //
    if (o.post_hook_specified ())
      run_hooks (o.post_hook (), "post");

    if (verb)
      text << "created new " << t << ' ' << (sub ? "source subdirectory" :
                                             pkg ? "package" : "project")
           << ' ' << n << " in " << out;

    // --no-init | --package | --subdirectory
    //
    if (o.no_init () || pkg || sub)
      return 0;

    // Create .bdep/.
    //
    mk (prj / bdep_dir);

    // Initialize tmp directory.
    //
    init_tmp (prj);

    // Everything else requires a database.
    //
    database db (open (prj, trace, true /* create */));

    if (ca || cc)
    {
      package_locations pkgs;

      if (t != type::empty) // prj == pkg
        pkgs.push_back (package_location {move (pkgn), nullopt, dir_path ()});

      strings cfg_args;
      if (cc)
        for (; args.more (); cfg_args.push_back (args.next ())) ;

      configurations cfgs {
        cmd_init_config (
          o,
          o,
          prj,
          pkgs,
          db,
          ca ? o.config_add () : o.config_create (),
          cfg_args,
          ca,
          cc)};

      cmd_init (o, prj, db, cfgs, pkgs, strings () /* pkg_args */);
    }

    return 0;
  }

  default_options_files
  options_files (const char*, const cmd_new_options& o, const strings&)
  {
    // NOTE: remember to update the documentation if changing anything here.

    // bdep.options
    // bdep-{config config-add}.options               # -A
    // bdep-{config config-add config-create}.options # -C
    // bdep-new.options
    // bdep-new-{project|package|subdirectory}.options

    // Use the project directory as a start directory in the
    // package/subdirectory modes and the parent directory of the new project
    // otherwise.
    //
    // Note that we will not validate the command arguments and let cmd_new()
    // complain later in case of an error.
    //
    optional<dir_path> start;

    auto output_parent_dir = [&o] ()
    {
      return normalize (o.output_dir (), "output");
    };

    if (o.package () || o.subdirectory ())
    {
      start =
        o.output_dir_specified () ? output_parent_dir ()                  :
        o.directory_specified  () ? normalize (o.directory (), "project") :
        current_directory ();

      // Get the actual project directory.
      //
      project_package pp (find_project_package (*start,
                                                true /* ignore_not_found */));

      if (!pp.project.empty ())
        start = move (pp.project);
      else if (!o.no_checks ())
        start = nullopt;           // Let cmd_new() fail.
    }
    else // New project.
    {
      start = o.output_dir_specified ()
              ? output_parent_dir ()
              : current_directory ();
    }

    default_options_files r {{path ("bdep.options")}, move (start)};

    auto add = [&r] (const string& n)
    {
      r.files.push_back (path ("bdep-" + n + ".options"));
    };

    if (o.config_add_specified () || o.config_create_specified ())
    {
      add ("config");
      add ("config-add");
    }

    if (o.config_create_specified ())
      add ("config-create");

    add ("new");

    // Add the mode-specific options file.
    //
    add (o.subdirectory () ? "new-subdirectory" :
         o.package ()      ? "new-package"      :
                             "new-project");

    return r;
  }

  cmd_new_options
  merge_options (const default_options<cmd_new_options>& defs,
                 const cmd_new_options& cmd)
  {
    // NOTE: remember to update the documentation if changing anything here.

    // While validating/merging the default options, check for the "remote"
    // hooks presence and prepare the prompt, if that's the case.
    //
    diag_record dr;

    auto verify = [&dr] (const default_options_entry<cmd_new_options>& e,
                         const cmd_new_options&)
    {
      const cmd_new_options& o (e.options);

      auto forbid = [&e] (const char* opt, bool specified)
      {
        if (specified)
          fail (e.file) << opt << " in default options file";
      };

      forbid ("--output-dir|-o",    o.output_dir_specified ());
      forbid ("--directory|-d",     o.directory_specified ());
      forbid ("--package",          o.package ());
      forbid ("--subdirectory",     o.subdirectory ());
      forbid ("--no-checks",        o.no_checks ());
      forbid ("--config-add|-A",    o.config_add_specified ());
      forbid ("--config-create|-C", o.config_create_specified ());
      forbid ("--wipe",             o.wipe ()); // Dangerous.

      if (e.remote && (o.pre_hook_specified () || o.post_hook_specified ()))
      {
        if (dr.empty ())
          dr << text;
        else
          dr << '\n';

        dr << "remote hook commands in " << e.file << ':';

        auto add = [&dr] (const strings& hs, const char* what)
        {
          for (const string& cmd: hs)
            dr << "\n  " << what << ' ' << cmd;
        };

        if (o.pre_hook_specified ())
          add (o.pre_hook (),   "pre: ");

        if (o.post_hook_specified ())
          add (o.post_hook (), "post:");
      }
    };

    cmd_new_options r (merge_default_options (defs, cmd, verify));

    if (!dr.empty ())
    {
      dr.flush ();

      if (!yn_prompt ("execute? [y/n]"))
        throw failed ();
    }

    return r;
  }
}
