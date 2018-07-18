// file      : bdep/utility.cxx -*- C++ -*-
// copyright : Copyright (c) 2014-2018 Code Synthesis Ltd
// license   : MIT; see accompanying LICENSE file

#include <bdep/utility.hxx>

#include <libbutl/process.mxx>
#include <libbutl/fdstream.mxx>

#include <bdep/diagnostics.hxx>

using namespace std;
using namespace butl;

namespace bdep
{
  const string   empty_string;
  const path     empty_path;
  const dir_path empty_dir_path;

  const dir_path bdep_dir  (".bdep");
  const path     bdep_file (bdep_dir / "bdep.sqlite3");

  const path manifest_file       ("manifest");
  const path packages_file       ("packages.manifest");
  const path repositories_file   ("repositories.manifest");
  const path configurations_file ("configurations.manifest");

  const char* argv0;
  dir_path exec_dir;

  dir_path temp_dir;

  auto_rmfile
  tmp_file (const string& p)
  {
    assert (!temp_dir.empty ());
    return auto_rmfile (temp_dir / path::traits::temp_name (p));
  }

  auto_rmdir
  tmp_dir (const string& p)
  {
    assert (!temp_dir.empty ());
    return auto_rmdir (temp_dir / dir_path (path::traits::temp_name (p)));
  }

  void
  init_tmp (const dir_path& prj)
  {
    // Whether the project is required or optional depends on the command so
    // if the project directory does not exist or it is not a bdep project
    // directory, we simply create tmp in a system one and let the command
    // complain if necessary.
    //
    dir_path d (prj.empty () ||
                !exists (prj / bdep_dir, true /* ignore_error */)
                ? dir_path::temp_path ("bdep")
                : prj / bdep_dir / dir_path ("tmp"));

    if (exists (d))
      rm_r (d, true /* dir_itself */, 2);

    mk (d); // We shouldn't need mk_p().

    temp_dir = move (d);
  }

  void
  clean_tmp (bool ignore_error)
  {
    if (!temp_dir.empty () && exists (temp_dir))
    {
      rm_r (temp_dir,
            true /* dir_itself */,
            3,
            ignore_error ? rm_error_mode::ignore : rm_error_mode::fail);

      temp_dir.clear ();
    }
  }

  bool
  exists (const path& f, bool ignore_error)
  {
    try
    {
      return file_exists (f, true /* follow_symlinks */, ignore_error);
    }
    catch (const system_error& e)
    {
      fail << "unable to stat path " << f << ": " << e << endf;
    }
  }

  bool
  exists (const dir_path& d, bool ignore_error)
  {
    try
    {
      return dir_exists (d, ignore_error);
    }
    catch (const system_error& e)
    {
      fail << "unable to stat path " << d << ": " << e << endf;
    }
  }

  bool
  empty (const dir_path& d)
  {
    try
    {
      return dir_empty (d);
    }
    catch (const system_error& e)
    {
      fail << "unable to scan directory " << d << ": " << e << endf;
    }
  }

  void
  mk (const dir_path& d)
  {
    if (verb >= 3)
      text << "mkdir " << d;

    try
    {
      try_mkdir (d);
    }
    catch (const system_error& e)
    {
      fail << "unable to create directory " << d << ": " << e;
    }
  }

  void
  rm (const path& f, uint16_t v)
  {
    if (verb >= v)
      text << "rm " << f;

    try
    {
      if (try_rmfile (f) == rmfile_status::not_exist)
        fail << "unable to remove file " << f << ": file does not exist";
    }
    catch (const system_error& e)
    {
      fail << "unable to remove file " << f << ": " << e;
    }
  }

  void
  rm_r (const dir_path& d, bool dir, uint16_t v, rm_error_mode m)
  {
    if (verb >= v)
      text << (dir ? "rmdir -r " : "rm -r ") << (dir ? d : d / dir_path ("*"));

    try
    {
      rmdir_r (d, dir, m == rm_error_mode::ignore);
    }
    catch (const system_error& e)
    {
      bool w (m == rm_error_mode::warn);

      (w ? warn : error) << "unable to remove " << (dir ? "" : "contents of ")
                         << "directory " << d << ": " << e;

      if (!w)
        throw failed ();
    }
  }

  const char*
  name_bpkg (const common_options& co)
  {
    return co.bpkg_specified ()
      ? co.bpkg ().string ().c_str ()
      : "bpkg" BDEP_EXE_SUFFIX;
  }

  const char*
  name_b (const common_options& co)
  {
    return co.build_specified ()
      ? co.build ().string ().c_str ()
      : "b" BDEP_EXE_SUFFIX;
  }

  void
  scan_argument (strings& r, cli::group_scanner& s)
  {
    // Copy over the argument including the group.
    //
    using scanner = cli::scanner;
    using group_scanner = cli::group_scanner;

    r.push_back (group_scanner::escape (s.next ()));

    scanner& gs (s.group ());
    if (gs.more ())
    {
      r.push_back ("+{");
      for (; gs.more (); r.push_back (group_scanner::escape (gs.next ()))) ;
      r.push_back ("}");
    }
  }
}
