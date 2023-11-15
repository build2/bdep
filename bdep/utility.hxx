// file      : bdep/utility.hxx -*- C++ -*-
// license   : MIT; see accompanying LICENSE file

#ifndef BDEP_UTILITY_HXX
#define BDEP_UTILITY_HXX

#include <memory>    // make_shared()
#include <string>    // to_string()
#include <utility>   // move(), forward(), declval(), make_pair()
#include <cassert>   // assert()
#include <iterator>  // make_move_iterator()
#include <algorithm> // find(), find_if()

#include <libbutl/ft/lang.hxx>

#include <libbutl/utility.hxx>         // icasecmp(), reverse_iterate(), etc
#include <libbutl/prompt.hxx>
#include <libbutl/fdstream.hxx>
#include <libbutl/filesystem.hxx>
#include <libbutl/default-options.hxx>

#include <libbutl/manifest-parser.hxx>     // manifest_parser::filter_function
#include <libbutl/manifest-serializer.hxx> // manifest_serializer::filter_function

#include <bdep/types.hxx>
#include <bdep/version.hxx>
#include <bdep/common-options.hxx>

namespace bdep
{
  using std::move;
  using std::forward;
  using std::declval;

  using std::make_pair;
  using std::make_shared;
  using std::make_move_iterator;
  using std::to_string;

  using std::find;
  using std::find_if;

  // <libbutl/utility.hxx>
  //
  using butl::ucase;
  using butl::lcase;
  using butl::icasecmp;

  using butl::alpha;
  using butl::alnum;

  using butl::trim;
  using butl::next_word;
  using butl::sanitize_identifier;

  using butl::reverse_iterate;

  using butl::make_guard;
  using butl::make_exception_guard;

  using butl::getenv;
  using butl::setenv;
  using butl::unsetenv;

  // <libbutl/prompt.hxx>
  //
  using butl::yn_prompt;

  // <libbutl/filesystem.hxx>
  //
  using butl::auto_rmfile;
  using butl::auto_rmdir;

  // <libbutl/default-options.hxx>
  //
  using butl::load_default_options;
  using butl::merge_default_options;
  using butl::default_options_start;

  // Empty string and path.
  //
  extern const string   empty_string;
  extern const path     empty_path;
  extern const dir_path empty_dir_path;

  // Widely-used paths.
  //
  extern const dir_path bdep_dir;  // .bdep/
  extern const path     bdep_file; // .bdep/bdep.sqlite3
  extern const dir_path bpkg_dir;  // .bpkg/

  extern const path manifest_file;       // manifest
  extern const path packages_file;       // packages.manifest
  extern const path repositories_file;   // repositories.manifest
  extern const path configurations_file; // configurations.manifest

  // Temporary directory facility.
  //
  // This is normally .bdep/tmp/ but can also be some system-wide directory
  // (e.g., /tmp/bdep-XXX/) if there is no bdep project. This directory
  // is automatically created and cleaned up for most commands in main() so
  // you don't need to call init_tmp() explicitly except for certain special
  // commands (like new).
  //
  extern dir_path temp_dir;

  auto_rmfile
  tmp_file (const string& prefix);

  auto_rmdir
  tmp_dir (const string& prefix);

  void
  init_tmp (const dir_path& prj);

  void
  clean_tmp (bool ignore_errors);

  // Process path (argv[0]).
  //
  extern const char* argv0;

  // Directory extracted from argv[0] (i.e., this process' recall directory)
  // or empty if there is none. Can be used as a search fallback.
  //
  extern dir_path exec_dir;

  // Path.
  //
  dir_path
  current_directory ();

  dir_path
  home_directory ();

  // Normalize a directory path. Also make the relative path absolute using
  // the current directory.
  //
  dir_path&
  normalize (dir_path&, const char* what);

  inline dir_path
  normalize (const dir_path& d, const char* what)
  {
    dir_path r (d);
    return move (normalize (r, what));
  }

  // Diagnostics.
  //
  // If stderr is not a terminal, then the value is absent (so can be used as
  // bool). Otherwise, it is the value of the TERM environment variable (which
  // can be NULL).
  //
  extern optional<const char*> stderr_term;

  // True if the color can be used on the stderr terminal.
  //
  extern bool stderr_term_color;

  // Filesystem.
  //
  bool
  exists (const path&, bool ignore_error = false);

  bool
  exists (const dir_path&, bool ignore_error = false);

  bool
  empty (const dir_path&);

  void
  mk (const dir_path&);

  void
  mk_p (const dir_path&);

  void
  rm (const path&, uint16_t verbosity = 3);

  enum class rm_error_mode {ignore, warn, fail};

  void
  rm_r (const dir_path&,
        bool dir_itself = true,
        uint16_t verbosity = 3,
        rm_error_mode = rm_error_mode::fail);

  // File descriptor streams.
  //
  fdpipe
  open_pipe ();

  auto_fd
  open_null ();

  // Run a process.
  //
  template <typename I, typename O, typename E, typename P, typename... A>
  process
  start (I&& in, O&& out, E&& err, const P& prog, A&&... args);

  template <typename P>
  void
  finish (const P& prog, process&, bool io_read = false, bool io_write = false);

  template <typename P, typename... A>
  void
  run (const P& prog, A&&... args);

  // Run the bpkg process.
  //
  const char*
  name_bpkg (const common_options&);

  template <typename O, typename E, typename... A>
  process
  start_bpkg (uint16_t verbosity,
              const common_options&,
              O&& out,
              E&& err,
              A&&... args);

  inline void
  finish_bpkg (const common_options& co, process& pr, bool io_read = false)
  {
    finish (name_bpkg (co), pr, io_read);
  }

  template <typename... A>
  void
  run_bpkg (uint16_t verbosity, const common_options&, A&&... args);

  // Run the b process.
  //
  const char*
  name_b (const common_options&);

  template <typename O, typename E, typename... A>
  process
  start_b (const common_options&, O&& out, E&& err, A&&... args);

  inline void
  finish_b (const common_options& co, process& pr, bool io_read = false)
  {
    finish (name_b (co), pr, io_read);
  }

  template <typename... A>
  void
  run_b (const common_options&, A&&... args);

  // Manifest parsing and serialization.
  //
  // For parsing, if path is '-', then read from stdin.
  //
  template <typename T>
  T
  parse_manifest (const path&,
                  const char* what,
                  bool ignore_unknown = false,
                  function<butl::manifest_parser::filter_function> = {});

  template <typename T>
  T
  parse_manifest (istream&,
                  const string& name,
                  const char* what,
                  bool ignore_unknown = false,
                  function<butl::manifest_parser::filter_function> = {});

  template <typename T>
  void
  serialize_manifest (
    const T&,
    const path&,
    const char* what,
    function<butl::manifest_serializer::filter_function> = {});

  template <typename T>
  void
  serialize_manifest (
    const T&,
    ostream&,
    const string& name,
    const char* what,
    function<butl::manifest_serializer::filter_function> = {});

  // CLI (sub)command parsing helper.
  //
  template <typename C>
  C
  parse_command (cli::scanner& scan, const char* what, const char* help);

  template <typename C>
  bool
  parse_command (cli::scanner& scan, C&);

  // Scan and return/append arguments preserving grouping.
  //
  void
  scan_argument (strings&, cli::group_scanner&);

  inline void
  scan_arguments (strings& r, cli::group_scanner& s)
  {
    for (; s.more (); scan_argument (r, s)) ;
  }

  inline strings
  scan_arguments (cli::group_scanner& s)
  {
    strings r;
    scan_arguments (r, s);
    return r;
  }

  namespace cli
  {
    class vector_group_scanner: public group_scanner
    {
    public:
      explicit
      vector_group_scanner (const std::vector<std::string>& args)
          : group_scanner (scan_), scan_ (args) {}

      void
      skip_group ()
      {
        for (scanner& g (group ()); g.more (); g.skip ()) ;
      }

    private:
      vector_scanner scan_;
    };
  }

  // Verify that a string is a valid UTF-8 byte sequence encoding only the
  // graphic Unicode codepoints. Issue diagnostics (including a suggestion to
  // use option opt, if specified) and fail if that's not the case.
  //
  void
  validate_utf8_graphic (const string&,
                         const char* what,
                         const char* opt = nullptr);

  // Return the canonical name of a directory repository location.
  //
  string
  repository_name (const dir_path&);
}

#include <bdep/utility.txx>

#endif // BDEP_UTILITY_HXX
