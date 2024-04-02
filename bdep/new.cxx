// file      : bdep/new.cxx -*- C++ -*-
// license   : MIT; see accompanying LICENSE file

#include <bdep/new.hxx>

#include <map>
#include <algorithm> // replace()

#include <libbutl/regex.hxx>
#include <libbutl/command.hxx>
#include <libbutl/project-name.hxx>

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
  using type = cmd_new_type;
  using lang = cmd_new_lang;
  using vcs  = cmd_new_vcs;

  // While we don't have any specific requirements for git version here, let's
  // use the lowest common denominator for other bdep commands. Note that
  // *_git() functions require the minimum supported git version as an
  // argument.
  //
  static const semantic_version git_ver {2, 1, 0};

  // License id to full name map.
  //
  // Used for the license full name search for the auto-detected and
  // explicitly specified license ids and for obtaining their canonical case.
  //
  static const map<string, string, icase_compare_string> licenses = {
    {"MIT",               "MIT License"                                       },
    {"BSD-1-Clause",      "BSD 1-Clause License"                              },
    {"BSD-2-Clause",      "BSD 2-Clause \"Simplified\" License"               },
    {"BSD-3-Clause",      "BSD 3-Clause \"New\" or \"Revised\" License"       },
    {"BSD-4-Clause",      "BSD 4-Clause \"Original\" or \"Old\" License"      },
    {"GPL-2.0-only",      "GNU General Public License v2.0 only"              },
    {"GPL-2.0-or-later",  "GNU General Public License v2.0 or later"          },
    {"GPL-3.0-only",      "GNU General Public License v3.0 only"              },
    {"GPL-3.0-or-later",  "GNU General Public License v3.0 or later"          },
    {"LGPL-2.0-only",     "GNU Library General Public License v2 only"        },
    {"LGPL-2.0-or-later", "GNU Library General Public License v2 or later"    },
    {"LGPL-2.1-only",     "GNU Lesser General Public License v2.1 only"       },
    {"LGPL-2.1-or-later", "GNU Lesser General Public License v2.1 or later"   },
    {"LGPL-3.0-only",     "GNU Lesser General Public License v3.0 only"       },
    {"LGPL-3.0-or-later", "GNU Lesser General Public License v3.0 or later"   },
    {"AGPL-1.0-only",     "Affero General Public License v1.0 only"           },
    {"AGPL-1.0-or-later", "Affero General Public License v1.0 or later"       },
    {"AGPL-2.0-only",     "Affero General Public License v2.0 only"           },
    {"AGPL-2.0-or-later", "Affero General Public License v2.0 or later"       },
    {"AGPL-3.0-only",     "GNU Affero General Public License v3.0 only"       },
    {"AGPL-3.0-or-later", "GNU Affero General Public License v3.0 or later"   },
    {"Apache-1.0",        "Apache License 1.0"                                },
    {"Apache-1.1",        "Apache License 1.1"                                },
    {"Apache-2.0",        "Apache License 2.0"                                },
    {"MPL-1.0",           "Mozilla Public License 1.0"                        },
    {"MPL-1.1",           "Mozilla Public License 1.1"                        },
    {"MPL-2.0",           "Mozilla Public License 2.0"                        },
    {"BSL-1.0",           "Boost Software License 1.0"                        },
    {"Unlicense",         "The Unlicense (public domain)"                     },

    {"other: public domain",    "Released into the public domain"             },
    {"other: available source", "Not free/open source with public source code"},
    {"other: proprietary",      "Not free/open source"                        },
    {"other: TODO",             "License is not yet decided"                  }};

  // Extract a license id from a license file returning an empty string if it
  // doesn't match any known license file signatures.
  //
  static string
  extract_license (const path& f)
  {
    // The overall plan is to read the license heading and then try to match it
    // against a bunch of regular expression.
    //
    // Some license headings are spread over multiple lines but all the files
    // that we have seen so far separate the heading from the license body with
    // a blank line, for example:
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

    // Note that some licenses (for example, GNU licenses) don't spell the zero
    // minor version. So for them we may need to provide two properly ordered
    // regular expressions.
    //
    (test ("MIT License",                                            "MIT") ||
     test ("BSD ([1234])-Clause License",                  "BSD-$1-Clause") ||
     test ("Apache License Version ([0-9]+\\.[0-9])",          "Apache-$1") ||
     test ("Mozilla Public License Version ([0-9]+\\.[0-9])",     "MPL-$1") ||
     test ("GNU GENERAL PUBLIC LICENSE Version ([0-9]+)",  "GPL-$1.0-only") ||

     test ("GNU LESSER GENERAL PUBLIC LICENSE Version ([0-9]+\\.[0-9]+)",
           "LGPL-$1-only")                                                  ||

     test ("GNU LESSER GENERAL PUBLIC LICENSE Version ([0-9]+)",
           "LGPL-$1.0-only")                                                ||

     test ("GNU AFFERO GENERAL PUBLIC LICENSE Version ([0-9]+)",
           "AGPL-$1.0-only")                                                ||

     test ("Boost Software License - Version ([0-9]+\\.[0-9]+)",  "BSL-$1") ||

     test ("This is free and unencumbered software released into the "
           "public domain\\.",
           "Unlicense")                                                     ||

     test ("public domain",                           "other: public domain"));

    return r;
  }

  // Extract a summary line from a README.md file returning an empty string if
  // unable to. The project name can be empty.
  //
  static string
  extract_summary (const path& f, const string& pkg, const string& prj)
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
    // We will also treat the heading that doesn't start with <name> as:
    //
    // # <summary>
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

      if (!next () || l.compare (0, 2, "# ") != 0)
        return "";

      l.erase (0, 2); // Remove `# `.

      size_t m;
      auto prefix = [&l, &m] (const string& n) -> bool
      {
        m = n.size ();
        return (l.size () >= m                            &&
                icasecmp (l.c_str (), n.c_str (), m) == 0 &&
                (l.size () == m ||
                 (!alnum (l[m]) && // Separated.
                  l[m] != '_' && l[m] != '-' && l[m] != '+' && l[m] != '.')));
      };

      if (prefix (pkg) || (!prj.empty () && prefix (prj)))
      {
        size_t n (l.size ());
        if (n > m) // # <name> - <summary>
        {
          // Let's try to handle similar cases like `# <name>: <summary>`.
          //
          size_t i (m + 1);

          for (; i != n && !alnum (l[i]); ++i) // Skip separators.
            ;

          l.erase (0, i);
        }
        else if (next ())
        {
          // # <name>
          // <summary>
          //
          // Use the following line as is.
        }
        else
          return "";
      }
      else
      {
        // # <summary>
        //
        // Use this line as is.
      }

      // Sanitize the line.
      //

      // Keep only the first sentence and strip trailing period.
      //
      size_t p (l.find ('.'));
      if (p != string::npos)
        l.resize (p);

      if (!trim (l).empty ())
      {
        // Uppercase the first letter.
        //
        ucase (l, 0, 1);
      }

      return l;
    }
    catch (const io_error& e)
    {
      fail << "unable to read " << f << ": " << e << endf;
    }
  }

  static void
  generate_lib_readme (ostream& os,
                       const package_name& pkg,
                       const lang& l,
                       const optional<string>& sum,
                       bool package_readme)
  {
    const string& p (pkg.string ());
    const string& v (pkg.variable ());
    const string& s (sum && !sum->empty ()
                     ? *sum
                     : "A " + l.string () + " library");

    os << "# " << p << " - " << s                                      << '\n'
       <<                                                                 '\n';

    if (package_readme)
    {
      os << "This is a `build2` package for the [`<UPSTREAM-NAME>`](https://<UPSTREAM-URL>)" << '\n'
         << l << " library. It provides <SUMMARY-OF-FUNCTIONALITY>."   << '\n';
    }
    else
    {
      os << "The `" << p << "` " << l << " library provides <SUMMARY-OF-FUNCTIONALITY>." << '\n';
    }

    os <<                                                                 '\n'
       <<                                                                 '\n'
       << "## Usage"                                                   << '\n'
       <<                                                                 '\n'
       << "To start using `" << p << "` in your project, add the following `depends`" << '\n'
       << "value to your `manifest`, adjusting the version constraint as appropriate:" << '\n'
       <<                                                                 '\n'
       << "```"                                                        << '\n'
       << "depends: " << p << " ^<VERSION>"                            << '\n'
       << "```"                                                        << '\n'
       <<                                                                 '\n'
       << "Then import the library in your `buildfile`:"               << '\n'
       <<                                                                 '\n'
       << "```"                                                        << '\n'
       << "import libs = " << p << "%lib{<TARGET>}"                    << '\n'
       << "```"                                                        << '\n';

    os <<                                                                 '\n'
       <<                                                                 '\n'
       << "## Importable targets"                                      << '\n'
       <<                                                                 '\n'
       << "This package provides the following importable targets:"    << '\n'
       <<                                                                 '\n'
       << "```"                                                        << '\n'
       << "lib{<TARGET>}"                                              << '\n'
       << "```"                                                        << '\n'
       <<                                                                 '\n'
       << "<DESCRIPTION-OF-IMPORTABLE-TARGETS>"                        << '\n';

    os <<                                                                 '\n'
       <<                                                                 '\n'
       << "## Configuration variables"                                 << '\n'
       <<                                                                 '\n'
       << "This package provides the following configuration variables:" << '\n'
       <<                                                                 '\n'
       << "```"                                                        << '\n'
       << "[bool] config." << v << ".<VARIABLE> ?= false"              << '\n'
       << "```"                                                        << '\n'
       <<                                                                 '\n'
       << "<DESCRIPTION-OF-CONFIG-VARIABLES>"                          << '\n';
  }

  // Key differences compared to the library:
  //
  // - Build-time dependency.
  // - We don't need to mention the implementation language.
  // - We use "it is a" rather than "it provides" in the description.
  // - We mention metadata.
  //
  static void
  generate_exe_readme (ostream& os,
                       const package_name& pkg,
                       const optional<string>& sum,
                       bool package_readme)
  {
    const string& p (pkg.string ());
    const string& v (pkg.variable ());
    const string& s (sum && !sum->empty () ? *sum : "An executable");

    os << "# " << p << " - " << s                                      << '\n'
       <<                                                                 '\n';

    if (package_readme)
    {
      os << "This is a `build2` package for the [`<UPSTREAM-NAME>`](https://<UPSTREAM-URL>)" << '\n'
         << "executable. It is a <SUMMARY-OF-FUNCTIONALITY>."          << '\n';
    }
    else
    {
      os << "The `" << p << "` executable is a <SUMMARY-OF-FUNCTIONALITY>." << '\n';
    }

    os <<                                                                 '\n'
       << "Note that the `" << p << "` executable in this package provides `build2` metadata." << '\n';

    os <<                                                                 '\n'
       <<                                                                 '\n'
       << "## Usage"                                                   << '\n'
       <<                                                                 '\n'
       << "To start using `" << p << "` in your project, add the following build-time" << '\n'
       << "`depends` value to your `manifest`, adjusting the version constraint as" << '\n'
       << "appropriate:"                                               << '\n'
       <<                                                                 '\n'
       << "```"                                                        << '\n'
       << "depends: * " << p << " ^<VERSION>"                          << '\n'
       << "```"                                                        << '\n'
       <<                                                                 '\n'
       << "Then import the executable in your `buildfile`:"            << '\n'
       <<                                                                 '\n'
       << "```"                                                        << '\n'
       << "import! [metadata] <TARGET> = " << p << "%exe{<TARGET>}"    << '\n'
       << "```"                                                        << '\n';

    os <<                                                                 '\n'
       <<                                                                 '\n'
       << "## Importable targets"                                      << '\n'
       <<                                                                 '\n'
       << "This package provides the following importable targets:"    << '\n'
       <<                                                                 '\n'
       << "```"                                                        << '\n'
       << "exe{<TARGET>}"                                              << '\n'
       << "```"                                                        << '\n'
       <<                                                                 '\n'
       << "<DESCRIPTION-OF-IMPORTABLE-TARGETS>"                        << '\n';

    os <<                                                                 '\n'
       <<                                                                 '\n'
       << "## Configuration variables"                                 << '\n'
       <<                                                                 '\n'
       << "This package provides the following configuration variables:" << '\n'
       <<                                                                 '\n'
       << "```"                                                        << '\n'
       << "[bool] config." << v << ".<VARIABLE> ?= false"              << '\n'
       << "```"                                                        << '\n'
       <<                                                                 '\n'
       << "<DESCRIPTION-OF-CONFIG-VARIABLES>"                          << '\n';
  }

  // Generate README.md for a multi-package repository root (--type empty).
  //
  static void
  generate_empty_readme (ostream& os,
                         const string& n,
                         bool tp /* third-party */)
  {
    os << "# " << n << " - <SUMMARY>"                                  << '\n'
       <<                                                                 '\n';

    if (tp)
    {
      os << "This is a `build2` package repository for [`" << n << "`](https://<UPSTREAM-URL>)," << '\n'
         << "a <SUMMARY-OF-FUNCTIONALITY>."                            << '\n';
    }
    else
    {
      os << "`" << n << "` is a <SUMMARY-OF-FUNCTIONALITY>."           << '\n';
    }

    os <<                                                                 '\n'
       << "This file contains setup instructions and other details that are more" << '\n'
       << "appropriate for development rather than consumption. If you want to use" << '\n'
       << "`" << n << "` in your `build2`-based project, then instead see the accompanying" << '\n';
    if (tp)
      os << "[`PACKAGE-README.md`](<PACKAGE>/PACKAGE-README.md) file." << '\n';
    else
      os << "package [`README.md`](<PACKAGE>/README.md) file." << '\n';

    os <<                                                                 '\n'
       << "The development setup for `" << n << "` uses the standard `bdep`-based workflow." << '\n'
       << "For example:"                                               << '\n'
       <<                                                                 '\n'
       << "```"                                                        << '\n'
       << "git clone .../" << n << ".git"                              << '\n'
       << "cd " << n                                                   << '\n'
       <<                                                                 '\n'
       << "bdep init -C @gcc cc config.cxx=g++"                        << '\n'
       << "bdep update"                                                << '\n'
       << "bdep test"                                                  << '\n'
       << "```"                                                        << '\n';
  }
}

int bdep::
cmd_new (cmd_new_options&& o, cli::group_scanner& args)
{
  tracer trace ("new");

  // Validate options.
  //
  bool ca (o.config_add_specified ());

  // Type of configuration being created, if --config-create is specified.
  //
  // Note that our --type|-t overrides --type|--config-type from
  // configuration_add_options.
  //
  configuration_add_options& cao (o);
  optional<string> cc (o.config_create_specified ()
                       ? cao.type ()
                       : optional<string> ());

  if (o.subdirectory ())
    fail << "--subdirectory was renamed to --source";

  if (o.package () && o.source ())
    fail << "both --package and --source specified";

  const char* m (o.package () ? "--package" :
                 o.source ()  ? "--source"  : nullptr);

  if (m && o.no_init ())
    fail << "both --no-init and " << m << " specified";

  if (const char* n = (o.no_init () ? "--no-init" :
                       m            ? m           : nullptr))
  {
    if (ca) fail << "both " << n << " and --config-add specified";
    if (cc) fail << "both " << n << " and --config-create specified";
  }

  if (o.directory_specified () && !m)
    fail << "--directory|-d only valid with --package or --source";

  if (const char* n = cmd_config_validate_add (o))
  {
    if (!ca && !cc)
      fail << n << " specified without --config-(add|create)";

    // Note: no --type|... (see above).
    //
    if (cao.type_specified () && !cc)
      fail << "--config-type specified without --config-create";

    if (o.existing () && !cc)
      fail << "--existing|-e specified without --config-create";

    if (o.wipe () && !cc)
      fail << "--wipe specified without --config-create";
  }

  // Validate language options.
  //
  const lang& l (o.lang ());

  // Verify that the specified extension is not empty.
  //
  auto empty_ext = [] (const string& v, const char* o, const char* lang)
  {
    if (v.empty () || (v.size () == 1 && v[0] == '.'))
      fail << "empty extension specified with '" << o << "' " << lang
           << " option";
  };

  switch (l)
  {
  case lang::c:
    {
      auto& o (l.c_opt);

      // If the c++ option is specified, then verify that none of the C++
      // extensions are specified as empty, except for hxx. Otherwise, verify
      // that none of the C++ extensions is specified.
      //
      if (o.cpp ())
      {
        if (o.cxx_specified ()) empty_ext (o.cxx (), "cxx", "c");
        if (o.ixx_specified ()) empty_ext (o.ixx (), "ixx", "c");
        if (o.txx_specified ()) empty_ext (o.txx (), "txx", "c");
        if (o.mxx_specified ()) empty_ext (o.mxx (), "mxx", "c");
      }
      else
      {
        auto unexpected = [] (const char* o)
        {
          fail << "'" << o << "' c option specified without 'c++'";
        };

        if (o.hxx_specified ()) unexpected ("hxx");
        if (o.cxx_specified ()) unexpected ("cxx");
        if (o.ixx_specified ()) unexpected ("ixx");
        if (o.txx_specified ()) unexpected ("txx");
        if (o.mxx_specified ()) unexpected ("mxx");
      }

      break;
    }
  case lang::cxx:
    {
      auto& o (l.cxx_opt);

      if (o.cpp () && o.extension_specified ())
        fail << "'extension' and 'cpp' are mutually exclusive c++ options";

      // Verify that none of the extensions are specified as empty, except for
      // hxx.
      //
      if (o.extension_specified ())
        empty_ext (o.extension (), "extension", "c++");

      if (o.cxx_specified ()) empty_ext (o.cxx (), "cxx", "c++");
      if (o.ixx_specified ()) empty_ext (o.ixx (), "ixx", "c++");
      if (o.txx_specified ()) empty_ext (o.txx (), "txx", "c++");
      if (o.mxx_specified ()) empty_ext (o.mxx (), "mxx", "c++");

      break;
    }
  }

  // Validate type options.
  //
  const type& t (o.type ());

  if ((t == type::exe && t.exe_opt.source_specified ()) ||
      (t == type::lib && t.lib_opt.source_specified ()))
    fail << "--type|-t,source was renamed to --type|-t,subdir";

  // For a library source subdirectory (--source) we don't generate the export
  // stub, integration tests (because there is no export stub), or the version
  // header (because the project name used in the .in file will most likely be
  // wrong). All this seems reasonable for what this mode is expected to be
  // used ("end-product" kind of projects).
  //
  bool third_party    (false); // third-party (only exe and lib)
  bool readme         (false); // !no-readme && !third_party
  bool pkg_readme     (false); // !no-package-readme && third-party
  bool altn           (false); // alt-naming
  bool binless        (false); // binless
  bool itest          (false); // !no-tests
  bool utest          (false); // unit-tests
  bool install        (false); // !no-install
  bool ver            (false); // !no-version && !third_party
  bool no_symexport   (false); // no-symexport || (third_party && !auto_symexport)
  bool auto_symexport (false); // auto-symexport
  bool symexport      (false); // !no-symexport && !binless

  string license;
  bool   license_o (false);
  {
    bool pkg (o.package ());
    bool src (o.source ());

    switch (t)
    {
    case type::exe:
      {
        third_party =  t.exe_opt.third_party ();
        readme      = !t.exe_opt.no_readme () && !third_party && !src;
        pkg_readme  = !t.exe_opt.no_package_readme () && third_party && !src;
        altn        =  t.exe_opt.alt_naming ();
        itest       = !t.exe_opt.no_tests ();
        utest       =  t.exe_opt.unit_tests ();
        install     = !t.exe_opt.no_install ();

        if (!src)
        {
          license   = t.exe_opt.license ();
          license_o = t.exe_opt.license_specified ();
        }
        break;
      }
    case type::lib:
      {
        third_party =  t.lib_opt.third_party ();
        readme      = !t.lib_opt.no_readme () && !third_party && !src;
        pkg_readme  = !t.lib_opt.no_package_readme () && third_party && !src;
        altn        =  t.lib_opt.alt_naming ();
        binless     =  t.lib_opt.binless ();
        itest       = !t.lib_opt.no_tests () && !src;
        utest       =  t.lib_opt.unit_tests ();
        install     = !t.lib_opt.no_install ();
        ver         = !t.lib_opt.no_version () && !third_party && !src;

        if (!src)
        {
          license   = t.lib_opt.license ();
          license_o = t.lib_opt.license_specified ();
        }

        no_symexport   = t.lib_opt.no_symexport ();
        auto_symexport = t.lib_opt.auto_symexport ();

        if (auto_symexport)
        {
          if (binless)
            fail << "both --type|-t,binless and --type|-t,auto-symexport "
                 << "specified";

          if (no_symexport)
            fail << "both --type|-t,no-symexport and --type|-t,auto-symexport "
                 << "specified";
        }

        if (third_party && !auto_symexport)
          no_symexport = true;

        symexport = !no_symexport && !binless;

        break;
      }
    case type::bare:
      {
        if (src)
          fail << "cannot create bare source subdirectory";

        readme  = !t.bare_opt.no_readme ();
        altn    =  t.bare_opt.alt_naming ();
        itest   = !t.bare_opt.no_tests ();
        install = !t.bare_opt.no_install ();

        if (!src)
        {
          license   = t.bare_opt.license ();
          license_o = t.bare_opt.license_specified ();
        }
        break;
      }
    case type::empty:
      {
        if (const char* w = (src ? "source subdirectory" :
                             pkg ? "package"             : nullptr))
          fail << "cannot create empty " << w;

        // Note: don't initialize third_party.
        //
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

  // User-supplied source subdirectory (--type,subdir).
  //
  // Should we derive the C++ namespace from this (e.g., foo::bar from
  // libfoo/bar) and allow its customization (e.g., --type,namespace)? That
  // was the initial impulse but doing this will complicate things quite a
  // bit. In particular, we will have to handle varying indentation levels.
  // On the other hand, our goal is not to produce a project that requires an
  // absolute minimum of changes but rather a project that is easy to
  // tweak. And changing the namespace is straightforward (unlike changing the
  // source subdirectory, which appears in quite a few places). So let's keep
  // it simple for now.
  //
  const dir_path* subdir (t == type::exe ? (t.exe_opt.subdir_specified ()
                                            ? &t.exe_opt.subdir ()
                                            : nullptr) :
                          t == type::lib ? (t.lib_opt.subdir_specified ()
                                            ? &t.lib_opt.subdir ()
                                            : nullptr) :
                          nullptr);

  bool sub_inc; // false if the header subdirectory is omitted.
  bool sub_src; // false if the source subdirectory is omitted.
  {
    bool no_subdir_inc (t == type::lib && t.lib_opt.no_subdir_include ());
    bool no_subdir_src (t == type::lib && t.lib_opt.no_subdir_source ());

    if (no_subdir_inc && no_subdir_src)
    {
      if (subdir != nullptr)
        fail << "both --type|-t,subdir and --type|-t,no-subdir-include,"
             << "no-subdir-source specified";
    }

    bool no_subdir (t == type::exe ? t.exe_opt.no_subdir () :
                    t == type::lib ? t.lib_opt.no_subdir () :
                    false);

    if (no_subdir)
    {
      if (subdir != nullptr)
        fail << "both --type|-t,subdir and --type|-t,no-subdir specified";

      if (no_subdir_inc)
        fail << "both --type|-t,no-subdir and --type|-t,no-subdir-include "
             << "specified";

      if (no_subdir_src)
        fail << "both --type|-t,no-subdir and --type|-t,no-subdir-source "
             << "specified";

      no_subdir_inc = no_subdir_src = true;
    }

    if (no_subdir_inc)
    {
      // Note that the generated header machinery requires the source
      // subdirectory as a prefix for #include directive. Thus, the version
      // header generation needs to be disabled if no-subdir-include is
      // specified.
      //
      if (t == type::lib && ver)
        fail << "generated version header is not supported in this layout" <<
          info << "specify --type|-t,no-version explicitly";
    }

    sub_inc = !no_subdir_inc;
    sub_src = !no_subdir_src;
  }

  if (subdir != nullptr && subdir->absolute ())
    fail << "absolute path " << *subdir << " specified for --type|-t,subdir";

  // Validate vcs options.
  //
  vcs  vc   (o.vcs ());
  bool vc_o (o.vcs_specified ());

  // Skip `--` which separates the name argument, if any, as for example in:
  //
  // $ bdep new -- hello
  //
  bool sep (false);
  if (args.more () && args.peek () == string ("--"))
  {
    sep = true;
    args.next ();
  }

  // Check if we have the argument (name). If not, then we use the specified
  // output or current working directory name.
  //
  string a;
  if (args.more ())
  {
    a = args.next ();

    // Skip `--` which separates the bpkg options, if any, as for example in:
    //
    // $ bdep new -C @cfg hello -- -v cc config.cxx=g++
    //
    if (args.more () && args.peek () == string ("--"))
    {
      sep = true;
      args.next ();
    }

    // Reduce name with a directory component to the simple name with
    // --output-dir case.
    //
    if (path_traits::find_separator (a) != string::npos)
    {
      try
      {
        dir_path p (move (a));
        a = p.leaf ().string ();

        if (!p.simple ())
        {
          if (o.output_dir_specified ())
          {
            fail << "both --output-dir|-o and directory component in name "
                 << "specified";
          }

          o.output_dir (move (p));
          o.output_dir_specified (true);
        }
      }
      catch (const invalid_path& e)
      {
        fail << "invalid path '" << e.path << "'";
      }
    }
  }
  else
  {
    if (!o.output_dir_specified ())
    {
      // Reduce this case (for the logic that follows) to as-if the current
      // working directory was specified as the output directory. Unless we
      // are in the source mode and the subdir sub-option was specified (see
      // the relevant code below for the whole picture).
      //
      if (o.source () && subdir != nullptr)
        a = subdir->leaf ().string ();
      else
      {
        o.output_dir (path::current_directory ());
        o.output_dir_specified (true);
      }
    }

    if (a.empty ())
      a = o.output_dir ().leaf ().string ();
  }

  // If the project type is not empty then the project name is also a package
  // name. But even if it is empty, verify it is a valid package name since it
  // will most likely end up in the 'project' manifest value.
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

  strings cfg_args;
  if (cc)
  {
    // Read the configuration arguments.
    //
    // Also make sure that there is at least one module unless the `--`
    // separator is specified (see cmd_config_create() for details).
    //
    bool module (false);

    while (args.more ())
    {
      string a (args.next ());

      if (!sep)
      {
        if (a.find ('=') == string::npos)
          module = true;
      }

      cfg_args.push_back (move (a));
    }

    if (!sep && !module)
      fail << "no build system module(s) specified for configuration "
           << "to be created" <<
        info << "for example, for C/C++ configuration, specify 'cc'" <<
        info << "use '--' to create configuration without modules" <<
        info << "for example: bdep new -C ... --";
  }

  // Full package name vs base name (e.g., libhello in libhello.bash) vs the
  // name stem (e.g, hello in libhello).
  //
  // We use the full name in the manifest and the top-level directory, the
  // base name for inner filesystem directories and preprocessor macros, while
  // the (sanitized) stem for modules, namespaces, etc.
  //
  const string&   n (pkgn.string ());    // Full name.
  const string&   b (pkgn.base ());      // Base name.
  const string&   e (pkgn.extension ()); // Name extension.
  const string&   v (pkgn.variable ());  // Variable name.
  string          s (b);                 // Name stem.
  {
    // Warn about the lib prefix unless we are creating a source subdirectory,
    // in which case the project is probably not meant to be a package anyway.
    //
    bool w (!o.source ());

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

  // The anatomy and associated terminology of the paths inside the project:
  //
  // libfoo/{src,include}/libfoo
  //    ^         ^          ^
  //    |         |          |
  //  project/  source    source
  //  package   prefix   subdirectory
  //   root

  // Source prefix defaults to project/package root.
  //
  dir_path pfx_inc;
  dir_path pfx_src;

  // Calculate the effective source/include prefixes based on the project type
  // 'prefix*' and 'split' sub-options. Fail if any mutually exclusive
  // sub-options were specified.
  //
  // Note that for the executable project type the include and source prefixes
  // are always the same.
  //
  switch (t)
  {
  case type::exe:
    {
      const cmd_new_exe_options& opt (t.exe_opt);

      if (opt.prefix_specified ())
      {
        // In the source mode --output-dir|-o is the source subdirectory and
        // so implies no source prefix.
        //
        if (o.source () && o.output_dir_specified ())
          fail << "both --output-dir|-o and --type|-t,prefix specified";

        pfx_inc = pfx_src = opt.prefix ();
      }

      break;
    }
  case type::lib:
    {
      const cmd_new_lib_options& opt (t.lib_opt);

      // In the source mode --output-dir|-o is the source subdirectory and so
      // implies no source prefix.
      //
      if (o.source () && o.output_dir_specified ())
      {
        if (opt.split ())
          fail << "both --output-dir|-o and --type|-t,split specified";

        if (opt.prefix_specified ())
          fail << "both --output-dir|-o and --type|-t,prefix specified";

        if (opt.prefix_include_specified ())
          fail << "both --output-dir|-o and --type|-t,prefix-include specified";

        if (opt.prefix_source_specified ())
          fail << "both --output-dir|-o and --type|-t,prefix-source specified";
      }

      if (opt.split ())
      {
        if (opt.prefix_specified ())
          fail << "both --type|-t,split and --type|-t,prefix specified";

        if (opt.prefix_include_specified ())
          fail << "both --type|-t,split and --type|-t,prefix-include specified";

        if (opt.prefix_source_specified ())
          fail << "both --type|-t,split and --type|-t,prefix-source specified";

        pfx_inc = dir_path ("include");
        pfx_src = dir_path ("src");
        break;
      }

      if (opt.prefix_specified ())
      {
        if (opt.prefix_include_specified ())
          fail << "both --type|-t,prefix and --type|-t,prefix-include specified";

        if (opt.prefix_source_specified ())
          fail << "both --type|-t,prefix and --type|-t,prefix-source specified";

        pfx_inc = pfx_src = opt.prefix ();
        break;
      }

      pfx_inc = opt.prefix_include ();
      pfx_src = opt.prefix_source ();
      break;
    }
  case type::bare:
  case type::empty:
    break;
  }

  // Note that in the --source mode, prj is the package (rather than the
  // project) root if the subdirectory is created inside a package directory.
  //
  dir_path           prj; // Project root directory.
  optional<dir_path> pkg; // Package mode/directory relative to project root.
  dir_path           sub; // Source subdirectory relative to source prefix.
  bool               src (false); // Source subdirectory mode.

  dir_path out;         // Project/package root output directory.
  dir_path out_pfx_inc; // Include prefix output directory (out + prefix).
  dir_path out_pfx_src; // Source prefix output directory (out + prefix).
  dir_path out_inc;     // Include output directory (out + prefix + subdir)
  dir_path out_src;     // Source output directory (out + prefix + subdir).

  {
    // In all the cases out_inc and our_src are derived the same way except
    // for the --source mode if --output is specified.
    //
    auto set_out = [&sub,
                    &out, &out_pfx_inc, &out_pfx_src, &out_inc, &out_src,
                    &pfx_inc, &pfx_src,
                    subdir, sub_inc, sub_src]
                   (const string& n)
    {
      sub = (subdir != nullptr  ? *subdir      :
             sub_inc || sub_src ? dir_path (n) :
             dir_path ());

      // Normalize the output directory paths for the sake of comparison.
      //
      out_pfx_inc = out / pfx_inc;
      normalize (out_pfx_inc, "include prefix");

      out_pfx_src = out / pfx_src;
      normalize (out_pfx_src, "source prefix");

      out_inc = out_pfx_inc / (sub_inc ? sub : dir_path ());
      normalize (out_inc, "include subdirectory");

      out_src = out_pfx_src / (sub_src ? sub : dir_path ());
      normalize (out_inc, "source subdirectory");
    };

    // Figure the final output and tentative project directories.
    //
    if (o.package ())
    {
      if (o.directory_specified ())
        prj = normalize (o.directory (), "project directory");
      else
        prj = current_directory ();

      out = o.output_dir_specified () ? o.output_dir () : prj / dir_path (n);
      normalize (out, "output directory");
      set_out (b);
    }
    else if (o.source ())
    {
      // In the source mode --output-dir|-o is the source subdirectory so
      // for this mode we have two ways of specifying the same thing (but
      // see also the output directory fallback above for a special case).
      //
      if (o.output_dir_specified () && subdir != nullptr)
        fail << "both --output-dir|-o and --type|-t,subdir specified";

      if (o.directory_specified ())
        prj = normalize (o.directory (), "project directory");
      else
        prj = current_directory ();

      out = prj;

      if (o.output_dir_specified ())
      {
        // Note that in this case we deffer determining the source
        // subdirectory until the project directory is finalized.
        //
        out_src = o.output_dir ();
        normalize (out_src, "output directory");
        out_inc = out_src;
      }
      else
        set_out (n); // Give user extra rope.
    }
    else
    {
      out = o.output_dir_specified () ? o.output_dir () : dir_path (n);
      normalize (out, "output directory");
      prj = out;
      set_out (b);
    }

    // Get the actual project/package information as "seen" from the output
    // directory.
    //
    project_package pp (find_project_package (out,
                                              true /* allow_subdir */,
                                              true /* ignore_not_found */));

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
    else if (o.source ())
    {
      if (!o.no_checks ())
      {
        if (pp.project.empty () || !pp.package)
          warn << prj << " does not look like a package directory";
      }

      // Note: our prj should actually be the package (i.e., the build system
      // project root).
      //
      if (!pp.project.empty ())
      {
        dir_path pkg (move (pp.project));

        if (pp.package)
          pkg /= *pp.package;

        if (prj != pkg)
          prj = move (pkg);
      }

      // We use this information to form the include directories. The idea is
      // that if the user places the subdirectory somewhere deeper (say into
      // core/libfoo/), then we want the include directives to contain the
      // prefix from the project root (so it will be <core/libfoo/...>) since
      // all our buildfiles are hardwired with -I$src_root.
      //
      // Note also that a crafty user can adjust the prefix by picking the
      // appropriate --directory|-d (i.e., it can point somewhere deeper than
      // the project root). They will need to adjust their buildfiles, however
      // (or we could get smarter by finding the actual package root and
      // adding the difference to -I). Also, some other things, such as the
      // namespace, currently do not contain the prefix.
      //
      if (o.output_dir_specified ()) // out_src == out_inc
      {
        if (!out_src.sub (prj) || out_src == prj)
          fail << "source subdirectory " << out_src << " is not a "
               << "subdirectory of package directory " << prj;

        // Here we treat out as subdirectory unless instructed otherwise in
        // which case we treat it as a prefix.
        //
        // Note: out == prj (is always the case for --source; see above for
        // details).
        //
        dir_path s (out_src.leaf (prj));
        if (sub_inc || sub_src)
          sub = move (s);
        else
          pfx_inc = pfx_src = move (s);

        out_pfx_inc = prj / pfx_inc;
        out_pfx_src = prj / pfx_src;
      }

      src = true;
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
  }

  // We cannot be creating a package and source subdirectory simultaneously
  // and so the pkg and src cannot be both true. However, we can be creating a
  // project in which case none of them are true.
  //
  assert ((!pkg && !src) || pkg.has_value () == !src);

  // We should have failed earlier otherwise.
  //
  assert ((sub_inc || sub_src) == !sub.empty ());

  // Project name.
  //
  // If this is a package in a project (--package mode), then use the
  // project directory name as the project name. Otherwise, the project
  // name is the same as the package and is therefore omitted.
  //
  // In case of a library, we could have used either the full name or the
  // stem without the lib prefix. And it could go either way: if a library
  // is (likely to be) accompanied by an executable (or some other extra
  // packages), then its project should probably be the stem. Otherwise,
  // if it is a standalone library, then the full library name is probably
  // preferred. The stem also has another problem: it could be an invalid
  // project name. So using the full name seems like a simpler and more
  // robust approach.
  //
  // There was also an idea to warn if the project name ends with a digit
  // (think libfoo and libfoo2).
  //
  optional<project_name> prjn;
  if (pkg)
  {
    string p (prj.leaf ().string ());

    if (p != n) // Omit if the same as the package name.
    {
      try
      {
        prjn = project_name (move (p));
      }
      catch (const invalid_argument& e)
      {
        warn << "project name '" << p << "' is invalid: " << e <<
          info << "leaving the 'project' manifest value empty";

        prjn = project_name ();
      }
    }
  }

  bool no_source (binless && !utest);

  // Note that to re-locate a buildfile due to --type|-t,buildfile-in-prefix,
  // both the source prefix directory and the source subdirectory needs to be
  // present. In the split layout this may not be the case for either of the
  // header or source buildfile but not for both, in which case we fail.
  //
  bool bfile_in_pfx_inc (false);
  bool bfile_in_pfx_src (false);
  {
    bool bfile_in_pfx (t == type::exe ? t.exe_opt.buildfile_in_prefix () :
                       t == type::lib ? t.lib_opt.buildfile_in_prefix () :
                       false);

    if (bfile_in_pfx)
    {
      bfile_in_pfx_inc = !pfx_inc.empty () && sub_inc;
      bfile_in_pfx_src = !pfx_src.empty () && sub_src;

      if (!bfile_in_pfx_inc && !bfile_in_pfx_src)
        fail << "--type|-t,buildfile-in-prefix requires both source prefix "
             << "and source subdirectory";

      // Note that we don't need to check for the executable type of the
      // project here, since for such a project bfile_in_pfx_{inc,src} are
      // both either true or false.
      //
      if (!bfile_in_pfx_inc)
        info << "--type|-t,buildfile-in-prefix ignored for include prefix "
             << "because either prefix or subdirectory is absent";

      if (!bfile_in_pfx_src && !no_source)
        info << "--type|-t,buildfile-in-prefix ignored for source prefix "
             << "because either prefix or subdirectory is absent";

      // Issue a warning if on POSIX the executable file name may potentially
      // clash with the source subdirectory name.
      //
      if (t == type::exe)
      {
        assert (!sub.empty ()); // Would have already failed otherwise.

        if (*sub.begin () == s)
          warn << "source subdirectory " << sub << " may clash with "
               << "executable file name on some platforms" <<
            info << "consider specifying alternative subdirectory with "
                 << "--type|-t,subdir";
      }
    }
  }

  // Note that the header and source directories may differ due to different
  // source prefixes (--type|-t,prefix-{include,source}) as well as different
  // source subdirectories (--type|-t,no-sub-source,no-sub-include).
  //
  bool split_source (out_inc != out_src);

  // In the source directory split mode allowing the header or source
  // directories to be the project/package root directory could end up with
  // clashing of the root, header, and/or source .gitignore files in different
  // combinations. For the sake of simplicity let's not support it for now.
  //
  if (split_source)
  {
    if (out_inc == out)
      fail << "split header directory is project/package root";

    if (out_src == out)
      fail << "split source directory is project/package root";
  }

  const dir_path& inc_bfile_dir (!bfile_in_pfx_inc ? out_inc : out_pfx_inc);
  const dir_path& src_bfile_dir (!bfile_in_pfx_src ? out_src : out_pfx_src);

  // Note that a library's buildfile can be split into the header and source
  // buildfiles due to different source prefixes
  // (--type|-t,prefix-{include,source}) as well as different source
  // subdirectories (--type|-t,no-sub-source,no-sub-include). Also note that
  // the source directories split and the buildfiles split are orthogonal.
  // Specifically, there can be the source directory split without the
  // buildfile split (due to buildfiles being merged as result of the
  // buildfile-in-prefix sub-option) and the buildfile split without the
  // source directory split (some degenerate cases; see new/no-cfg/split tests
  // for details).
  //
  bool split_buildfile (inc_bfile_dir != src_bfile_dir);

  // In the buildfile split mode allowing the header or source buildfile
  // directories to be the project/package root directory could end up with
  // clashing of the root, header, and/or source buildfiles in different
  // combinations. For the sake of simplicity let's not support it for now.
  //
  if (split_buildfile)
  {
    if (inc_bfile_dir == out)
      fail << "header buildfile directory is project/package root";

    if (src_bfile_dir == out)
      fail << "source buildfile directory is project/package root";
  }

  // Fail in the split layout if the header buildfile "hides" the source
  // buildfile. Note that the source buildfile doesn't "hide" the header
  // buildfile since it includes it directly, unless this is a binless
  // library (with unit tests).
  //
  if (split_buildfile && !no_source)
  {
    if (src_bfile_dir.sub (inc_bfile_dir))
      fail << "split layout where source buildfile directory is a "
           << "subdirectory of header buildfile directory is not supported" <<
        info << "header buildfile directory: " << inc_bfile_dir <<
        info << "source buildfile directory: " << src_bfile_dir;

    if (binless && inc_bfile_dir.sub (src_bfile_dir))
      fail << "split layout where header buildfile directory is a "
           << "subdirectory of source buildfile directory is not supported "
           << "for binless library" <<
        info << "source buildfile directory: " << src_bfile_dir <<
        info << "header buildfile directory: " << inc_bfile_dir;
  }

  // Merging the source and root directory buildfiles is a bit hairy when the
  // project has the functional/integration tests subproject. Thus, we require
  // them to be explicitly disabled. Note however, that getting rid of this
  // requirement is not too complicated and can be considered in the future.
  //
  if (t == type::lib && itest && src_bfile_dir == out)
    fail << "functional/integration testing is not supported in this layout" <<
      info << "specify --type|-t,no-tests explicitly";

  // Create the output directory if it doesn't exist. Note that we are ok with
  // it already existing and containing some things; see below for details.
  //
  if (!exists (out))
    mk_p (out);

  // Run pre/post hooks.
  //
  auto run_hooks = [&prj, &src, &pkg, &n, &b, &s, &t, &l, &vc,
                    &out, &pfx_inc, &pfx_src, &sub]
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

    add_var ("mode", src ? "source" : pkg ? "package" : "project");
    add_var ("name", n);
    add_var ("base", b);
    add_var ("stem", s);

    if (pfx_inc != pfx_src)
    {
      add_var ("inc",  pfx_inc.string ());
      add_var ("src",  pfx_src.string ());
    }
    else
      add_var ("pfx",  pfx_src.string ());

    add_var ("sub",  sub.string ());
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
        fail << "unable to execute " << what << " hook '" << cmd << "': " << e;
      }
      // Also handles process_error exception (derived from system_error).
      //
      catch (const system_error& e)
      {
        fail << "unable to execute " << what << " hook '" << cmd << "': " << e;
      }
    }
  };

  // Run pre hooks.
  //
  if (o.pre_hook_specified ())
    run_hooks (o.pre_hook (), "pre");

  // Check if certain things already exist.
  //
  optional<vcs::vcs_type> vc_e;         // Detected version control system.
  optional<string>        readme_e;     // Extracted summary line.
  optional<path>          readme_f;     // README file path.
  optional<path>          pkg_readme_f; // PACKAGE-README file path.
  optional<path>          changes_f;    // NEWS/ChangeLog file path.
  optional<string>        license_e;    // Extracted license id.
  optional<path>          license_f;    // LICENSE file path.
  optional<path>          copyright_f;  // COPYRIGHT file path.
  optional<path>          authors_f;    // AUTHORS file path.
  {
    if (!src)
    {
      if (!pkg)
      {
        if (git_repository (out))
          vc_e = vcs::git;
      }

      path f;

      // README.md
      //
      if (exists ((f = out / "README.md")))
      {
        readme_e = extract_summary (f, n, prjn ? prjn->string () : string ());

        if (readme_e->empty ())
          warn << "unable to extract project summary from " << f <<
            info << "using generic summary in manifest";

        readme_f = move (f);
      }
      else if (third_party)
      {
        // Recognize a few more README equivalents in the third-party mode
        // (but without an attempt to extract the summary).
        //
        // Note: we should probably only recognize extensions that are also
        // recognized in the description-file manifest value.
        //
        if (exists ((f = out / "README")) ||
            exists ((f = out / "README.txt")))
        {
          warn << "unable to extract project summary from " << f <<
            info << "using generic summary in manifest";

          readme_f = move (f);
        }
        else
          warn << "no existing README.md (or equivalent) file in third-party "
               << "package";
      }

      // PACKAGE[-_]README.md
      // README[-_]PACKAGE.md
      //
      // Note that we are checking for this file even if not in the third-
      // party mode. Feels harmless and could be potentially useful.
      //
      if (exists ((f = out / "PACKAGE-README.md")) ||
          exists ((f = out / "PACKAGE_README.md")) ||
          exists ((f = out / "README-PACKAGE.md")) ||
          exists ((f = out / "README_PACKAGE.md")))
      {
        // If we have no README.md but have PACKAGE-README.md, might as well
        // try to extract the summary from that.
        //
        if (!readme_f)
        {
          readme_e = extract_summary (f, n, prjn ? prjn->string () : string ());

          if (readme_e->empty ())
            warn << "unable to extract project summary from " << f <<
              info << "using generic summary in manifest";
        }

        pkg_readme_f = move (f);
      }

      // NEWS{.md,txt,}
      // {CHANGES,Changes}{.md,.txt,}
      // {CHANGELOG,ChangeLog}{.md,.txt,}
      //
      // Note: some projects may have several, for example, NEWS and ChangeLog
      // and we look in a specific order (from the high-level overview to more
      // detailed).
      //
      // Note: we should probably only recognize extensions that are also
      // recognized in the changes-file manifest value.
      //
      // Other things seen in the wild:
      //
      // RELEASE-NOTES (curl)
      // WhatsNew.txt (SDL2)
      //
      if (exists ((f = out / "NEWS.md"))       ||
          exists ((f = out / "NEWS.txt"))      ||
          exists ((f = out / "NEWS"))          ||
          exists ((f = out / "CHANGES.md"))    ||
          exists ((f = out / "CHANGES.txt"))   ||
          exists ((f = out / "CHANGES"))       ||
          exists ((f = out / "Changes.md"))    ||
          exists ((f = out / "Changes.txt"))   ||
          exists ((f = out / "Changes"))       ||
          exists ((f = out / "CHANGELOG.md"))  ||
          exists ((f = out / "CHANGELOG.txt")) ||
          exists ((f = out / "CHANGELOG"))     ||
          exists ((f = out / "ChangeLog.md"))  ||
          exists ((f = out / "ChangeLog.txt")) ||
          exists ((f = out / "ChangeLog")))
      {
        changes_f = move (f);
      }

      // LICENSE
      // LICENSE.txt
      // LICENSE.md
      // COPYING
      // UNLICENSE
      //
      // Note that LICENSE.md is often just a plain text file without any
      // markup so we should be able to detect the license.
      //
      if (exists ((f = out / "LICENSE"))     ||
          exists ((f = out / "LICENSE.txt")) ||
          exists ((f = out / "LICENSE.md"))  ||
          exists ((f = out / "COPYING"))     ||
          exists ((f = out / "UNLICENSE")))
      {
        license_e = extract_license (f);

        if (license_e->empty () && !license_o)
        {
          diag_record dr; if (third_party) dr << warn; else dr << fail;

          dr << "unable to guess project license from " << f <<
            info << "use --type|-t,license sub-option to specify explicitly";

          if (third_party)
            dr << info << "or adjust manually in manifest";

          license_e = nullopt;
        }

        license_f = move (f);
      }
      else if (third_party)
        warn << "no existing LICENSE (or equivalent) file in third-party "
             << "package";

      // COPYRIGHT
      //
      if (exists ((f = out / "COPYRIGHT")))
      {
        copyright_f = move (f);
      }

      // AUTHORS
      //
      // Note that some projects distinguish between AUTHORS (legal names for
      // copyright purposes) and CONTRIBUTORS (individuals that have
      // contribute and/or are allowed to contribute to the project). It's not
      // clear whether we should distribute/install CONTRIBUTORS.
      //
      if (exists ((f = out / "AUTHORS")))
      {
        authors_f = move (f);
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

    if (license_e)
    {
      if (!license_o)
      {
        // We should have failed/cleared if the license wasn't recognized.
        //
        assert (!license_e->empty ());

        license = *license_e;
      }
      else if (!license_e->empty () && icasecmp (*license_e, license) != 0)
        fail << "extracted license does not match requested" <<
          info << "extracted: " << *license_e <<
          info << "requested: " << license;
    }
  }

  // Initialize the version control system. Do it before writing anything
  // ourselves in case it fails. Also, the email discovery may do the VCS
  // detection.
  //
  if (!vc_e && !pkg && !src)
  {
    switch (vc)
    {
    case vcs::git:
      {
        const cmd_new_git_options& opt (vc.git_opt);

        // If the branch name is specified, then pass it to git-init using the
        // --initial-branch option if the git version is at least 2.28.0 (the
        // one which has introduced this option). Otherwise, run git-init
        // normally and then change HEAD to refer to the requested name. Note
        // that the branch will only be created on the first commit.
        //
        if (opt.branch_specified ())
        {
          if (git_try_check_version (semantic_version {2, 28, 0},
                                     true /* system */))
          {
            run_git (git_ver, true /* system */, nullptr /* repo */,
                     "init", "-q", "--initial-branch", opt.branch (), out);
          }
          else
          {
            run_git (git_ver, true /* system */, nullptr /* repo */,
                     "init", "-q", out);

            run_git (git_ver, true /* system */, out,
                     "symbolic-ref",
                     "-q",
                     "HEAD",
                     "refs/heads/" + opt.branch ());
          }
        }
        else
          run_git (git_ver, true /* system */, nullptr /* repo */,
                   "init", "-q", out);

        break;
      }
    case vcs::none: break;
    }
  }

  // We support creating a project that already contains some files provided
  // none of them conflict with what we are trying to create (with a few
  // exceptions such as LICENSE and README.md that are handled explicitly plus
  // packages.manifest to which we append last).
  //
  // While we could verify at the outset that none of the files we will be
  // creating exist, that would be quite unwieldy. So instead we are going to
  // fail as we go along but, in this case, also cleanup any files that we
  // have already created.
  //
  vector<auto_rmfile> rms;
  for (path cf;;) // Breakout loop with the current file being written.
  try
  {
    ofdstream os;
    auto open = [&cf, &os, &rms] (path f)
    {
      mk_p (f.directory ());

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

    // .gitignore & .gitattributes
    //
    // Write the root .gitignore file content to the stream, optionally adding
    // an additional newline at the end.
    //
    auto write_root_gitignore = [&os, &pkg, t] (bool newline = false)
    {
      if (!pkg)
        os << bdep_dir.posix_representation ()                         << '\n'
           <<                                                             '\n'
           << "# Local default options files."                         << '\n'
           << "#"                                                      << '\n'
           << ".build2/local/"                                         << '\n';
      if (t != type::empty)
      {
        if (!pkg)
          os <<                                                           '\n';
        os << "# Compiler/linker output."                              << '\n'
           << "#"                                                      << '\n'
           << "*.d"                                                    << '\n'
           << "*.t"                                                    << '\n'
           << "*.i"                                                    << '\n'
           << "*.i.*"                                                  << '\n'
           << "*.ii"                                                   << '\n'
           << "*.ii.*"                                                 << '\n'
           << "*.o"                                                    << '\n'
           << "*.obj"                                                  << '\n'
           << "*.gcm"                                                  << '\n'
           << "*.pcm"                                                  << '\n'
           << "*.ifc"                                                  << '\n'
           << "*.so"                                                   << '\n'
           << "*.dylib"                                                << '\n'
           << "*.dll"                                                  << '\n'
           << "*.a"                                                    << '\n'
           << "*.lib"                                                  << '\n'
           << "*.exp"                                                  << '\n'
           << "*.pdb"                                                  << '\n'
           << "*.ilk"                                                  << '\n'
           << "*.exe"                                                  << '\n'
           << "*.exe.dlls/"                                            << '\n'
           << "*.exe.manifest"                                         << '\n'
           << "*.pc"                                                   << '\n';
      }

      // Only print the newline if anything is printed.
      //
      if (newline && (!pkg || t != type::empty))
        os <<                                                            '\n';
    };

    // See also tests/.gitignore below.
    //
    if (vc == vcs::git)
    {
      if (!src && out != out_src)
      {
        // Note: use POSIX directory separators in these files.
        //
        open (out / ".gitignore");
        write_root_gitignore ();
        os.close ();
      }

      if (!pkg && !src)
      {
        open (out / ".gitattributes");
        os << "# This is a good default: files that are auto-detected by git to be text are" << '\n'
           << "# converted to the platform-native line ending (LF on Unix, CRLF on Windows)" << '\n'
           << "# in the working tree and to LF in the repository."     << '\n'
           << "#"                                                      << '\n'
           << "* text=auto"                                            << '\n'
           <<                                                             '\n'
           << "# Use `eol=crlf` for files that should have the CRLF line ending both in the" << '\n'
           << "# working tree (even on Unix) and in the repository."   << '\n'
           << "#"                                                      << '\n'
           << "#*.bat text eol=crlf"                                   << '\n'
           <<                                                             '\n'
           << "# Use `eol=lf` for files that should have the LF line ending both in the"     << '\n'
           << "# working tree (even on Windows) and in the repository." << '\n'
           << "#"                                                      << '\n'
           << "#*.sh text eol=lf"                                      << '\n'
           <<                                                             '\n'
           << "# Use `binary` to make sure certain files are never auto-detected as text."   << '\n'
           << "#"                                                      << '\n'
           << "#*.png binary"                                          << '\n';
        os.close ();
      }
    }

    // repositories.manifest
    //
    if (!pkg && !src)
    {
      open (out / "repositories.manifest");
      os << ": 1"                                                      << '\n'
         << "summary: " << n << " project repository"                  << '\n'
         <<                                                               '\n'
         << "#:"                                                       << '\n'
         << "#role: prerequisite"                                      << '\n'
         << "#location: https://pkg.cppget.org/1/stable"               << '\n'
         << "#trust: ..."                                              << '\n'
         <<                                                               '\n'
         << "#:"                                                       << '\n'
         << "#role: prerequisite"                                      << '\n'
         << "#location: https://git.build2.org/hello/libhello.git"     << '\n';
      os.close ();
    }

    // README.md
    //
    if (!readme_f && readme)
    {
      open (*(readme_f = out / "README.md"));
      switch (t)
      {
      case type::exe:
        {
          generate_exe_readme (os, pkgn, readme_e, false /*package_readme*/);
          break;
        }
      case type::lib:
        {
          generate_lib_readme (os, pkgn, l, readme_e, false /*package_readme*/);
          break;
        }
      case type::bare:
        {
          os << "# " << n                                              << '\n';
          break;
        }
      case type::empty:
        {
          generate_empty_readme (os, n, t.empty_opt.third_party ());
          break;
        }
      }
      os.close ();
    }

    // PACKAGE-README.md
    //
    if (!pkg_readme_f && pkg_readme)
    {
      open (*(pkg_readme_f = out / "PACKAGE-README.md"));
      switch (t)
      {
      case type::exe:
        {
          generate_exe_readme (os, pkgn, readme_e, true /*package_readme*/);
          break;
        }
      case type::lib:
        {
          generate_lib_readme (os, pkgn, l, readme_e, true /*package_readme*/);
          break;
        }
      case type::bare:
      case type::empty:
        assert (false);
      }
      os.close ();
    }

    if (t == type::empty)
      break; // Done.

    // manifest
    //
    if (!src)
    {
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
      }

      open (out / "manifest");
      os << ": 1"                                                      << '\n'
         << "name: " << n                                              << '\n'
         << "version: 0.1.0-a.0.z"                                     << '\n';

      // Add the type value unless the package type is implied by the package
      // name, that is, the type is binful lib and the package name starts
      // with lib or the type is exe and the name doesn't start with lib.
      //
      switch (t)
      {
      case type::exe:
        {
          if (n.size () > 3 && n.compare (0, 3, "lib") == 0)
            os << "type: exe"                                          << '\n';
          break;
        }
      case type::lib:
        {
          if (t.lib_opt.binless ())
            os << "type: lib,binless"                                  << '\n';
          else if (!(n.size () > 3 && n.compare (0, 3, "lib") == 0))
            os << "type: lib"                                          << '\n';
          break;
        }
      case type::bare:
        {
          os << "type: other"                                          << '\n';
          break;
        }
      case type::empty:
        {
          assert (false);
        }
      }

      // Add the language value(s) unless one language is used and it's
      // implied by the package name, that is it matches the package name
      // extension.
      //
      // @@ TODO: add support for --lang c,c++=impl
      //
      switch (l)
      {
      case lang::c:
        {
          bool cpp (l.c_opt.cpp ());
          if (cpp || e != "c")
          {
            os <<   "language: c"                                      << '\n';
            if (cpp)
              os << "language: c++"                                    << '\n';
          }
          break;
        }
      case lang::cxx:
        {
          bool c (l.cxx_opt.c ());
          if (c || e != "c++")
          {
            os <<   "language: c++"                                    << '\n';
            if (c)
              os << "language: c"                                      << '\n';
          }
          break;
        }
      }

      if (prjn)
        os << "project: " << *prjn                                     << '\n';
      if (readme_e && !readme_e->empty ())
        os << "summary: " << *readme_e                                 << '\n';
      else
        os << "summary: " << s << " " << l << " " << t                 << '\n';
      if (ln.empty ())
        os << "license: " << license                                   << '\n';
      else
        os << "license: " << license << " ; " << ln << "."             << '\n';
      if (readme_f)
        os << "description-file: " << readme_f->leaf (out).posix_representation () << '\n';
      if (pkg_readme_f)
        os << "package-description-file: " << pkg_readme_f->leaf (out).posix_representation () << '\n';
      if (changes_f)
        os << "changes-file: " << changes_f->leaf (out).posix_representation () << '\n';
      if (!third_party)
        os << "url: https://example.org/" << (prjn ? prjn->string () : n)<< '\n'
           << "email: " << pe                                          << '\n';
      else
        os << "url: https://example.org/upstream"                      << '\n'
           << "email: upstream@example.org"                            << '\n'
           << "package-url: https://github.com/build2-packaging/" << (prjn ? prjn->string () : n) << '\n'
           << "package-email: packaging@build2.org ; Mailing list."    << '\n';
      if (!third_party)
        os << "#build-error-email: " << pe                             << '\n';
      os << "depends: * build2 >= 0.16.0"                              << '\n'
         << "depends: * bpkg >= 0.16.0"                                << '\n';
      if (!third_party)
        os  << "#depends: libhello ^1.0.0"                             << '\n';
      os.close ();
    }

    string mp; // Primary language module.
    string mc; // Common language module if this a multi-language project and
               // the (primary) language module otherwise. Used for setting
               // common compiler/linker options, etc.
    string xa; // All source target types.
    string ha; // All header-like target types.
    string hg; // Generated headers (version, export, etc) target type.
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
        const auto& opt (l.c_opt);

        mp = "c";
        mc = !opt.cpp () ? "c" : "cc";
        xa = !opt.cpp () ? "c" : "c cxx";
        ha = !opt.cpp () ? "h" : "h hxx";
        hg = "h";
        xe = ".c";
        he = ".h";

        if (opt.cpp ())
        {
          // If specified (s) return the extension (v) or return the default
          // (d) if any.
          //
          auto ext = [] (bool s,
                         const string& v,
                         const char* d = nullptr) -> optional<string>
          {
            optional<string> r;

            if (s)
              r = v;
            else if (d != nullptr)
              r = d;

            // Add leading dot if absent.
            //
            if (r && !r->empty () && r->front () != '.')
              r = '.' + *r;

            return r;
          };

          xe = *ext (opt.cxx_specified (), opt.cxx (), "cxx");
          he = *ext (opt.hxx_specified (), opt.hxx (), "hxx");

          // We only want default .ixx/.txx/.mxx if the user didn't specify
          // any of the extension-related options explicitly.
          //
          bool d (!opt.cxx_specified () &&
                  !opt.hxx_specified () &&
                  !opt.ixx_specified () &&
                  !opt.txx_specified () &&
                  !opt.mxx_specified ());

          ie = ext (opt.ixx_specified (), opt.ixx (), d ? "ixx" : nullptr);
          te = ext (opt.txx_specified (), opt.txx (), d ? "txx" : nullptr);

          // For now only include mxx in buildfiles if its extension was
          // explicitly specified with mxx=.
          //
          me = ext (opt.mxx_specified (), opt.mxx ());

          if (ie) ha += " ixx";
          if (te) ha += " txx";
          if (me) ha += " mxx";
        }

        break;
      }
    case lang::cxx:
      {
        const auto& opt (l.cxx_opt);

        mp = "cxx";
        mc = !opt.c () ? "cxx" : "cc";
        xa = !opt.c () ? "cxx" : "cxx c";
        ha = "hxx"; // Note: 'h' target type will potentially be added below.
        hg = "hxx";

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

        // We only want default .ixx/.txx/.mxx if the user didn't specify any
        // of the extension-related options explicitly.
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
        me = ext (opt.mxx_specified (), opt.mxx (), nullopt);

        if (ie)       ha += " ixx";
        if (te)       ha += " txx";
        if (me)       ha += " mxx";
        if (opt.c ()) ha += " h";

        break;
      }
    }

    // Return the pointer to the extension suffix after the leading dot or to
    // the extension beginning if it is empty.
    //
    auto pure_ext = [] (const string& e)
    {
      assert (e.empty () || e[0] == '.');
      return e.c_str () + (e.empty () ? 0 : 1);
    };

    // Write the C and C++ module initialization code to the stream,
    // optionally adding an additional newline at the end.
    //
    auto write_c_module = [&os] (bool newline = false)
    {
      // @@ TODO: 'latest' in c.std.
      //
      // << "c.std = latest"                                           << '\n'
      // <<                                                               '\n'
      os << "using c"                                                  << '\n'
         <<                                                               '\n'
         << "h{*}: extension = h"                                      << '\n'
         << "c{*}: extension = c"                                      << '\n';

#if 0
      os <<                                                               '\n'
         << "# Assume headers are importable unless stated otherwise." << '\n'
         << "#"                                                        << '\n'
         << "h{*}: c.importable = true"                                << '\n';
#endif

      if (newline)
        os <<                                                             '\n';
    };

    auto write_cxx_module = [&os, &me, &he, &ie, &te, &xe, &pure_ext]
                            (bool newline = false)
    {
      os << "cxx.std = latest"                                         << '\n'
         <<                                                               '\n'
         << "using cxx"                                                << '\n'
         <<                                                               '\n';

      if (me) os << "mxx{*}: extension = " << pure_ext (*me)           << '\n';
      os         << "hxx{*}: extension = " << pure_ext (he)            << '\n';
      if (ie) os << "ixx{*}: extension = " << pure_ext (*ie)           << '\n';
      if (te) os << "txx{*}: extension = " << pure_ext (*te)           << '\n';
      os         << "cxx{*}: extension = " << pure_ext (xe)            << '\n';

#if 0
      os <<                                                               '\n'
         << "# Assume headers are importable unless stated otherwise." << '\n'
         << "#"                                                        << '\n'
         << "hxx{*}: cxx.importable = true"                            << '\n';
#endif

      if (newline)
        os <<                                                             '\n';
    };

    // build/
    //
    dir_path bd;
    if (!src)
    {
      bd = out / build_dir;

      // build/bootstrap.build
      //
      open (bd / "bootstrap." + build_ext);
      os << "project = " << n                                          << '\n';
      if (o.no_amalgamation ())
        os << "amalgamation = # Disabled."                             << '\n';
      os <<                                                               '\n'
         << "using version"                                            << '\n'
         << "using config"                                             << '\n';
      if (itest || utest)
        os << "using test"                                             << '\n';
      if (install)
        os << "using install"                                          << '\n';
      os << "using dist"                                               << '\n';
      os.close ();

      // build/root.build
      //
      // Note: see also tests/build/root.build below.
      //
      open (bd / "root." + build_ext);

      os << "# Uncomment to suppress warnings coming from external libraries." << '\n'
         << "#"                                                        << '\n'
         << "#" << mc << ".internal.scope = current"                   << '\n'
         <<                                                               '\n';

      switch (l)
      {
      case lang::c:
        {
          write_c_module (l.c_opt.cpp ());

          if (l.c_opt.cpp ())
            write_cxx_module ();

          break;
        }
      case lang::cxx:
        {
          write_cxx_module (l.cxx_opt.c ());

          if (l.cxx_opt.c ())
            write_c_module ();

          break;
        }
      }

      if (itest || utest)
        os <<                                                             '\n'
           << "# The test target for cross-testing (running tests under Wine, etc)."   << '\n'
           << "#"                                                      << '\n'
           << "test.target = $" << mp << ".target"                     << '\n';

      os.close ();

      // build/.gitignore
      //
      if (vc == vcs::git)
      {
        open (bd / ".gitignore");
        os << "/config." << build_ext                                  << '\n'
           << "/root/"                                                 << '\n'
           << "/bootstrap/"                                            << '\n'
           << "build/"                                                 << '\n';
        os.close ();
      }
    }

    // buildfile
    //
    // Write the root directory doc type prerequisites to the stream,
    // optionally adding the trailing newline.
    //
    auto write_doc_prerequisites = [&os, &out,
                                    &readme_f,
                                    &pkg_readme_f,
                                    &changes_f,
                                    &license_f,
                                    &copyright_f,
                                    &authors_f] (bool newline = false)
    {
      if (readme_f     ||
          pkg_readme_f ||
          changes_f    ||
          license_f    ||
          copyright_f  ||
          authors_f)
      {
        const char* s;
        auto write = [&os, &out, &s] (const path& f)
        {
          os << s << f.leaf (out).posix_representation ();
          s = " ";
        };

        if (readme_f || pkg_readme_f || changes_f)
        {
          s = "";

          os << "doc{";
          if (readme_f)     write (*readme_f);
          if (pkg_readme_f) write (*pkg_readme_f);
          if (changes_f)    write (*changes_f);
          os << "} ";
        }

        if (license_f || copyright_f || authors_f)
        {
          s = "";

          os << "legal{";
          if (license_f)   write (*license_f);
          if (copyright_f) write (*copyright_f);
          if (authors_f)   write (*authors_f);
          os << "} ";
        }
      }

      os << "manifest";

      if (newline)
        os <<                                                             '\n';
    };

    if (!src && out != out_src)
    {
      open (out / buildfile_file);

      os << "./: {*/ -" << build_dir.posix_representation () << "} ";
      write_doc_prerequisites (true /* newline */);

      if (itest && install && t == type::lib) // Have tests/ subproject.
        os <<                                                             '\n'
           << "# Don't install tests."                                 << '\n'
           << "#"                                                      << '\n'
           << "tests/: install = false"                                << '\n';
      os.close ();
    }

    if (t == type::bare)
      break; // Done

    // If the argument contains multiple space separated target types, then
    // return them as a group (wrapped into curly braces). Otherwise, the
    // argument must be a single type, which is returned as is.
    //
    auto tt = [] (const string& ts)
    {
      assert (!ts.empty ());
      return ts.find (' ') == string::npos ? ts : '{' + ts + '}';
    };

    switch (t)
    {
    case type::exe:
      {
        switch (l)
        {
        case lang::c:
          {
            // <src>/<stem>.c
            //
            open (out_src / s + ".c");
            os << "#include <stdio.h>"                                 << '\n'
               <<                                                         '\n'
               << "int main (int argc, char *argv[])"                  << '\n'
               << "{"                                                  << '\n'
               << "  if (argc < 2)"                                    << '\n'
               << "  {"                                                << '\n'
               << "    fprintf (stderr, \"error: missing name\\n\");"  << '\n'
               << "    return 1;"                                      << '\n'
               << "  }"                                                << '\n'
               <<                                                         '\n'
               << "  printf (\"Hello, %s!\\n\", argv[1]);"             << '\n'
               << "  return 0;"                                        << '\n'
               << "}"                                                  << '\n';
            os.close ();

            break;
          }
        case lang::cxx:
          {
            // <src>/<stem>.<cxx-ext>
            //
            open (out_src / s + xe);
            os << "#include <iostream>"                                << '\n'
               <<                                                         '\n'
               << "int main (int argc, char* argv[])"                  << '\n'
               << "{"                                                  << '\n'
               << "  using namespace std;"                             << '\n'
               <<                                                         '\n'
               << "  if (argc < 2)"                                    << '\n'
               << "  {"                                                << '\n'
               << "    cerr << \"error: missing name\" << endl;"       << '\n'
               << "    return 1;"                                      << '\n'
               << "  }"                                                << '\n'
               <<                                                         '\n'
               << "  cout << \"Hello, \" << argv[1] << '!' << endl;"   << '\n'
               << "}"                                                  << '\n';
            os.close ();

            break;
          }
        }

        // <[pfx_]src>/buildfile
        //
        open (src_bfile_dir / buildfile_file);
        os << "libs ="                                                 << '\n'
           << "#import libs += libhello%lib{hello}"                    << '\n'
           <<                                                             '\n';

        // Add the root buildfile content, if required.
        //
        if (out_src == out)
        {
          os << "./: exe{" << s << "} ";
          write_doc_prerequisites (true /* newline */);
          os <<                                                           '\n';
        }

        // If the source directory is the project/package root, then use the
        // non-recursive sources wildcard not to accidentally pick up unrelated
        // files (documentation examples, integration tests, etc).
        //
        const char* w (out_src == out ? "*" : "**");

        string src_dir (!bfile_in_pfx_src ? "" : sub.posix_representation ());

        if (!utest)
        {
          os << "exe{" << s << "}: "                                   <<
            tt (ha + ' ' + xa) << "{" << w << "} "                     <<
            "$libs";

          if (itest)
            os << ' ' << src_dir << "testscript";

          os                                                           << '\n';
        }
        else
        {
          os << "./: exe{" << s << "}: libue{" << s << "}: "           <<
            tt (ha + ' ' + xa) << "{" << w << " -" << w << ".test...} $libs" << '\n';

          if (itest)
            os << "exe{" << s << "}: " << src_dir << "testscript"      << '\n';

          os <<                                                           '\n'
             << "# Unit tests."                                        << '\n'
             << "#"                                                    << '\n';

          if (install)
            os << "exe{*.test}:"                                       << '\n'
               << "{"                                                  << '\n'
               << "  test = true"                                      << '\n'
               << "  install = false"                                  << '\n'
               << "}"                                                  << '\n';
          else
            os << "exe{*.test}: test = true"                           << '\n';

          os <<                                                           '\n'
             << "for t: " << tt (xa) << "{" << w << ".test...}"        << '\n'
             << "{"                                                    << '\n'
             << "  d = $directory($t)"                                 << '\n'
             << "  n = $name($t)..."                                   << '\n'
             <<                                                           '\n'
             << "  ./: $d/exe{$n}: $t $d/" << tt (ha) << "{+$n} $d/testscript{+$n}" << '\n'
             << "  $d/exe{$n}: libue{" << s << "}: bin.whole = false"  << '\n'
             << "}"                                                    << '\n';
        }

        string ps (pfx_src.posix_representation ());

        string op (!ps.empty () ? "$out_pfx" : "$out_root");
        string sp (!ps.empty () ? "$src_pfx" : "$src_root");

        if (!ps.empty ())
          os <<                                                           '\n'
             << "out_pfx = [dir_path] $out_root/" << ps                << '\n'
             << "src_pfx = [dir_path] $src_root/" << ps                << '\n';

        os  <<                                                            '\n'
            << mc << ".poptions =+ \"-I" << op << "\" \"-I" << sp << '"' << '\n';

        os.close ();

        // <src>/.gitignore
        //
        // Add the root .gitignore file content at the beginning of our
        // .gitignore file, if required.
        //
        bool root      (out_src == out);
        bool exe_src   (!bfile_in_pfx_src);          // Add <stem>.
        bool itest_src (itest && !bfile_in_pfx_src); // Add test-<stem>.

        if (root || exe_src || utest || itest_src)
        {
          if (vc == vcs::git)
          {
            open (out_src / ".gitignore");

            // Add the root .gitignore file content, if required.
            //
            if (root)
              write_root_gitignore (exe_src || utest || itest_src /* newline */);

            if (exe_src)
              os << s                                                  << '\n';

            if (utest)
              os << "*.test"                                           << '\n';

            if (itest_src || utest)
            {
              if (exe_src || utest)
                os <<                                                     '\n';

              os << "# Testscript output directory (can be symlink)."  << '\n'
                 << "#"                                                << '\n';

              if (itest_src)
                os << "test-" << s                                     << '\n';
              if (utest)
                os << "test-*.test"                                    << '\n';
            }

            os.close ();
          }
        }

        // <pfx_src>/.gitignore
        //
        // Note: should never need to be merged with root since we banned such
        // layouts (buildfile will clash with root buildfile).
        //
        if (bfile_in_pfx_src)
        {
          if (vc == vcs::git)
          {
            open (out_pfx_src / ".gitignore");
            os << s                                                    << '\n';

            if (itest)
              os <<                                                       '\n'
                 << "# Testscript output directory (can be symlink)."  << '\n'
                 << "#"                                                << '\n'
                 << "test-" << s                                       << '\n';

            os.close ();
          }
        }

        // <src>/testscript
        //
        if (itest)
        {
          open (out_src / "testscript");
          os << ": basics"                                             << '\n'
             << ":"                                                    << '\n'
             << "$* 'World' >'Hello, World!'"                          << '\n'
             <<                                                           '\n'
             << ": missing-name"                                       << '\n'
             << ":"                                                    << '\n'
             << "$* 2>>EOE != 0"                                       << '\n'
             << "error: missing name"                                  << '\n'
             << "EOE"                                                  << '\n';
          os.close ();
        }

        // <src>/<stem>.test.*
        //
        if (utest)
        {
          switch (l)
          {
          case lang::c:
            {
              // <src>/<stem>.test.c
              //
              open (out_src / s + ".test.c");
              os << "#include <stdio.h>"                               << '\n'
                 <<                                                       '\n'
                 << "#undef NDEBUG"                                    << '\n'
                 << "#include <assert.h>"                              << '\n'
                 <<                                                       '\n'
                 << "int main ()"                                      << '\n'
                 << "{"                                                << '\n'
                 << "  return 0;"                                      << '\n'
                 << "}"                                                << '\n';
              os.close ();

              break;
            }
          case lang::cxx:
            {
              // <src>/<stem>.test.<cxx-ext>
              //
              open (out_src / s + ".test" + xe);
              os << "#include <iostream>"                              << '\n'
                 <<                                                       '\n'
                 << "#undef NDEBUG"                                    << '\n'
                 << "#include <cassert>"                               << '\n'
                 <<                                                       '\n'
                 << "int main ()"                                      << '\n'
                 << "{"                                                << '\n'
                 <<                                                       '\n'
                 << "}"                                                << '\n';
              os.close ();

              break;
            }
          }
        }

        // build/export.build
        //
        if (!src && t.exe_opt.export_stub ())
        {
          string sd (
            (pfx_src / (sub_src && !bfile_in_pfx_src ? sub : empty_dir_path)).
            posix_representation ());

          open (bd / "export." + build_ext);
          os << "$out_root/"                                           << '\n'
             << "{"                                                    << '\n'
             << "  include " << (!sd.empty () ? sd : "./")             << '\n'
             << "}"                                                    << '\n'
             <<                                                           '\n'
             << "export $out_root/" << sd << "$import.target"          << '\n';
          os.close ();
        }

        break;
      }
    case type::lib:
      {
        // Include prefix.
        //
        string ip (sub_inc ? sub.posix_representation () : "");

        // Macro prefix.
        //
        // In absence of the source subdirectory, fallback to the package name
        // to minimize the potential macro name clash.
        //
        string mx (
          sanitize_identifier (
            ucase (const_cast<const string&> (!ip.empty () ? ip : n))));

        // Strip the trailing underscore (produced from slash).
        //
        if (!ip.empty ())
          mx.pop_back ();

        string apih; // API header name.
        string exph; // Export header name (empty if binless, no/auto-symexport).
        string verh; // Version header name.

        // Language-specific source code comment markers.
        //
        string O; // Open multi-line comment (no trailing space).
        string X; // Middle of multi-line comment (no trailing space).
        string Q; // Close multi-line comment.

        string B; // Begin of a single-line comment (no trailing space).
        string E; // End of a single-line comment.

        switch (l)
        {
        case lang::c:
          {
            O = "/*";
            X = " *";
            Q = " */";
            B = "/*";
            E = " */";

            if (binless)
            {
              apih = s + ".h";
              verh = ver ? "version.h" : string ();

              // <inc>/<stem>.h
              //
              open (out_inc / apih);
              os << "#pragma once"                                     << '\n'
                 <<                                                       '\n'
                 << "#include <stdio.h>"                               << '\n'
                 << "#include <errno.h>"                               << '\n'
                 <<                                                       '\n'
                 << "/* Print a greeting for the specified name into the specified" << '\n'
                 << " * stream. On success, return the number of characters printed." << '\n'
                 << " * On failure, set errno and return a negative value." << '\n'
                 << " */"                                              << '\n'
                 << "static inline int"                                << '\n'
                 << "say_hello (FILE *f, const char* name)"            << '\n'
                 << "{"                                                << '\n'
                 << "  if (f == NULL || name == NULL || *name == '\\0')" << '\n'
                 << "  {"                                              << '\n'
                 << "    errno = EINVAL;"                              << '\n'
                 << "    return -1;"                                   << '\n'
                 << "  }"                                              << '\n'
                 <<                                                       '\n'
                 << "  return fprintf (f, \"Hello, %s!\\n\", name);"   << '\n'
                 << "}"                                                << '\n';
              os.close ();
            }
            else
            {
              apih = s + ".h";

              if (symexport && !auto_symexport)
                exph = "export.h";

              verh = ver ? "version.h" : string ();

              // <inc>/<stem>.h
              //
              open (out_inc / apih);
              os << "#pragma once"                                     << '\n'
                 <<                                                       '\n'
                 << "#include <stdio.h>"                               << '\n'
                 <<                                                       '\n';

              if (!exph.empty ())
                os << "#include <" << ip << exph << ">"                << '\n'
                   <<                                                     '\n';

              os << "/* Print a greeting for the specified name into the specified" << '\n'
                 << " * stream. On success, return the number of characters printed." << '\n'
                 << " * On failure, set errno and return a negative value." << '\n'
                 << " */"                                              << '\n';

              if (!exph.empty ())
                os << mx << "_SYMEXPORT ";

              os << "int"                                              << '\n'
                 << "say_hello (FILE *, const char *name);"            << '\n';
              os.close ();

              // <src>/<stem>.c
              //
              open (out_src / s + ".c");
              os << "#include <" << ip << apih << ">"                  << '\n'
                 <<                                                       '\n'
                 << "#include <errno.h>"                               << '\n'
                 <<                                                       '\n'
                 << "int say_hello (FILE *f, const char* n)"           << '\n'
                 << "{"                                                << '\n'
                 << "  if (f == NULL || n == NULL || *n == '\\0')"     << '\n'
                 << "  {"                                              << '\n'
                 << "    errno = EINVAL;"                              << '\n'
                 << "    return -1;"                                   << '\n'
                 << "  }"                                              << '\n'
                 <<                                                       '\n'
                 << "  return fprintf (f, \"Hello, %s!\\n\", n);"      << '\n'
                 << "}"                                                << '\n';
              os.close ();
            }

            break;
          }
        case lang::cxx:
          {
            O = "//";
            X = "//";
            Q = "//";
            B = "//";
            E = "";

            if (binless)
            {
              apih = s + he;
              verh = ver ? "version" + he : string ();

              // <inc>/<stem>[.<hxx-ext>]
              //
              open (out_inc / apih);
              os << "#pragma once"                                     << '\n'
                 <<                                                       '\n'
                 << "#include <string>"                                << '\n'
                 << "#include <ostream>"                               << '\n'
                 << "#include <stdexcept>"                             << '\n'
                 <<                                                       '\n'
                 << "namespace " << id                                 << '\n'
                 << "{"                                                << '\n'
                 << "  // Print a greeting for the specified name into the specified" << '\n'
                 << "  // stream. Throw std::invalid_argument if the name is empty." << '\n'
                 << "  //"                                             << '\n'
                 << "  inline void"                                    << '\n'
                 << "  say_hello (std::ostream& o, const std::string& name)" << '\n'
                 << "  {"                                              << '\n'
                 << "    using namespace std;"                         << '\n'
                 <<                                                       '\n'
                 << "    if (name.empty ())"                           << '\n'
                 << "      throw invalid_argument (\"empty name\");"   << '\n'
                 <<                                                       '\n'
                 << "    o << \"Hello, \" << name << '!' << endl;"     << '\n'
                 << "  }"                                              << '\n'
                 << "}"                                                << '\n';
              os.close ();
            }
            else
            {
              apih = s + he;

              if (symexport && !auto_symexport)
                exph = "export" + he;

              verh = ver ? "version" + he : string ();

              // <inc>/<stem>[.<hxx-ext>]
              //
              open (out_inc / apih);
              os << "#pragma once"                                     << '\n'
                 <<                                                       '\n'
                 << "#include <iosfwd>"                                << '\n'
                 << "#include <string>"                                << '\n'
                 <<                                                       '\n';

              if (!exph.empty ())
                os << "#include <" << ip << exph << ">"                << '\n'
                   <<                                                     '\n';

              os << "namespace " << id                                 << '\n'
                 << "{"                                                << '\n'
                 << "  // Print a greeting for the specified name into the specified" << '\n'
                 << "  // stream. Throw std::invalid_argument if the name is empty." << '\n'
                 << "  //"                                             << '\n'
                 << "  ";

              if (!exph.empty ())
                os << mx << "_SYMEXPORT ";

              os <<   "void"                                           << '\n'
                 << "  say_hello (std::ostream&, const std::string& name);" << '\n'
                 << "}"                                                << '\n';
              os.close ();

              // <src>/<stem>.<cxx-ext>
              //
              open (out_src / s + xe);
              os << "#include <" << ip << apih << ">"                  << '\n'
                 <<                                                       '\n'
                 << "#include <ostream>"                               << '\n'
                 << "#include <stdexcept>"                             << '\n'
                 <<                                                       '\n'
                 << "using namespace std;"                             << '\n'
                 <<                                                       '\n'
                 << "namespace " << id                                 << '\n'
                 << "{"                                                << '\n'
                 << "  void say_hello (ostream& o, const string& n)"   << '\n'
                 << "  {"                                              << '\n'
                 << "    if (n.empty ())"                              << '\n'
                 << "      throw invalid_argument (\"empty name\");"   << '\n'
                 <<                                                       '\n'
                 << "    o << \"Hello, \" << n << '!' << endl;"        << '\n'
                 << "  }"                                              << '\n'
                 << "}"                                                << '\n';
              os.close ();
            }

            break;
          }
        }

        // <inc>/export.h[??]
        //
        if (!exph.empty ())
        {
          open (out_inc / exph);
          os << "#pragma once"                                         << '\n'
             <<                                                           '\n';
          if (l == lang::cxx)
          {
            os << "// Normally we don't export class templates (but do complete specializations)," << '\n'
               << "// inline functions, and classes with only inline member functions. Exporting"  << '\n'
               << "// classes that inherit from non-exported/imported bases (e.g., std::string)"   << '\n'
               << "// will end up badly. The only known workarounds are to not inherit or to not"  << '\n'
               << "// export. Also, MinGW GCC doesn't like seeing non-exported functions being"    << '\n'
               << "// used before their inline definition. The workaround is to reorder code. In"  << '\n'
               << "// the end it's all trial and error."               << '\n'
               <<                                                         '\n';
          }
          os << "#if defined(" << mx << "_STATIC)         " << B << " Using static." << E    << '\n'
             << "#  define " << mx << "_SYMEXPORT"                                           << '\n'
             << "#elif defined(" << mx << "_STATIC_BUILD) " << B << " Building static." << E << '\n'
             << "#  define " << mx << "_SYMEXPORT"                                           << '\n'
             << "#elif defined(" << mx << "_SHARED)       " << B << " Using shared." << E    << '\n'
             << "#  ifdef _WIN32"                                                            << '\n'
             << "#    define " << mx << "_SYMEXPORT __declspec(dllimport)"                   << '\n'
             << "#  else"                                                                    << '\n'
             << "#    define " << mx << "_SYMEXPORT"                                         << '\n'
             << "#  endif"                                                                   << '\n'
             << "#elif defined(" << mx << "_SHARED_BUILD) " << B << " Building shared." << E << '\n'
             << "#  ifdef _WIN32"                                                           << '\n'
             << "#    define " << mx << "_SYMEXPORT __declspec(dllexport)"                  << '\n'
             << "#  else"                                                                   << '\n'
             << "#    define " << mx << "_SYMEXPORT"                                        << '\n'
             << "#  endif"                                                                  << '\n'
             << "#else"                                                                     << '\n'
             << O << " If none of the above macros are defined, then we assume we are being used" << '\n'
             << X << " by some third-party build system that cannot/doesn't signal the library"   << '\n'
             << X << " type. Note that this fallback works for both static and shared libraries"  << '\n'
             << X << " provided the library only exports functions (in other words, no global"    << '\n'
             << X << " exported data) and for the shared case the result will be sub-optimal"     << '\n'
             << X << " compared to having dllimport. If, however, your library does export data," << '\n'
             << X << " then you will probably want to replace the fallback with the (commented"   << '\n'
             << X << " out) error since it won't work for the shared case."                       << '\n'
             << Q                                                                                 << '\n'
             << "#  define " << mx << "_SYMEXPORT         " << B << " Using static or shared." << E << '\n'
             << B << "#  error define " << mx << "_STATIC or " << mx << "_SHARED preprocessor macro to signal " << n << " library type being linked" << E << '\n'
             << "#endif"                                               << '\n';
          os.close ();
        }

        // <inc>/version.h[??].in
        //
        if (ver)
        {
          open (out_inc / verh + ".in");

          os << "#pragma once"                                         << '\n'
             <<                                                           '\n'
             << O << " The numeric version format is AAAAABBBBBCCCCCDDDE where:" << '\n'
             << X                                                       << '\n'
             << X << " AAAAA - major version number"                    << '\n'
             << X << " BBBBB - minor version number"                    << '\n'
             << X << " CCCCC - bugfix version number"                   << '\n'
             << X << " DDD   - alpha / beta (DDD + 500) version number" << '\n'
             << X << " E     - final (0) / snapshot (1)"                << '\n'
             << X                                                       << '\n'
             << X << " When DDDE is not 0, 1 is subtracted from AAAAABBBBBCCCCC. For example:" << '\n'
             << X                                                       << '\n'
             << X << " Version      AAAAABBBBBCCCCCDDDE"                << '\n'
             << X                                                       << '\n'
             << X << " 0.1.0        0000000001000000000"                << '\n'
             << X << " 0.1.2        0000000001000020000"                << '\n'
             << X << " 1.2.3        0000100002000030000"                << '\n'
             << X << " 2.2.0-a.1    0000200001999990010"                << '\n'
             << X << " 3.0.0-b.2    0000299999999995020"                << '\n'
             << X << " 2.2.0-a.1.z  0000200001999990011"                << '\n'
             << Q                                                       << '\n'
             << "#define " << mx << "_VERSION       $" << v << ".version.project_number$ULL" << '\n'
             << "#define " << mx << "_VERSION_STR   \"$" << v << ".version.project$\""       << '\n'
             << "#define " << mx << "_VERSION_ID    \"$" << v << ".version.project_id$\""    << '\n'
             << "#define " << mx << "_VERSION_FULL  \"$" << v << ".version$\""               << '\n'
             <<                                                                                 '\n'
             << "#define " << mx << "_VERSION_MAJOR $" << v << ".version.major$"             << '\n'
             << "#define " << mx << "_VERSION_MINOR $" << v << ".version.minor$"             << '\n'
             << "#define " << mx << "_VERSION_PATCH $" << v << ".version.patch$"             << '\n'
             <<                                                                                 '\n'
             << "#define " << mx << "_PRE_RELEASE   $" << v << ".version.pre_release$"       << '\n'
             <<                                                                                 '\n'
             << "#define " << mx << "_SNAPSHOT_SN   $" << v << ".version.snapshot_sn$ULL"    << '\n'
             << "#define " << mx << "_SNAPSHOT_ID   \"$" << v << ".version.snapshot_id$\""   << '\n';
          os.close ();
        }

        // <[pfx_]inc>/buildfile
        //
        if (split_buildfile)
        {
          // We shouldn't clash with the root buildfile: we should have failed
          // earlier if that were the case.
          //
          assert (inc_bfile_dir != out);

          open (inc_bfile_dir / buildfile_file);

          // Use the recursive headers wildcard since the include directory
          // cannot be the project/package root for a split layout (see
          // above).
          //
          const char* w ("**");

          string inc_dir (!bfile_in_pfx_inc ? "" : sub.posix_representation ());

          if (binless)
          {
            os << "intf_libs = # Interface dependencies."              << '\n'
               << "#import xxxx_libs += libhello%lib{hello}"           << '\n'
               <<                                                         '\n'
               << "lib{" << s << "}: " << tt (ha) << "{" << w;
            if (ver)
              os << " -" << inc_dir << "version} " << inc_dir << hg
                 << "{version}";
            else
              os << "}";
            os << " $intf_libs"                                        << '\n';
          }
          else
          {
            os << "pub_hdrs = " << tt (ha) << "{" << w;
            if (ver)
              os << " -" << inc_dir << "version} " << inc_dir << hg    <<
                "{version}"                                            << '\n';
            else
              os << "}"                                                << '\n';
            os <<                                                         '\n'
               << "./: $pub_hdrs"                                      << '\n';
          }

          if (ver)
            os <<                                                         '\n'
               << inc_dir << hg << "{version}: " << inc_dir << "in{version} " <<
              "$src_root/manifest"                                     << '\n';

#if 0
          if (!exph.empty ())
            os <<                                                         '\n'
               << inc_dir << hg << "{export}@"                         <<
              (!bfile_in_pfx_inc ? "./" : inc_dir) << ": " << mp       <<
              ".importable = false"                                    << '\n';
#endif

          if (binless)
          {
            string pi (pfx_inc.posix_representation ());

            os <<                                                         '\n'
               << "# Export options."                                  << '\n'
               << "#"                                                  << '\n';

            string op (!pi.empty () ? "$out_pfx" : "$out_root");
            string sp (!pi.empty () ? "$src_pfx" : "$src_root");

            if (!pi.empty ())
              os << "out_pfx = [dir_path] $out_root/" << pi            << '\n'
                 << "src_pfx = [dir_path] $src_root/" << pi            << '\n';

            os <<                                                         '\n'
               << "lib{" <<  s << "}:"                                 << '\n'
               << "{"                                                  << '\n'
               << "  " << mc << ".export.poptions = \"-I" << op << "\" "
               <<                                  "\"-I" << sp << '"' << '\n'
               << "  " << mc << ".export.libs = $intf_libs"            << '\n'
               << "}"                                                  << '\n';
          }

          if (install)
          {
            const string& install_dir (!bfile_in_pfx_inc ? ip : empty_string);

            if (!install_dir.empty ())
              os <<                                                       '\n'
                 << "# Install into the " << install_dir << " subdirectory of, say, /usr/include/" << '\n'
                 << "# recreating subdirectories."                     << '\n'
                 << "#"                                                << '\n';
            else
              os <<                                                       '\n'
                 << "# Install recreating subdirectories."             << '\n'
                 << "#"                                                << '\n';

            os << tt (ha) << "{*}:"                                    << '\n'
               << "{"                                                  << '\n'
               << "  install         = include/" << install_dir        << '\n'
               << "  install.subdirs = true"                           << '\n'
               << "}"                                                  << '\n';
          }

          os.close ();
        }

        // <[pfx_]src>/buildfile
        //
        // Note that there is no <src>/buildfile for a split binless library
        // with the unit tests disabled, since there are no files in <src>/ in
        // this case.
        //
        if (!(split_buildfile && no_source))
        {
          open (src_bfile_dir / buildfile_file);

          if (!(split_buildfile && binless))
          {
            os << "intf_libs = # Interface dependencies."              << '\n';

            if (!binless)
              os << "impl_libs = # Implementation dependencies."       << '\n';

            os << "#import xxxx_libs += libhello%lib{hello}"           << '\n'
               <<                                                         '\n';
          }

          if (split_buildfile)
          {
            string rel (
              inc_bfile_dir.relative (src_bfile_dir).posix_representation ());

            os << "# Public headers."                                  << '\n'
               << "#"                                                  << '\n'
               << "pub = [dir_path] " << rel                           << '\n'
               <<                                                         '\n'
               << "include $pub"                                       << '\n'
               <<                                                         '\n';

            if (!binless)
              os << "pub_hdrs = $($pub/ pub_hdrs)"                     << '\n'
                 <<                                                       '\n';
          }

          // Add the root buildfile content, if required.
          //
          if (out_src == out)
          {
            os << "./: lib{" << s << "} ";
            write_doc_prerequisites (true /* newline */);
            os <<                                                         '\n';
          }

          // If the source directory is the project/package root, then use the
          // non-recursive sources wildcard (see above for details).
          //
          const char* w (out_src == out ? "*" : "**");

          // Note that inc_dir is only used if there is no buildfile split.
          // Also note that there can still be the source directory split, in
          // which case it is not empty.
          //
          string inc_dir (!bfile_in_pfx_inc ? "" : sub.posix_representation ());

          if (!utest)
          {
            if (split_buildfile)
            {
              assert (!binless); // Make sure pub_hdrs is assigned (see above).

              os << "lib{" << s << "}: ";

              if (auto_symexport)
                os << "libul{" << s << "}: ";

              os << "$pub/{$pub_hdrs}"                                 << '\n'
                 <<                                                       '\n'
                 << "# Private headers and sources as well as dependencies." << '\n'
                 << "#"                                                << '\n'
                 << (!auto_symexport ? "lib{" : "libul{") << s << "}: ";
            }
            else
            {
              os << "lib{" << s << "}: ";

              if (auto_symexport)
                os << "libul{" << s << "}: ";
            }

            os << tt (binless ? ha : ha + ' ' + xa) << "{" << w;

            if (ver && !split_buildfile)
              os << " -" << inc_dir << "version} " << inc_dir << hg
                 << "{version}";
            else
              os << "}";

            if (binless)
              os << " $intf_libs"                                      << '\n';
            else
              os << " $impl_libs $intf_libs"                           << '\n';
          }
          else
          {
            if (!binless)
            {
              if (split_buildfile)
                os << "./: lib{" << s << "}: libul{" << s << "}: $pub/{$pub_hdrs}" << '\n'
                   <<                                                     '\n'
                   << "# Private headers and sources as well as dependencies." << '\n'
                   << "#"                                              << '\n';
              else
                os << "./: lib{" << s << "}: ";

              os << "libul{" << s << "}: " << tt (ha + ' ' + xa)       <<
                "{" << w << " -" << w << ".test...";

              if (ver && !split_buildfile)
                os << " -" << inc_dir << "version} \\"                 << '\n'
                   << "  " << inc_dir << hg << "{version}";
              else
                os << "}";
              os << " $impl_libs $intf_libs"                           << '\n';
            }
            else if (!split_buildfile) // Binless.
            {
              os << "./: lib{" << s << "}: "
                 << tt (ha) << "{" << w << " -" << w << ".test...";
              if (ver)
                os << " -" << inc_dir << "version} " << inc_dir << hg  <<
                  "{version} \\"                                       << '\n'
                   << " ";
              else
                os << "}";
              os << " $intf_libs"                                      << '\n';
            }
          }

          if (auto_symexport)
            os <<                                                         '\n'
               << "libs{" << s << "}: def{" << s << "}: include = ($"  <<
              mp << ".target.system == 'win32-msvc')"                  << '\n'
               << "def{" << s << "}: libul{" << s << "}"               << '\n';

          if (utest)
          {
            os <<                                                         '\n'
               << "# Unit tests."                                      << '\n'
               << "#"                                                  << '\n';

            if (install)
              os << "exe{*.test}:"                                     << '\n'
                 << "{"                                                << '\n'
                 << "  test = true"                                    << '\n'
                 << "  install = false"                                << '\n'
                 << "}"                                                << '\n';
            else
              os << "exe{*.test}: test = true"                         << '\n';

            // Note that for a split binless library all headers in <src>/ are
            // presumably for unit tests.
            //
            os <<                                                         '\n'
               << "for t: " << tt (xa) << "{" << w << ".test...}"      << '\n'
               << "{"                                                  << '\n'
               << "  d = $directory($t)"                               << '\n'
               << "  n = $name($t)..."                                 << '\n'
               <<                                                         '\n'
               << "  ./: $d/exe{$n}: $t $d/" << tt (ha) << "{"
               << (split_buildfile && binless ? w : "+$n")
               << "} $d/testscript{+$n}";

            if (binless)
              os << (split_buildfile ? " $pub/" : " ") << "lib{" << s << "}" << '\n';
            else
              os <<                                                       '\n'
                 << "  $d/exe{$n}: libul{" << s << "}: bin.whole = false" << '\n';

            os << "}"                                                  << '\n';
          }

          if (ver && !split_buildfile)
            os <<                                                         '\n'
               << inc_dir << hg << "{version}: " << inc_dir << "in{version} " <<
              "$src_root/manifest"                                     << '\n';

#if 0
          if (!exph.empty () && !split_buildfile)
            os <<                                                         '\n'
               << inc_dir << hg << "{export}@"                         <<
              (!bfile_in_pfx_inc ? "./" : inc_dir) << ": " << mp       <<
              ".importable = false"                                    << '\n';
#endif

          // Build.
          //
          string pi (pfx_inc.posix_representation ());
          string ps (pfx_src.posix_representation ());

          os <<                                                           '\n'
             << "# Build options."                                     << '\n'
             << "#"                                                    << '\n';

          string opi;
          string spi;

          if (split_source && !binless)
          {
            opi = !pi.empty () ? "$out_pfx_inc" : "$out_root";
            spi = !pi.empty () ? "$src_pfx_inc" : "$src_root";

            if (!pi.empty ())
              os << "out_pfx_inc = [dir_path] $out_root/" << pi        << '\n'
                 << "src_pfx_inc = [dir_path] $src_root/" << pi        << '\n';

            string ops (!ps.empty () ? "$out_pfx_src" : "$out_root");
            string sps (!ps.empty () ? "$src_pfx_src" : "$src_root");

            if (!ps.empty ())
              os << "out_pfx_src = [dir_path] $out_root/" << ps        << '\n'
                 << "src_pfx_src = [dir_path] $src_root/" << ps        << '\n';

            if (!pi.empty () || !ps.empty ())
              os <<                                                       '\n';

            os << mc << ".poptions =+ \"-I" << ops << "\" \"-I" << sps << "\" \\" << '\n'
               << string (mc.size () + 13, ' ')
               <<                    "\"-I" << opi << "\" \"-I" << spi << '"' << '\n';
          }
          else
          {
            opi = !pi.empty () ? "$out_pfx" : "$out_root";
            spi = !pi.empty () ? "$src_pfx" : "$src_root";

            if (!pi.empty ())
              os << "out_pfx = [dir_path] $out_root/" << pi            << '\n'
                 << "src_pfx = [dir_path] $src_root/" << pi            << '\n'
                 <<                                                       '\n';

            os << mc << ".poptions =+ \"-I" << opi << "\" \"-I" << spi << '"' << '\n';
          }

          if (!binless)
          {
            if (!auto_symexport)
              os <<                                                       '\n'
#if 0
                 << (no_symexport ? "#" : "") << "{hbmia obja}{*}: "   <<
#else
                 << (no_symexport ? "#" : "") << "obja{*}: "           <<
#endif
                mc << ".poptions += -D" << mx << "_STATIC_BUILD"       << '\n'
#if 0
                 << (no_symexport ? "#" : "") << "{hbmis objs}{*}: "   <<
#else
                 << (no_symexport ? "#" : "") << "objs{*}: "           <<
#endif
                mc << ".poptions += -D" << mx << "_SHARED_BUILD"       << '\n';
            else
              os <<                                                       '\n'
                 << "if ($" << mp << ".target.system == 'mingw32')"    << '\n'
                 << "  " << mp << ".loptions += -Wl,--export-all-symbols" << '\n';
          }

          // Export.
          //
          if (!(split_buildfile && binless))
          {
            os <<                                                         '\n'
               << "# Export options."                                  << '\n'
               << "#"                                                  << '\n'
               << "lib{" <<  s << "}:"                                 << '\n'
               << "{"                                                  << '\n';

            os << "  " << mc << ".export.poptions = \"-I" << opi << "\" "
               <<                                  "\"-I" << spi << "\"" << '\n'
               << "  " << mc << ".export.libs = $intf_libs"            << '\n'
               << "}"                                                  << '\n';
          }

          if (!binless && !auto_symexport)
            os <<                                                         '\n'
               << (no_symexport ? "#" : "") << "liba{" << s << "}: "   <<
              mc << ".export.poptions += -D" << mx << "_STATIC"        << '\n'
               << (no_symexport ? "#" : "") << "libs{" << s << "}: "   <<
              mc << ".export.poptions += -D" << mx << "_SHARED"        << '\n';

          // Library versioning.
          //
          if (!binless)
            os <<                                                         '\n'
               << "# For pre-releases use the complete version to make sure they cannot" << '\n'
               << "# be used in place of another pre-release or the final version. See"  << '\n'
               << "# the version module for details on the version.* variable values."   << '\n'
               << "#"                                                  << '\n'
               << "if $version.pre_release"                            << '\n'
               << "  lib{" << s << "}: bin.lib.version = \"-$version.project_id\"" << '\n'
               << "else"                                               << '\n'
               << "  lib{" << s << "}: bin.lib.version = \"-$version.major.$version.minor\"" << '\n';

          // Installation.
          //
          if (install)
          {
            if (!split_buildfile)
            {
              const string& install_dir (!bfile_in_pfx_inc ? ip : empty_string);

              if (!install_dir.empty ())
                os <<                                                     '\n'
                   << "# Install into the " << install_dir << " subdirectory of, say, /usr/include/" << '\n'
                   << "# recreating subdirectories."                   << '\n'
                   << "#"                                              << '\n';
              else
                os <<                                                     '\n'
                   << "# Install recreating subdirectories."           << '\n'
                   << "#"                                              << '\n';

              os << tt (ha) << "{*}:"                                  << '\n'
                 << "{"                                                << '\n'
                 << "  install         = include/" << install_dir      << '\n'
                 << "  install.subdirs = true"                         << '\n'
                 << "}"                                                << '\n';
            }
            else
              os <<                                                       '\n'
                 << "# Don't install private headers."                 << '\n'
                 << "#"                                                << '\n'
                 << tt (ha) << "{*}: install = false"                  << '\n';
          }

          os.close ();
        }

        // <src>/.gitignore
        //
        // Add the root .gitignore file content at the beginning of our
        // .gitignore file, if required.
        //
        bool root (out_src == out);

        // Add <stem>.def.
        //
        bool auto_symexport_src (auto_symexport && !bfile_in_pfx_src);

        if (root || ver || auto_symexport_src || utest)
        {
          if (vc == vcs::git)
          {
            // If the source directory is the project/package root, then it is
            // combined (see above). Thus, the version header name will go
            // into the same .gitignore file, if its generation is enabled.
            //
            if (root)
            {
              assert (!split_source);

              open (out_src / ".gitignore");
              write_root_gitignore ();

              if (!ver && !auto_symexport_src && !utest)
                os.close ();
            }

            // Note: these can go into a single .gitignore file or be split
            // into two for the split case.
            //
            if (ver)
            {
              if (!os.is_open ())
                open (out_inc / ".gitignore");
              else
                os <<                                                     '\n';

              os << "# Generated version header."                      << '\n'
                 << "#"                                                << '\n'
                 << verh                                               << '\n';

              if ((!auto_symexport_src && !utest) || split_source)
                os.close ();
            }

            if (auto_symexport_src)
            {
              if (!os.is_open ())
                open (out_src / ".gitignore");
              else
                os <<                                                     '\n';

              os << "# Generated DLL symbol-exporting file."           << '\n'
                 << "#"                                                << '\n'
                 << s << ".def"                                        << '\n';

              if (!utest)
                os.close ();
            }

            if (utest)
            {
              if (!os.is_open ())
                open (out_src / ".gitignore");
              else
                os <<                                                     '\n';

              os << "# Unit test executables and Testscript output directories" << '\n'
                 << "# (can be symlinks)."                             << '\n'
                 << "#"                                                << '\n'
                 << "*.test"                                           << '\n'
                 << "test-*.test"                                      << '\n';

              os.close ();
            }
          }
        }

        // <pfx_src>/.gitignore
        //
        if (bfile_in_pfx_src && auto_symexport)
        {
          if (vc == vcs::git)
          {
            open (out_pfx_src / ".gitignore");

            os << "# Generated DLL symbol-exporting file."             << '\n'
               << "#"                                                  << '\n'
               << s << ".def"                                          << '\n';

            os.close ();
          }
        }

        // <src>/<stem>.test.*
        //
        if (utest)
        {
          switch (l)
          {
          case lang::c:
            {
              // <src>/<stem>.test.c
              //
              open (out_src / s + ".test.c");
              os << "#include <stdio.h>"                               << '\n'
                 <<                                                       '\n'
                 << "#include <" << ip << apih << ">"                  << '\n'
                 <<                                                       '\n'
                 << "#undef NDEBUG"                                    << '\n'
                 << "#include <assert.h>"                              << '\n'
                 <<                                                       '\n'
                 << "int main ()"                                      << '\n'
                 << "{"                                                << '\n'
                 << "  return 0;"                                      << '\n'
                 << "}"                                                << '\n';
              os.close ();

              break;
            }
          case lang::cxx:
            {
              // <src>/<stem>.test.<cxx-ext>
              //
              open (out_src / s + ".test" + xe);
              os << "#include <iostream>"                              << '\n'
                 <<                                                       '\n'
                 << "#include <" << ip << apih << ">"                  << '\n'
                 <<                                                       '\n'
                 << "#undef NDEBUG"                                    << '\n'
                 << "#include <cassert>"                               << '\n'
                 <<                                                       '\n'
                 << "int main ()"                                      << '\n'
                 << "{"                                                << '\n'
                 <<                                                       '\n'
                 << "}"                                                << '\n';
              os.close ();

              break;
            }
          }
        }

        // build/export.build
        //
        if (!src)
        {
          // Note: for the binless library the library target is defined in
          // the header directory buildfile.
          //
          string sd (
            (binless
             ? pfx_inc / (sub_inc && !bfile_in_pfx_inc ? sub : empty_dir_path)
             : pfx_src / (sub_src && !bfile_in_pfx_src ? sub : empty_dir_path)).
            posix_representation ());

          open (bd / "export." + build_ext);
          os << "$out_root/"                                           << '\n'
             << "{"                                                    << '\n'
             << "  include " << (!sd.empty () ? sd : "./")             << '\n'
             << "}"                                                    << '\n'
             <<                                                           '\n'
             << "export $out_root/" << sd << "$import.target"          << '\n';
          os.close ();
        }

        // tests/ (tests subproject).
        //
        if (!itest)
          break;

        dir_path td (dir_path (out) /= "tests");

        // tests/build/
        //
        dir_path tbd (dir_path (td) / build_dir);

        // tests/build/bootstrap.build
        //
        open (tbd / "bootstrap." + build_ext);
        os << "project = # Unnamed tests subproject."                  << '\n'
           <<                                                             '\n'
           << "using config"                                           << '\n'
           << "using test"                                             << '\n'
           << "using dist"                                             << '\n';
        os.close ();

        // tests/build/root.build
        //
        open (tbd / "root." + build_ext);
        switch (l)
        {
        case lang::c:
          {
            write_c_module (l.c_opt.cpp ());

            if (l.c_opt.cpp ())
              write_cxx_module ();

            break;
          }
        case lang::cxx:
          {
            write_cxx_module (l.cxx_opt.c ());

            if (l.cxx_opt.c ())
              write_c_module ();

            break;
          }
        }
        os <<                                                             '\n'
           << "# Every exe{} in this subproject is by default a test." << '\n'
           << "#"                                                      << '\n'
           << "exe{*}: test = true"                                    << '\n'
           <<                                                             '\n'
           << "# The test target for cross-testing (running tests under Wine, etc)." << '\n'
           << "#"                                                      << '\n'
           << "test.target = $" << mp << ".target"                     << '\n';
        os.close ();

        // tests/build/.gitignore
        //
        if (vc == vcs::git)
        {
          open (tbd / ".gitignore");
          os << "/config." << build_ext                                << '\n'
             << "/root/"                                               << '\n'
             << "/bootstrap/"                                          << '\n'
             << "build/"                                               << '\n';
          os.close ();
        }

        // tests/buildfile
        //
        open (td / buildfile_file);
        os << "./: {*/ -" << build_dir.posix_representation () << "}"  << '\n';
        os.close ();

        // tests/.gitignore
        //
        if (vc == vcs::git)
        {
          open (td / ".gitignore");
          os << "# Test executables."                                  << '\n'
             << "#"                                                    << '\n'
             << "driver"                                               << '\n'
             <<                                                           '\n'
             << "# Testscript output directories (can be symlinks)."   << '\n'
             << "#"                                                    << '\n'
             << "test"                                                 << '\n'
             << "test-*"                                               << '\n';
          os.close ();
        }

        // tests/basics/
        //
        td /= "basics";

        switch (l)
        {
        case lang::c:
          {
            // It would have been nice if we could use something like
            // open_memstream() or fmemopen() but these are not portable.  So
            // we resort to tmpfile(), which, turns out, is broken on Windows
            // (it may try to create a file in the root directory of a drive
            // and that may require elevated privileges). So we provide our
            // own implementation for that. Who thought writing portable C
            // could be so hard?

            // tests/basics/driver.c
            //
            open (td / "driver.c");
            os << "#include <stdio.h>"                                 << '\n'
               << "#include <errno.h>"                                 << '\n'
               << "#include <string.h>"                                << '\n'
               <<                                                         '\n';
            if (ver)
              os << "#include <" << ip << verh << ">"                  << '\n';
            os << "#include <" << ip << apih << ">"                    << '\n'
               <<                                                         '\n'
               << "#undef NDEBUG"                                      << '\n'
               << "#include <assert.h>"                                << '\n'
               <<                                                         '\n'
               << "#ifdef _WIN32"                                      << '\n'
               << "#define tmpfile mytmpfile"                          << '\n'
               << "static FILE *mytmpfile ();"                         << '\n'
               << "#endif"                                             << '\n'
               <<                                                         '\n'
               << "int main ()"                                        << '\n'
               << "{"                                                  << '\n'
               << "  char b[256];"                                     << '\n'
               <<                                                         '\n'
               << "  /* Basics."                                       << '\n'
               << "   */"                                              << '\n'
               << "  {"                                                << '\n'
               << "    FILE *o = tmpfile ();"                          << '\n'
               << "    assert (say_hello (o, \"World\") > 0);"         << '\n'
               << "    rewind (o);"                                    << '\n'
               << "    assert (fread (b, 1, sizeof (b), o) == 14 &&"   << '\n'
               << "            strncmp (b, \"Hello, World!\\n\", 14) == 0);" << '\n'
               << "    fclose (o);"                                    << '\n'
               << "  }"                                                << '\n'
               <<                                                         '\n'
               << "  /* Empty name."                                   << '\n'
               << "   */"                                              << '\n'
               << "  {"                                                << '\n'
               << "    FILE *o = tmpfile ();"                          << '\n'
               << "    assert (say_hello (o, \"\") < 0 && errno == EINVAL);" << '\n'
               << "    fclose (o);"                                    << '\n'
               << "  }"                                                << '\n'
               <<                                                         '\n'
               << "  return 0;"                                        << '\n'
               << "}"                                                  << '\n'
               <<                                                         '\n'
               << "#ifdef _WIN32"                                      << '\n'
               << "#include <windows.h>"                               << '\n'
               << "#include <fcntl.h>"                                 << '\n'
               << "#include <io.h>"                                    << '\n'
               <<                                                         '\n'
               << "FILE *mytmpfile ()"                                 << '\n'
               << "{"                                                  << '\n'
               << "  char d[MAX_PATH + 1], p[MAX_PATH + 1];"           << '\n'
               << "  if (GetTempPathA (sizeof (d), d) == 0 ||"         << '\n'
               << "      GetTempFileNameA (d, \"tmp\", 0, p) == 0)"    << '\n'
               << "    return NULL;"                                   << '\n'
               <<                                                         '\n'
               << "  HANDLE h = CreateFileA (p,"                       << '\n'
               << "                          GENERIC_READ | GENERIC_WRITE," << '\n'
               << "                          0,"                       << '\n'
               << "                          NULL,"                    << '\n'
               << "                          CREATE_ALWAYS,"           << '\n'
               << "                          FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE," << '\n'
               << "                          NULL);"                   << '\n'
               << "  if (h == INVALID_HANDLE_VALUE)"                   << '\n'
               << "    return NULL;"                                   << '\n'
               <<                                                         '\n'
               << "  int fd = _open_osfhandle ((intptr_t) h, _O_RDWR);" << '\n'
               << "  if (fd == -1)"                                    << '\n'
               << "    return NULL;"                                   << '\n'
               <<                                                         '\n'
               << "  FILE *f = _fdopen (fd, \"wb+\");"                 << '\n'
               << "  if (f == NULL)"                                   << '\n'
               << "    _close (fd);"                                   << '\n'
               <<                                                         '\n'
               << "  return f;"                                        << '\n'
               << "}"                                                  << '\n'
               << "#endif"                                             << '\n';
            os.close ();

            break;
          }
        case lang::cxx:
          {
            // tests/basics/driver.<cxx-ext>
            //
            open (td / "driver" + xe);
            os << "#include <sstream>"                                 << '\n'
               << "#include <stdexcept>"                               << '\n'
               <<                                                         '\n';
            if (ver)
              os << "#include <" << ip << verh << ">"                  << '\n';
            os << "#include <" << ip << apih << ">"                    << '\n'
               <<                                                         '\n'
               << "#undef NDEBUG"                                      << '\n'
               << "#include <cassert>"                                 << '\n'
               <<                                                         '\n'
               << "int main ()"                                        << '\n'
               << "{"                                                  << '\n'
               << "  using namespace std;"                             << '\n'
               << "  using namespace " << id << ";"                    << '\n'
               <<                                                         '\n'
               << "  // Basics."                                       << '\n'
               << "  //"                                               << '\n'
               << "  {"                                                << '\n'
               << "    ostringstream o;"                               << '\n'
               << "    say_hello (o, \"World\");"                      << '\n'
               << "    assert (o.str () == \"Hello, World!\\n\");"     << '\n'
               << "  }"                                                << '\n'
               <<                                                         '\n'
               << "  // Empty name."                                   << '\n'
               << "  //"                                               << '\n'
               << "  try"                                              << '\n'
               << "  {"                                                << '\n'
               << "    ostringstream o;"                               << '\n'
               << "    say_hello (o, \"\");"                           << '\n'
               << "    assert (false);"                                << '\n'
               << "  }"                                                << '\n'
               << "  catch (const invalid_argument& e)"                << '\n'
               << "  {"                                                << '\n'
               << "    assert (e.what () == string (\"empty name\"));" << '\n'
               << "  }"                                                << '\n'
               << "}"                                                  << '\n';
            os.close ();

            break;
          }
        }

        // tests/basics/buildfile
        //
        open (td / buildfile_file);
        os << "import libs = " << n << "%lib{" << s << "}"             << '\n'
           <<                                                             '\n'
           << "exe{driver}: " << tt (ha + ' ' + xa) << "{**} $libs testscript{**}" << '\n';
        // <<                                                             '\n'
        // << m << ".poptions =+ \"-I$out_root\" \"-I$src_root\""      << '\n';
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
    fail << "unable to write to " << cf << ": " << e;
  }

  // Cancel auto-removal of the files we have created.
  //
  for (auto& rm: rms)
    rm.cancel ();

  // packages.manifest and glue buildfile
  //
  if (pkg)
  {
    path f (prj / "packages.manifest");
    bool e (exists (f));
    try
    {
      // Note: add an extra newline if appending to an existing file. While
      // this will normally result in an extra blank line between entries
      // which we don't really need, it will also make sure we don't generate
      // invalid file if there is no newline at the end of file (which can
      // happen if the file was edited manually).
      //
      ofdstream os (f, (fdopen_mode::out    |
                        fdopen_mode::create |
                        fdopen_mode::append));
      os << (e ? "\n:" : ": 1")                                        << '\n'
         << "location: " << pkg->posix_representation ()               << '\n';
      os.close ();
    }
    catch (const io_error& e)
    {
      fail << "unable to write to " << f << ": " << e;
    }

    // Only create the glue buildfile if we've also created packages.manifest.
    //
    // Note that the first version of our glue buildfile pulled all the
    // subdirectories with a wildcard (*/). This proved to have several
    // drawbacks: adding a non-package subdirectory (e.g., upstream/) is
    // error-prone since we need to remember to manually exclude it. Also,
    // some users seem to prefer to create configurations as subdirectories of
    // the project root. Forgetting to exclude them leads to all kinds of
    // bizarre errors (see GitHub issue #159 for one example).
    //
    if (!e)
    {
      path f (prj / buildfile_file);
      if (!exists (f))
      try
      {
        ofdstream os (f, (fdopen_mode::out    |
                          fdopen_mode::create |
                          fdopen_mode::exclusive));
        os << "# Glue buildfile that \"pulls\" all the packages in the project." << '\n'
           << "#"                                                      << '\n'
           << "import pkgs = [dir_paths] $process.run_regex(\\"        << '\n'
           << "  cat $src_root/packages.manifest, '\\s*location\\s*:\\s*(\\S+)\\s*', '\\1')" << '\n'
           <<                                                             '\n'
           << "./: $pkgs"                                              << '\n';
        os.close ();
      }
      catch (const io_error& e)
      {
        fail << "unable to write to " << f << ": " << e;
      }
    }
  }

  // Run post hooks.
  //
  if (o.post_hook_specified ())
    run_hooks (o.post_hook (), "post");

  if (verb)
  {
    diag_record dr (text);
    dr << "created new " << t << ' ';

    if (src)
    {
      bool pi (exists (out_inc));
      bool ps (split_source && exists (out_src));

      dr << "source subdirectory " << n << " in";

      if (pi && ps)
        dr << "\n  " << out_inc
           << "\n  " << out_src;
      else
        dr << ' ' << (pi ? out_inc : out_src);
    }
    else
      dr << (pkg ? "package " : "project ") << n << " in " << out;
  }

  // --no-init | --package | --source
  //
  if (o.no_init () || pkg || src)
    return 0;

  // Create .bdep/.
  //
  mk_bdep_dir (prj);

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
        move (cc))};

    cmd_init (o, prj, db, cfgs, pkgs, strings () /* pkg_args */);
  }

  return 0;
}

bdep::default_options_files bdep::
options_files (const char*, const cmd_new_options& o, const strings&)
{
  // NOTE: remember to update the documentation if changing anything here.

  // bdep.options
  // bdep-{config config-add}.options               # -A
  // bdep-{config config-add config-create}.options # -C
  // bdep-new.options
  // bdep-new-{project|package|source}.options

  // Use the project directory as a start directory in the package/source
  // modes and the parent directory of the new project otherwise.
  //
  // Note that we will not validate the command arguments and let cmd_new()
  // complain later in case of an error.
  //
  optional<dir_path> start;

  auto output_parent_dir = [&o] ()
  {
    return normalize (o.output_dir (), "output directory");
  };

  if (o.package () || o.source ())
  {
    start =
      o.output_dir_specified () ? output_parent_dir ()            :
      o.directory_specified  () ? normalize (o.directory (),
                                             "project directory") :
      current_directory ();

    // Get the actual project directory.
    //
    project_package pp (find_project_package (*start,
                                              true /* allow_subdir */,
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
  add (o.source ()  ? "new-source"  :
       o.package () ? "new-package" :
       "new-project");

  return r;
}

bdep::cmd_new_options bdep::
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
    forbid ("--source",           o.source ());
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
