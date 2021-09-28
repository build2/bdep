// file      : bdep/http-service.hxx -*- C++ -*-
// license   : MIT; see accompanying LICENSE file

#ifndef BDEP_HTTP_SERVICE_HXX
#define BDEP_HTTP_SERVICE_HXX

#include <libbutl/manifest-types.hxx>

#include <bdep/types.hxx>
#include <bdep/utility.hxx>

#include <bdep/common-options.hxx>

namespace bdep
{
  namespace http_service
  {
    // If type is file, then the value is a path to be uploaded. If type is
    // file_text, then the value is a file content to be uploaded.
    //
    struct parameter
    {
      enum {text, file, file_text} type;
      string name;
      string value;
    };
    using parameters = vector<parameter>;

    struct result
    {
      string           message;
      optional<string> reference;

      // Does not include status, message, or reference.
      //
      vector<butl::manifest_name_value> body;
    };

    // Submit text parameters and/or upload files to an HTTP service via the
    // POST method. Use the multipart/form-data content type if any files are
    // uploaded and application/x-www-form-urlencoded otherwise.
    //
    // Note: currently only one file_text parameter can be specified.
    //
    // On success, return the response manifest message and reference (if
    // present, see below) and the rest of the manifest values, if any. Issue
    // diagnostics and fail if anything goes wrong or the response manifest
    // status value is not 200 (success).
    //
    // Note that the HTTP service is expected to respond with the result
    // manifest that starts with the 'status' (HTTP status code) and 'message'
    // (diagnostics message) values optionally followed by 'reference' and
    // then other manifest values. If the status is not 200 and reference is
    // present, then it is included in the diagnostics.
    //
    result
    post (const common_options&, const url&, const parameters&);
  }
}

#endif // BDEP_HTTP_SERVICE_HXX
