#!/usr/bin/env bash
#
# Build the GoldenEye 007 PC port.
#
# Usage:
#   ./build-pc.sh [ntsc-final|pal-final|jpn-final]
#
# Dependencies (see docs/building.md):
#   - CMake >= 3.16
#   - SDL2 dev
#   - zlib dev
#   - OpenGL dev (opengl32 on Windows, GL on Linux, OpenGL.framework on macOS)
#
# Example (Linux):
#   sudo apt install cmake libsdl2-dev zlib1g-dev libgl1-mesa-dev
# Example (macOS):
#   brew install cmake libsdl2-dev
# Example (Windows/MSYS2):
#   pacman -S mingw-w64-x86_64-toolchain mingw-w64-x86_64-SDL2 \
#             mingw-w64-x86_64-zlib mingw-w64-x86_64-cmake
#
set -euo pipefail

ROMID="${1:-ntsc-final}"
BUILD_DIR="${BUILD_DIR:-build-pc}"

# ---------------------------------------------------------------------------
# Windows toolchain env guard (see AGENTS.md "Build" — the recurring
# "Cannot create temporary file in C:\Windows\: Permission denied" link
# failure).
#
# The PE toolchain (ninja -> cmd -> gcc/ld) creates temp files under the
# child's TMP/TEMP. A full MSYS2 MINGW64 login shell arranges that for
# native children; other shells (agent harnesses, non-login shells) do not
# -- the msys->native env conversion silently drops/breaks TMP and the
# linker falls back to C:\Windows\. Probe what a native child actually
# sees (via a file: msys console emulation makes piped cmd.exe stdout
# unreliable), and if broken, re-run the cmake+build steps under
# PowerShell. Crucially the temp vars are set INSIDE PowerShell: the broken
# boundary is msys->native, while native->native (powershell -> cmake ->
# ninja) passes env vars through intact.
# GE_PC_BUILD_VIA_NATIVE=1 prevents a re-exec loop.
# ---------------------------------------------------------------------------
if [ -n "${MSYSTEM:-}" ] && [ -z "${GE_PC_BUILD_VIA_NATIVE:-}" ]; then
    _probe="$(mktemp -d "${TMPDIR:-/tmp}/gebuildprobe.XXXXXX")"
    _probe_win="$(cygpath -w "${_probe}" 2>/dev/null || echo 'C:\\msys64\\tmp')"
    /c/Windows/system32/cmd.exe //c "if exist \"%TMP%\" (echo ok > \"${_probe_win}/ok.txt\") else (echo bad > \"${_probe_win}/bad.txt\")" >/dev/null 2>&1 || true
    if [ ! -s "${_probe}/ok.txt" ]; then
        command -v powershell >/dev/null 2>&1 || {
            echo "ERROR: native TMP is unusable from this shell and powershell.exe was not found for the fallback." >&2
            echo "Manual fix: run cmake+ninja from cmd/PowerShell with C:\\msys64\\mingw64\\bin on PATH, or" >&2
            echo "set the user env vars (setx TMP \"C:\\msys64\\tmp\"; setx TEMP \"C:\\msys64\\tmp\") and re-open the shell." >&2
            rm -rf "${_probe}"; exit 1
        }
        echo "==> native TMP unusable from this shell; re-running cmake+build via PowerShell"
        export GE_PC_BUILD_VIA_NATIVE=1
        _here="$(pwd -W 2>/dev/null || cygpath -w "$(pwd)")"
        # Resolve the Windows-side toolchain paths from what msys found (no
        # hardcoded install dir: C:\msys64 on this box, D:\M\msys64
        # elsewhere). usr/bin first: mingw gcc.exe needs msys-2.0.dll from
        # there.
        _cmake_root="$(dirname "$(command -v cmake)")"          # .../mingw64/bin
        _msys_root="$(cd "${_cmake_root}/../.." && pwd -W 2>/dev/null || cygpath -w "$(cd "${_cmake_root}/../.." && pwd)")"  # msys install root (.. twice: bin -> mingw64 -> root)
        _bin_w="$(cygpath -w "${_cmake_root}")"
        _usrbin_w="$(cygpath -w "${_msys_root}/usr/bin")"
        # The PowerShell script is written to a .ps1 FILE, not passed via
        # -Command: the msys->native argv conversion mangles $-bearing
        # strings (a '\$LASTEXITCODE' check arrived as an empty token and a
        # failed cmake would have exited 0 silently). A file write is
        # conversion-free; only plain path/word arguments cross the boundary.
        _ps1="$(mktemp "${TMPDIR:-/tmp}/ge007-rerun.XXXXXX.ps1")"
        cat > "${_ps1}" <<'PS1'
param([string]$UsrBin, [string]$Bin, [string]$MsysRoot, [string]$Here, [string]$BuildDir, [string]$RomId)
# Self-logging: a headless shell loses this process's console output, so
# every fact that matters goes to diag/log files in the build dir.
$diag = Join-Path $BuildDir "ge007-native-reexec-diag.log"
$cmakeLog = Join-Path $BuildDir "ge007-native-reexec-cmake.log"
$buildLog = Join-Path $BuildDir "ge007-native-reexec-build.log"
# The inherited native PATH may be mangled/dropped by the broken msys->native
# env conversion, so build it explicitly: the toolchain only needs the two
# msys bin dirs plus the usual system dirs. (usr/bin first: mingw gcc/cmake
# need msys-2.0.dll from there.)
$env:PATH = "$UsrBin;$Bin;C:\Windows\System32;C:\Windows;C:\Windows\System32\Wbem"
# Writable temp for the native toolchain. [System.IO.Path]::GetTempPath()
# CANNOT be used here: it honours the inherited (broken) TMP/TEMP env vars
# and returned C:\Windows\. Pick the first writable candidate instead.
$tmpdir = $null
foreach ($cand in @((Join-Path ([Environment]::GetFolderPath('LocalApplicationData')) 'Temp'), (Join-Path $MsysRoot 'tmp'))) {
    try {
        if (-not (Test-Path $cand)) { New-Item -ItemType Directory -Force -Path $cand -ErrorAction Stop | Out-Null }
        $probe = Join-Path $cand 'ge007-tmp-write-test.tmp'
        'x' | Out-File -FilePath $probe -ErrorAction Stop
        Remove-Item $probe -ErrorAction SilentlyContinue
        $tmpdir = $cand
        break
    } catch { }
}
if ($null -eq $tmpdir) { "no writable temp dir found" | Out-File $diag -Encoding ascii; Write-Host 'no writable temp dir for the native toolchain'; exit 4 }
$env:TMP = $tmpdir
$env:TEMP = $tmpdir
"start UsrBin=[$UsrBin] Bin=[$Bin] MsysRoot=[$MsysRoot] Here=[$Here] BuildDir=[$BuildDir] RomId=[$RomId] tmp=[$tmpdir]" | Out-File $diag -Encoding ascii
$exe = "$Bin\cmake.exe"
"exe=[$exe] exists=[$(Test-Path -LiteralPath $exe)]" | Out-File $diag -Append -Encoding ascii
Set-Location $Here
"cwd=[$([System.IO.Directory]::GetCurrentDirectory())]" | Out-File $diag -Append -Encoding ascii
try {
    # -DROMID must be a DOUBLE-QUOTED string: a bare `-DROMID=$RomId` token
    # does NOT expand the variable in PowerShell (cmake got the literal
    # "$RomId" and failed with "Unknown ROMID").
    $out = & $exe -S . -B $BuildDir "-DROMID=$RomId" 2>&1
    "cmake ran LASTEXITCODE=[$LASTEXITCODE]" | Out-File $diag -Append -Encoding ascii
    $out | Out-File -FilePath $cmakeLog -Encoding ascii
} catch {
    "cmake TERMINAL ERROR: $($_.Exception.Message)" | Out-File $diag -Append -Encoding ascii
}
if ($null -eq $LASTEXITCODE) { Write-Host "cmake.exe did not execute (diag: $diag)"; exit 2 }
if ($LASTEXITCODE -ne 0) { Write-Host "cmake re-configure failed (rc=$LASTEXITCODE; log: $cmakeLog)"; exit $LASTEXITCODE }
Write-Host "cmake reconfigured (TMP=$($env:TMP))"
try {
    $out = & $exe --build $BuildDir -j 2>&1
    "build ran LASTEXITCODE=[$LASTEXITCODE]" | Out-File $diag -Append -Encoding ascii
    $out | Out-File -FilePath $buildLog -Encoding ascii
} catch {
    "build TERMINAL ERROR: $($_.Exception.Message)" | Out-File $diag -Append -Encoding ascii
}
if ($null -eq $LASTEXITCODE) { Write-Host "cmake --build did not execute (diag: $diag)"; exit 3 }
if ($LASTEXITCODE -ne 0) { Write-Host "build failed (rc=$LASTEXITCODE; log: $buildLog)"; exit $LASTEXITCODE }
"done" | Out-File $diag -Append -Encoding ascii
Write-Host "cmake + build ok (native re-exec, TMP=$($env:TMP))"
PS1
        _ps1_win="$(cygpath -w "${_ps1}")"
        rc=0
        # NOTE: do NOT launch powershell via `env -i` (tried 2026-09-28): a
        # native child started with a scrubbed msys env cannot launch
        # cmake.exe (CreateProcess fails silently; LASTEXITCODE stays null).
        powershell -NoProfile -ExecutionPolicy Bypass -File "${_ps1_win}" "${_usrbin_w}" "${_bin_w}" "${_msys_root}" "${_here}" "${BUILD_DIR}" "${ROMID}" || rc=$?
        rm -f "${_ps1}"
        if [ "${rc}" -ne 0 ]; then
            # Surface the .ps1's own diagnostics (console output is lost in a
            # headless shell) plus whatever the failing step wrote.
            for _lg in "${BUILD_DIR}/ge007-native-reexec-diag.log" "${BUILD_DIR}/ge007-native-reexec-cmake.log" "${BUILD_DIR}/ge007-native-reexec-build.log"; do
                if [ -s "${_lg}" ]; then
                    echo "---- ${_lg} (last 40 lines) ----" >&2
                    tail -40 "${_lg}" >&2
                fi
            done
        fi
        exit "${rc}"
    fi
    rm -rf "${_probe}"
fi

echo "==> Configuring PC port (ROMID=${ROMID})"
cmake -S . -B "${BUILD_DIR}" -DROMID="${ROMID}"

echo "==> Building"
cmake --build "${BUILD_DIR}" -j

echo "==> Done."
echo "    Binary: ${BUILD_DIR}/ge007.*"
echo "    Put your ROM in ./data/ (see README) and run the binary."
