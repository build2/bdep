# file      : buildfile
# license   : MIT; see accompanying LICENSE file

./: {*/ -build/}                                               \
    doc{INSTALL LICENSE COPYRIGHT NEWS README CONTRIBUTING.md} \
    manifest

# Don't install tests or the INSTALL file.
#
tests/:          install = false
doc{INSTALL}@./: install = false
