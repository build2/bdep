// file      : bdep/bdep.cxx -*- C++ -*-
// license   : MIT; see accompanying LICENSE file

#ifndef _WIN32
#  include <signal.h> // signal()
#endif

#include <limits>
#include <cstdlib>     // getenv()
#include <cstring>     // strcmp()
#include <iostream>
#include <exception>   // set_terminate(), terminate_handler
#include <type_traits> // enable_if, is_base_of

#include <libbutl/backtrace.hxx> // backtrace()

#include <bdep/types.hxx>
#include <bdep/utility.hxx>

#include <bdep/project.hxx>         // find_project()
#include <bdep/diagnostics.hxx>
#include <bdep/bdep-options.hxx>
#include <bdep/project-options.hxx>

// Commands.
//
#include <bdep/help.hxx>

#include <bdep/new.hxx>
#include <bdep/init.hxx>
#include <bdep/sync.hxx>
#include <bdep/fetch.hxx>
#include <bdep/status.hxx>
#include <bdep/ci.hxx>
#include <bdep/release.hxx>
#include <bdep/publish.hxx>
#include <bdep/deinit.hxx>
#include <bdep/config.hxx>
#include <bdep/test.hxx>
#include <bdep/update.hxx>
#include <bdep/clean.hxx>

using namespace std;
using namespace butl;
using namespace bdep;

namespace bdep
{
  // Deduce the default options files and the directory to start searching
  // from based on the command line options and arguments.
  //
  // default_options_files
  // options_files (const char* cmd,
  //                const cmd_xxx_options&,
  //                const strings& args);

  // Return the default options files and the project directory as a search
  // start directory for commands that operate on project/packages (and thus
  // have their options derived from project_options).
  //
  // Note that currently we don't support package-level default options files,
  // since it can be surprising that running a command for multiple packages
  // and doing the same for these packages individually may end up with
  // different outcomes.
  //
  static inline default_options_files
  options_files (const char* cmd, const project_options& o, const strings&)
  {
    // bdep.options
    // bdep-<cmd>.options

    return default_options_files {
      {path ("bdep.options"), path (string ("bdep-") + cmd + ".options")},
      find_project (o)};
  }

  // Merge the default options and the command line options. Fail if options
  // used to deduce the default options files or the start directory appear in
  // an options file (or for other good reasons).
  //
  // cmd_xxx_options
  // merge_options (const default_options<cmd_xxx_options>&,
  //                const cmd_xxx_options&);

  // Merge the default options and the command line options for commands
  // that operate on project/packages. Fail if --directory|-d appears in the
  // options file to avoid the chicken and egg problem.
  //
  template <typename O>
  static inline typename enable_if<is_base_of<project_options, O>::value,
                                   O>::type
  merge_options (const default_options<O>& defs, const O& cmd)
  {
    return merge_default_options (
      defs,
      cmd,
      [] (const default_options_entry<O>& e, const O&)
      {
        if (e.options.directory_specified ())
          fail (e.file) << "--directory|-d in default options file";
      });
  }

  int
  main (int argc, char* argv[]);
}

// Command line arguments starting position.
//
// We want the positions of the command line arguments to be after the default
// options files (parsed in init()). Normally that would be achieved by
// passing the last position of the previous scanner to the next. The problem
// is that we parse the command line arguments first (for good reasons). Also
// the default options files parsing machinery needs the maximum number of
// arguments to be specified and assigns the positions below this value (see
// load_default_options() for details). So we are going to "reserve" the first
// half of the size_t value range for the default options positions and the
// second half for the command line arguments positions.
//
static const size_t args_pos (numeric_limits<size_t>::max () / 2);

// Initialize the command option class O with the common options and then
// parse the rest of the command line placing non-option arguments to args.
// Once this is done, use the "final" values of the common options to do
// global initializations (verbosity level, etc).
//
// If O is-a configuration_name_options, then also handle the [-]@<cfg-name>
// arguments and place them into configuration_name_options::config_name.
//
static inline void
cfg_name (configuration_name_options* o, const char* a, size_t p)
{
  string n (a + (*a == '@' ? 1 : 2));

  if (n.empty ())
    fail << "missing configuration name in '" << a << "'";

  o->config_name ().emplace_back (move (n), p);
  o->config_name_specified (true);
}

static inline bool
cfg_name (...)
{
  return false;
}

// Find the project directory if it is possible for the option class O and
// return empty path otherwise.
//
template <typename O>
static inline auto
prj_dir (const O* o) -> decltype(find_project (*o))
{
  return find_project (*o);
}

static inline auto
prj_dir (...) -> const dir_path& {return empty_dir_path;}

template <typename O>
static O
init (const common_options& co,
      cli::group_scanner& scan,
      strings& args, cli::vector_scanner& args_scan,
      const char* cmd,
      bool keep_sep,
      bool tmp)
{
  using bdep::optional;
  using bdep::getenv;

  tracer trace ("init");

  O o;
  static_cast<common_options&> (o) = co;

  // We want to be able to specify options and arguments in any order (it is
  // really handy to just add -v at the end of the command line).
  //
  for (bool opt (true); scan.more (); )
  {
    if (opt)
    {
      // Parse the next chunk of options until we reach an argument (or eos).
      // Stop (rather than fail) on unknown option to handle -@<cfg-name>.
      //
      if (o.parse (scan, cli::unknown_mode::stop) && !scan.more ())
        break;

      const char* a (scan.peek ());

      // If we see first "--", then we are done parsing options.
      //
      if (strcmp (a, "--") == 0)
      {
        if (!keep_sep)
          scan.next ();

        opt = false;
        continue;
      }

      // Handle @<cfg-name> & -@<cfg-name>
      //
      if (*a == '@' || (*a == '-' && a[1] == '@'))
      {
        cfg_name (&o, a, scan.position ());
        scan.next ();
        continue;
      }

      // Handle unknown option.
      //
      if (a[0] == '-' && a[1] != '\0')
        throw cli::unknown_option (a);

      // Fall through.
    }

    // Copy over the argument including the group.
    //
    scan_argument (args, scan);
  }

  // Carry over the positions of the arguments. In particular, this can be
  // used to get the max position for the options.
  //
  args_scan.reset (0, scan.position ());

  // Note that the diagnostics verbosity level can only be calculated after
  // default options are loaded and merged (see below). Thus, to trace the
  // default options files search, we refer to the verbosity level specified
  // on the command line.
  //
  auto verbosity = [&o] ()
  {
    return o.verbose_specified ()
           ? o.verbose ()
           : o.V () ? 3 : o.v () ? 2 : o.quiet () ? 0 : 1;
  };

  // Load the default options files, unless --no-default-options is specified
  // on the command line or the BDEP_DEF_OPT environment variable is set to a
  // value other than 'true' or '1'.
  //
  optional<string> env_def (getenv ("BDEP_DEF_OPT"));

  // False if --no-default-options is specified on the command line. Note that
  // we cache the flag since it can be overridden by a default options file.
  //
  bool cmd_def (!o.no_default_options ());

  // Note: don't need to use group_scaner (no arguments in options files).
  //
  if (cmd_def && (!env_def || *env_def == "true" || *env_def == "1"))
  try
  {
    optional<dir_path> extra;
    if (o.default_options_specified ())
    {
      extra = o.default_options ();

      // Note that load_default_options() expects absolute and normalized
      // directory.
      //
      try
      {
        if (extra->relative ())
          extra->complete ();

        extra->normalize ();
      }
      catch (const invalid_path& e)
      {
        fail << "invalid --default-options value " << e.path;
      }
    }

    default_options<O> dos (
      load_default_options<O, cli::argv_file_scanner, cli::unknown_mode> (
        nullopt /* sys_dir */,
        home_directory (),
        extra,
        options_files (cmd, o, args),
        [&trace, &verbosity] (const path& f, bool r, bool o)
        {
          if (verbosity () >= 3)
          {
            if (o)
              trace << "treating " << f << " as " << (r ? "remote" : "local");
            else
              trace << "loading " << (r ? "remote " : "local ") << f;
          }
        },
        "--options-file",
        args_pos,
        1024));

    // Verify common options.
    //
    // Also merge the --*/--no-* options, overriding a less specific flag with
    // a more specific.
    //
    optional<bool> progress, diag_color;
    auto merge_no = [&progress, &diag_color] (
      const O& o,
      const default_options_entry<O>* e = nullptr)
    {
      {
        if (o.progress () && o.no_progress ())
        {
          diag_record dr;
          (e != nullptr ? dr << fail (e->file) : dr << fail)
          << "both --progress and --no-progress specified";
        }

        if (o.progress ())
          progress = true;
        else if (o.no_progress ())
          progress = false;
      }

      {
        if (o.diag_color () && o.no_diag_color ())
        {
          diag_record dr;
          (e != nullptr ? dr << fail (e->file) : dr << fail)
          << "both --diag-color and --no-diag-color specified";
        }

        if (o.diag_color ())
          diag_color = true;
        else if (o.no_diag_color ())
          diag_color = false;
      }
    };

    for (const default_options_entry<O>& e: dos)
      merge_no (e.options, &e);

    merge_no (o);

    o = merge_options (dos, o);

    if (progress)
    {
      o.progress (*progress);
      o.no_progress (!*progress);
    }

    if (diag_color)
    {
      o.diag_color (*diag_color);
      o.no_diag_color (!*diag_color);
    }
  }
  catch (const invalid_argument& e)
  {
    fail << "unable to load default options files: " << e;
  }
  catch (const pair<path, system_error>& e)
  {
    fail << "unable to load default options files: " << e.first << ": "
         << e.second;
  }

  // Propagate disabling of the default options files to the potential nested
  // invocations.
  //
  if (!cmd_def && (!env_def || *env_def != "0"))
    setenv ("BDEP_DEF_OPT", "0");

  // Global initializations.
  //

  // Diagnostics verbosity.
  //
  verb = verbosity ();

  // Temporary directory.
  //
  if (tmp)
    init_tmp (dir_path (prj_dir (&o)));

  return o;
}

// Print backtrace if terminating due to an unhandled exception. Note that
// custom_terminate is non-static and not a lambda to reduce the noise.
//
static terminate_handler default_terminate;

void
custom_terminate ()
{
  *diag_stream << backtrace ();

  if (default_terminate != nullptr)
    default_terminate ();
}

int bdep::
main (int argc, char* argv[])
try
{
  using namespace cli;

  default_terminate = set_terminate (custom_terminate);

  if (fdterm (stderr_fd ()))
  {
    stderr_term = std::getenv ("TERM");

    stderr_term_color =
#ifdef _WIN32
      // For now we disable color on Windows since it's unclear if/where/how
      // it is supported. Maybe one day someone will figure this out.
      //
      false
#else
      // This test was lifted from GCC (Emacs shell sets TERM=dumb).
      //
      *stderr_term != nullptr && strcmp (*stderr_term, "dumb") != 0
#endif
      ;
  }

  argv0 = argv[0];
  exec_dir = path (argv0).directory ();

  // This is a little hack to make our baseutils for Windows work when called
  // with absolute path. In a nutshell, MSYS2's exec*p() doesn't search in the
  // parent's executable directory, only in PATH. And since we are running
  // without a shell (that would read /etc/profile which sets PATH to some
  // sensible values), we are only getting Win32 PATH values. And MSYS2 /bin
  // is not one of them. So what we are going to do is add /bin at the end of
  // PATH (which will be passed as is by the MSYS2 machinery). This will make
  // MSYS2 search in /bin (where our baseutils live). And for everyone else
  // this should be harmless since it is not a valid Win32 path.
  //
#ifdef _WIN32
  {
    string mp;
    if (optional<string> p = getenv ("PATH"))
    {
      mp = move (*p);
      mp += ';';
    }
    mp += "/bin";

    setenv ("PATH", mp);
  }
#endif

  // On POSIX ignore SIGPIPE which is signaled to a pipe-writing process if
  // the pipe reading end is closed. Note that by default this signal
  // terminates a process. Also note that there is no way to disable this
  // behavior on a file descriptor basis or for the write() function call.
  //
#ifndef _WIN32
  if (signal (SIGPIPE, SIG_IGN) == SIG_ERR)
    fail << "unable to ignore broken pipe (SIGPIPE) signal: "
         << system_error (errno, generic_category ()); // Sanitize.
#endif

  argv_file_scanner argv_scan (argc, argv, "--options-file", false, args_pos);
  group_scanner scan (argv_scan);

  // First parse common options and --version/--help.
  //
  bdep::options o;
  o.parse (scan, unknown_mode::stop);

  if (o.version ())
  {
    cout << "bdep " << BDEP_VERSION_ID << endl
         << "libbpkg " << LIBBPKG_VERSION_ID << endl
         << "libbutl " << LIBBUTL_VERSION_ID << endl
         << "Copyright (c) " << BDEP_COPYRIGHT << "." << endl
         << "This is free software released under the MIT license." << endl;
    return 0;
  }

  strings argsv; // To be filled by parse() above.
  vector_scanner scanv (argsv);
  group_scanner args (scanv);

  const common_options& co (o);

  if (o.help ())
    return help (init<help_options> (co,
                                     scan,
                                     argsv, scanv,
                                     "help",
                                     false /* keep_sep */,
                                     false /* tmp */),
                 "",
                 nullptr);

  // The next argument should be a command.
  //
  commands cmd (parse_command<commands> (scan, "bdep command", "bdep help"));

  // If the command is 'help', then what's coming next is another command.
  // Parse it into cmd so that we only need to check for each command in one
  // place.
  //
  bool h (cmd.help ());
  help_options ho;

  if (h)
  {
    ho = init<help_options> (co,
                             scan,
                             argsv, scanv,
                             "help",
                             false /* keep_sep */,
                             false /* tmp */);

    if (args.more ())
    {
      const char* a (args.next ());

      cmd = commands (); // Clear the help option.

      // If not a command, then it got to be a help topic.
      //
      if (!parse_command (a, cmd))
        return help (ho, a, nullptr);
    }
    else
      return help (ho, "", nullptr);
  }

  // Handle commands.
  //
  int r (1);
  for (;;) // Breakout loop.
  try
  {
    // help
    //
    if (cmd.help ())
    {
      assert (h);
      r = help (ho, "help", print_bdep_help_usage);
      break;
    }

    // Commands.
    //
    // if (cmd.new_ ())
    // {
    //  if (h)
    //    r = help (ho, "new", print_bdep_cmd_new_usage);
    //  else
    //    r = cmd_new (init<cmd_new_options> (co,
    //                                        scan,
    //                                        argsv,
    //                                        scanv,
    //                                        false /* keep_sep */,
    //                                        true  /* tmp */),
    //                 args);
    //
    //  break;
    // }
    //
#define COMMAND_IMPL(ON, FN, SN, SEP, TMP)             \
    if (cmd.ON ())                                     \
    {                                                  \
      if (h)                                           \
        r = help (ho, SN, print_bdep_##FN##_usage);    \
      else                                             \
        r = cmd_##FN (init<cmd_##FN##_options> (co,    \
                                                scan,  \
                                                argsv, \
                                                scanv, \
                                                SN,    \
                                                SEP,   \
                                                TMP),  \
                      args);                           \
                                                       \
      break;                                           \
    }

    // Temp dir is initialized manually for these commands.
    //
    COMMAND_IMPL (new_,    new,     "new",     true, false);
    COMMAND_IMPL (sync,    sync,    "sync",    false, false);

    COMMAND_IMPL (init,    init,    "init",    true,  true);
    COMMAND_IMPL (fetch,   fetch,   "fetch",   false, true);
    COMMAND_IMPL (status,  status,  "status",  false, true);
    COMMAND_IMPL (ci,      ci,      "ci",      false, true);
    COMMAND_IMPL (release, release, "release", false, true);
    COMMAND_IMPL (publish, publish, "publish", false, true);
    COMMAND_IMPL (deinit,  deinit,  "deinit",  false, true);
    COMMAND_IMPL (config,  config,  "config",  true,  true);
    COMMAND_IMPL (test,    test,    "test",    false, true);
    COMMAND_IMPL (update,  update,  "update",  false, true);
    COMMAND_IMPL (clean,   clean,   "clean",   false, true);

    assert (false);
    fail << "unhandled command";
  }
  catch (const failed&)
  {
    r = 1;
    break;
  }

  clean_tmp (true /* ignore_error */);

  if (r != 0)
    return r;

  // Warn if args contain some leftover junk. We already successfully
  // performed the command so failing would probably be misleading.
  //
  if (args.more ())
  {
    diag_record dr;
    dr << warn << "ignoring unexpected argument(s)";
    while (args.more ())
      dr << " '" << args.next () << "'";
  }

  return 0;
}
catch (const failed&)
{
  return 1; // Diagnostics has already been issued.
}
// Note that there commands that rely on this handler.
//
catch (const cli::exception& e)
{
  error << e;
  return 1;
}
/*
catch (const std::exception& e)
{
  error << e;
  return 1;
}
*/

int
main (int argc, char* argv[])
{
  return bdep::main (argc, argv);
}
