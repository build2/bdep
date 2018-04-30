// file      : bdep/utility.txx -*- C++ -*-
// copyright : Copyright (c) 2014-2017 Code Synthesis Ltd
// license   : MIT; see accompanying LICENSE file

#include <iostream> // cin

#include <libbutl/manifest-parser.mxx>
#include <libbutl/manifest-serializer.mxx>

#include <bdep/diagnostics.hxx>

namespace bdep
{
  template <typename I, typename O, typename E, typename P, typename... A>
  process
  start (I&& in, O&& out, E&& err, const P& prog, A&&... args)
  {
    try
    {
      return process_start_callback (
        [] (const char* const args[], size_t n)
        {
          if (verb >= 2)
            print_process (args, n);
        },
        forward<I> (in),
        forward<O> (out),
        forward<E> (err),
        prog,
        forward<A> (args)...);
    }
    catch (const process_error& e)
    {
      fail << "unable to execute " << prog << ": " << e << endf;
    }
  }

  template <typename P, typename... A>
  void
  run (const P& prog, A&&... args)
  {
    process pr (start (0 /* stdin  */,
                       1 /* stdout */,
                       2 /* stderr */,
                       prog,
                       forward<A> (args)...));
    if (!pr.wait ())
    {
      const process_exit& e (*pr.exit);

      if (e.normal ())
        throw failed (); // Assume the child issued diagnostics.

      fail << "process " << prog << " " << e;
    }
  }

  // *_bpkg()
  //
  template <typename O, typename E, typename... A>
  process
  start_bpkg (const common_options& co,
              O&& out,
              E&& err,
              A&&... args)
  {
    const char* bpkg (name_bpkg (co));

    try
    {
      process_path pp (process::path_search (bpkg, exec_dir));

      small_vector<const char*, 1> ops;

      // Map verbosity level. If we are running quiet or at level 1, then run
      // bpkg quiet. Otherwise, run it at the same level as us.
      //
      bool quiet (false); // Maybe will become an argument one day.

      string vl;
      switch (verb)
      {
      case  0: ops.push_back (        "-q");                 break;
      case  1: ops.push_back (quiet ? "-q" : "--no-result"); break;
      case  2: ops.push_back (        "-v");                 break;
      default:
        {
          vl = to_string (verb);
          ops.push_back ("--verbose");
          ops.push_back (vl.c_str ());
        }
      }

      // Forward our --build* options.
      //
      if (co.build_specified ())
      {
        ops.push_back ("--build");
        ops.push_back (co.build ().string ().c_str ());
      }

      for (const string& o: co.build_option ())
      {
        ops.push_back ("--build-option");
        ops.push_back (o.c_str ());
      }

      return process_start_callback (
        [] (const char* const args[], size_t n)
        {
          if (verb >= 2)
            print_process (args, n);
        },
        0 /* stdin */,
        forward<O> (out),
        forward<E> (err),
        pp,
        ops,
        co.bpkg_option (),
        forward<A> (args)...);
    }
    catch (const process_error& e)
    {
      fail << "unable to execute " << bpkg << ": " << e << endf;
    }
  }

  template <typename... A>
  void
  run_bpkg (const common_options& co, A&&... args)
  {
    process pr (start_bpkg (co,
                            1 /* stdout */,
                            2 /* stderr */,
                            forward<A> (args)...));
    finish_bpkg (co, pr);
  }

  // *_b()
  //
  template <typename O, typename E, typename... A>
  process
  start_b (const common_options& co,
              O&& out,
              E&& err,
              A&&... args)
  {
    const char* b (name_b (co));

    try
    {
      process_path pp (process::path_search (b, exec_dir));

      small_vector<const char*, 1> ops;

      // Map verbosity level. If we are running quiet or at level 1, then run
      // b quiet. Otherwise, run it at the same level as us.
      //
      bool quiet (true); // Maybe will become an argument one day.

      string vl;
      switch (verb)
      {
      case  0:            ops.push_back ("-q"); break;
      case  1: if (quiet) ops.push_back ("-q"); break;
      case  2:            ops.push_back ("-v"); break;
      default:
        {
          vl = to_string (verb);
          ops.push_back ("--verbose");
          ops.push_back (vl.c_str ());
        }
      }

      return process_start_callback (
        [] (const char* const args[], size_t n)
        {
          if (verb >= 2)
            print_process (args, n);
        },
        0 /* stdin */,
        forward<O> (out),
        forward<E> (err),
        pp,
        ops,
        co.build_option (),
        forward<A> (args)...);
    }
    catch (const process_error& e)
    {
      fail << "unable to execute " << b << ": " << e << endf;
    }
  }

  template <typename... A>
  void
  run_b (const common_options& co, A&&... args)
  {
    process pr (start_b (co,
                         1 /* stdout */,
                         2 /* stderr */,
                         forward<A> (args)...));
    if (!pr.wait ())
    {
      const process_exit& e (*pr.exit);

      if (e.normal ())
        throw failed (); // Assume the child issued diagnostics.

      fail << "process " << name_b (co) << " " << e;
    }
  }

  // *_manifest()
  //
  template <typename T>
  T
  parse_manifest (const path& f, const char* what, bool iu)
  {
    using namespace butl;

    try
    {
      if (f.string () == "-")
        return parse_manifest<T> (std::cin, "stdin", what, iu);

      if (!file_exists (f))
        fail << what << " manifest file " << f << " does not exist";

      ifdstream ifs (f);
      return parse_manifest<T> (ifs, f.string (), what, iu);
    }
    catch (const system_error& e) // EACCES, etc.
    {
      fail << "unable to access " << what << " manifest " << f << ": " << e
           << endf;
    }
  }

  template <typename T>
  T
  parse_manifest (istream& is, const string& name, const char* what, bool iu)
  {
    using namespace butl;

    try
    {
      manifest_parser p (is, name);
      return T (p, iu);
    }
    catch (const manifest_parsing& e)
    {
      fail << "invalid " << what << " manifest: " << name << ':'
           << e.line << ':' << e.column << ": " << e.description << endf;
    }
    catch (const io_error& e)
    {
      fail << "unable to read " << what << " manifest " << name << ": " << e
           << endf;
    }
  }

  template <typename T>
  void
  serialize_manifest (const T& m, const path& f, const char* what)
  {
    using namespace std;
    using namespace butl;

    try
    {
      ofdstream ofs (f, ios::binary);
      auto_rmfile arm (f); // Try to remove on failure ignoring errors.

      serialize_manifest (m, ofs, f.string (), what);

      ofs.close ();
      arm.cancel ();
    }
    catch (const system_error& e) // EACCES, etc.
    {
      fail << "unable to access " << what << " manifest " << f << ": " << e;
    }
  }

  template <typename T>
  void
  serialize_manifest (const T& m,
                      ostream& os,
                      const string& name,
                      const char* what)
  {
    using namespace butl;

    try
    {
      manifest_serializer s (os, name);
      m.serialize (s);
      return;
    }
    catch (const manifest_serialization& e)
    {
      fail << "invalid " << what << " manifest: " << e.description;
    }
    catch (const io_error& e)
    {
      fail << "unable to write " << what << " manifest " << name << ": " << e;
    }
  }

  template <typename C>
  bool
  parse_command (const char* cmd, C& r)
  {
    int argc (2);
    char* argv[] {const_cast<char*> (""), const_cast<char*> (cmd)};

    r.parse (argc, argv, true, cli::unknown_mode::stop);

    return argc == 1;
  }

  template <typename C>
  C
  parse_command (cli::scanner& scan, const char* what, const char* help)
  {
    // The next argument should be a command.
    //
    if (!scan.more ())
      fail << what << " expected" <<
        info << "run '" << help << "' for more information";

    const char* cmd (scan.next ());

    C r;
    if (parse_command (cmd, r))
      return r;

    fail << "unknown " << what << " '" << cmd << "'" <<
      info << "run '" << help << "' for more information" << endf;
  }
}
