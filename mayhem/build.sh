#!/usr/bin/env bash
#
# mayhem/build.sh — build everything the Mayhem commit image needs, from the ELVM checkout:
#   (1) an instrumented in-process libFuzzer harness over the Whirl interpreter (the fuzzed code
#       carries ASan/UBSan because fuzz_whirl.cc #includes tools/whirl.cc),
#   (2) a standalone (non-libFuzzer) reproducer for the same harness, and
#   (3) the upstream whirl-backend functional test suite, built with the project's NORMAL flags,
#       plus the frozen reference (golden) outputs that mayhem/test.sh diffs against.
#
# Idempotent and air-gapped: only the 8cc submodule (the C front-end used to compile test/*.c to
# ELVM IR) is needed; it is initialised from the in-image .git, so a re-run needs no network.
set -euo pipefail

# ---- build contract (base image exports these as ENV; defaults mirror the C/C++ template) --------
[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH
: "${SANITIZER_FLAGS=-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer}"
: "${DEBUG_FLAGS:=-g -gdwarf-3}"
: "${CC:=clang}"
: "${CXX:=clang++}"
: "${LIB_FUZZING_ENGINE:=-fsanitize=fuzzer}"
: "${MAYHEM_JOBS:=$(nproc)}"
: "${STANDALONE_FUZZ_MAIN:=/opt/mayhem/StandaloneFuzzTargetMain.c}"
: "${COVERAGE_FLAGS=}"
export SANITIZER_FLAGS DEBUG_FLAGS CC CXX LIB_FUZZING_ENGINE MAYHEM_JOBS COVERAGE_FLAGS

SRC="${SRC:-/mayhem}"
cd "$SRC"

# ---- (1) instrumented in-process libFuzzer harness -----------------------------------------------
# -gdwarf-3: Mayhem's triage can't read DWARF >= 4 and clang's plain -g emits DWARF-5.
$CXX $SANITIZER_FLAGS $DEBUG_FLAGS $COVERAGE_FLAGS -std=c++11 \
    mayhem/fuzz_whirl.cc $LIB_FUZZING_ENGINE -o /mayhem/fuzz_whirl

# ---- (2) standalone run-once reproducer (same harness, no libFuzzer runtime) ---------------------
# Compile LLVM's driver as C first, else clang++ mangles its LLVMFuzzerTestOneInput reference.
$CC $SANITIZER_FLAGS $DEBUG_FLAGS -c "$STANDALONE_FUZZ_MAIN" -o /tmp/StandaloneFuzzTargetMain.o
$CXX $SANITIZER_FLAGS $DEBUG_FLAGS $COVERAGE_FLAGS -std=c++11 \
    mayhem/fuzz_whirl.cc /tmp/StandaloneFuzzTargetMain.o -o /mayhem/fuzz_whirl-standalone

# ---- (3) upstream whirl-backend test suite (project NORMAL flags — a separate, clean build) ------
# Initialise ONLY the 8cc submodule and stamp the marker so make does not fetch the other, unused
# submodules (Whitespace/tinycc/lci belong to backends we don't test here).
git submodule update --init 8cc
mkdir -p out
touch out/git_submodule.stamp

make -j"${MAYHEM_JOBS}" out/whirl out/8cc out/elc out/eli
make -j"${MAYHEM_JOBS}" build-whirl                     # elc -whirl: all out/*.eir.whirl programs

# Freeze the reference (expected) outputs via ELVM's own reference interpreter (eli). test.sh runs
# ONLY whirl against these frozen goldens, so a sabotaged/neutered whirl is detected by the diff.
mapfile -t ACT < <(make -n test-whirl 2>/dev/null \
    | grep -oE 'out/[A-Za-z0-9_./]+\.eir\.whirl\.out\b' | sort -u)
EXP=(); for a in "${ACT[@]}"; do EXP+=("${a%.whirl.out}.out"); done
make -j"${MAYHEM_JOBS}" "${EXP[@]}"

printf '%s\n' "${ACT[@]}" > out/whirl-tests.list

echo "build.sh: harness + standalone built; ${#ACT[@]} frozen whirl goldens ready"
