#!/bin/sh
# Run Lua's official suite once on glibc (C0), then once on Scudo with
# GWP-ASan at its default settings (C2). The binary is not rebuilt.
set -eu
cd "$(dirname "$0")"

if [ ! -x build/lua ]; then
  echo "missing build/lua; run build.sh first" >&2
  exit 1
fi
if [ ! -f tests/all.lua ]; then
  echo "missing tests/all.lua; run fetch.sh first" >&2
  exit 1
fi

run() {
  name=$1
  shift
  echo "======== $name"
  set +e
  ( cd tests && "$@" ../build/lua -e '_U=true' all.lua )
  echo "exit: $?"
  set -e
}

# C0: the plain binary. malloc stays glibc.
run C0

# C2: same command, Scudo preloaded. GWP-ASan stays at its default rate.
run C2 env "LD_PRELOAD=${SCUDO_SO:?set SCUDO_SO}"
