// file      : bdep/ci-types.cxx -*- C++ -*-
// license   : MIT; see accompanying LICENSE file

#include <bdep/ci-types.hxx>

namespace bdep
{
  const char*
  to_string (cmd_ci_override_origin o)
  {
    using orig = cmd_ci_override_origin;

    switch (o)
    {
    case orig::override:       return "--override option value";
    case orig::overrides_file: return "file referenced by --overrides-file option";
    case orig::build_email:    return "--build-email option value";
    case orig::builds:         return "--builds option value";
    case orig::target_config:  return "--target-config option value";
    case orig::build_config:   return "--build-config option value";
    case orig::package_config: return "--package-config option value";
    case orig::interactive:    return "--interactive|-i option value";
    }

    assert (false); // Can't be here.
    return "";
  }
}
