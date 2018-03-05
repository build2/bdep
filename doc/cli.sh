#! /usr/bin/env bash

version=0.7.0-a.0.z
date="$(date +"%B %Y")"

trap 'exit 1' ERR
set -o errtrace # Trap in functions.

function info () { echo "$*" 1>&2; }
function error () { info "$*"; exit 1; }

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

  cli -I .. -v project="bdep" -v version="$version" -v date="$date" \
--include-base-last "${o[@]}" --generate-html --html-prologue-file \
man-prologue.xhtml --html-epilogue-file man-epilogue.xhtml --html-suffix .xhtml \
--link-regex '%bdep(#.+)?%build2-project-manager-manual.xhtml$1%' \
../bdep/$n.cli

  cli -I .. -v project="bdep" -v version="$version" -v date="$date" \
--include-base-last "${o[@]}" --generate-man --man-prologue-file \
man-prologue.1 --man-epilogue-file man-epilogue.1 --man-suffix .1 \
--link-regex '%bdep(#.+)?%$1%' \
../bdep/$n.cli
}

o="--output-prefix bdep- --class-doc bdep::common_options=short"

# A few special cases.
#
compile "common" $o --output-suffix "-options" --class-doc bdep::common_options=long
compile "bdep" $o --output-prefix "" --suppress-undocumented --class-doc bdep::commands=short --class-doc bdep::topics=short

pages="config help init"

for p in $pages; do
  compile $p $o
done

# Manual.
#
exit 0

cli -I .. \
-v version="$(echo "$version" | sed -e 's/^\([^.]*\.[^.]*\).*/\1/')" \
-v date="$date" \
--generate-html --html-suffix .xhtml \
--html-prologue-file doc-prologue.xhtml \
--html-epilogue-file doc-epilogue.xhtml \
--link-regex '%b([-.].+)%../../build2/doc/b$1%' \
--link-regex '%build2(#.+)?%../../build2/doc/build2-build-system-manual.xhtml$1%' \
--output-prefix build2-project-manager- manual.cli

html2ps -f doc.html2ps:a4.html2ps -o build2-project-manager-manual-a4.ps build2-project-manager-manual.xhtml
ps2pdf14 -sPAPERSIZE=a4 -dOptimize=true -dEmbedAllFonts=true build2-project-manager-manual-a4.ps build2-project-manager-manual-a4.pdf

html2ps -f doc.html2ps:letter.html2ps -o build2-project-manager-manual-letter.ps build2-project-manager-manual.xhtml
ps2pdf14 -sPAPERSIZE=letter -dOptimize=true -dEmbedAllFonts=true build2-project-manager-manual-letter.ps build2-project-manager-manual-letter.pdf
