#!/usr/bin/env bash
# Populates third_party/ from upstream releases. Run manually; the build never calls this.
set -euo pipefail
cd "$(dirname "$0")/.."
LZ4_VER=1.10.0; ZSTD_VER=1.5.7; PUGI_VER=1.14; DOCTEST_VER=2.4.11
TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT
rm -rf third_party/lz4 third_party/zstd third_party/pugixml third_party/doctest
mkdir -p third_party/{lz4,zstd,pugixml,doctest}

curl -sSL "https://github.com/lz4/lz4/archive/refs/tags/v${LZ4_VER}.tar.gz" | tar xz -C "$TMP"
cp "$TMP/lz4-${LZ4_VER}/lib/"{lz4.c,lz4.h} third_party/lz4/
cp "$TMP/lz4-${LZ4_VER}/LICENSE" third_party/lz4/

curl -sSL "https://github.com/facebook/zstd/releases/download/v${ZSTD_VER}/zstd-${ZSTD_VER}.tar.gz" | tar xz -C "$TMP"
cp -r "$TMP/zstd-${ZSTD_VER}/lib/"{common,compress,decompress} third_party/zstd/
cp "$TMP/zstd-${ZSTD_VER}/lib/"{zstd.h,zstd_errors.h} third_party/zstd/
cp "$TMP/zstd-${ZSTD_VER}/LICENSE" third_party/zstd/
# Assembly is disabled via ZSTD_DISABLE_ASM; drop the .S so no toolchain needs to assemble it.
rm -f third_party/zstd/decompress/*.S

curl -sSL "https://github.com/zeux/pugixml/releases/download/v${PUGI_VER}/pugixml-${PUGI_VER}.tar.gz" | tar xz -C "$TMP"
cp "$TMP/pugixml-${PUGI_VER}/src/"{pugixml.cpp,pugixml.hpp,pugiconfig.hpp} third_party/pugixml/
cp "$TMP/pugixml-${PUGI_VER}/LICENSE.md" third_party/pugixml/

curl -sSL -o third_party/doctest/doctest.h \
  "https://raw.githubusercontent.com/doctest/doctest/v${DOCTEST_VER}/doctest/doctest.h"
curl -sSL -o third_party/doctest/LICENSE.txt \
  "https://raw.githubusercontent.com/doctest/doctest/v${DOCTEST_VER}/LICENSE.txt"
echo "vendored: lz4 ${LZ4_VER}, zstd ${ZSTD_VER}, pugixml ${PUGI_VER}, doctest ${DOCTEST_VER}"
