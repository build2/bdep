#! /usr/bin/env bash

trap 'exit 1' ERR

odb=odb
lib="\
-I$HOME/work/odb/builds/default/libodb-sqlite-default \
-I$HOME/work/odb/libodb-sqlite \
-I$HOME/work/odb/builds/default/libodb-default \
-I$HOME/work/odb/libodb"

$odb $lib -I.. -I../../libbpkg -I../../libbutl                        \
    -DLIBODB_BUILD2 -DLIBODB_SQLITE_BUILD2 --generate-schema          \
    -d sqlite --std c++11 --generate-query                            \
    --odb-epilogue '#include <bdep/wrapper-traits.hxx>'               \
    --hxx-prologue '#include <bdep/wrapper-traits.hxx>'               \
    --include-with-brackets --include-prefix bdep --guard-prefix BDEP \
    --sqlite-override-null project.hxx
