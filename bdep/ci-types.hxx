// file      : bdep/ci-types.hxx -*- C++ -*-
// license   : MIT; see accompanying LICENSE file

#ifndef BDEP_CI_TYPES_HXX
#define BDEP_CI_TYPES_HXX

#include <bdep/types.hxx>

#include <libbutl/manifest-types.hxx>

namespace bdep
{
  // This type is intended for accumulating package manifest override values
  // from all the override-related options (see ci-parsers.hxx for details) to
  // then validate it as a whole.
  //
  class cmd_ci_override: public vector<butl::manifest_name_value>
  {
  };

  // While handling a potential validation error we need to be able to
  // attribute this error back to the option which resulted with an invalid
  // override. For that we will encode the value's origin option into the
  // name/value line.
  //
  enum class cmd_ci_override_origin: uint64_t
  {
    override = 1,   // --override
    overrides_file, // --overrides-file
    build_email,    // --build-email
    builds,         // --builds
    target_config,  // --target-config
    build_config,   // --build-config
    package_config, // --package-config
    interactive     // --interactive
  };

  // Return the override origin description as, for example,
  // `--override option value`.
  //
  const char*
  to_string (cmd_ci_override_origin);
}

#endif // BDEP_CI_TYPES_HXX
