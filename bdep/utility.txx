// file      : bdep/utility.txx -*- C++ -*-
// copyright : Copyright (c) 2014-2017 Code Synthesis Ltd
// license   : MIT; see accompanying LICENSE file

#include <iostream> // cin

#include <libbutl/fdstream.mxx>

#include <libbutl/manifest-parser.mxx>
#include <libbutl/manifest-serializer.mxx>

#include <bdep/diagnostics.hxx>

namespace bdep
{
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
}
