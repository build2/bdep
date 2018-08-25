// file      : bdep/submit.cxx -*- C++ -*-
// copyright : Copyright (c) 2014-2018 Code Synthesis Ltd
// license   : MIT; see accompanying LICENSE file

#include <bdep/http-service.hxx>

#include <cstdlib> // strtoul()

#include <libbutl/fdstream.mxx> // fdterm()

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
      process pr;
      bool io (false);
      try
      {
        // Map the verbosity level.
        //
        cstrings v;
        if (verb < 1)
        {
          v.push_back ("-s");
          v.push_back ("-S"); // But show errors.
        }
        else if (verb == 1 && fdterm (2))
          v.push_back ("--progress-bar");
        else if (verb > 3)
          v.push_back ("-v");

        // Convert the submit arguments to curl's --form* options.
        //
        strings fos;
        for (const parameter& p: params)
        {
          fos.emplace_back (p.type == parameter::file
                            ? "--form"
                            : "--form-string");

          fos.emplace_back (p.type == parameter::file
                            ? p.name + "=@" + p.value
                            : p.name + "="  + p.value);
        }

        // Start curl program.
        //
        fdpipe pipe (fdopen_pipe ()); // Text mode seems appropriate.

        // Note that we don't specify any default timeouts, assuming that bdep
        // is an interactive program and the user can always interrupt the
        // command (or pass the timeout with --curl-option).
        //
        pr = start (0          /* stdin  */,
                    pipe       /* stdout */,
                    2          /* stderr */,
                    o.curl (),
                    v,
                    "-A", (BDEP_USER_AGENT " curl"),

                    o.curl_option (),

                    // Include the response headers in the output so we can
                    // get the status code/reason, content type, and the
                    // redirect location.
                    //
                    "--include",

                    fos,
                    u.string ());

        pipe.out.close ();

        // First we read the HTTP response status line and headers. At this
        // stage we will read until the empty line (containing just CRLF). Not
        // being able to reach such a line is an error, which is the reason
        // for the exception mask choice.
        //
        ifdstream is (
          move (pipe.in),
          fdstream_mode::skip,
          ifdstream::badbit | ifdstream::failbit | ifdstream::eofbit);

        // Parse and return the HTTP status code. Return 0 if the argument is
        // invalid.
        //
        auto status_code = [] (const string& s)
        {
          char* e (nullptr);
          unsigned long c (strtoul (s.c_str (), &e, 10)); // Can't throw.
          assert (e != nullptr);

          return *e == '\0' && c >= 100 && c < 600
                 ? static_cast<uint16_t> (c)
                 : 0;
        };

        // Read the CRLF-terminated line from the stream stripping the
        // trailing CRLF.
        //
        auto read_line = [&is] ()
        {
          string l;
          getline (is, l); // Strips the trailing LF (0xA).

          // Note that on POSIX CRLF is not automatically translated into
          // LF, so we need to strip CR (0xD) manually.
          //
          if (!l.empty () && l.back () == '\r')
            l.pop_back ();

          return l;
        };

        auto bad_response = [] (const string& d) {throw runtime_error (d);};

        // Read and parse the HTTP response status line, return the status
        // code and the reason phrase.
        //
        struct http_status
        {
          uint16_t code;
          string reason;
        };

        auto read_status = [&read_line, &status_code, &bad_response] ()
        {
          string l (read_line ());

          for (;;) // Breakout loop.
          {
            if (l.compare (0, 5, "HTTP/") != 0)
              break;

            size_t p (l.find (' ', 5));             // The protocol end.
            if (p == string::npos)
              break;

            p = l.find_first_not_of (' ', p + 1);   // The code start.
            if (p == string::npos)
              break;

            size_t e (l.find (' ', p + 1));         // The code end.
            if (e == string::npos)
              break;

            uint16_t c (status_code (string (l, p, e - p)));
            if (c == 0)
              break;

            string r;
            p = l.find_first_not_of (' ', e + 1);   // The reason start.
            if (p != string::npos)
            {
              e = l.find_last_not_of (' ');         // The reason end.
              assert (e != string::npos && e >= p);

              r = string (l, p, e - p + 1);
            }

            return http_status {c, move (r)};
          }

          bad_response ("invalid HTTP response status line '" + l + "'");

          assert (false); // Can't be here.
          return http_status {};
        };

        // The curl output for a successfull request looks like this:
        //
        // HTTP/1.1 100 Continue
        //
        // HTTP/1.1 200 OK
        // Content-Length: 83
        // Content-Type: text/manifest;charset=utf-8
        //
        // : 1
        // status: 200
        // message: submission is queued
        // reference: 256910ca46d5
        //
        // curl normally sends the 'Expect: 100-continue' header for uploads,
        // so we need to handle the interim HTTP server response with the
        // continue (100) status code.
        //
        // Interestingly, Apache can respond with the continue (100) code and
        // with the not found (404) code afterwords. Can it be configured to
        // just respond with 404?
        //
        http_status rs (read_status ());

        if (rs.code == 100)
        {
          while (!read_line ().empty ()) ; // Skips the interim response.
          rs = read_status ();             // Reads the final status code.
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
          if (!(casecmp (name, l, n) == 0 && l[n] == ':'))
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

        while (!(l = read_line ()).empty ())
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
          if (casecmp ("text/manifest", *ctype, 13) == 0)
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

            uint16_t c (status_code (v));
            if (c == 0)
              bad_value ("invalid HTTP status '" + v + "'");

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
          else if (casecmp ("text/plain", *ctype, 10) == 0)
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
              message += " (" + lcase (rs.reason) + ")";
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
        io = true;
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

      finish (o.curl (), pr, io);

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
