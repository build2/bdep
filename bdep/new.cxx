// file      : bdep/new.cxx -*- C++ -*-
// copyright : Copyright (c) 2014-2017 Code Synthesis Ltd
// license   : MIT; see accompanying LICENSE file

#include <bdep/new.hxx>

#include <bdep/project.hxx>
#include <bdep/database.hxx>
#include <bdep/diagnostics.hxx>

#include <bdep/init.hxx>

using namespace std;

namespace bdep
{
  using type = cmd_new_type;
  using lang = cmd_new_lang;
  using vcs  = cmd_new_vcs;

  int
  cmd_new (const cmd_new_options& o, cli::scanner& args)
  {
    tracer trace ("new");

    bool ca (o.config_add_specified ());
    bool cc (o.config_create_specified ());

    optional<bool> cd;
    if (o.default_ () || o.no_default ())
    {
      if (!ca && !cc)
        fail << "--[no-]default specified without --config-(add|create)";

      if (o.default_ () && o.no_default ())
        fail << "both --default and --no-default specified";

      cd = o.default_ () && !o.no_default ();
    }

    if (o.no_init ())
    {
      if (ca) fail << "both --no-init and --config-add specified";
      if (cc) fail << "both --no-init and --config-create specified";
    }

    // Validate type options.
    //
    const type& t (o.type ());

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
    const vcs& v (o.vcs ());

    // Validate argument.
    //
    string n (args.more () ? args.next () : "");
    if (n.empty ())
      fail << "project name argument expected";

    //@@ TODO: verify valid package name (put the helper in libbpkg).

    // Full name vs the name stem (e.g, 'hello' in 'libhello').
    //
    // We use the full name for filesystem directories and preprocessor macros
    // while the stem for modules, namespaces, etc.
    //
    string s;
    if (o.type () == type::lib)
    {
      if (n.compare (0, 3, "lib") != 0)
        fail << "library name does not start with 'lib'";

      s.assign (n, 3);
    }

    dir_path prj (n);
    prj.complete ();

    // If the directory already exists, make sure it is empty. Otherwise
    // create it.
    //
    if (!exists (prj))
      mk (prj);
    else if (!empty (prj))
      fail << "directory " << prj << " already exists";

    // Initialize the version control system. Do it before writing anything
    // ourselves in case it fails.
    //
    switch (v)
    {
    case vcs::git:  run ("git", "init", "-q", prj); break;
    case vcs::none:                                 break;
    }

    path f; // File currently being written.
    try
    {
      ofdstream os;

      // manifest
      //
      os.open (f = prj / "manifest");
      os << ": 1"                                                      << endl
         << "name: " << n                                              << endl
         << "version: 0.1.0-a.0.z"                                     << endl
         << "summary: " << s << " " << t << " project"                 << endl
         << "license: proprietary"                                     << endl
         << "url: https://example.org/" << n                           << endl
         << "email: you@example.org"                                   << endl
         << "depends: * build2 >= 0.7.0-"                              << endl
         << "depends: * bpkg >= 0.7.0-"                                << endl;
      os.close ();

      // repositories.manifest
      //
      os.open (f = prj / "repositories.manifest");
      os << ": 1"                                                      << endl
         << "# To add a repository for a dependency, uncomment the"    << endl
         << "# next four lines and specify its location."              << endl
         << "#role: prerequisite"                                      << endl
         << "#location: ..."                                           << endl
         << "#"                                                        << endl
         << "#:"                                                       << endl
         << "role: base"                                               << endl
         << "summary: " << n << " project repository"                  << endl;
      os.close ();

      // build/
      //
      dir_path bd (dir_path (prj) /= "build");
      mk (bd);

      // build/bootstrap.build
      //
      os.open (f = bd / "bootstrap.build");
      os << "project = " << n                                          << endl
         <<                                                               endl
         << "using version"                                            << endl
         << "using config"                                             << endl
         << "using test"                                               << endl
         << "using dist"                                               << endl
         << "using install"                                            << endl;
      os.close ();

      // build/root.build
      //
      os.open (f = bd / "root.build");
      switch (l)
      {
      case lang::c:
        {
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
          const char* s (l.cxx_opt.cpp () ? "pp" : "xx");

          os << "cxx.std = latest"                                     << endl
             <<                                                           endl
             << "using cxx"                                            << endl
             <<                                                           endl
             << "hxx{*}: extension = h" << s                           << endl
             << "ixx{*}: extension = i" << s                           << endl
             << "txx{*}: extension = t" << s                           << endl
             << "cxx{*}: extension = c" << s                           << endl;
          break;
        }
      }
      os.close ();

      // build/.gitignore
      //
      if (v == vcs::git)
      {
        os.open (f = bd / ".gitignore");
        os << "config.build"                                           << endl
           << "bootstrap/out-root.build"                               << endl;
        os.close ();
      }

      // buildfile
      //
      os.open (f = prj / "buildfile");
      os << "./: {*/ -build/} file{manifest}"                          << endl;
      os.close ();

      // .gitignore
      //
      if (v == vcs::git)
      {
        // Use POSIX directory separators here.
        //
        os.open (f = prj / ".gitignore");
        os << bdep_dir.string () << '/'                                << endl
           <<                                                             endl
           << "# Compiler/linker output."                              << endl
           << "#"                                                      << endl
           << "*.d"                                                    << endl
           << "*.t"                                                    << endl
           << "*.i"                                                    << endl
           << "*.ii"                                                   << endl
           << "*.o"                                                    << endl
           << "*.obj"                                                  << endl
           << "*.so"                                                   << endl
           << "*.dll"                                                  << endl
           << "*.a"                                                    << endl
           << "*.lib"                                                  << endl
           << "*.exp"                                                  << endl
           << "*.pdb"                                                  << endl
           << "*.ilk"                                                  << endl
           << "*.exe"                                                  << endl
           << "*.exe.dlls/"                                            << endl
           << "*.exe.manifest"                                         << endl
           << "*.pc"                                                   << endl;
        os.close ();
      }

      // <name>/ (source subdirectory).
      //
      dir_path sd (dir_path (prj) /= n);

      if (t != type::bare)
        mk (sd);

      switch (t)
      {
      case type::exe:
        {
          switch (l)
          {
          case lang::c:
            {
              // <name>.c
              //
              os.open (f = sd / n + ".c");
              os << "#include <stdio.h>"                               << endl
                 <<                                                       endl
                 << "int main ()"                                      << endl
                 << "{"                                                << endl
                 << "  printf (\"Hello, World!\\n\");"                 << endl
                 << "  return 0;"                                      << endl
                 << "}"                                                << endl;
              os.close ();

              break;
            }
          case lang::cxx:
            {
              string x (l.cxx_opt.cpp () ? "pp" : "xx");

              // <name>.c(xx|pp)
              //
              os.open (f = sd / n + ".c" + x);
              os << "#include <iostream>"                              << endl
                 <<                                                       endl
                 << "int main ()"                                      << endl
                 << "{"                                                << endl
                 << "  std::cout << \"Hello, World!\" << std::endl;"   << endl
                 << "}"                                                << endl;
              os.close ();

              break;
            }
          }

          // buildfile
          //
          os.open (f = sd / "buildfile");
          os << "libs ="                                           << endl
             << "#import libs += libhello%lib{hello}"              << endl
             <<                                                       endl;

          const char* x (nullptr); // Language module.
          switch (l)
          {
          case lang::c:
            {
              os << "exe{" << n << "}: {h c}{*} $libs"                 << endl;

              x = "c";
              break;
            }
          case lang::cxx:
            {
              os << "exe{" << n << "}: {hxx ixx txx cxx}{*} $libs"     << endl;

              x = "cxx";
              break;
            }
          }

          os <<                                                           endl
             << x << ".poptions =+ \"-I$out_root\" \"-I$src_root\""    << endl;
          os.close ();

          break;
        }
      case type::lib:
        {
          string m; // Macro prefix.
          transform (
            n.begin (), n.end (), back_inserter (m),
            [] (char c)
            {
              return (c == '-' || c == '+' || c == '.') ? '_' : ucase (c);
            });

          string exp; // Export header name.
          string ver; // Version header name.

          switch (l)
          {
          case lang::c:
            {
              exp = "export.h";
              ver = "version.h";

              // <stem>.h
              //
              os.open (f = sd / s + ".h");
              os << "#pragma once"                                     << endl
                 <<                                                       endl
                 << "#include <" << n << "/" << exp << ">"             << endl
                 <<                                                       endl
                 << m << "_SYMEXPORT void"                             << endl
                 << "say_hello (const char* name);"                    << endl;
              os.close ();

              // <stem>.c
              //
              os.open (f = sd / s + ".c");
              os << "#include <" << n << "/" << s << ".h" << ">"       << endl
                 <<                                                       endl
                 << "#include <stdio.h>"                               << endl
                 <<                                                       endl
                 << "#include <" << n << "/" << ver << ">"             << endl
                 <<                                                       endl
                 << "void"                                             << endl
                 << "say_hello (const char* n)"                        << endl
                 << "{"                                                << endl
                 << "  printf (\"Hello, %s from " << n << " %s!\\n\", n, " << m << "_VERSION_ID);" << endl
                 << "}"                                                << endl;
              os.close ();

              break;
            }
          case lang::cxx:
            {
              string x (l.cxx_opt.cpp () ? "pp" : "xx");

              exp = "export.h" + x;
              ver = "version.h" + x;

              // <stem>.h(xx|pp)
              //
              os.open (f = sd / s + ".h" + x);
              os << "#pragma once"                                     << endl
                 <<                                                       endl
                 << "#include <string>"                                << endl
                 <<                                                       endl
                 << "#include <" << n << "/" << exp << ">"             << endl
                 <<                                                       endl
                 << "namespace " << s                                  << endl
                 << "{"                                                << endl
                 << "  " << m << "_SYMEXPORT void"                     << endl
                 << "  say_hello (const std::string& name);"           << endl
                 << "}"                                                << endl;
              os.close ();

              // <stem>.c(xx|pp)
              //
              os.open (f = sd / s + ".c" + x);
              os << "#include <" << n << "/" << s << ".h" << x << ">"  << endl
                 <<                                                       endl
                 << "#include <iostream>"                              << endl
                 <<                                                       endl
                 << "#include <" << n << "/" << ver << ">"             << endl
                 <<                                                       endl
                 << "using namespace std;"                             << endl
                 <<                                                       endl
                 << "namespace " << s                                  << endl
                 << "{"                                                << endl
                 << "  void"                                           << endl
                 << "  say_hello (const string& n)"                    << endl
                 << "  {"                                              << endl
                 << "    cout << \"Hello, \" << n << \" from \""       << endl
                 << "         << \"" << n << " \" << " << m << "_VERSION_ID << '!' << endl;" << endl
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
               << "// export. Also, MinGW GCC doesn't like seeing non-exported function being"     << endl
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
             << "#define " << m << "_VERSION       $" << n << ".version.project_number$ULL"   << endl
             << "#define " << m << "_VERSION_STR   \"$" << n << ".version.project$\""         << endl
             << "#define " << m << "_VERSION_ID    \"$" << n << ".version.project_id$\""      << endl
             <<                                                                                  endl
             << "#define " << m << "_VERSION_MAJOR $" << n << ".version.major$" << endl
             << "#define " << m << "_VERSION_MINOR $" << n << ".version.minor$" << endl
             << "#define " << m << "_VERSION_PATCH $" << n << ".version.patch$" << endl
             <<                                                                    endl
             << "#define " << m << "_PRE_RELEASE   $" << n << ".version.pre_release$" << endl
             <<                                                                          endl
             << "#define " << m << "_SNAPSHOT_SN   $" << n << ".version.snapshot_sn$ULL"  << endl
             << "#define " << m << "_SNAPSHOT_ID   \"$" << n << ".version.snapshot_id$\"" << endl;
          os.close ();

          // buildfile
          //
          os.open (f = sd / "buildfile");
          os << "int_libs = # Interface dependencies."                 << endl
             << "imp_libs = # Implementation dependencies."            << endl
             << "#import imp_libs += libhello%lib{hello}"              << endl
             <<                                                           endl;

          const char* x  (nullptr); // Language module.
          const char* h  (nullptr); // Header target type.
          const char* hs (nullptr); // All header target types.
          switch (l)
          {
          case lang::c:
            {
              os << "lib{" <<  s << "}: {h c}{* -version} h{version}"  << endl;

              x  = "c";
              h  = "h";
              hs = "h";
              break;
            }
          case lang::cxx:
            {
              os << "lib{" <<  s << "}: {hxx ixx txx cxx}{* -version} hxx{version} $imp_libs $int_libs" << endl;

              x  = "cxx";
              h  = "hxx";
              hs = "hxx ixx txx";
              break;
            }
          }

          os <<                                                            endl
             << "# Include the generated version header into the distribution (so that we don't" << endl
             << "# pick up an installed one) and don't remove it when cleaning in src (so that"  << endl
             << "# clean results in a state identical to distributed)."                          << endl
             << "#"                                                                              << endl
             << h << "{version}: in{version} $src_root/file{manifest}"  << endl
             << h << "{version}: dist  = true"                          << endl
             << h << "{version}: clean = ($src_root != $out_root)"      << endl
             <<                                                            endl
             << x << ".poptions =+ \"-I$out_root\" \"-I$src_root\""     << endl
             << "lib{" <<  s << "}: " << x << ".export.poptions = \"-I$out_root\" \"-I$src_root\"" << endl
             <<                                                            endl
             << "liba{" << s << "}: " << x << ".export.poptions += -D" << m << "_STATIC" << endl
             << "libs{" << s << "}: " << x << ".export.poptions += -D" << m << "_SHARED" << endl
             <<                                                            endl
             << "obja{*}: " << x << ".poptions += -D" << m << "_STATIC_BUILD" << endl
             << "objs{*}: " << x << ".poptions += -D" << m << "_SHARED_BUILD" << endl
             <<                                                            endl
             << "lib{" << s << "}: " << x << ".export.libs = $int_libs" << endl
             <<                                                            endl
             << "# For pre-releases use the complete version to make sure they cannot be used" << endl
             << "# in place of another pre-release or the final version."                      << endl
             << "#"                                                                            << endl
             << "if $version.pre_release"                                                   << endl
             << "  lib{" << s << "}: bin.lib.version = @\"-$version.project_id\""           << endl
             << "else"                                                                      << endl
             << "  lib{" << s << "}: bin.lib.version = @\"-$version.major.$version.minor\"" << endl
             <<                                                            endl
             << "# Install into the " << n << "/ subdirectory of, say, /usr/include/" << endl
             << "# recreating subdirectories."                                        << endl
             << "#"                                                                   << endl
             << "{" << hs << "}{*}: install         = include/$project/" << endl
             << "{" << hs << "}{*}: install.subdirs = true"              << endl;
          os.close ();

          // build/export.build
          //
          os.open (f = bd / "export.build");
          os << "$out_root/:"                                          << endl
             << "{"                                                    << endl
             << "  include " << n << "/"                               << endl
             << "}"                                                    << endl
             <<                                                           endl
             << "export $out_root/" << n << "/lib{" << s << "}"        << endl;
          os.close ();

          break;
        }
      case type::bare:
        {
          break;
        }
      }
    }
    catch (const io_error& e)
    {
      fail << "unable to write " << f << ": " << e;
    }

    if (verb)
      text << "created new " << t << " project " << n << " in " << prj;

    // --no-init
    //
    if (o.no_init ())
      return 0;

    // Create .bdep/.
    //
    {
      dir_path d (prj / bdep_dir);
      mk (prj / bdep_dir);
    }

    // Everything else requires a database.
    //
    database db (open (prj, trace, true /* create */));

    if (ca || cc)
    {
      configurations cfgs {
        cmd_init_config (
          o,
          prj,
          db,
          ca ? o.config_add () : o.config_create (),
          args,
          ca,
          cc,
          cd)};

      package_locations pkgs {{n, dir_path ()}}; // project == package

      cmd_init (o, prj, db, cfgs, pkgs);
    }

    return 0;
  }
}
