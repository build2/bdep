#! /usr/bin/env bash

trap 'exit 1' ERR

odb=odb
inc=()

if test -d ../.bdep; then

  if [ -n "$1" ]; then
    cfg="$1"
  else
    # Use default configuration for headers.
    #
    cfg="$(bdep config list -d .. | \
sed -r -ne 's#^(@[^ ]+ )?([^ ]+)/ .*default.*$#\2#p')"
  fi

  # Note: there is nothing generated in libbutl-odb.
  #
  inc+=("-I../../libbutl/libbutl-odb")

  inc+=("-I$cfg/libbutl")
  inc+=("-I../../libbutl")

  inc+=("-I$cfg/libbpkg")
  inc+=("-I../../libbpkg")

  inc+=("-I$cfg/bdep")
  inc+=("-I..")

else

  inc+=("-I../../libbutl/libbutl-odb")

  inc+=(-I.. -I../../libbpkg -I../../libbutl)

fi

$odb "${inc[@]}"                                                      \
    -DLIBODB_BUILD2 -DLIBODB_SQLITE_BUILD2 --generate-schema          \
    -d sqlite --std c++14 --generate-query                            \
    --odb-epilogue '#include <bdep/wrapper-traits.hxx>'               \
    --hxx-prologue '#include <bdep/wrapper-traits.hxx>'               \
    --hxx-prologue '#include <bdep/value-traits.hxx>'                 \
    --include-with-brackets --include-prefix bdep --guard-prefix BDEP \
    --sqlite-override-null project.hxx

$odb "${inc[@]}"                                                      \
    -DLIBODB_BUILD2 -DLIBODB_SQLITE_BUILD2                            \
    -d sqlite --std c++14 --generate-query                            \
    --odb-epilogue '#include <bdep/wrapper-traits.hxx>'               \
    --hxx-prologue '#include <bdep/wrapper-traits.hxx>'               \
    --include-with-brackets --include-prefix bdep --guard-prefix BDEP \
    --sqlite-override-null database-views.hxx
