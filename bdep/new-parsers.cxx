// file      : bdep/new-parsers.cxx -*- C++ -*-
// copyright : Copyright (c) 2014-2017 Code Synthesis Ltd
// license   : MIT; see accompanying LICENSE file

#include <bdep/new-parsers.hxx>

#include <bdep/new-options.hxx> // bdep::cli namespace, cmd_new_*_options

namespace bdep
{
  namespace cli
  {
    using type = cmd_new_type;
    using lang = cmd_new_lang;
    using vcs  = cmd_new_vcs;

    // Parse comma-separated list of of options starting from the first comma
    // at pos.
    //
    template <typename O>
    static O
    parse_options (const char* o, const string v, size_t pos)
    {
      // Use vector_scanner to parse the comma-separated list as options.
      //
      vector<string> os;
      for (size_t i (pos), j; i != string::npos; )
      {
        j = i + 1;
        i = v.find (',', j);
        os.push_back (string (v, j, i != string::npos ? i - j : i));
      }

      vector_scanner s (os);

      try
      {
        O r;
        r.parse (s,
                 unknown_mode::fail /* unknown_option */,
                 unknown_mode::fail /* unknown_argument */);
        return r;
      }
      catch (const unknown_option& e)
      {
        throw invalid_value (o, e.option ());
      }
      catch (const unknown_argument& e)
      {
        throw invalid_value (o, e.argument ());
      }
    }

    void parser<type>::
    parse (type& r, bool& xs, scanner& s)
    {
      const char* o (s.next ());

      if (!s.more ())
        throw missing_value (o);

      string v (s.next ());
      size_t i (v.find (','));
      string l (v, 0, i);

      if (l == "exe")
      {
        r.type = type::exe;
        r.exe_opt = parse_options<cmd_new_exe_options> (o, v, i);
      }
      else if (l == "lib")
      {
        r.type = type::lib;
        r.lib_opt = parse_options<cmd_new_lib_options> (o, v, i);
      }
      else if (l == "bare")
      {
        r.type = type::bare;
        r.bare_opt = parse_options<cmd_new_bare_options> (o, v, i);
      }
      else
        throw invalid_value (o, l);

      xs = true;
    }

    void parser<lang>::
    parse (lang& r, bool& xs, scanner& s)
    {
      const char* o (s.next ());

      if (!s.more ())
        throw missing_value (o);

      string v (s.next ());
      size_t i (v.find (','));
      string l (v, 0, i);

      if (l == "c")
      {
        r.lang = lang::c;
        r.c_opt = parse_options<cmd_new_c_options> (o, v, i);
      }
      else if (l == "c++")
      {
        r.lang = lang::cxx;
        r.cxx_opt = parse_options<cmd_new_cxx_options> (o, v, i);
      }
      else
        throw invalid_value (o, l);

      xs = true;
    }

    void parser<vcs>::
    parse (vcs& r, bool& xs, scanner& s)
    {
      const char* o (s.next ());

      if (!s.more ())
        throw missing_value (o);

      string v (s.next ());
      size_t i (v.find (','));
      string l (v, 0, i);

      if (l == "git")
      {
        r.vcs = vcs::git;
        r.git_opt = parse_options<cmd_new_git_options> (o, v, i);
      }
      else if (l == "none")
      {
        r.vcs = vcs::none;
        r.none_opt = parse_options<cmd_new_none_options> (o, v, i);
      }
      else
        throw invalid_value (o, l);

      xs = true;
    }
  }
}
