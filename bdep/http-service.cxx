// file      : bdep/http-service.cxx -*- C++ -*-
// license   : MIT; see accompanying LICENSE file

#include <bdep/http-service.hxx>

#include <libbutl/curl.hxx>
#include <libbutl/fdstream.hxx> // fdterm()

#include <bdep/diagnostics.hxx>

using namespace std;
using namespace butl;

namespace bdep
{
  namespace http_service
  {
    result
    post (const common_options& o, const url& u, const parameters& params)
    {
      using parser     = manifest_parser;
      using parsing    = manifest_parsing;
      using name_value = manifest_name_value;

      // The overall plan is to post the data using the curl program, read
      // the HTTP response status and content type, read and parse the body
      // according to the content type, and obtain the result message and
      // optional reference in case of both the request success and failure.
      //
      // The successful request response (HTTP status code 200) is expected to
      // contain the result manifest (text/manifest content type). The faulty
      // response (HTTP status code other than 200) can either contain the
      // result manifest or a plain text error description (text/plain content
      // type) or some other content (for example text/html). We will print
      // the manifest message value, if available or the first line of the
      // plain text error description or, as a last resort, construct the
      // message from the HTTP status code and reason phrase.
      //
      string message;
      optional<uint16_t> status;  // Request result manifest status value.
      optional<string> reference;
      vector<name_value> body;

      // None of the 3XX redirect code semantics assume automatic re-posting.
      // We will treat all such codes as failures, additionally printing the
      // location header value to advise the user to try the other URL for the
      // request.
      //
      // Note that services that move to a new URL may well be responding with
      // the 301 (moved permanently) code.
      //
      optional<url> location;

      // Map the verbosity level.
      //
      cstrings v;

      auto suppress_progress = [&v] ()
      {
        v.push_back ("-s");
        v.push_back ("-S"); // But show errors.
      };
      bool sp (o.no_progress ());

      if (verb < 1)
      {
        if (!o.progress ())
        {
          suppress_progress ();
          sp = false;  // No need to suppress (already done).
        }
      }
      else if (verb == 1)
      {
        if (fdterm (2) && !sp)
          v.push_back ("--progress-bar");
      }
      else if (verb > 3)
        v.push_back ("-v");

      // Suppress progress.
      //
      // Note: the `-v -s` options combination is valid and results in a
      // verbose output without progress.
      //
      if (sp)
        suppress_progress ();

      // Convert the submit arguments to curl's --form* options and cache the
      // pointer to the file_text parameter value, if present, for writing
      // into curl's stdin.
      //
      strings fos;
      const string* file_text (nullptr);

      for (const parameter& p: params)
      {
        if (p.type == parameter::file_text)
        {
          assert (file_text == nullptr);
          file_text = &p.value;
        }

        fos.emplace_back (p.type == parameter::file ||
                          p.type == parameter::file_text
                          ? "--form"
                          : "--form-string");

        fos.emplace_back (
          p.type == parameter::file      ? p.name + "=@" + p.value :
          p.type == parameter::file_text ? p.name + "=@-"          :
          p.name + '='  + p.value);
      }

      // Note that it's a bad idea to issue the diagnostics while curl is
      // running, as it will be messed up with the progress output. Thus, we
      // throw the runtime_error exception on the HTTP response parsing error
      // (rather than use our fail stream) and issue the diagnostics after
      // curl finishes.
      //
      // Also note that we prefer the start/finish process facility for
      // running curl over using butl::curl because in this context it is
      // restrictive and inconvenient.
      //
      // Start curl program.
      //
      // Text mode seems appropriate.
      //
      fdpipe in_pipe  (open_pipe ());
      fdpipe out_pipe (file_text != nullptr ? open_pipe () : fdpipe ());

      // Note that we don't specify any default timeouts, assuming that bdep
      // is an interactive program and the user can always interrupt the
      // command (or pass the timeout with --curl-option).
      //
      process pr (
        start (file_text != nullptr ? out_pipe.in.get () : 0 /* stdin  */,
               in_pipe                                       /* stdout */,
               2                                             /* stderr */,
               o.curl (),
               v,
               "-A", (BDEP_USER_AGENT " curl"),

               o.curl_option (),

               // Include the response headers in the output so we can get the
               // status code/reason, content type, and the redirect location.
               //
               "--include",

               fos,
               u.string ()));

      // Shouldn't throw, unless something is severely damaged.
      //
      in_pipe.out.close ();

      if (file_text != nullptr)
        out_pipe.in.close ();

      bool io_write (false);
      bool io_read  (false);

      try
      {
        // First we read the HTTP response status line and headers. At this
        // stage we will read until the empty line (containing just CRLF). Not
        // being able to reach such a line is an error, which is the reason
        // for the exception mask choice.
        //
        ifdstream is (
          move (in_pipe.in),
          fdstream_mode::skip,
          ifdstream::badbit | ifdstream::failbit | ifdstream::eofbit);

        if (file_text != nullptr)
        {
          ofdstream os (move (out_pipe.out));
          os << *file_text;
          os.close ();

          // Indicate to the potential IO error handling that we are done with
          // writing.
          //
          file_text = nullptr;
        }

        auto bad_response = [] (const string& d) {throw runtime_error (d);};

        curl::http_status rs;

        try
        {
          rs = curl::read_http_status (is, false /* skip_headers */);
        }
        catch (const invalid_argument& e)
        {
          bad_response (
            string ("unable to read HTTP response status line: ") + e.what ());
        }

        // Read through the response headers until the empty line is
        // encountered and obtain the content type and/or the redirect
        // location, if present.
        //
        optional<string> ctype;

        // Check if the line contains the specified header and return its
        // value if that's the case. Return nullopt otherwise.
        //
        // Note that we don't expect the header values that we are interested
        // in to span over multiple lines.
        //
        string l;
        auto header = [&l] (const char* name) -> optional<string>
        {
          size_t n (string::traits_type::length (name));
          if (!(icasecmp (name, l, n) == 0 && l[n] == ':'))
            return nullopt;

          string r;
          size_t p (l.find_first_not_of (' ', n + 1)); // The value begin.
          if (p != string::npos)
          {
            size_t e (l.find_last_not_of (' '));       // The value end.
            assert (e != string::npos && e >= p);

            r = string (l, p, e - p + 1);
          }

          return optional<string> (move (r));
        };

        while (!(l = curl::read_http_response_line (is)).empty ())
        {
          if (optional<string> v = header ("Content-Type"))
            ctype = move (v);
          else if (optional<string> v = header ("Location"))
          {
            if ((rs.code >= 301 && rs.code <= 303) || rs.code == 307)
            try
            {
              location = url (*v);
              location->query = nullopt; // Can possibly contain '?submit'.
            }
            catch (const invalid_argument&)
            {
              // Let's just ignore invalid locations.
              //
            }
          }
        }

        assert (!eof (is)); // Would have already failed otherwise.

        // Now parse the response payload if the content type is specified and
        // is recognized (text/manifest or text/plain), skip it (with the
        // ifdstream's close() function) otherwise.
        //
        // Note that eof and getline() fail conditions are not errors anymore,
        // so we adjust the exception mask accordingly.
        //
        is.exceptions (ifdstream::badbit);

        if (ctype)
        {
          if (icasecmp ("text/manifest", *ctype, 13) == 0)
          {
            parser p (is, "manifest");
            name_value nv (p.next ());

            if (nv.empty ())
              bad_response ("empty manifest");

            const string& n (nv.name);
            string& v (nv.value);

            // The format version pair is verified by the parser.
            //
            assert (n.empty () && v == "1");

            body.push_back (move (nv)); // Save the format version pair.

            auto bad_value = [&p, &nv] (const string& d) {
              throw parsing (p.name (), nv.value_line, nv.value_column, d);};

            // Get and verify the HTTP status.
            //
            nv = p.next ();
            if (n != "status")
              bad_value ("no status specified");

            uint16_t c (curl::parse_http_status_code (v));
            if (c == 0)
              bad_value ("invalid HTTP status '" + v + '\'');

            if (c != rs.code)
              bad_value ("status " + v + " doesn't match HTTP response "
                         "code " + to_string (rs.code));

            // Get the message.
            //
            nv = p.next ();
            if (n != "message" || v.empty ())
              bad_value ("no message specified");

            message = move (v);

            // Try to get an optional reference.
            //
            nv = p.next ();

            if (n == "reference")
            {
              if (v.empty ())
                bad_value ("empty reference specified");

              reference = move (v);

              nv = p.next ();
            }

            // Save the remaining name/value pairs.
            //
            for (; !nv.empty (); nv = p.next ())
              body.push_back (move (nv));

            status = c;
          }
          else if (icasecmp ("text/plain", *ctype, 10) == 0)
            getline (is, message); // Can result in the empty message.
        }

        is.close (); // Detect errors.

        // The meaningful result we expect is either manifest (status code is
        // not necessarily 200) or HTTP redirect (location is present). We
        // unable to interpret any other cases and so report them as a bad
        // response.
        //
        if (!status)
        {
          if (rs.code == 200)
            bad_response ("manifest expected");

          if (message.empty ())
          {
            message = "HTTP status code " + to_string (rs.code);

            if (!rs.reason.empty ())
              message += " (" + lcase (rs.reason) + ')';
          }

          if (!location)
            bad_response (message);
        }
      }
      catch (const io_error&)
      {
        // Presumably the child process failed and issued diagnostics so let
        // finish() try to deal with that first.
        //
        (file_text != nullptr ? io_write : io_read) = true;
      }
      // Handle all parsing errors, including the manifest_parsing exception
      // that inherits from the runtime_error exception.
      //
      // Note that the io_error class inherits from the runtime_error class,
      // so this catch-clause must go last.
      //
      catch (const runtime_error& e)
      {
        finish (o.curl (), pr); // Throws on process failure.

        // Finally we can safely issue the diagnostics (see above for
        // details).
        //
        diag_record dr (fail);

        url du (u);
        du.query = nullopt; // Strip URL parameters from the diagnostics.

        dr << e <<
          info << "consider reporting this to " << du << " maintainers";

        if (reference)
          dr << info << "reference: " << *reference;
      }

      finish (o.curl (), pr, io_read, io_write);

      assert (!message.empty ());

      // Print the request failure reason and fail.
      //
      if (!status || *status != 200)
      {
        diag_record dr (fail);
        dr << message;

        if (reference)
          dr << info << "reference: " << *reference;

        if (location)
          dr << info << "new location: " << *location;

        // In case of a server error advise the user to re-try later, assuming
        // that the issue is temporary (service overload, network connectivity
        // loss, etc.).
        //
        if (status && *status >= 500 && *status < 600)
          dr << info << "try again later";
      }

      return result {move (message), move (reference), move (body)};
    }
  }
}
