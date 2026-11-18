#!/bin/sh
# Builds fwdgen-pc.exe from the shared sources with zig (pip install ziglang).
set -eu
cd "$(dirname "$0")/.."
python3 -m ziglang cc -target x86_64-windows-gnu -O2 -DSTBI_WINDOWS_UTF8 -municode \
	-o pc/fwdgen-pc.exe pc/fwdgen_pc.c source/cia_patch.c source/sha256.c \
	source/bannergen.c source/icongen.c source/idalloc.c
echo built pc/fwdgen-pc.exe
