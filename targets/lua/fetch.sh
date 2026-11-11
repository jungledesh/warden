#!/bin/sh
# Download the official Lua 5.5.1 source and its separate test suite.
# Record both SHA-256 checksums.
set -eu
cd "$(dirname "$0")"

fetch() {
  url=$1
  tarball=$2
  expected=$3
  # lua.org/tests rejects some clients unless they send a user agent.
  curl -fsSL -A "Mozilla/5.0" -o "$tarball" "$url"
  actual=$(sha256sum "$tarball" | awk '{print $1}')
  echo "$actual  $tarball"
  printf '%s  %s\n' "$actual" "$tarball" >> SHA256SUMS
  if [ "$actual" != "$expected" ]; then
    echo "checksum mismatch for $tarball" >&2
    echo "expected $expected" >&2
    echo "actual   $actual" >&2
    exit 1
  fi
}

: > SHA256SUMS
fetch https://www.lua.org/ftp/lua-5.5.1.tar.gz \
  lua-5.5.1.tar.gz \
  1c4b4068d67061f2a2231ad2b5422e77acea1487ea9890f6320af614f4373dce
fetch https://www.lua.org/tests/lua-5.5.1-tests.tar.gz \
  lua-5.5.1-tests.tar.gz \
  da07b543872dc0bb2ff12aabd0c248578d78df3eb6b67efdc537a46d455c7f31

rm -rf src tests
mkdir -p src tests
tar -xzf lua-5.5.1.tar.gz -C src --strip-components=1
tar -xzf lua-5.5.1-tests.tar.gz -C tests --strip-components=1
echo "source extracted to $(pwd)/src"
echo "tests extracted to $(pwd)/tests"
