# file      : buildfile
# license   : MIT; see accompanying LICENSE file

./: {*/ -build/}                                     \
    doc{NEWS README.md} legal{LICENSE AUTHORS LEGAL} \
    manifest

# Don't install tests.
#
tests/: install = false
