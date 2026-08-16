#!/usr/bin/env bash
# Build and run the hardware-free host tests with MSVC cl.exe.
# Works in Git Bash. Adjust SDK version if your kit differs.
set -euo pipefail

export MSYS2_ARG_CONV_EXCL='*'

MSVC_ROOT="D:/VSBuildTools/VC/Tools/MSVC/14.44.35207"
SDK_INC="D:/CodexWork/WindowsKits/10/Include/10.0.26100.0"
SDK_LIB="D:/CodexWork/WindowsKits/10/Lib/10.0.26100.0"
CL="$MSVC_ROOT/bin/Hostx64/x64/cl.exe"

# Convert the MSYS path (e.g. /d/NestedHv) to a Windows path (D:/NestedHv)
# that cl.exe understands.
ROOT_MSYS="$(cd "$(dirname "$0")/../.." && pwd)"
ROOT="$(cygpath -m "$ROOT_MSYS")"

OUT="$ROOT/out/host"
mkdir -p "$OUT"

export INCLUDE="$MSVC_ROOT/include;$SDK_INC/ucrt;$SDK_INC/shared;$SDK_INC/um"
export LIB="$MSVC_ROOT/lib/x64;$SDK_LIB/ucrt/x64;$SDK_LIB/um/x64"

"$CL" \
    /nologo /W4 /WX /wd4127 /O2 /std:c11 \
    /I"$ROOT/include" \
    "$ROOT/src/nhv_vmcs12.c" \
    "$ROOT/tests/host/test_vmcs12.c" \
    /Fe:"$OUT/test_vmcs12.exe" \
    /Fo:"$OUT/" \
    /link /SUBSYSTEM:CONSOLE

"$OUT/test_vmcs12.exe"
