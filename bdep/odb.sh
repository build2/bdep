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
  if true; then
    inc+=("-I../../libbutl/libbutl-odb")
  else
    inc+=("-I$HOME/work/odb/builds/gcc-sqlite/libodb")

    inc+=("-I$HOME/work/odb/odb/libodb-sqlite")
    inc+=("-I$HOME/work/odb/odb/libodb")
  fi

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
    -DLIBODB_BUILD2 -DLIBODB_SQLITE_BUILD2                            \
    --std c++14 -d sqlite --sqlite-version 3.53.3                     \
    --generate-query --generate-schema                                \
    --odb-epilogue '#include <bdep/wrapper-traits.hxx>'               \
    --hxx-prologue '#include <bdep/wrapper-traits.hxx>'               \
    --hxx-prologue '#include <bdep/value-traits.hxx>'                 \
    --include-with-brackets --include-prefix bdep --guard-prefix BDEP \
    project.hxx

$odb "${inc[@]}"                                                      \
    -DLIBODB_BUILD2 -DLIBODB_SQLITE_BUILD2                            \
    --std c++14 -d sqlite --sqlite-version 3.53.3                     \
    --generate-query                                                  \
    --odb-epilogue '#include <bdep/wrapper-traits.hxx>'               \
    --hxx-prologue '#include <bdep/wrapper-traits.hxx>'               \
    --include-with-brackets --include-prefix bdep --guard-prefix BDEP \
    database-views.hxx
