// file      : bdep/bdep.cxx -*- C++ -*-
// copyright : Copyright (c) 2014-2018 Code Synthesis Ltd
// license   : MIT; see accompanying LICENSE file

#ifndef _WIN32
#  include <signal.h> // signal()
#endif

#include <cstring>  // strcmp()
#include <iostream>

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
#include <bdep/publish.hxx>
#include <bdep/deinit.hxx>
#include <bdep/config.hxx>
#include <bdep/test.hxx>
#include <bdep/update.hxx>
#include <bdep/clean.hxx>

using namespace std;
using namespace bdep;

namespace bdep
{
  int
  main (int argc, char* argv[]);
}

// Initialize the command option class O with the common options and then
// parse the rest of the command line placing non-option arguments to args.
// Once this is done, use the "final" values of the common options to do
// global initializations (verbosity level, etc).
//
// If O is-a configuration_name_options, then also handle the @<cfg-name>
// arguments and place them into configuration_name_options::config_name.
//
static inline bool
cfg_name (configuration_name_options* o, const char* a)
{
  string n (a);

  if (n.empty ())
    fail << "empty configuration name";

  o->config_name ().push_back (move (n));
  o->config_name_specified (true);
  return true;
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
      strings& args,
      bool tmp)
{
  O o;
  static_cast<common_options&> (o) = co;

  // We want to be able to specify options and arguments in any order (it is
  // really handy to just add -v at the end of the command line).
  //
  for (bool opt (true); scan.more (); )
  {
    if (opt)
    {
      const char* a (scan.peek ());

      // If we see first "--", then we are done parsing options.
      //
      if (strcmp (a, "--") == 0)
      {
        scan.next ();
        opt = false;
        continue;
      }

      // @<cfg-name> & -@<cfg-name>
      //
      if ((*a == '@' &&                cfg_name (&o, a + 1)) ||
          (*a == '-' && a[1] == '@' && cfg_name (&o, a + 2)))
      {
        scan.next ();
        continue;
      }

      // Parse the next chunk of options until we reach an argument (or eos).
      //
      if (o.parse (scan))
        continue;

      // Fall through.
    }

    // Copy over the argument including the group.
    //
    scan_argument (args, scan);
  }

  // Global initializations.
  //

  // Diagnostics verbosity.
  //
  verb = o.verbose_specified ()
    ? o.verbose ()
    : o.V () ? 3 : o.v () ? 2 : o.quiet () ? 0 : 1;

  // Temporary directory.
  //
  if (tmp)
    init_tmp (dir_path (prj_dir (&o)));

  return o;
}

int bdep::
main (int argc, char* argv[])
try
{
  using namespace cli;

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

  argv_file_scanner argv_scan (argc, argv, "--options-file");
  group_scanner scan (argv_scan);

  // First parse common options and --version/--help.
  //
  options o;
  o.parse (scan, unknown_mode::stop);

  if (o.version ())
  {
    cout << "bdep " << BDEP_VERSION_ID << endl
         << "libbpkg " << LIBBPKG_VERSION_ID << endl
         << "libbutl " << LIBBUTL_VERSION_ID << endl
         << "Copyright (c) 2014-2018 Code Synthesis Ltd" << endl
         << "This is free software released under the MIT license." << endl;
    return 0;
  }

  strings argsv; // To be filled by parse() above.
  vector_scanner vect_args (argsv);
  group_scanner args (vect_args);

  const common_options& co (o);

  if (o.help ())
    return help (init<help_options> (co, scan, argsv, false), "", nullptr);

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
    ho = init<help_options> (co, scan, argsv, false);

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
    //    r = cmd_new (init<cmd_new_options> (co, scan, argsv), args);
    //
    //  break;
    // }
    //
#define COMMAND_IMPL(ON, FN, SN, TMP)                                         \
    if (cmd.ON ())                                                            \
    {                                                                         \
      if (h)                                                                  \
        r = help (ho, SN, print_bdep_##FN##_usage);                           \
      else                                                                    \
        r = cmd_##FN (init<cmd_##FN##_options> (co, scan, argsv, TMP), args); \
                                                                              \
      break;                                                                  \
    }

    // Temp dir is initialized manually for these commands.
    //
    COMMAND_IMPL (new_,    new,     "new",     false);
    COMMAND_IMPL (sync,    sync,    "sync",    false);

    COMMAND_IMPL (init,    init,    "init",    true);
    COMMAND_IMPL (fetch,   fetch,   "fetch",   true);
    COMMAND_IMPL (status,  status,  "status",  true);
    COMMAND_IMPL (publish, publish, "publish", true);
    COMMAND_IMPL (deinit,  deinit,  "deinit",  true);
    COMMAND_IMPL (config,  config,  "config",  true);
    COMMAND_IMPL (test,    test,    "test",    true);
    COMMAND_IMPL (update,  update,  "update",  true);
    COMMAND_IMPL (clean,   clean,   "clean",   true);

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
