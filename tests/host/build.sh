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

# Common flags: strict warnings, no CRT exceptions expected in pure logic.
FLAGS=(/nologo /W4 /WX /wd4127 /O2 /std:c11 /I"$ROOT/include")

# Each test links the library sources it exercises.
build_and_run() {
    local exe="$1"; shift
    "$CL" "${FLAGS[@]}" \
        "$ROOT/src/nhv_vmcs12.c" \
        "$ROOT/src/nhv_vmcs02.c" \
        "$@" \
        "/Fe:$OUT/$exe.exe" \
        /Fo:"$OUT/" \
        /link /SUBSYSTEM:CONSOLE
    "$OUT/$exe.exe"
}

build_and_run test_vmcs12 "$ROOT/tests/host/test_vmcs12.c"
build_and_run test_vmcs02 "$ROOT/tests/host/test_vmcs02.c"
