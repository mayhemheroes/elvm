# syntax=docker/dockerfile:1.7
#
# Commit-image Dockerfile (SMALL form) — builds ON TOP of the org C/C++ base image, which provides
# debian + clang/llvm + libFuzzer + the mayhem identities + the build-contract ENV. This only adds
# the per-commit ELVM build (the Whirl libFuzzer harness + the whirl-backend test suite).

FROM ghcr.io/mayhemheroes/base:20260621

ARG COMMIT
ARG REPO_URL
# SOURCE_DATE_EPOCH stays an ARG (available to build.sh); do NOT bake it as ENV — an empty value
# breaks clang. build.sh unsets it when empty.
ARG SOURCE_DATE_EPOCH
# Undefaulted so the base ENV (ASan+UBSan, halting) flows through unless overridden via --build-arg.
ARG SANITIZER_FLAGS
ARG COVERAGE_FLAGS=
ARG MAYHEM_JOBS=

# ruby is required to expand the test/*.eir.rb fixtures into ELVM IR during the test-suite build.
USER root
RUN apt-get update \
 && DEBIAN_FRONTEND=noninteractive apt-get install -yq --no-install-recommends ruby \
 && rm -rf /var/lib/apt/lists/*
USER mayhem

# The FULL repo (incl. .git history, needed to init the 8cc submodule offline) enters from the CI
# build context and is copied to /mayhem; build.sh builds in place.
WORKDIR /mayhem
COPY --chown=2000:2000 . /mayhem

RUN bash mayhem/build.sh

# Run the upstream functional suite at build time; a failing suite fails the image build.
RUN if [ -f mayhem/test.sh ]; then bash mayhem/test.sh; else echo "no mayhem/test.sh — repo ships no functional tests"; fi

LABEL org.opencontainers.image.source="${REPO_URL}" \
      org.opencontainers.image.revision="${COMMIT}" \
      rlenv.tier="commit" \
      rlenv.source_date_epoch="${SOURCE_DATE_EPOCH}" \
      rlenv.user="mayhem"

USER mayhem
WORKDIR /mayhem
