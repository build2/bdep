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

  int
  cmd_new (const cmd_new_options& o, cli::scanner& args)
  {
    tracer trace ("new");

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

    // Validate argument.
    //
    string n (args.more () ? args.next () : "");
    if (n.empty ())
      fail << "project name argument expected";

    //@@ TODO: verify valid package name (put the helper in libbpkg).

    if (o.type () == type::lib && n.compare (0, 3, "lib") != 0)
      fail << "library name does not start with 'lib'";

    dir_path prj (n);
    prj.complete ();

    // If the directory already exists, make sure it is empty. Otherwise
    // create it.
    //
    if (!exists (prj))
      mk (prj);
    else if (!empty (prj))
      fail << "directory " << prj << " already exists";

    // Initialize the git repository. Do it before writing anything ourselves
    // in case it fails.
    //
    if (!o.no_git ())
      run ("git", "init", "-q", prj);

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
         << "summary: new " << t << " project"                         << endl
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
             << "c{*}: extension = c"                                  << endl
             <<                                                           endl
             << "c.poptions =+ \"-I$out_root\" \"-I$src_root\""        << endl;
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
             << "cxx{*}: extension = c" << s                           << endl
             <<                                                           endl
             << "cxx.poptions =+ \"-I$out_root\" \"-I$src_root\""      << endl;
          break;
        }
      }
      os.close ();

      // build/.gitignore
      //
      if (!o.no_git ())
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
      if (!o.no_git ())
      {
        os.open (f = prj / ".gitignore");
        os << "# Compiler/linker output."                              << endl
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
      {
        mk (sd);
        os.open (f = sd / "buildfile");
      }

      switch (t)
      {
      case type::exe:
        {
          switch (l)
          {
          case lang::c:
            {
              // buildfile
              //
              os << "exe{" << n << "}: {h c}{*}" << endl;
              os.close ();

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
              // buildfile
              //
              os << "exe{" << n << "}: {hxx ixx txx cxx}{*}" << endl;
              os.close ();

              const char* s (l.cxx_opt.cpp () ? "pp" : "xx");

              // <name>.c(xx|pp)
              //
              os.open (f = sd / n + ".c" + s);
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
          break;
        }
      case type::lib:
        {
          switch (l)
          {
          case lang::c:
            {
              // buildfile
              //
              os << "lib{" << n << "}: {h c}{*}" << endl;
              os.close ();

              //@@ TODO

              break;
            }
          case lang::cxx:
            {
              // buildfile
              //
              os << "lib{" << n << "}: {hxx ixx txx cxx}{*}" << endl;
              os.close ();

              //@@ TODO

              break;
            }
          }
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
    bool ca (o.config_add_specified ());
    bool cc (o.config_create_specified ());

    if (o.no_init ())
    {
      if (ca) fail << "both --no-init and --config-add specified";
      if (cc) fail << "both --no-init and --config-create specified";

      return 0;
    }

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
          ca,
          cc)};

      package_locations pkgs {{n, dir_path ()}}; // project == package

      cmd_init (o, prj, db, cfgs, pkgs);
    }

    return 0;
  }
}
