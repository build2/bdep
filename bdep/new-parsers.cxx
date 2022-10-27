// file      : bdep/new-parsers.cxx -*- C++ -*-
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
    // at pos, merging them with options parsed previously.
    //
    template <typename O>
    static void
    parse_options (const char* o, const string v, size_t pos, O& r)
    {
      // Use vector_scanner to parse the comma-separated list of
      // parameter-specific options. Make sure that option values are only
      // specified if required (no value for flags, etc).
      //
      vector<string> os;
      const options& ods (O::description ());

      for (size_t i (pos), j; i != string::npos; )
      {
        j = i + 1;
        i = v.find (',', j);

        string po (v, j, i != string::npos ? i - j : i);

        // Split the parameter-specific option into the name and value.
        //
        optional<string> pv;
        {
          size_t i (po.find ('='));

          if (i != string::npos)
          {
            pv = string (po, i + 1);
            po.resize (i);
          }
        }

        // Verify that the option is known and its value is only specified if
        // required.
        //
        {
          auto i (ods.find (po));

          if (i == ods.end ())
            throw invalid_value (o, po);

          bool flag (!pv);

          if (flag != i->flag ())
            throw invalid_value (o,
                                 po,
                                 string (flag ? "missing" : "unexpected") +
                                 " value for '" + po + '\'');
        }

        os.push_back (move (po));

        if (pv)
          os.push_back (move (*pv));
      }

      vector_scanner s (os);
      r.parse (s);
    }

    // parser<type>
    //
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
        parse_options<cmd_new_exe_options> (o, v, i, r.exe_opt);
      }
      else if (l == "lib")
      {
        r.type = type::lib;
        parse_options<cmd_new_lib_options> (o, v, i, r.lib_opt);
      }
      else if (l == "bare")
      {
        r.type = type::bare;
        parse_options<cmd_new_bare_options> (o, v, i, r.bare_opt);
      }
      else if (l == "empty")
      {
        r.type = type::empty;
        parse_options<cmd_new_empty_options> (o, v, i, r.empty_opt);
      }
      else
        throw invalid_value (o, l);

      xs = true;
    }

    void parser<type>::
    merge (type& b, const type& a)
    {
      b.type = a.type;

      b.exe_opt.merge   (a.exe_opt);
      b.lib_opt.merge   (a.lib_opt);
      b.bare_opt.merge  (a.bare_opt);
      b.empty_opt.merge (a.empty_opt);
    }

    // parser<lang>
    //
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
        parse_options<cmd_new_c_options> (o, v, i, r.c_opt);
      }
      else if (l == "c++")
      {
        r.lang = lang::cxx;
        parse_options<cmd_new_cxx_options> (o, v, i, r.cxx_opt);
      }
      else
        throw invalid_value (o, l);

      xs = true;
    }

    void parser<lang>::
    merge (lang& b, const lang& a)
    {
      b.lang = a.lang;

      b.c_opt.merge   (a.c_opt);
      b.cxx_opt.merge (a.cxx_opt);
    }

    // parser<vcs>
    //
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
        parse_options<cmd_new_git_options> (o, v, i, r.git_opt);
      }
      else if (l == "none")
      {
        r.vcs = vcs::none;
        parse_options<cmd_new_none_options> (o, v, i, r.none_opt);
      }
      else
        throw invalid_value (o, l);

      xs = true;
    }

    void parser<vcs>::
    merge (vcs& b, const vcs& a)
    {
      b.vcs = a.vcs;

      b.git_opt.merge  (a.git_opt);
      b.none_opt.merge (a.none_opt);
    }
  }
}
