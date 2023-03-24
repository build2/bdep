// file      : bdep/utility.txx -*- C++ -*-
// license   : MIT; see accompanying LICENSE file

#include <cstring>  // strcmp()
#include <iostream> // cin

#include <bdep/diagnostics.hxx>

namespace bdep
{
  template <typename I, typename O, typename E, typename P, typename... A>
  process
  start (I&& in, O&& out, E&& err, const P& prog, A&&... args)
  {
    try
    {
      return butl::process_start_callback (
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

  template <typename P>
  void
  finish (const P& prog, process& pr, bool io_read, bool io_write)
  {
    if (!pr.wait ())
    {
      const process_exit& e (*pr.exit);

      if (e.normal ())
        throw failed (); // Assume the child issued diagnostics.

      fail << "process " << prog << " " << e;
    }

    if (io_read)
      fail << "error reading " << prog << " output";

    if (io_write)
      fail << "error writing " << prog << " input";
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

    finish (prog, pr);
  }

  // *_bpkg()
  //
  template <typename O, typename E, typename... A>
  process
  start_bpkg (uint16_t v,
              const common_options& co,
              O&& out,
              E&& err,
              A&&... args)
  {
    const char* bpkg (name_bpkg (co));

    try
    {
      process_path pp (process::path_search (bpkg, true /* init */, exec_dir));

      small_vector<const char*, 1> ops;

      // Map verbosity level.
      //
      bool q (false);

      string vl;
      {
        const char* o (nullptr);
        bool no_progress (co.no_progress ());

        switch (verb)
        {
        case  0: o =          "-q";                 break;
        case  1: o = q      ? "-q" : "--no-result"; break;
        case  2: o = 2 >= v ? "-v" : "--no-result"; break;
        default:
          {
            ops.push_back ("--verbose");
            vl = to_string (verb);
            o = vl.c_str ();
          }
        }

        if (o != nullptr)
        {
          ops.push_back (o);

          if (strcmp (o, "-q") == 0)
            no_progress = false; // No need to suppress (already done with -q).
        }

        if (co.progress ())
          ops.push_back ("--progress");

        if (no_progress)
          ops.push_back ("--no-progress");
      }

      // Forward our --[no]diag-color options.
      //
      if (co.diag_color ())
        ops.push_back ("--diag-color");

      if (co.no_diag_color ())
        ops.push_back ("--no-diag-color");

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

      // Forward our --curl* options.
      //
      if (co.curl_specified ())
      {
        ops.push_back ("--curl");
        ops.push_back (co.curl ().string ().c_str ());
      }

      for (const string& o: co.curl_option ())
      {
        ops.push_back ("--curl-option");
        ops.push_back (o.c_str ());
      }

      return process_start_callback (
        [v] (const char* const args[], size_t n)
        {
          if (verb >= v)
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
  run_bpkg (uint16_t v, const common_options& co, A&&... args)
  {
    process pr (start_bpkg (v,
                            co,
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
      process_path pp (process::path_search (b, true /* init */, exec_dir));

      small_vector<const char*, 1> ops;

      // Map verbosity level.
      //
      bool q (true);
      uint16_t v (3);

      string vl;
      {
        const char* o (nullptr);
        bool no_progress (co.no_progress ());

        switch (verb)
        {
        case  0: o = "-q";                               break;
        case  1: o = q ? "-q" : nullptr;                 break;
        case  2: o = 2 >= v ? "-v" : q ? "-q" : nullptr; break;
        default:
          {
            ops.push_back ("--verbose");
            vl = to_string (verb);
            o = vl.c_str ();
          }
        }

        if (o != nullptr)
        {
          ops.push_back (o);

          if (strcmp (o, "-q") == 0)
            no_progress = false; // No need to suppress (already done with -q).
        }

        if (co.progress ())
          ops.push_back ("--progress");

        if (no_progress)
          ops.push_back ("--no-progress");
      }

      // Forward our --[no]diag-color options.
      //
      if (co.diag_color ())
        ops.push_back ("--diag-color");

      if (co.no_diag_color ())
        ops.push_back ("--no-diag-color");

      return process_start_callback (
        [v] (const char* const args[], size_t n)
        {
          if (verb >= v)
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
    finish_b (co, pr);
  }

  // *_manifest()
  //
  template <typename T>
  T
  parse_manifest (const path& f,
                  const char* what,
                  bool iu,
                  function<butl::manifest_parser::filter_function> ff)
  {
    using namespace butl;

    try
    {
      if (f.string () == "-")
        return parse_manifest<T> (std::cin, "<stdin>", what, iu);

      if (!file_exists (f))
        fail << what << " manifest file " << f << " does not exist";

      ifdstream ifs (f);
      return parse_manifest<T> (ifs, f.string (), what, iu, move (ff));
    }
    catch (const system_error& e) // EACCES, etc.
    {
      fail << "unable to access " << what << " manifest " << f << ": " << e
           << endf;
    }
  }

  template <typename T>
  T
  parse_manifest (istream& is,
                  const string& name,
                  const char* what,
                  bool iu,
                  function<butl::manifest_parser::filter_function> ff)
  {
    using namespace butl;

    try
    {
      manifest_parser p (is, name, move (ff));
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
  serialize_manifest (const T& m,
                      const path& f,
                      const char* what,
                      function<butl::manifest_serializer::filter_function> ff)
  {
    using namespace std;
    using namespace butl;

    try
    {
      ofdstream ofs (f, fdopen_mode::binary);
      auto_rmfile arm (f); // Try to remove on failure ignoring errors.

      serialize_manifest (m, ofs, f.string (), what, move (ff));

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
                      const char* what,
                      function<butl::manifest_serializer::filter_function> ff)
  {
    using namespace butl;

    try
    {
      manifest_serializer s (os, name, false /* long_lines */, move (ff));
      m.serialize (s);
      return;
    }
    catch (const manifest_serialization& e)
    {
      fail << "invalid " << what << " manifest: " << e.description;
    }
    catch (const io_error& e)
    {
      fail << "unable to write to " << what << " manifest " << name << ": "
           << e;
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
