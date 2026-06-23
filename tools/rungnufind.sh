#!/bin/bash

set -e

PROG="$PWD/$1"

# gnufind churns through a large number of tiny files, so prefer an
# in-memory filesystem (tmpfs) for the scratch directory when one is available:
# it is a bit faster and avoids wearing the SSD. Order of preference:
#   GF_TMPDIR (explicit override) > TMPDIR > tmpfs (/dev/shm) > /tmp
if [[ -n "${GF_TMPDIR}" ]]; then
  GF_PARENT="${GF_TMPDIR}"
elif [[ -n "${TMPDIR}" ]]; then
  GF_PARENT="${TMPDIR}"
elif [[ -d /dev/shm && -w /dev/shm ]]; then
  GF_PARENT=/dev/shm
else
  GF_PARENT=/tmp
fi
DIR=$(mktemp -d "${GF_PARENT}/gnufind_elvm.XXXXXXXXXX")
cd "$DIR"
trap 'rm -rf "$DIR"' EXIT

ERR=/dev/null
if [[ ! -z "${GF_DEBUG}" ]]; then
  ERR=/dev/stderr
  echo "GNU find: computing under $DIR" 1>&2
fi

ulimit -s $(( 6 * 1024 * 4 ))

# e.g. ABC -> 65\066\067\0
perl -0777 -pe 's/(.)/ord($1)."\0"/gse' | env GF_UNSAFE=1 $PROG 2> $ERR
