// file      : bdep/publish.cxx -*- C++ -*-
// copyright : Copyright (c) 2014-2018 Code Synthesis Ltd
// license   : MIT; see accompanying LICENSE file

#include <bdep/publish.hxx>

#include <cstdlib> // strtoul()

#include <libbutl/fdstream.mxx>         // fdterm()
#include <libbutl/manifest-parser.mxx>
#include <libbutl/standard-version.mxx>
#include <libbutl/manifest-serializer.mxx>

#include <libbpkg/manifest.hxx>

#include <bdep/git.hxx>
#include <bdep/project.hxx>
#include <bdep/project-author.hxx>
#include <bdep/database.hxx>
#include <bdep/diagnostics.hxx>

#include <bdep/sync.hxx>

using namespace std;
using namespace butl;

namespace bdep
{
  // The minimum supported git version must be at least 2.5.0 due to the git
  // worktree command used. We also use bpkg that caps the git version at
  // 2.12.0, so let's use is as the lowest common denominator.
  //
  static const semantic_version git_ver {2, 12, 0};

  static inline url
  parse_url (const string& s, const char* what)
  {
    try
    {
      return url (s);
    }
    catch (const invalid_argument& e)
    {
      fail << "invalid " << what << " value '" << s << "': " << e << endf;
    }
  };

  // Get the project's control repository URL.
  //
  static url
  control_url (const dir_path& prj)
  {
    if (git_repository (prj))
    {
      // First try remote.origin.build2ControlUrl which can be used to specify
      // a custom URL (e.g., if a correct one cannot be automatically derived
      // from remote.origin.url).
      //
      if (optional<string> l = git_line (git_ver,
                                         prj,
                                         true /* ignore_error */,
                                         "config",
                                         "--get",
                                         "remote.origin.build2ControlUrl"))
      {
        return parse_url (*l, "remote.origin.build2ControlUrl");
      }

      // Otherwise, get remote.origin.url and try to derive an HTTPS URL from
      // it.
      //
      if (optional<string> l = git_line (git_ver,
                                         prj,
                                         true /* ignore_error */,
                                         "config",
                                         "--get",
                                         "remote.origin.url"))
      {
        string& s (*l);

        // This one will be fuzzy and hairy. Here are some representative
        // examples of what we can encounter:
        //
        //            example.org:/path/to/repo.git
        //       user@example.org:/path/to/repo.git
        //       user@example.org:~user/path/to/repo.git
        // ssh://user@example.org/path/to/repo.git
        //
        // git://example.org/path/to/repo.git
        //
        // http://example.org/path/to/repo.git
        // https://example.org/path/to/repo.git
        //
        //        /path/to/repo.git
        // file:///path/to/repo.git
        //
        // Note that git seem to always make remote.origin.url absolute in
        // case of a local filesystem path.
        //
        // So the algorithm will be as follows:
        //
        // 1. If there is scheme, then parse as URL.
        //
        // 2. Otherwise, check if this is an absolute path.
        //
        // 3. Otherwise, assume SSH <host>:<path> thing.
        //
        url u;

        // Find the scheme.
        //
        // Note that in example.org:/path/... example.org is a valid scheme.
        // To distinguish this, we check if the scheme contains any dots (none
        // of the schemes recognized by git currently do and probably never
        // will).
        //
        size_t p (s.find (':'));
        if (p != string::npos                  && // Has ':'.
            url::traits::find (s, p) == 0      && // Scheme starts at 0.
            s.rfind ('.', p - 1) == string::npos) // No dots in scheme.
        {
          u = parse_url (s, "remote.origin.url");
        }
        else
        {
          // Absolute path or the SSH thing.
          //
          if (path::traits::absolute (s))
          {
            // This is what we want to end up with:
            //
            // file:///tmp
            // file:///c:/tmp
            //
            const char* h (s[0] == '/' ? "file://" : "file:///");
            u = parse_url (h + s, "remote.origin.url");
          }
          else if (p != string::npos)
          {
            // This can still include user (user@host) so let's add the
            // scheme, replace/erase ':', and parse it as a string
            // representation of a URL.
            //
            if (s[p + 1] == '/') // POSIX notation.
              s.erase (p, 1);
            else
              s[p] = '/';

            u = parse_url ("ssh://" + s, "remote.origin.url");
          }
          else
            fail << "invalid remote.origin.url value '" << s << "': not a URL";
        }

        if (u.scheme == "http" || u.scheme == "https")
          return u;

        // Derive an HTTPS URL from a remote URL (and hope for the best).
        //
        if (u.scheme != "file" && u.authority && u.path)
          return url ("https", u.authority->host, *u.path);

        fail << "unable to derive control repository URL from " << u <<
          info << "consider setting remote.origin.build2ControlUrl" <<
          info << "or use --control to specify explicitly";
      }

      fail << "unable to discover control repository URL: no git "
           << "remote.origin.url value";
    }

    fail << "unable to discover control repository URL" <<
      info << "use --control to specify explicitly" << endf;
  }

  static standard_version
  package_version (const common_options& o,
                   const dir_path& cfg,
                   const package_name& p)
  {
    // We could have used bpkg-pkg-status but then we would have to deal with
    // iterations. So we use the build system's info meta-operation directly.
    //
    string v;
    {
      process pr;
      bool io (false);
      try
      {
        fdpipe pipe (fdopen_pipe ()); // Text mode seems appropriate.

        // Note: the package directory inside the configuration is a bit of an
        // assumption.
        //
        pr = start_b (
          o,
          pipe /* stdout */,
          2    /* stderr */,
          "info:", (dir_path (cfg) /= p.string ()).representation ());

        pipe.out.close ();
        ifdstream is (move (pipe.in), fdstream_mode::skip, ifdstream::badbit);

        for (string l; !eof (getline (is, l)); )
        {
          // Verify the name for good measure (comes before version).
          //
          if (l.compare (0, 9, "project: ") == 0)
          {
            if (l.compare (9, string::npos, p.string ()) != 0)
              fail << "name mismatch for package " << p;
          }
          else if (l.compare (0, 9, "version: ") == 0)
          {
            v = string (l, 9);
            break;
          }
        }

        is.close (); // Detect errors.
      }
      catch (const io_error&)
      {
        // Presumably the child process failed and issued diagnostics so let
        // finish_b() try to deal with that first.
        //
        io = true;
      }

      finish_b (o, pr, io);
    }

    try
    {
      return standard_version (v);
    }
    catch (const invalid_argument& e)
    {
      fail << "invalid package " << p << " version " << v << ": " << e << endf;
    }
  }

  // Submit package archive using the curl program and parse the response
  // manifest. On success, return the submission reference (first) and message
  // (second). Issue diagnostics and fail if anything goes wrong.
  //
  static pair<string, string>
  submit (const cmd_publish_options& o,
          const path& archive,
          const string& checksum,
          const string& section,
          const project_author& author,
          const optional<url>& ctrl)
  {
    using parser     = manifest_parser;
    using parsing    = manifest_parsing;
    using name_value = manifest_name_value;

    // The overall plan is to post the archive using the curl program, read
    // the HTTP response status and content type, read and parse the body
    // according to the content type, and obtain the result reference and
    // message in case of both the submission success and failure.
    //
    // The successful submission response (HTTP status code 200) is expected
    // to contain the submission result manifest (text/manifest content type).
    // The faulty response (HTTP status code other than 200) can either
    // contain the result manifest or a plain text error description
    // (text/plain content type) or some other content (for example
    // text/html). We will print the manifest message value, if available or
    // the first line of the plain text error description or, as a last
    // resort, construct the message from the HTTP status code and reason
    // phrase.
    //
    string message;
    optional<uint16_t> status;  // Submission result manifest status value.
    optional<string> reference; // Must be present on the submission success.

    // None of the 3XX redirect code semantics assume automatic re-posting. We
    // will treat all such codes as failures, additionally printing the
    // location header value to advise the user to try the other URL for the
    // package submission.
    //
    // Note that repositories that move to a new URL may well be responding
    // with the moved permanently (301) code.
    //
    optional<url> location;

    // Note that it's a bad idea to issue the diagnostics while curl is
    // running, as it will be messed up with the progress output. Thus, we
    // throw the runtime_error exception on the HTTP response parsing error
    // (rather than use our fail stream) and issue the diagnostics after curl
    // finishes.
    //
    // Also note that we prefer the start/finish process facility for running
    // curl over using butl::curl because in this context it is restrictive
    // and inconvenient.
    //
    process pr;
    bool io (false);
    try
    {
      url u (o.repository ());
      u.query = "submit";

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

                  // Include the response headers in the output so we can get
                  // the status code/reason, content type, and the redirect
                  // location.
                  //
                  "--include",

                  "--form",        "archive=@"     + archive.string (),
                  "--form-string", "sha256sum="    + checksum,
                  "--form-string", "section="      + section,
                  "--form-string", "author-name="  + *author.name,
                  "--form-string", "author-email=" + *author.email,

                  ctrl
                  ? strings ({"--form-string", "control=" + ctrl->string ()})
                  : strings (),

                  o.simulate_specified ()
                  ? strings ({"--form-string", "simulate=" + o.simulate ()})
                  : strings (),

                  u.string ());

      pipe.out.close ();

      // First we read the HTTP response status line and headers. At this
      // stage we will read until the empty line (containing just CRLF). Not
      // being able to reach such a line is an error, which is the reason for
      // the exception mask choice.
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

      // Read the CRLF-terminated line from the stream stripping the trailing
      // CRLF.
      //
      auto read_line = [&is] ()
      {
        string l;
        getline (is, l); // Strips the trailing LF (0xA).

        // Note that on POSIX CRLF is not automatically translated into LF,
        // so we need to strip CR (0xD) manually.
        //
        if (!l.empty () && l.back () == '\r')
          l.pop_back ();

        return l;
      };

      auto bad_response = [] (const string& d) {throw runtime_error (d);};

      // Read and parse the HTTP response status line, return the status code
      // and the reason phrase.
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

          size_t p (l.find (' ', 5));             // Finds the protocol end.
          if (p == string::npos)
            break;

          p = l.find_first_not_of (' ', p + 1);   // Finds the code start.
          if (p == string::npos)
            break;

          size_t e (l.find (' ', p + 1));         // Finds the code end.
          if (e == string::npos)
            break;

          uint16_t c (status_code (string (l, p, e - p)));
          if (c == 0)
            break;

          string r;
          p = l.find_first_not_of (' ', e + 1);   // Finds the reason start.
          if (p != string::npos)
          {
            e = l.find_last_not_of (' ');         // Finds the reason end.
            assert (e != string::npos && e >= p);

            r = string (l, p, e - p + 1);
          }

          return http_status {c, move (r)};
        }

        bad_response ("invalid HTTP response status line '" + l + "'");

        assert (false); // Can't be here.
        return http_status {};
      };

      // The curl output for a successfull submission looks like this:
      //
      // HTTP/1.1 100 Continue
      //
      // HTTP/1.1 200 OK
      // Content-Length: 83
      // Content-Type: text/manifest;charset=utf-8
      //
      // : 1
      // status: 200
      // message: submission queued
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

      // Read through the response headers until the empty line is encountered
      // and obtain the content type and/or the redirect location, if present.
      //
      optional<string> ctype;

      // Check if the line contains the specified header and return its value
      // if that's the case. Return nullopt otherwise.
      //
      // Note that we don't expect the header values that we are interested in
      // to span over multiple lines.
      //
      string l;
      auto header = [&l] (const char* name) -> optional<string>
      {
        size_t n (string::traits_type::length (name));
        if (!(casecmp (name, l, n) == 0 && l[n] == ':'))
          return nullopt;

        string r;
        size_t p (l.find_first_not_of (' ', n + 1)); // Finds value begin.
        if (p != string::npos)
        {
          size_t e (l.find_last_not_of (' '));       // Finds value end.
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

          // Get the reference if the submission succeeded.
          //
          if (c == 200)
          {
            nv = p.next ();
            if (n != "reference" || v.empty ())
              bad_value ("no reference specified");

            reference = move (v);
          }

          // Skip the remaining name/value pairs.
          //
          for (nv = p.next (); !nv.empty (); nv = p.next ()) ;

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
    // Handle all parsing errors, including the manifest_parsing exception that
    // inherits from the runtime_error exception.
    //
    // Note that the io_error class inherits from the runtime_error class, so
    // this catch-clause must go last.
    //
    catch (const runtime_error& e)
    {
      finish (o.curl (), pr); // Throws on process failure.

      // Finally we can safely issue the diagnostics (see above for details).
      //
      diag_record dr (fail);
      dr << e <<
        info << "consider reporting this to " << o.repository ()
         << " repository maintainers";

      if (reference)
        dr << info << "reference: " << *reference;
      else
        dr << info << "checksum: " << checksum;
    }

    finish (o.curl (), pr, io);

    assert (!message.empty ());

    // Print the submission failure reason and fail.
    //
    if (!reference)
    {
      diag_record dr (fail);
      dr << message;

      if (location)
        dr << info << "new repository location: " << *location;

      // In case of a server error advise the user to re-try later, assuming
      // that the issue is temporary (service overload, network connectivity
      // loss, etc.).
      //
      if (status && *status >= 500 && *status < 600)
        dr << info << "try again later";
    }

    return make_pair (move (*reference), message);
  }

  static int
  cmd_publish (const cmd_publish_options& o,
               const dir_path& prj,
               const dir_path& cfg,
               package_locations&& pkg_locs)
  {
    using bpkg::package_manifest;

    const url& repo (o.repository ());

    // Control repository URL.
    //
    optional<url> ctrl;
    if (o.control_specified ())
    {
      if (o.control () != "none")
        ctrl = parse_url (o.control (), "--control option");
    }
    else
      ctrl = control_url (prj);

    // Publisher's name/email.
    //
    project_author author;

    if (o.author_name_specified  ()) author.name  = o.author_name  ();
    if (o.author_email_specified ()) author.email = o.author_email ();

    if (!author.name || !author.email)
    {
      project_author a (find_project_author (prj));

      if (!author.name)  author.name  = move (a.name);
      if (!author.email) author.email = move (a.email);
    }

    if (!author.name || author.name->empty ())
      fail << "unable to obtain publisher's name" <<
        info << "use --author-name to specify explicitly";

    if (!author.email || author.email->empty ())
      fail << "unable to obtain publisher's email" <<
        info << "use --author-email to specify explicitly";

    // Collect package information (version, project, section, archive
    // path/checksum, and manifest).
    //
    // @@ It would have been nice to publish them in the dependency order.
    //    Perhaps we need something like bpkg-pkg-order (also would be needed
    //    in init --clone).
    //
    struct package
    {
      package_name     name;
      standard_version version;
      package_name     project;
      string           section; // alpha|beta|stable (or --section)

      path             archive;
      string           checksum;

      package_manifest manifest;
    };
    vector<package> pkgs;

    for (package_location& pl: pkg_locs)
    {
      package_name n (move (pl.name));
      package_name p (pl.project ? move (*pl.project) : n);

      standard_version v (package_version (o, cfg, n));

      // Should we allow publishing snapshots and, if so, to which section?
      // For example, is it correct to consider a "between betas" snapshot a
      // beta version?
      //
      if (v.snapshot ())
        fail << "package " << n << " version " << v << " is a snapshot";

      // Per semver we treat zero major versions as alpha.
      //
      string s (o.section_specified () ? o.section ()   :
                v.alpha () || v.major () == 0 ? "alpha" :
                v.beta ()                     ? "beta"  : "stable");

      pkgs.push_back (package {move (n),
                               move (v),
                               move (p),
                               move (s),
                               path ()   /* archive */,
                               string () /* checksum */,
                               package_manifest ()});
    }

    // Print the plan and ask for confirmation.
    //
    if (!o.yes ())
    {
      text << "publishing:" << '\n'
           << "  to:      " << repo << '\n'
           << "  as:      " << *author.name << " <" << *author.email << '>'
           << '\n';

      for (size_t i (0); i != pkgs.size (); ++i)
      {
        const package& p (pkgs[i]);

        diag_record dr (text);

        // If printing multiple packages, separate them with a blank line.
        //
        if (i != 0)
          dr << '\n';

        // Currently the control repository is the same for all packages, but
        // this could change in the future (e.g., multi-project publishing).
        //
        dr << "  package: " << p.name    << '\n'
           << "  version: " << p.version << '\n'
           << "  project: " << p.project << '\n'
           << "  section: " << p.section;

        if (ctrl)
          dr << '\n'
             << "  control: " << *ctrl;
      }

      if (!yn_prompt ("continue? [y/n]"))
        return 1;
    }

    // Prepare package archives and calculate their checksums. Also verify
    // each archive with bpkg-pkg-verify and parse the package manifest it
    // contains.
    //
    auto_rmdir dr_rm (tmp_dir ("publish"));
    const dir_path& dr (dr_rm.path);        // dist.root
    mk (dr);

    for (package& p: pkgs)
    {
      // Similar to extracting package version, we call the build system
      // directly to prepare the distribution. If/when we have bpkg-pkg-dist,
      // we may want to switch to that.
      //
      run_b (o,
             "dist:", (dir_path (cfg) /= p.name.string ()).representation (),
             "config.dist.root=" + dr.representation (),
             "config.dist.archives=tar.gz",
             "config.dist.checksums=sha256");

      // This is the canonical package archive name that we expect dist to
      // produce.
      //
      path a (dr / p.name.string () + '-' + p.version.string () + ".tar.gz");
      path c (a + ".sha256");

      if (!exists (a))
        fail << "package distribution did not produce expected archive " << a;

      if (!exists (c))
        fail << "package distribution did not produce expected checksum " << c;

      // Verify that archive name/content all match and while at it extract
      // its manifest.
      //
      process pr;
      bool io (false);
      try
      {
        fdpipe pipe (fdopen_pipe ()); // Text mode seems appropriate.

        pr = start_bpkg (2    /* verbosity */,
                         o,
                         pipe /* stdout */,
                         2    /* stderr */,
                         "pkg-verify",
                         "--manifest",
                         a);

        pipe.out.close ();
        ifdstream is (move (pipe.in), fdstream_mode::skip);

        manifest_parser mp (is, manifest_file.string ());
        p.manifest = package_manifest (mp);
        is.close ();
      }
      // This exception is unlikely to be thrown as the package manifest is
      // already validated by bpkg-pkg-verify. However, it's still possible if
      // something is skew (e.g., different bpkg/bdep versions).
      //
      catch (const manifest_parsing& e)
      {
        finish_bpkg (o, pr);

        fail << "unable to parse package manifest in archive " << a << ": "
             << e;
      }
      catch (const io_error&)
      {
        // Presumably the child process failed and issued diagnostics so let
        // finish_bpkg() try to deal with that first.
        //
        io = true;
      }

      finish_bpkg (o, pr, io);

      // Read the checksum.
      //
      try
      {
        ifdstream is (c);
        string l;
        getline (is, l);
        is.close ();

        p.checksum = string (l, 0, 64);
      }
      catch (const io_error& e)
      {
        fail << "unable to read " << c << ": " << e;
      }

      p.archive = move (a);
    }

    // Add the package archive "authorization" files to the build2-control
    // branch.
    //
    // Their names are 16-character abbreviated checksums (a bit more than the
    // standard 12 for security) and their content is the package manifest
    // header (for the record).
    //
    // See if this is a VCS repository we recognize.
    //
    if (ctrl && git_repository (prj))
    {
      // Checkout the build2-control branch into a separate working tree not
      // to interfere with the user's stuff.
      //
      auto_rmdir wd_rm (tmp_dir ("control"));
      const dir_path& wd (wd_rm.path);
      mk (wd);

      const dir_path submit_dir ("submit");
      dir_path sd (wd / submit_dir);

      // The 'git worktree add' command is quite verbose, printing something
      // like:
      //
      // Preparing /tmp/hello/.bdep/tmp/control-14926-1 (identifier control-14926-1)
      // HEAD is now at 3fd69a3 Publish hello/0.1.0-a.1
      //
      // Note that there doesn't seem to be an option (yet) for suppressing
      // this output. Also note that the first line is printed to stderr and
      // the second one to stdout. So what we are going to do is redirect both
      // stderr and stdout to /dev/null if the verbosity level is less than
      // two and advise the user to re-run with -v on failure.
      //
      auto worktree_add = [&prj, &wd] ()
      {
        bool q (verb < 2);
        auto_fd null (q ? fdnull () : auto_fd ());

        process pr (start_git (git_ver,
                               prj,
                               0                   /* stdin  */,
                               q ? null.get () : 1 /* stdout */,
                               q ? null.get () : 2 /* stderr */,
                               "worktree",
                               "add",
                               wd,
                               "build2-control"));

        if (pr.wait ())
          return;

        if (!q)
          throw failed (); // Diagnostics already issued.

        assert (pr.exit);

        const process_exit& e (*pr.exit);

        if (e.normal ())
          fail << "unable to add worktree for build2-control branch" <<
            info << "re-run with -v for more information";
        else
          fail << "git " << e;
      };

      auto worktree_prune = [&prj] ()
      {
        run_git (git_ver, prj, "worktree", "prune", verb > 2 ? "-v" : nullptr);
      };

      // Create the build2-control branch if it doesn't exist, from scratch if
      // there is no remote-tracking branch and as a checkout with --track -b
      // otherwise. If the local branch exists, then fast-forward it using the
      // locally fetched data (no network).
      //
      // Note that we don't fetch in advance, so push conflicts are possible.
      // The idea behind this is that it will be more efficient in most cases
      // as the remote-tracking branch is likely to already be up-to-date due
      // to the implicit branch fetches peformed by other operations like
      // pull. In the rare conflict cases we will advise the user to run the
      // fetch command and re-try.
      //
      bool local_exists (git_line (git_ver,
                                   prj,
                                   false /* ignore_error */,
                                   "branch",
                                   "--list",
                                   "build2-control"));

      // @@ Should we allow using the remote name other than origin (here and
      //    everywhere) via the --remote option or smth? Maybe later.
      //
      bool remote_exists (git_line (git_ver,
                                    prj,
                                    false /* ignore_error */,
                                    "branch",
                                    "--list",
                                    "--remote",
                                    "origin/build2-control"));

      bool local_new (false); // The branch is created from scratch.

      // Note that the case where the local build2-control branch exists while
      // the remote-tracking one doesn't is treated similar (but different) to
      // the brand new branch: the upstream branch will be set up by the push
      // operation but the local branch will not be deleted on the push
      // failure.
      //
      if (!local_exists)
      {
        // Create the brand new local branch if the remote-tracking branch
        // doesn't exist.
        //
        // The tricky part is to make sure that it doesn't inherit the current
        // branch history. To accomplish this we will create an empty tree
        // object, base the root (no parents) commit on it, and create the
        // build2-control branch pointing to this commit.
        //
        if (!remote_exists)
        {
          // Create the empty tree object.
          //
          auto_fd null (fdnull ());
          fdpipe pipe (fdopen_pipe ());

          process pr (start_git (git_ver,
                                 prj,
                                 null.get () /* stdin  */,
                                 pipe        /* stdout */,
                                 2           /* stderr */,
                                 "hash-object",
                                 "-wt", "tree",
                                 "--stdin"));

          optional<string> tree (git_line (move (pr), move (pipe), false));

          if (!tree)
            fail << "unable to create git tree object for build2-control";

          // Create the (empty) root commit.
          //
          optional<string> commit (git_line (git_ver,
                                             prj,
                                             false,
                                             "commit-tree",
                                             "-m", "Start",
                                             *tree));

          if (!commit)
            fail << "unable to create root git commit for build2-control";

          // Create the branch.
          //
          // Note that we pass the empty oldvalue to make sure that the ref we
          // are creating does not exist. It should be impossible but let's
          // tighten things up a bit.
          //
          run_git (git_ver,
                   prj,
                   "update-ref",
                   "refs/heads/build2-control",
                   *commit,
                   "" /* oldvalue */);

          // Checkout the branch. Note that the upstream branch is not setup
          // for it yet. This will be done by the push operation.
          //
          worktree_add ();

          // Create the checksum files subdirectory.
          //
          mk (sd);

          local_new = true;
        }
        else
        {
          // Create the local branch, setting up the corresponding upstream
          // branch.
          //
          run_git (git_ver,
                   prj,
                   "branch",
                   verb < 2 ? "-q" : nullptr,
                   "build2-control",
                   "origin/build2-control");

          worktree_add ();
        }
      }
      else
      {
        // Checkout the existing local branch. Note that we still need to
        // fast-forward it (see below).
        //
        // Prune the build2-control worktree that could potentially stay from
        // the interrupted previous publishing attempt.
        //
        worktree_prune ();

        worktree_add ();
      }

      // "Release" the checked out branch and delete the worktree, if exists.
      //
      // Note that until this is done the branch can not be checked out in any
      // other worktree.
      //
      auto worktree_remove = [&wd, &wd_rm, &worktree_prune] ()
      {
        if (exists (wd))
        {
          // Note that we cannot (yet) use git-worktree-remove since it is not
          // available in older versions.
          //
          rm_r (wd);
          wd_rm.cancel ();

          worktree_prune ();
        }
      };

      // Now, given that we successfully checked out the build2-control
      // branch, add the authorization files for packages being published,
      // commit, and push. Skip already existing files. Don't push if no files
      // were added.
      //
      {
        // Remove the control2-branch worktree on failure. Failed that we will
        // get the 'build2-control is already checked out' error on the next
        // publish attempt.
        //
        auto wg (make_exception_guard ([&worktree_remove] ()
        {
          try
          {
            worktree_remove (); // Can throw failed.
          }
          catch (const failed&)
          {
            // We can't do much here and will let the user deal with the mess.
            // Note that running 'git worktree prune' will likely be enough.
            // Anyway, that's unlikely to happen.
          }
        }));

        // If the local branch existed from the beginning then fast-forward it
        // over the remote-tracking branch.
        //
        // Note that fast-forwarding can potentially fail. That will mean the
        // local branch has diverged from the remote one for some reason
        // (e.g., inability to revert the commit, etc.). We again leave it to
        // the use to deal with.
        //
        if (local_exists && remote_exists)
          run_git (git_ver,
                   wd,
                   "merge",
                   verb < 2 ? "-q" : verb > 2 ? "-v" : nullptr,
                   "--ff-only",
                   "origin/build2-control");

        // Create the authorization files and add them to the repository.
        //
        bool added (false);

        for (const package& p: pkgs)
        {
          // Use 16 characters of the sha256sum instead of 12 for extra
          // security.
          //
          path ac (string (p.checksum, 0, 16));
          path mf (sd / ac);

          if (exists (mf))
            continue;

          try
          {
            ofdstream os (mf);
            manifest_serializer s (os, mf.string ());
            p.manifest.serialize_header (s);
            os.close ();
          }
          catch (const manifest_serialization&)
          {
            // This shouldn't happen as we just parsed the manifest.
            //
            assert (false);
          }
          catch (const io_error& e)
          {
            fail << "unable to write " << mf << ": " << e;
          }

          run_git (git_ver,
                   wd,
                   "add",
                   verb > 2 ? "-v" : nullptr,
                   submit_dir / ac);

          added = true;
        }

        // Commit and push.
        //
        // Note that we push even if we haven't committed anything in case we
        // have added but haven't managed to push it on the previous run.
        //
        if (added)
        {
          // Format the commit message.
          //
          string m;

          auto pkg_str = [] (const package& p)
          {
            return p.name.string () + '/' + p.version.string ();
          };

          if (pkgs.size () == 1)
            m = "Add " + pkg_str (pkgs[0]) + " publish authorization";
          else
          {
            m = "Add publish authorizations\n";

            for (const package& p: pkgs)
            {
              m += '\n';
              m += pkg_str (p);
            }
          }

          run_git (git_ver,
                   wd,
                   "commit",
                   verb < 2 ? "-q" : verb > 2 ? "-v" : nullptr,
                   "-m", m);
        }

        // If we fail to push the control branch, then revert the commit and
        // advice the user to fetch the repository and re-try.
        //
        auto pg (
          make_exception_guard (
            [added, &prj, &wd, &worktree_remove, local_new] ()
            {
              if (added)
              try
              {
                // If the local build2-control branch was just created, then
                // we need to drop not just the last commit but the whole
                // branch (including it's root commit). Note that this is
                // not an optimization. Imagine that the remote branch is
                // not fetched yet and we just created the local one. If we
                // leave this branch around after the failed push, then we
                // will still be in trouble after the fetch since we won't
                // be able to merge unrelated histories.
                //
                if (local_new)
                {
                  worktree_remove (); // Release the branch before removal.

                  run_git (git_ver,
                           prj,
                           "branch",
                           verb < 2 ? "-q" : nullptr,
                           "-D",
                           "build2-control");
                }
                else
                  run_git (git_ver,
                           wd,
                           "reset",
                           verb < 2 ? "-q" : nullptr,
                           "--hard",
                           "HEAD^");

                error << "unable to push build2-control branch" <<
                  info << "run 'git fetch' and try again";
              }
              catch (const failed&)
              {
                // We can't do much here and will leave the user to deal
                // with the mess. Note that running 'git fetch' will not be
                // enough as the local and remote branches are likely to
                // have diverged.
              }
            }));

        if (verb)
          text << "pushing build2-control";

        // Note that we suppress the (too detailed) push command output if
        // the verbosity level is 1. However, we still want to see the
        // progress in this case.
        //
        run_git (git_ver,
                 wd,
                 "push",

                 verb < 2 ? "-q" : verb > 3 ? "-v" : nullptr,
                 verb == 1 ? "--progress" : nullptr,

                 !remote_exists
                 ? cstrings ({"--set-upstream", "origin", "build2-control"})
                 : cstrings ());
      }

      worktree_remove ();
    }

    // Submit each package.
    //
    for (const package& p: pkgs)
    {
      // The path points into the temporary directory so let's omit the
      // directory part.
      //
      if (verb)
        text << "submitting " << p.archive.leaf ();

      pair<string, string> r (
        submit (o, p.archive, p.checksum, p.section, author, ctrl));

      if (verb)
        text << r.second << " (" << r.first << ")";
    }

    return 0;
  }

  int
  cmd_publish (const cmd_publish_options& o, cli::scanner&)
  {
    tracer trace ("publish");

    // If we are publishing the entire project, then we have two choices: we
    // can publish all the packages in the project or we can only do so for
    // packages that were initialized in the configuration that we are going
    // to use for the preparation of distributions. Normally, the two sets
    // will be the same but if they are not, it feels more likely to be a
    // mistake than the desired behavior. So we will assume it's all the
    // packages and verify they are all initialized in the configuration.
    //
    project_packages pp (
      find_project_packages (o,
                             false /* ignore_packages */,
                             true  /* load_packages   */));

    const dir_path& prj (pp.project);
    database db (open (prj, trace));

    // We need a single configuration to prepare package distribution.
    //
    shared_ptr<configuration> cfg;
    {
      transaction t (db.begin ());
      configurations cfgs (find_configurations (o, prj, t));
      t.commit ();

      if (cfgs.size () > 1)
        fail << "multiple configurations specified for publish";

      // Verify packages are present in the configuration.
      //
      verify_project_packages (pp, cfgs);

      cfg = move (cfgs[0]);
    }

    // Pre-sync the configuration to avoid triggering the build system hook
    // (see sync for details).
    //
    cmd_sync (o, prj, cfg, strings () /* pkg_args */, true /* implicit */);

    return cmd_publish (o, prj, cfg->path, move (pp.packages));
  }
}
