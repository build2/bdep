#! /usr/bin/env bash

version=0.17.0

trap 'exit 1' ERR
set -o errtrace # Trap in functions.

function info () { echo "$*" 1>&2; }
function error () { info "$*"; exit 1; }

date="$(date +"%B %Y")"
copyright="$(sed -n -re 's%^Copyright \(c\) (.+) \(see the AUTHORS and LEGAL files\)\.$%\1%p' ../LICENSE)"

while [ $# -gt 0 ]; do
  case $1 in
    --clean)
      rm -f bdep*.xhtml bdep*.1
      rm -f build2-project-manager-manual*.ps \
         build2-project-manager-manual*.pdf   \
         build2-project-manager-manual.xhtml
      exit 0
      ;;
    *)
      error "unexpected $1"
      ;;
  esac
done

function compile ()
{
  local n=$1; shift

  # Use a bash array to handle empty arguments.
  #
  local o=()
  while [ $# -gt 0 ]; do
    o=("${o[@]}" "$1")
    shift
  done

  cli -I .. \
-v project="bdep" \
-v version="$version" \
-v date="$date" \
-v copyright="$copyright" \
--include-base-last "${o[@]}" \
--generate-html --html-suffix .xhtml \
--html-prologue-file man-prologue.xhtml \
--html-epilogue-file man-epilogue.xhtml \
--link-regex '%b([-.].+)%../../build2/doc/b$1%' \
--link-regex '%testscript(#.+)?%../../build2/doc/build2-testscript-manual.xhtml$1%' \
--link-regex '%bpkg([-.].+)%../../bpkg/doc/bpkg$1%' \
--link-regex '%bpkg(#.+)?%../../bpkg/doc/build2-package-manager-manual.xhtml$1%' \
--link-regex '%brep(#.+)?%../../brep/doc/build2-repository-interface-manual.xhtml$1%' \
--link-regex '%bbot(#.+)?%../../bbot/doc/build2-build-bot-manual.xhtml$1%' \
--link-regex '%bdep(#.+)?%build2-project-manager-manual.xhtml$1%' \
--link-regex '%intro(#.+)?%../../build2-toolchain/doc/build2-toolchain-intro.xhtml$1%' \
../bdep/$n.cli

  cli -I .. \
-v project="bdep" \
-v version="$version" \
-v date="$date" \
-v copyright="$copyright" \
--include-base-last "${o[@]}" \
--generate-man --man-suffix .1 --ascii-tree \
--man-prologue-file man-prologue.1 \
--man-epilogue-file man-epilogue.1 \
--link-regex '%bpkg(#.+)?%$1%' \
--link-regex '%brep(#.+)?%$1%' \
--link-regex '%bdep(#.+)?%$1%' \
../bdep/$n.cli
}

o="--suppress-undocumented --output-prefix bdep- --class-doc bdep::common_options=short"

# A few special cases.
#
compile "common" $o --output-suffix "-options" --class-doc bdep::common_options=long
compile "bdep" $o --output-prefix "" --class-doc bdep::commands=short --class-doc bdep::topics=short

# NOTE: remember to update a similar list in buildfile and bdep.cli as well as
# the help topics sections in bdep/buildfile and help.cxx.
#
pages="new help init sync fetch status ci release publish deinit config test \
update clean projects-configs argument-grouping default-options-files"

for p in $pages; do
  compile $p $o
done

# Manual.
#
exit 0

# @@ Note that we now have --ascii-tree CLI option.
#
function xhtml_to_ps () # <from> <to> [<html2ps-options>]
{
  local from="$1"
  shift
  local to="$1"
  shift

  sed -e 's/├/|/g' -e 's/│/|/g' -e 's/─/-/g' -e 's/└/\xb7/g' "$from" | \
  html2ps "${@}" -o "$to"
}

cli -I .. \
-v version="$(echo "$version" | sed -e 's/^\([^.]*\.[^.]*\).*/\1/')" \
-v date="$date" \
-v copyright="$copyright" \
--generate-html --html-suffix .xhtml \
--html-prologue-file doc-prologue.xhtml \
--html-epilogue-file doc-epilogue.xhtml \
--link-regex '%b([-.].+)%../../build2/doc/b$1%' \
--link-regex '%build2(#.+)?%../../build2/doc/build2-build-system-manual.xhtml$1%' \
--output-prefix build2-project-manager- \
manual.cli

xhtml_to_ps build2-project-manager-manual.xhtml build2-project-manager-manual-a4.ps -f doc.html2ps:a4.html2ps
ps2pdf14 -sPAPERSIZE=a4 -dOptimize=true -dEmbedAllFonts=true build2-project-manager-manual-a4.ps build2-project-manager-manual-a4.pdf

xhtml_to_ps build2-project-manager-manual.xhtml build2-project-manager-manual-letter.ps -f doc.html2ps:letter.html2ps
ps2pdf14 -sPAPERSIZE=letter -dOptimize=true -dEmbedAllFonts=true build2-project-manager-manual-letter.ps build2-project-manager-manual-letter.pdf
