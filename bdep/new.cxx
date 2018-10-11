// file      : bdep/new.cxx -*- C++ -*-
// copyright : Copyright (c) 2014-2018 Code Synthesis Ltd
// license   : MIT; see accompanying LICENSE file

#include <bdep/new.hxx>

#include <libbutl/project-name.mxx>

#include <bdep/project.hxx>
#include <bdep/project-author.hxx>
#include <bdep/database.hxx>
#include <bdep/diagnostics.hxx>

#include <bdep/init.hxx>
#include <bdep/config.hxx>

using namespace std;

namespace bdep
{
  using butl::project_name;

  using type = cmd_new_type;
  using lang = cmd_new_lang;
  using vcs  = cmd_new_vcs;

  int
  cmd_new (const cmd_new_options& o, cli::group_scanner& args)
  {
    tracer trace ("new");

    bool ca (o.config_add_specified ());
    bool cc (o.config_create_specified ());

    if (o.package () && o.no_init ())
      fail << "both --no-init and --package specified";

    if (const char* n = (o.no_init () ? "--no-init" :
                         o.package () ? "--package" : nullptr))
    {
      if (ca) fail << "both " << n << " and --config-add specified";
      if (cc) fail << "both " << n << " and --config-create specified";
    }

    if (o.directory_specified () && !o.package ())
      fail << "--directory|-d only valid with --package";

    if (const char* n = cmd_config_validate_add (o))
    {
      if (!ca && !cc)
        fail << n << " specified without --config-(add|create)";

      if (o.wipe () && !cc)
        fail << "--wipe specified without --config-create";
    }

    // Validate type options.
    //
    const type& t (o.type ());

    bool itest (t == type::exe  ? !t.exe_opt.no_tests ()  :
                t == type::lib  ? !t.lib_opt.no_tests ()  :
                t == type::bare ? !t.bare_opt.no_tests () : false);

    bool utest (t == type::exe  ? t.exe_opt.unit_tests () :
                t == type::lib  ? t.lib_opt.unit_tests () : false);

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

        if (o.cpp () && o.cxx ())
          fail << "'cxx' and 'cpp' are mutually exclusive c++ options";

        break;
      }
    }

    // Validate vcs options.
    //
    const vcs& vc (o.vcs ());

    // Validate argument.
    //
    string a (args.more () ? args.next () : "");
    if (a.empty ())
      fail << "project name argument expected";

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
    const string& n (pkgn.string ());
    const string& b (pkgn.base ());
    const string& v (pkgn.variable ());

    string s (b);
    switch (t)
    {
    case type::exe:
      {
        if (s.compare (0, 3, "lib") == 0)
          warn << "executable name starts with 'lib'";

        break;
      }
    case type::lib:
      {
        if (s.compare (0, 3, "lib") == 0)
        {
          s.erase (0, 3);

          if (s.empty ())
            fail << "empty library name stem in '" << b << "'";
        }
        else
          warn << "library name does not start with 'lib'";

        break;
      }
    case type::bare:
    case type::empty:
      break;
    }

    // Sanitize the stem to be a valid language identifier.
    //
    string id;
    switch (l)
    {
    case lang::c:
    case lang::cxx:
      {
        auto sanitize = [] (char c)
        {
          return (c == '-' || c == '+' || c == '.') ? '_' : c;
        };

        transform (s.begin (), s.end (), back_inserter (id), sanitize);
        break;
      }
    }

    dir_path out;           // Project/package output directory.
    dir_path prj;           // Project.
    optional<dir_path> pkg; // Package relative to its project root.

    if (o.package ())
    {
      if (o.directory_specified ())
        (prj = o.directory ()).complete ().normalize ();
      else
        prj = path::current_directory ();

      out = o.output_dir_specified () ? o.output_dir () : prj / dir_path (n);
      out.complete ().normalize ();

      if (!out.sub (prj))
        fail << "package directory " << out << " is not a subdirectory of "
             << "project directory " << prj;

      pkg = out.leaf (prj);
    }
    else
    {
      out = o.output_dir_specified () ? o.output_dir () : dir_path (n);
      out.complete ().normalize ();
      prj = out;
    }

    // Do some sanity check (nested packages, etc; you would be surprised what
    // people come up with).
    //
    if (!o.no_checks ())
    {
      project_package pp (
        find_project_package (out, true /* ignore_not_found */));

      if (o.package ())
      {
        if (!pp.project.empty ())
        {
          if (pp.project != prj)
            fail << prj << " is not a project directory" <<
              info << pp.project << " looks like a project directory";

          if (pp.package)
            fail << "package directory " << out << " is inside another "
                 << "package directory " << prj / *pp.package <<
              info << "nested packages are not allowed";
        }
        else
          warn << prj << " does not look like a project directory";
      }
      else
      {
        if (!pp.project.empty ())
          fail << "project directory " << out << " is inside another "
               << "project directory " << pp.project <<
            info << "nested projects are not allowed";
      }
    }

    // If the output directory already exists, make sure it is empty.
    // Otherwise create it.
    //
    if (!exists (out))
      mk (out);
    else if (!empty (out))
      fail << "directory " << out << " already exists";

    // Initialize the version control system. Do it before writing anything
    // ourselves in case it fails. Also, the email discovery may do the VCS
    // detection.
    //
    if (!pkg)
    {
      switch (vc)
      {
      case vcs::git:  run ("git", "init", "-q", out); break;
      case vcs::none:                                 break;
      }
    }

    for (path f;;) // Breakout loop with file currently being written.
    try
    {
      ofdstream os;

      // .gitignore
      //
      // See also tests/.gitignore below.
      //
      if (vc == vcs::git)
      {
        // Use POSIX directory separators here.
        //
        os.open (f = out / ".gitignore");
        if (!pkg)
          os << bdep_dir.string () << '/'                              << endl
             <<                                                           endl;
        if (t != type::empty)
          os << "# Compiler/linker output."                            << endl
             << "#"                                                    << endl
             << "*.d"                                                  << endl
             << "*.t"                                                  << endl
             << "*.i"                                                  << endl
             << "*.ii"                                                 << endl
             << "*.o"                                                  << endl
             << "*.obj"                                                << endl
             << "*.so"                                                 << endl
             << "*.dll"                                                << endl
             << "*.a"                                                  << endl
             << "*.lib"                                                << endl
             << "*.exp"                                                << endl
             << "*.pdb"                                                << endl
             << "*.ilk"                                                << endl
             << "*.exe"                                                << endl
             << "*.exe.dlls/"                                          << endl
             << "*.exe.manifest"                                       << endl
             << "*.pc"                                                 << endl;
        os.close ();
      }

      // repositories.manifest
      //
      if (!pkg)
      {
        os.open (f = out / "repositories.manifest");
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
      // packages.manifest
      //
      else
      {
        bool e (exists (f = prj / "packages.manifest"));
        os.open (f, fdopen_mode::create | fdopen_mode::append);
        os << (e ? ":" : ": 1")                                        << endl
           << "location: " << pkg->posix_representation ()             << endl;
        os.close ();
      }

      if (t == type::empty)
        break;

      // manifest
      //

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

      if (o.package ())
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

      // Project email.
      //
      string email;
      {
        optional<string> r (find_project_author_email (prj));
        email = r ? move (*r) : "you@example.org";
      }

      os.open (f = out / "manifest");
      os << ": 1"                                                       << endl
         << "name: " << n                                               << endl
         << "version: 0.1.0-a.0.z"                                      << endl;
      if (prjn)
        os << "project: " << *prjn                                      << endl;
      os << "summary: " << s << " " << t                                << endl
         << "license: TODO"                                             << endl
         << "url: https://example.org/" << (prjn ? prjn->string () : n) << endl
         << "email: " << email                                          << endl
         << "depends: * build2 >= 0.8.0-"                               << endl
         << "depends: * bpkg >= 0.8.0-"                                 << endl
         << "#depends: libhello ^1.0.0"                                 << endl;
      os.close ();

      // build/
      //
      dir_path bd (dir_path (out) /= "build");
      mk (bd);

      // build/bootstrap.build
      //
      os.open (f = bd / "bootstrap.build");
      os << "project = " << n                                          << endl;
      if (o.no_amalgamation ())
        os << "amalgamation = # Disabled."                             << endl;
      os <<                                                               endl
         << "using version"                                            << endl
         << "using config"                                             << endl;
      if (itest || utest)
        os << "using test"                                             << endl;
      os << "using install"                                            << endl
         << "using dist"                                               << endl;
      os.close ();

      // build/root.build
      //
      // Note: see also tests/build/root.build below.
      //
      os.open (f = bd / "root.build");

      const char* x  (nullptr); // Language module/source target type.
      const char* h  (nullptr); // Header target type.
      const char* hs (nullptr); // All header target types.
      string      es;           // Source file extension suffix (pp, xx).

      switch (l)
      {
      case lang::c:
        {
          x  = "c";
          h  = "h";
          hs = "h";

          // @@ TODO: 'latest' in c.std.
          //
          os //<< "c.std = latest"                                       << endl
             //<<                                                           endl
             << "using c"                                              << endl
             <<                                                           endl
             << "h{*}: extension = h"                                  << endl
             << "c{*}: extension = c"                                  << endl;
          break;
        }
      case lang::cxx:
        {
          x  = "cxx";
          h  = "hxx";
          hs = "hxx ixx txx";
          es = l.cxx_opt.cpp () ? "pp" : "xx";

          os << "cxx.std = latest"                                     << endl
             <<                                                           endl
             << "using cxx"                                            << endl
             <<                                                           endl
             << "hxx{*}: extension = h" << es                          << endl
             << "ixx{*}: extension = i" << es                          << endl
             << "txx{*}: extension = t" << es                          << endl
             << "cxx{*}: extension = c" << es                          << endl;
          break;
        }
      }

      if ((itest || utest) && x != nullptr)
        os <<                                                           endl
           << "# The test target for cross-testing (running tests under Wine, etc)." << endl
           << "#"                                                    << endl
           << "test.target = $" << x << ".target"                    << endl;

      os.close ();

      // build/.gitignore
      //
      if (vc == vcs::git)
      {
        os.open (f = bd / ".gitignore");
        os << "config.build"                                           << endl
           << "root/"                                                  << endl
           << "bootstrap/"                                             << endl;
        os.close ();
      }

      // buildfile
      //
      os.open (f = out / "buildfile");
      os << "./: {*/ -build/} manifest"                                << endl;
      if (itest && t == type::lib) // Have tests/ subproject.
        os <<                                                             endl
           << "# Don't install tests."                                 << endl
           << "#"                                                      << endl
           << "tests/: install = false"                                << endl;
      os.close ();

      if (t == type::bare)
        break;

      // <base>/ (source subdirectory).
      //
      dir_path sd (dir_path (out) /= b);
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
              os.open (f = sd / s + ".c");
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
              // <base>/<stem>.c(xx|pp)
              //
              os.open (f = sd / s + ".c" + es);
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
          os.open (f = sd / "buildfile");
          os << "libs ="                                               << endl
             << "#import libs += libhello%lib{hello}"                  << endl
             <<                                                           endl;

          if (!utest)
            os << "exe{" << s << "}: "                                 <<
              "{" << hs << ' ' << x << "}{**} "                        <<
              "$libs"                                                  <<
              (itest ? " testscript" : "")                             << endl;
          else
            os << "./: exe{" << s << "}"                               << endl
               << "exe{" << s << "}: libue{" << s << "}"               <<
              (itest ? " testscript" : "")                             << endl
               << "libue{" << s << "}: "                               <<
              "{" << hs << ' ' << x << "}{** -**.test...} $libs"       << endl
               <<                                                         endl
               << "# Unit tests."                                      << endl
               << "#"                                                  << endl
               << "exe{*.test}: test = true"                           << endl
               << "exe{*.test}: install = false"                       << endl
               <<                                                         endl
               << "for t: " << x << "{**.test...}"                     << endl
               << "{"                                                  << endl
               << "  d = $directory($t)"                               << endl
               << "  n = $name($t)..."                                 << endl
               <<                                                         endl
               << "  ./: $d/exe{$n}"                                   << endl
               << "  $d/exe{$n}: $t $d/{" << hs                        <<
              "}{+$n} $d/testscript{+$n}"                              << endl
               << "  $d/exe{$n}: libue{" << s << "}: bin.whole = false"<< endl
               << "}"                                                  << endl;

          os <<                                                           endl
             << x << ".poptions =+ \"-I$out_root\" \"-I$src_root\""    << endl;
          os.close ();

          // <base>/.gitignore
          //
          if (vc == vcs::git)
          {
            os.open (f = sd / ".gitignore");
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
            os.open (f = sd / "testscript");
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
                os.open (f = sd / s + ".test.c");
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
                // <base>/<stem>.test.c(xx|pp)
                //
                os.open (f = sd / s + ".test.c" + es);
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
          string m; // Macro prefix.
          transform (
            b.begin (), b.end (), back_inserter (m),
            [] (char c)
            {
              return (c == '-' || c == '+' || c == '.') ? '_' : ucase (c);
            });

          string hdr; // API header name.
          string exp; // Export header name.
          string ver; // Version header name.

          switch (l)
          {
          case lang::c:
            {
              hdr = s + ".h";
              exp = "export.h";
              ver = "version.h";

              // <stem>.h
              //
              os.open (f = sd / hdr);
              os << "#pragma once"                                     << endl
                 <<                                                       endl
                 << "#include <stdio.h>"                               << endl
                 <<                                                       endl
                 << "#include <" << b << "/" << exp << ">"             << endl
                 <<                                                       endl
                 << "// Print a greeting for the specified name into the specified"  << endl
                 << "// stream. On success, return the number of character printed." << endl
                 << "// On failure, set errno and return a negative value."          << endl
                 << "//"                                                             << endl
                 << m << "_SYMEXPORT int"                              << endl
                 << "say_hello (FILE *, const char *name);"            << endl;
              os.close ();

              // <stem>.c
              //
              os.open (f = sd / s + ".c");
              os << "#include <" << b << "/" << hdr << ">"             << endl
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
            {
              hdr = s + ".h" + es;
              exp = "export.h" + es;
              ver = "version.h" + es;

              // <stem>.h(xx|pp)
              //
              os.open (f = sd / hdr);
              os << "#pragma once"                                     << endl
                 <<                                                       endl
                 << "#include <iosfwd>"                                << endl
                 << "#include <string>"                                << endl
                 <<                                                       endl
                 << "#include <" << b << "/" << exp << ">"             << endl
                 <<                                                       endl
                 << "namespace " << id                                 << endl
                 << "{"                                                << endl
                 << "  // Print a greeting for the specified name into the specified" << endl
                 << "  // stream. Throw std::invalid_argument if the name is empty."  << endl
                 << "  //"                                                            << endl
                 << "  " << m << "_SYMEXPORT void"                     << endl
                 << "  say_hello (std::ostream&, "                     <<
                "const std::string& name);"                            << endl
                 << "}"                                                << endl;
              os.close ();

              // <stem>.c(xx|pp)
              //
              os.open (f = sd / s + ".c" + es);
              os << "#include <" << b << "/" << hdr << ">"             << endl
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
          os.open (f = sd / exp);
          os << "#pragma once"                                     << endl
             <<                                                       endl;
          if (l == lang::cxx)
          {
            os << "// Normally we don't export class templates (but do complete specializations)," << endl
               << "// inline functions, and classes with only inline member functions. Exporting"  << endl
               << "// classes that inherit from non-exported/imported bases (e.g., std::string)"   << endl
               << "// will end up badly. The only known workarounds are to not inherit or to not"  << endl
               << "// export. Also, MinGW GCC doesn't like seeing non-exported functions being"     << endl
               << "// used before their inline definition. The workaround is to reorder code. In"  << endl
               << "// the end it's all trial and error."                                           << endl
               <<                                                                                     endl;
          }
          os << "#if defined(" << m << "_STATIC)         // Using static."    << endl
             << "#  define " << m << "_SYMEXPORT"                             << endl
             << "#elif defined(" << m << "_STATIC_BUILD) // Building static." << endl
             << "#  define " << m << "_SYMEXPORT"                             << endl
             << "#elif defined(" << m << "_SHARED)       // Using shared."    << endl
             << "#  ifdef _WIN32"                                             << endl
             << "#    define " << m << "_SYMEXPORT __declspec(dllimport)"     << endl
             << "#  else"                                                     << endl
             << "#    define " << m << "_SYMEXPORT"                           << endl
             << "#  endif"                                                    << endl
             << "#elif defined(" << m << "_SHARED_BUILD) // Building shared." << endl
             << "#  ifdef _WIN32"                                             << endl
             << "#    define " << m << "_SYMEXPORT __declspec(dllexport)"     << endl
             << "#  else"                                                     << endl
             << "#    define " << m << "_SYMEXPORT"                           << endl
             << "#  endif"                                                    << endl
             << "#else"                                                       << endl
             << "// If none of the above macros are defined, then we assume we are being used"  << endl
             << "// by some third-party build system that cannot/doesn't signal the library"    << endl
             << "// type. Note that this fallback works for both static and shared but in case" << endl
             << "// of shared will be sub-optimal compared to having dllimport."                << endl
             << "//"                                                                            << endl
             << "#  define " << m << "_SYMEXPORT         // Using static or shared." << endl
             << "#endif"                                                             << endl;
          os.close ();

          // version.h[??].in
          //
          os.open (f = sd / ver + ".in");

          os << "#pragma once"                                         << endl
             <<                                                           endl
             << "// The numeric version format is AAABBBCCCDDDE where:"<< endl
             << "//"                                                   << endl
             << "// AAA - major version number"                        << endl
             << "// BBB - minor version number"                        << endl
             << "// CCC - bugfix version number"                       << endl
             << "// DDD - alpha / beta (DDD + 500) version number"     << endl
             << "// E   - final (0) / snapshot (1)"                    << endl
             << "//"                                                   << endl
             << "// When DDDE is not 0, 1 is subtracted from AAABBBCCC. For example:" << endl
             << "//"                                                   << endl
             << "// Version      AAABBBCCCDDDE"                        << endl
             << "//"                                                   << endl
             << "// 0.1.0        0000010000000"                        << endl
             << "// 0.1.2        0000010010000"                        << endl
             << "// 1.2.3        0010020030000"                        << endl
             << "// 2.2.0-a.1    0020019990010"                        << endl
             << "// 3.0.0-b.2    0029999995020"                        << endl
             << "// 2.2.0-a.1.z  0020019990011"                        << endl
             << "//"                                                   << endl
             << "#define " << m << "_VERSION       $" << v << ".version.project_number$ULL"   << endl
             << "#define " << m << "_VERSION_STR   \"$" << v << ".version.project$\""         << endl
             << "#define " << m << "_VERSION_ID    \"$" << v << ".version.project_id$\""      << endl
             <<                                                                                  endl
             << "#define " << m << "_VERSION_MAJOR $" << v << ".version.major$" << endl
             << "#define " << m << "_VERSION_MINOR $" << v << ".version.minor$" << endl
             << "#define " << m << "_VERSION_PATCH $" << v << ".version.patch$" << endl
             <<                                                                    endl
             << "#define " << m << "_PRE_RELEASE   $" << v << ".version.pre_release$" << endl
             <<                                                                          endl
             << "#define " << m << "_SNAPSHOT_SN   $" << v << ".version.snapshot_sn$ULL"  << endl
             << "#define " << m << "_SNAPSHOT_ID   \"$" << v << ".version.snapshot_id$\"" << endl;
          os.close ();

          // buildfile
          //
          os.open (f = sd / "buildfile");
          os << "int_libs = # Interface dependencies."                 << endl
             << "imp_libs = # Implementation dependencies."            << endl
             << "#import imp_libs += libhello%lib{hello}"              << endl
             <<                                                           endl;

          if (!utest)
            os << "lib{" << s << "}: "                                   <<
              "{" << hs << ' ' << x << "}{** -version} "                 <<
              h << "{version} $imp_libs $int_libs"                       << endl;
          else
            os << "./: lib{" << s << "}"                               << endl
               << "lib{" << s << "}: libul{" << s << "}"               << endl
               << "libul{" << s << "}: "                               <<
              "{" << hs << ' ' << x << "}{** -version -**.test...} "   <<
              h << "{version} \\"                                      << endl
               << "  $imp_libs $int_libs"                              << endl
               <<                                                         endl
               << "# Unit tests."                                      << endl
               << "#"                                                  << endl
               << "exe{*.test}: test = true"                           << endl
               << "exe{*.test}: install = false"                       << endl
               <<                                                         endl
               << "for t: " << x << "{**.test...}"                     << endl
               << "{"                                                  << endl
               << "  d = $directory($t)"                               << endl
               << "  n = $name($t)..."                                 << endl
               <<                                                         endl
               << "  ./: $d/exe{$n}"                                   << endl
               << "  $d/exe{$n}: $t $d/{" << hs                        <<
              "}{+$n} $d/testscript{+$n}"                              << endl
               << "  $d/exe{$n}: libul{" << s << "}: bin.whole = false"<< endl
               << "}"                                                  << endl;

          os <<                                                            endl
             << "# Include the generated version header into the distribution (so that we don't" << endl
             << "# pick up an installed one) and don't remove it when cleaning in src (so that"  << endl
             << "# clean results in a state identical to distributed)."                          << endl
             << "#"                                                                              << endl
             << h << "{version}: in{version} $src_root/manifest"        << endl
             << h << "{version}: dist  = true"                          << endl
             << h << "{version}: clean = ($src_root != $out_root)"      << endl
             <<                                                            endl
             << x << ".poptions =+ \"-I$out_root\" \"-I$src_root\""     << endl
             <<                                                            endl
             << "obja{*}: " << x << ".poptions += -D" << m << "_STATIC_BUILD" << endl
             << "objs{*}: " << x << ".poptions += -D" << m << "_SHARED_BUILD" << endl
             <<                                                            endl
             << "lib{" <<  s << "}: " << x << ".export.poptions = \"-I$out_root\" \"-I$src_root\"" << endl
             <<                                                            endl
             << "liba{" << s << "}: " << x << ".export.poptions += -D" << m << "_STATIC" << endl
             << "libs{" << s << "}: " << x << ".export.poptions += -D" << m << "_SHARED" << endl
             <<                                                            endl
             << "lib{" << s << "}: " << x << ".export.libs = $int_libs" << endl
             <<                                                            endl
             << "# For pre-releases use the complete version to make sure they cannot be used" << endl
             << "# in place of another pre-release or the final version. See the version module" << endl
             << "# for details on the version.* variable values."       << endl
             << "#"                                                                            << endl
             << "if $version.pre_release"                                                   << endl
             << "  lib{" << s << "}: bin.lib.version = @\"-$version.project_id\""           << endl
             << "else"                                                                      << endl
             << "  lib{" << s << "}: bin.lib.version = @\"-$version.major.$version.minor\"" << endl
             <<                                                            endl
             << "# Install into the " << b << "/ subdirectory of, say, /usr/include/" << endl
             << "# recreating subdirectories."                                        << endl
             << "#"                                                                   << endl
             << "{" << hs << "}{*}: install         = include/" << b << "/" << endl
             << "{" << hs << "}{*}: install.subdirs = true"              << endl;
          os.close ();

          // <base>/.gitignore
          //
          if (vc == vcs::git)
          {
            os.open (f = sd / ".gitignore");
            os << "# Generated version header."                        << endl
               << "#"                                                  << endl
               << ver                                                  << endl;
            if (utest)
              os <<                                                       endl
                 << "# Unit test executables and Testscript output directories" << endl
                 << "# (can be symlinks)."                             << endl
                 << "#"                                                << endl
                 << "*.test"                                           << endl
                 << "test-*.test"                                      << endl;
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
                os.open (f = sd / s + ".test.c");
                os << "#include <stdio.h>"                             << endl
                   << "#include <assert.h>"                            << endl
                   <<                                                     endl
                   << "#include <" << b << "/" << hdr << ">"           << endl
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
                // <base>/<stem>.test.c(xx|pp)
                //
                os.open (f = sd / s + ".test.c" + es);
                os << "#include <cassert>"                             << endl
                   << "#include <iostream>"                            << endl
                   <<                                                     endl
                   << "#include <" << b << "/" << hdr << ">"           << endl
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
          os.open (f = bd / "export.build");
          os << "$out_root/"                                           << endl
             << "{"                                                    << endl
             << "  include " << b << "/"                               << endl
             << "}"                                                    << endl
             <<                                                           endl
             << "export $out_root/" << b << "/$import.target"          << endl;
          os.close ();

          // tests/ (tests subproject).
          //
          if (!itest)
            break;

          dir_path td (dir_path (out) /= "tests");
          mk (td);

          // tests/build/
          //
          dir_path tbd (dir_path (td) /= "build");
          mk (tbd);

          // tests/build/bootstrap.build
          //
          os.open (f = tbd / "bootstrap.build");
          os << "project = # Unnamed tests subproject."                << endl
             <<                                                           endl
             << "using config"                                         << endl
             << "using test"                                           << endl
             << "using dist"                                           << endl;
          os.close ();

          // tests/build/root.build
          //
          os.open (f = tbd / "root.build");
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
                 <<                                                       endl
                 << "hxx{*}: extension = h" << es                      << endl
                 << "ixx{*}: extension = i" << es                      << endl
                 << "txx{*}: extension = t" << es                      << endl
                 << "cxx{*}: extension = c" << es                      << endl;
              break;
            }
          }
          os <<                                                           endl
             << "# Every exe{} in this subproject is by default a test."<< endl
             << "#"                                                    << endl
             << "exe{*}: test = true"                                  << endl
             <<                                                           endl
             << "# The test target for cross-testing (running tests under Wine, etc)." << endl
             << "#"                                                    << endl
             << "test.target = $" << x << ".target"                    << endl;
          os.close ();

          // tests/build/.gitignore
          //
          if (vc == vcs::git)
          {
            os.open (f = tbd / ".gitignore");
            os << "config.build"                                       << endl
               << "root/"                                              << endl
               << "bootstrap/"                                         << endl;
            os.close ();
          }

          // tests/buildfile
          //
          os.open (f = td / "buildfile");
          os << "./: {*/ -build/}"                                     << endl;
          os.close ();

          // tests/.gitignore
          //
          if (vc == vcs::git)
          {
            os.open (f = td / ".gitignore");
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
              // tests/basics/driver.c
              //
              os.open (f = td / "driver.c");
              os << "#include <stdio.h>"                               << endl
                 << "#include <errno.h>"                               << endl
                 << "#include <string.h>"                              << endl
                 << "#include <assert.h>"                              << endl
                 <<                                                       endl
                 << "#include <" << b << "/" << ver << ">"             << endl
                 << "#include <" << b << "/" << hdr << ">"             << endl
                 <<                                                       endl
                 << "int main ()"                                      << endl
                 << "{"                                                << endl
                 << "  char b[256];"                                   << endl
                 <<                                                       endl
                 << "  // Basics."                                     << endl
                 << "  //"                                             << endl
                 << "  {"                                              << endl
                 << "    FILE *o = fmemopen (b, sizeof (b), \"w\");"   << endl
                 << "    assert (say_hello (o, \"World\") > 0);"       << endl
                 << "    fclose (o);"                                  << endl
                 << "    assert (strcmp (b, \"Hello, World!\\n\") == 0);" << endl
                 << "  }"                                              << endl
                 <<                                                       endl
                 << "  // Empty name."                                 << endl
                 << "  //"                                             << endl
                 << "  {"                                              << endl
                 << "    FILE *o = fmemopen (b, sizeof (b), \"w\");"   << endl
                 << "    assert (say_hello (o, \"\") < 0 && errno == EINVAL);" << endl
                 << "    fclose (o);"                                  << endl
                 << "  }"                                              << endl
                 <<                                                       endl
                 << "  return 0;"                                      << endl
                 << "}"                                                << endl;
              os.close ();

              break;
            }
          case lang::cxx:
            {
              // tests/basics/driver.c(xx|pp)
              //
              os.open (f = td / "driver.c" + es);
              os << "#include <cassert>"                               << endl
                 << "#include <sstream>"                               << endl
                 << "#include <stdexcept>"                             << endl
                 <<                                                       endl
                 << "#include <" << b << "/" << ver << ">"             << endl
                 << "#include <" << b << "/" << hdr << ">"             << endl
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
          os.open (f = td / "buildfile");
          os << "import libs = " << n << "%lib{" << s << "}"           << endl
             <<                                                           endl
             << "exe{driver}: {" << hs << ' ' << x                     <<
            "}{**} $libs testscript{**}"                               << endl;
          // <<                                                         endl
          // << x << ".poptions =+ \"-I$out_root\" \"-I$src_root\""  << endl;
          os.close ();

          break;
        }
      case type::bare:
      case type::empty:
        {
          assert (false);
        }
      }

      break;
    }
    catch (const io_error& e)
    {
      fail << "unable to write " << f << ": " << e;
    }

    if (verb)
      text << "created new " << t << ' ' << (pkg ? "package" : "project")
           << ' ' << n << " in " << out;

    // --no-init | --package
    //
    if (o.no_init () || o.package ())
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

      configurations cfgs {
        cmd_init_config (
          o,
          o,
          prj,
          pkgs,
          db,
          ca ? o.config_add () : o.config_create (),
          args,
          ca,
          cc)};

      if (!pkgs.empty ())
        cmd_init (o, prj, db, cfgs, pkgs, scan_arguments (args) /* pkg_args */);
    }

    return 0;
  }
}
