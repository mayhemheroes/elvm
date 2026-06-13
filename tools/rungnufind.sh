#!/bin/bash

set -e

PROG="$PWD/$1"

DIR=$(mktemp -d gnufind_elvm.XXXXXXXXXX --tmpdir)
cd "$DIR"
trap "rm -rf $DIR" EXIT

ERR=/dev/null
if [[ ! -z "${GF_DEBUG}" ]]; then
  ERR=/dev/stderr
  echo "GNU find: computing under $DIR" 1>&2
fi

ulimit -s $(( 6 * 1024 * 4 ))

# e.g. ABC -> 65\066\067\0
perl -0777 -pe 's/(.)/ord($1)."\0"/gse' | env GF_UNSAFE=1 $PROG 2> $ERR
