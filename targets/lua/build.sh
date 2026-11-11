#!/bin/sh
# Compile Lua's C interpreter into one plain binary. No sanitizer flags.
# Scudo is attached later with LD_PRELOAD, not at build time.
set -eu
cd "$(dirname "$0")"

if [ ! -f src/Makefile ]; then
  echo "missing src/Makefile; run fetch.sh first" >&2
  exit 1
fi

# The image has clang-18 and no gcc. Command-line CC is passed into Lua's makefile.
make -C src linux CC="clang-18 -std=gnu99"

mkdir -p build
cp src/src/lua build/lua
chmod +x build/lua

echo "---- ldd build/lua ----"
ldd build/lua
