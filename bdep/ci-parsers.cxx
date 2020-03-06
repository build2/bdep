// file      : bdep/ci-parsers.cxx -*- C++ -*-
// license   : MIT; see accompanying LICENSE file

#include <sstream>

#include <bdep/utility.hxx> // trim()

#include <bdep/ci-parsers.hxx>

#include <bdep/ci-options.hxx> // bdep::cli namespace

namespace bdep
{
  namespace cli
  {
    using namespace std;
    using namespace butl;

    void parser<cmd_ci_override>::
    parse (cmd_ci_override& r, bool& xs, scanner& s)
    {
      auto add = [&r] (string&& n, string&& v)
      {
        r.push_back (
          manifest_name_value {move (n),
                               move (v),
                               0, 0, 0, 0, 0, 0, 0}); // Locations, etc.
      };

      string o (s.next ());

      if (!s.more ())
        throw missing_value (o);

      string v (s.next ());

      // Make sure that values we post to the CI service are UTF-8 encoded and
      // contain only the graphic Unicode codepoints.
      //
      auto validate_value = [&o, &v] ()
      {
        string what;
        if (!utf8 (v, what, codepoint_types::graphic))
          throw invalid_value (o, v, what);
      };

      if (o == "--build-email")
      {
        validate_value ();

        add ("build-email", move (v));
      }
      else if (o == "--builds")
      {
        validate_value ();

        add ("builds", move (v));
      }
      else if (o == "--override")
      {
        validate_value ();

        // Validate that the value has the <name>:<value> form.
        //
        // Note that the value semantics will be verified later, with the
        // package_manifest::verify_overrides() called on the final list.
        //
        size_t p (v.find (':'));

        if (p == string::npos)
          throw invalid_value (o, v, "missing ':'");

        string vn (v, 0, p);
        string vv (v, p + 1);

        // Let's trim the name and value in case they are copied from a
        // manifest file or similar.
        //
        trim (vn);
        trim (vv);

        if (vn.empty ())
          throw invalid_value (o, v, "empty value name");

        add (move (vn), move (vv));
      }
      else if (o == "--overrides-file")
      {
        // Parse the manifest file into the value list.
        //
        // Note that the values semantics will be verified later (see above).
        //
        try
        {
          ifdstream is (v);

          // In case we end up throwing invalid_value exception, its
          // description will mention the file path as an option value.
          // That's why we pass the empty name to the parser constructor not
          // to mention it twice.
          //
          manifest_parser p (is, "" /* name */);

          parse_manifest (p, r);
          is.close ();
        }
        catch (const manifest_parsing& e)
        {
          throw invalid_value (o,
                               v,
                               string ("unable to parse: ") + e.what ());
        }
        catch (const io_error& e)
        {
          // Sanitize the exception description.
          //
          ostringstream os;
          os << "unable to read: " << e;
          throw invalid_value (o, v, os.str ());
        }
      }
      else if (o == "--overrides") // Fake option.
      {
        throw unknown_option (o);
      }
      else
      {
        assert (false); // No other option is expected.
      }

      xs = true;
    }

    void parser<cmd_ci_override>::
    merge (cmd_ci_override& b, const cmd_ci_override& a)
    {
      b.insert (b.end (), a.begin (), a.end ());
    }
  }
}
