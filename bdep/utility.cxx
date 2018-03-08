// file      : bdep/utility.cxx -*- C++ -*-
// copyright : Copyright (c) 2014-2017 Code Synthesis Ltd
// license   : MIT; see accompanying LICENSE file

#include <bdep/utility.hxx>

#include <iostream>     // cout, cin

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

  const dir_path bdep_dir (".bdep");

  const path manifest_file       ("manifest");
  const path packages_file       ("packages.manifest");
  const path configurations_file ("configurations.manifest");

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

  dir_path exec_dir;
}
