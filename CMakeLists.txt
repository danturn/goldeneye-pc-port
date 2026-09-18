# ============================================================================
# GoldenEye 007 — PC port build
#
# This is a PARALLEL build system to the N64 `Makefile`. It compiles the same
# game C sources (src/game, src) for a modern host (Windows/Linux/macOS)
# against the PC shims in `port/`. The N64 build is untouched.
#
# See docs/internals.md for the architecture and plan.
# Modelled on the Perfect Dark PC port (perfect-dark-pc-port/perfect_dark).
#
# Usage:
#   cmake -S . -B build-pc -DROMID=ntsc-final
#   cmake --build build-pc -j
# ============================================================================

cmake_minimum_required(VERSION 3.16)
list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake)

# The decomp uses the `#define inherits struct` pattern (47x in
# src/bondtypes.h): a nested `struct Base;` inside a derived struct. GCC
# treats this as embedding the full base struct (correct layout, matches the
# N64/IDO behaviour the decomp relies on); Clang treats it as a bare forward
# declaration unless Microsoft extensions are on. So the flag differs by
# compiler, not by platform: GCC gets -fplan9-extensions (the superset that
# also enables the derived->base pointer conversion the idiom relies on),
# Clang gets -fms-extensions (same anonymous-struct layout; the pointer
# conversion becomes a warning, already demoted below). See the flag block
# near the bottom. On macOS `gcc` is Apple Clang, so CMAKE_C_COMPILER_ID is
# what selects the flags, not the executable name.
# (GCC does hard-error on the one struct that declares a member with the same
# name as an inherited one — CCTVRecord's `pad` — see docs/internals.md.)
# An explicit -DCMAKE_C_COMPILER=... always wins.
if(NOT DEFINED CMAKE_C_COMPILER)
  find_program(_GE007_GCC NAMES gcc)
  if(_GE007_GCC)
    set(CMAKE_C_COMPILER "${_GE007_GCC}" CACHE FILEPATH "C compiler" FORCE)
    get_filename_component(_GE007_GCC_DIR "${_GE007_GCC}" DIRECTORY)
    find_program(_GE007_GXX NAMES g++ PATHS "${_GE007_GCC_DIR}" NO_DEFAULT_PATH)
    if(_GE007_GXX)
      set(CMAKE_CXX_COMPILER "${_GE007_GXX}" CACHE FILEPATH "CXX compiler" FORCE)
    endif()
  endif()
endif()

if(APPLE)
  # arm64 (Apple Silicon) requires macOS 11.0+; x86_64 keeps the older floor.
  # CMAKE_OSX_ARCHITECTURES is empty for a native (non-universal) build, and
  # neither CMAKE_SYSTEM_PROCESSOR nor CMAKE_HOST_SYSTEM_PROCESSOR is set until
  # project() below — so ask uname, else a native arm64 build silently gets
  # -mmacosx-version-min=10.13. Explicit CMAKE_OSX_ARCHITECTURES still wins.
  execute_process(COMMAND uname -m
    OUTPUT_VARIABLE _GE007_HOST_MACHINE OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
  if(_GE007_HOST_MACHINE MATCHES "arm64|aarch64" OR CMAKE_OSX_ARCHITECTURES MATCHES "arm64")
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "" FORCE)
  else()
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "" FORCE)
  endif()
endif()

# ASM is needed for the generated absolute ROM asset symbols
# (port/src/romassets_<region>.s, see scripts/gen_romassets.py).
project(ge007 C CXX ASM)

include(TargetArch)
include(FindPkgConfig)

set(CMAKE_C_STANDARD 11)
# GNU extensions ON: strict -std=c11/-std=c++20 define __STRICT_ANSI__, which
# disables glibc's _DEFAULT_SOURCE and hides POSIX symbols the port needs
# (clock_gettime/CLOCK_REALTIME in port/src/libultra.c, mmap flags, etc.).
# -std=gnu11/gnu++20 is GCC's own default and what MinGW was already using.
set(CMAKE_C_EXTENSIONS ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_EXTENSIONS ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Opt-in ccache launcher. The game sources are frozen (1:1 with the decomp),
# so ccache makes branch-switch / worktree rebuilds nearly free. No-op when
# ccache is not installed.
find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
  set(CMAKE_C_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
  set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
  message(STATUS "ccache enabled: ${CCACHE_PROGRAM}")
endif()

# Default build type
if(NOT CMAKE_BUILD_TYPE AND NOT EXISTS ${CMAKE_BINARY_DIR}/CMakeCache.txt)
  set(CMAKE_BUILD_TYPE "RelWithDebInfo" CACHE STRING "" FORCE)
endif()

# ----------------------------------------------------------------------------
# Options
# ----------------------------------------------------------------------------
# Which ROM the executable expects in the data/ dir.
set(ROMID "ntsc-final" CACHE STRING "ROM id: ntsc-final (US), pal-final (EU), jpn-final (JP)")

# ----------------------------------------------------------------------------
# Target arch / platform
# ----------------------------------------------------------------------------
target_architecture(TARGET_ARCH)
if(TARGET_ARCH MATCHES "64")
  set(TARGET_IS_64BIT TRUE)
else()
  set(TARGET_IS_64BIT FALSE)
endif()
message(STATUS "Target arch: ${TARGET_ARCH} (64bit=${TARGET_IS_64BIT})")

if(WIN32)
  set(TARGET_OS "windows")
elseif(APPLE)
  set(TARGET_OS "osx")
elseif(UNIX)
  set(TARGET_OS "linux")
else()
  message(FATAL_ERROR "Unknown target OS")
endif()
set(TARGET_PLATFORM "${TARGET_ARCH}-${TARGET_OS}")
message(STATUS "Target platform: ${TARGET_PLATFORM}")

# Region -> compile definitions.
#
# The game code #ifdefs a full per-region macro set (see the N64 Makefile,
# the VERSION blocks). e.g. VERSION_EU is used in 60+ files, LEFTOVERDEBUG in
# 26, BUGFIX_R1 in 21. We MUST emit the same set the N64 build uses for the
# matching region, or the PC build takes different conditional branches and
# silently diverges (and fails to link). These mirror Makefile LCDEFS exactly.
if(ROMID STREQUAL "ntsc-final")
  set(VERSION_IS_PAL 0)
  set(BIN_REGION "")
  set(ASSET_REGION "u")
  set(REGION_DEFS
    -DVERSION_US -DLANG_US -DREFRESH_NTSC
    -DLEFTOVERDEBUG -DLEFTOVERSPECTRUM -DBUGFIX_R0 -DBYTEMATCH)
elseif(ROMID STREQUAL "pal-final")
  set(VERSION_IS_PAL 1)
  set(BIN_REGION ".pal")
  set(ASSET_REGION "e")
  set(REGION_DEFS
    -DVERSION_EU -DLANG_EU -DREFRESH_PAL
    -DBUGFIX_R1 -DBUGFIX_R2 -DBYTEMATCH)
elseif(ROMID STREQUAL "jpn-final")
  set(VERSION_IS_PAL 0)
  set(BIN_REGION ".jpn")
  set(ASSET_REGION "j")
  set(REGION_DEFS
    -DVERSION_JP -DLANG_JP -DREFRESH_NTSC
    -DBUGFIX_R1 -DLEFTOVERDEBUG -DLEFTOVERSPECTRUM -DBYTEMATCH)
else()
  message(FATAL_ERROR "Unknown ROMID: ${ROMID}")
endif()

# git hash for version info
execute_process(COMMAND git rev-parse --short HEAD
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  OUTPUT_VARIABLE VERSION_HASH OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_QUIET)
if(NOT VERSION_HASH)
  set(VERSION_HASH "unknown")
endif()

# Provenance, printed by --version and at startup (port/src/main.c): a build
# keeps pointing at this repository no matter who compiles or renames it.
set(VERSION_CODENAME "larkspur")
set(ORIGIN_URL "https://github.com/jkdansereau/goldeneye-pc-port")

configure_file(
  "${CMAKE_SOURCE_DIR}/port/include/versioninfo.h.in"
  "${CMAKE_BINARY_DIR}/port/include/versioninfo.h" @ONLY)

# Host <string.h> path for port/shim/string.h. The N64 include/string.h
# declares strlen/strchr with `const unsigned char *`, which conflicts with
# the host declarations that SDL (and other system headers) pull in, and it
# sits earlier on the include path than the toolchain's own string.h — so the
# shim cannot use #include_next (it would find the N64 header first). Derive
# the toolchain include dir from the compiler location (MinGW/MSYS2 layout:
# bin/../include) and generate an absolute-path include.
get_filename_component(_PORT_GCC_DIR "${CMAKE_C_COMPILER}" DIRECTORY)
# Where the HOST's real libc headers live. MinGW/Linux: bin/../include next to
# the compiler. macOS: /usr/include does not exist (since Catalina the headers
# are inside the SDK), so ask xcrun — otherwise the generated host*.h shims
# point at a path that is not there and every C++ TU fails on <cmath>/<cstddef>.
if(APPLE)
  execute_process(COMMAND xcrun --show-sdk-path
    OUTPUT_VARIABLE _PORT_APPLE_SDK OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
  if(_PORT_APPLE_SDK AND EXISTS "${_PORT_APPLE_SDK}/usr/include/string.h")
    set(_PORT_HOST_INCLUDE_DIR "${_PORT_APPLE_SDK}/usr/include")
  else()
    set(_PORT_HOST_INCLUDE_DIR "${_PORT_GCC_DIR}/../include")
  endif()
else()
  set(_PORT_HOST_INCLUDE_DIR "${_PORT_GCC_DIR}/../include")
endif()

# libc++ (macOS/clang) wraps the C headers in <SDK>/usr/include/c++/v1, and its
# <cmath>/<cstring>/<cstdlib>/<cstddef> include the C header and THEN require
# the wrapper's _LIBCPP_*_H guard (defined only by that wrapper). libstdc++
# (Linux/MinGW) has no such wrapper, so this is conditional. The decomp's
# include/ sits ahead of the system path on -I, so a plain #include <math.h>
# finds the N64 stub; the generated hostmath.h / hoststring.h / ... route C++
# TUs to these absolute paths instead. See docs/dev/findings.md D295.
set(PORT_LIBCPP_INCLUDE_DIR "${_PORT_HOST_INCLUDE_DIR}/c++/v1")
if(EXISTS "${PORT_LIBCPP_INCLUDE_DIR}/math.h" AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  set(PORT_HAVE_LIBCPP 1)
  set(PORT_LIBCPP_MATH_H   "${PORT_LIBCPP_INCLUDE_DIR}/math.h")
  set(PORT_LIBCPP_STRING_H "${PORT_LIBCPP_INCLUDE_DIR}/string.h")
  set(PORT_LIBCPP_STDLIB_H "${PORT_LIBCPP_INCLUDE_DIR}/stdlib.h")
  set(PORT_LIBCPP_STDDEF_H "${PORT_LIBCPP_INCLUDE_DIR}/stddef.h")
else()
  set(PORT_HAVE_LIBCPP 0)
  set(PORT_LIBCPP_MATH_H "")
  set(PORT_LIBCPP_STRING_H "")
  set(PORT_LIBCPP_STDLIB_H "")
  set(PORT_LIBCPP_STDDEF_H "")
endif()

get_filename_component(PORT_HOST_STRING_H "${_PORT_HOST_INCLUDE_DIR}/string.h" ABSOLUTE)
configure_file(
  "${CMAKE_SOURCE_DIR}/port/include/hoststring.h.in"
  "${CMAKE_BINARY_DIR}/port/include/hoststring.h" @ONLY)

# Host <sched.h> path for port/shim/sched.h. The decomp's src/sched.h is on
# the include path and shadows the system header; libstdc++'s <thread>/gthr
# headers in the fast3d C++ TUs need the real <sched.h> (cpu_set_t,
# sched_yield). Both MinGW and Linux keep <sched.h> at bin/../include.
get_filename_component(PORT_HOST_SCHED_H "${_PORT_HOST_INCLUDE_DIR}/sched.h" ABSOLUTE)
configure_file(
  "${CMAKE_SOURCE_DIR}/port/include/hostsched.h.in"
  "${CMAKE_BINARY_DIR}/port/include/hostsched.h" @ONLY)

# Host <stdlib.h> path for port/shim/stdlib.h. The N64 include/stdlib.h
# typedefs lldiv_t/ldiv_t, which conflict with the host's when both land in
# one C++ TU (fast3d): MinGW's own sec_api/stdlib_s.h does `#include
# <stdlib.h>`, which resolves to the N64 header via -I. The shim routes C++
# TUs to the host header by absolute path (same trick as hoststring.h).
get_filename_component(PORT_HOST_STDLIB_H "${_PORT_HOST_INCLUDE_DIR}/stdlib.h" ABSOLUTE)
configure_file(
  "${CMAKE_SOURCE_DIR}/port/include/hoststdlib.h.in"
  "${CMAKE_BINARY_DIR}/port/include/hoststdlib.h" @ONLY)

# Host <stddef.h> path for port/shim/stddef.h (same reason as stdlib: the N64
# stub shadows the host header in C++ TUs, hiding max_align_t from cstddef).
# stddef.h is compiler-internal, not in bin/../include: on a native Linux GCC
# it lives under lib/gcc/<triple>/<ver>/include. Ask the compiler where it is;
# fall back to the MinGW bin/../include layout.
execute_process(
  COMMAND "${CMAKE_C_COMPILER}" -print-file-name=include/stddef.h
  OUTPUT_VARIABLE _PORT_GCC_STDDEF OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
if(_PORT_GCC_STDDEF AND EXISTS "${_PORT_GCC_STDDEF}")
  get_filename_component(PORT_HOST_STDDDEF_H "${_PORT_GCC_STDDEF}" ABSOLUTE)
else()
  get_filename_component(PORT_HOST_STDDDEF_H "${_PORT_GCC_DIR}/../include/stddef.h" ABSOLUTE)
endif()
configure_file(
  "${CMAKE_SOURCE_DIR}/port/include/hoststddef.h.in"
  "${CMAKE_BINARY_DIR}/port/include/hoststddef.h" @ONLY)

# Host <math.h> for port/shim/math.h: the decomp's include/math.h is a
# constants-only stub that shadows the system header on -I, which starves the
# fast3d C++ TUs of acosf/fmodf/fabsf and, via libc++, of its wrapper guard.
# Same absolute-path trick as hoststring.h. (limits.h/assert.h do not need a
# shim: libc++'s <climits> defines its own guard and <cassert> only rejects a
# libc++-provided <assert.h>, which does not exist.)
get_filename_component(PORT_HOST_MATH_H "${_PORT_HOST_INCLUDE_DIR}/math.h" ABSOLUTE)
configure_file(
  "${CMAKE_SOURCE_DIR}/port/include/hostmath.h.in"
  "${CMAKE_BINARY_DIR}/port/include/hostmath.h" @ONLY)

# ----------------------------------------------------------------------------
# Compiler definitions & flags
# ----------------------------------------------------------------------------
add_definitions(
  -DPORT=1
  -D_LANGUAGE_C=1
  -DAVOID_UB=1
  -DPAL=${VERSION_IS_PAL}
  ${REGION_DEFS}
)
if(TARGET_IS_64BIT)
  add_definitions(-DPLATFORM_64BIT=1)
endif()

if(APPLE)
  # Apple clang's SDK headers enable _FORTIFY_SOURCE by default, which rewrites
  # sprintf() to __builtin___sprintf_chk and hard-conflicts with the N64
  # prototype in include/PR/os.h and the definition in src/sprintf.c, in every
  # TU that sees host <stdio.h> before <PR/os.h> (crash.c, input.c, libultra.c,
  # main.c, mixer.c, n64stubs.c). The decomp needs its own sprintf; disable
  # fortification, matching the -fno-stack-protector intent above.
  add_definitions(-D_FORTIFY_SOURCE=0)
endif()

# The decomp is big-endian N64 code with unaligned accesses; these flags are
# required to compile it cleanly on a little-endian host (same as the PD port).
add_compile_options(
  -fno-strict-aliasing
  -funsigned-char
  -fwrapv
  -include "versioninfo.h"
)
# The decompiled game code has a class of latent fixed-size stack-buffer
# over/under-runs (e.g. stan.c sub_GAME_7F0B1DDC's tileStack, bondview2.c
# bondviewCalcIntroSwirlCamera's pointbuf[10] indexed [-3..11]) that write a
# few words into adjacent stack scratch. On the N64, and on the MinGW/Windows
# build (no default stack protector), this is harmless in practice — verified
# stable over thousands of frames. Only Ubuntu's gcc default
# (-fstack-protector-strong) turns these into a fatal __stack_chk_fail at
# stage load / first frames. Match the N64 and MinGW builds so the same code
# runs the same way; the genuinely understood cases are still fixed properly
# under #ifdef PORT (D189). A hardened Linux build would need the whole class
# swept first — tracked in docs/dev/findings.md.
add_compile_options(-fno-stack-protector)
# Keep frame pointers: dbghelp's StackWalk64 (crash handler + thread dumps)
# cannot read DWARF unwind info on MinGW, so the EBP chain is the only way to
# get a real backtrace of a hung/crashed game thread.
add_compile_options(-fno-omit-frame-pointer)

# Optimisation: the software RSP + software rasteriser in port/fast3d are the
# hot path and are very sensitive to -O level (the difference between ~1 fps
# and playable). Non-Debug builds keep the CMAKE_BUILD_TYPE optimisation
# (RelWithDebInfo => -O2); only an explicit Debug build drops to -Og.
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  add_compile_options(-Og)
endif()

add_compile_options(
  -Wall
  -Wno-address
  -Wno-misleading-indentation
  -Wno-missing-braces
  -Wno-multichar
  -Wno-tautological-compare
  -Wno-unused-but-set-variable
  -Wno-unused-value
  -Wno-unused-variable
)
# The decomp was written for IDO's leniency. These downgrade hard errors to
# warnings so the unmodified game code compiles; each class is a tracked
# decomp finding (see docs/internals.md):
#   int-conversion: raw 32-bit ROM address constants assigned to pointers
#     (WIP decomp; genuine 64-bit hazards to fix upstream as phases progress)
#   incompatible-pointer-types: Mtxf union vs float[4][4], Gfx array idioms
#   implicit-function-declaration: missing header declarations (e.g.
#     get_ptr_long_watch_text_for_item, defined in gunfire.c)
add_compile_options(
  -Wno-error=int-conversion
  -Wno-error=incompatible-pointer-types
  -Wno-error=implicit-function-declaration
)
# Clang 16+ additionally promoted these to hard errors in C99+ modes; GCC's
# equivalents are covered by the three above. Gated by compiler because the
# -Wno-error= names are not portable to GCC (it warns "unrecognized option").
if(CMAKE_C_COMPILER_ID MATCHES "Clang")
  add_compile_options(
    -Wno-error=implicit-int
    -Wno-error=incompatible-function-pointer-types
  )
endif()
add_compile_options($<$<COMPILE_LANGUAGE:C>:-Wno-pointer-sign>)
add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-Wno-unused-function>)

# The decomp's `#define inherits struct` idiom (src/bondtypes.h) makes the base
# struct an unnamed tagged member and accesses its fields directly
# (obj->type where type lives in the embedded PropRecord). GCC targeting
# *-w64-mingw32 enables this via -fms-extensions implicitly; a native Linux/
# macOS GCC does not, so `'ObjectRecord' has no member named 'type'`. The flag
# is compiler-specific, and on macOS `gcc` is Apple Clang, so select on
# CMAKE_C_COMPILER_ID rather than the platform or executable name.
# C only — the decomp is C; fast3d C++ does not use `inherits`.
if(NOT MINGW)
  if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
    # Plan 9 extensions: superset of -fms-extensions, also enables the
    # automatic derived->base pointer conversion the idiom relies on.
    add_compile_options($<$<COMPILE_LANGUAGE:C>:-fplan9-extensions>)
    # GCC 14 promoted several long-standing C leniencies to hard errors
    # (int<->pointer for a known prototype, incompatible-pointer arg types,
    # implicit int/function). The WIP decomp still trips these (e.g.
    # append_text_picked_up's vestigial u8* params in propobj.c). -fpermissive
    # downgrades them to warnings — the C counterpart of the -Wno-error lines
    # above, and the documented path for building older C against GCC 14+.
    add_compile_options($<$<COMPILE_LANGUAGE:C>:-fpermissive>)
  elseif(CMAKE_C_COMPILER_ID MATCHES "Clang")
    # Clang has no -fplan9-extensions. -fms-extensions gives the same
    # anonymous-struct member access for the `inherits` idiom (correct layout);
    # the derived->base pointer conversion becomes -Wincompatible-pointer-types,
    # already demoted to non-error above. -Wno-microsoft-anon-tag silences the
    # 47x "anonymous structs are a Microsoft extension" diagnostic.
    add_compile_options($<$<COMPILE_LANGUAGE:C>:-fms-extensions>)
    add_compile_options($<$<COMPILE_LANGUAGE:C>:-Wno-microsoft-anon-tag>)
  endif()
endif()

# ASLR must be OFF: dram_syms.s defines absolute symbols (cfb_16 @ 0x70000000,
# _bssSegmentEnd @ 0x70050000) that the game references through pointer-typed
# externs (src/bondgame.h: `extern u32 *_bssSegmentEnd`). On x86-64/PE, taking
# their address emits a .refptr slot holding the absolute value PLUS a base
# relocation; with ASLR the loader rebases it to runtime_base+0x70050000 =
# garbage. With DYNAMICBASE disabled the image loads at its preferred base
# (0x140000000) and the relocations are no-ops. main.c sanity-checks the load
# base at startup.
if(MINGW)
  set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,--disable-dynamicbase")
elseif(UNIX AND NOT APPLE)
  # Linux equivalent of the ASLR-off requirement above: dram_syms.s defines
  # absolute symbols at 0x70000000 referenced through pointer-typed externs. A
  # PIE build (GCC's default on modern distros) emits GOT/relative relocations
  # for them — R_X86_64_32S link errors, or a runtime rebase to garbage. Build
  # and link no-PIE so the image keeps its preferred load address.
  add_compile_options(-fno-pie)
  set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -no-pie")
  # Move the image base to 0x20000000. The default no-PIE base 0x400000 puts
  # .data/.bss symbols around 0x00500000-0x00600000, which COLLIDES with the
  # N64 address ranges fast3d's seg_addr() decodes: a game-built display list
  # that embeds `&symbol` (via the osVirtualToPhysical identity shim) hands
  # seg_addr a value < 0x800000, which it treats as a K0-physical DRAM offset
  # and remaps to 0x80xxxxxx (zeroed DRAM mirror) -> "unknown GBI opcode 0x00"
  # -> black screen. 0x20000000 sits in the gap between the segmented-address
  # space (< 0x10000000), the cart map (0x10000000), the D131 module range
  # ([0x40000000,0x70000000)) and DRAM (>= 0x70000000); seg_addr passes such
  # addresses through untouched. Still < 2GiB so -mcmodel=small / R_X86_64_32
  # relocations are fine (unlike a 0x140000000 base, which needs mcmodel=large).
  # This is the ELF analogue of the Windows build's fixed 0x140000000 base.
  set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,-Ttext-segment=0x20000000")
endif()

# port/shim first: PC-only shadow headers (bondtypes.h, bondconstants.h,
# PR/gbi.h) that fix N64-compiler assumptions without touching src/ or
# include/. Inert in the N64 build (its Makefile never sees port/shim).
include_directories(
  "${CMAKE_SOURCE_DIR}/port/shim"
  "${CMAKE_SOURCE_DIR}"
  "${CMAKE_SOURCE_DIR}/include"
  "${CMAKE_SOURCE_DIR}/include/PR"
  "${CMAKE_SOURCE_DIR}/src"
  "${CMAKE_SOURCE_DIR}/src/game"
  "${CMAKE_SOURCE_DIR}/src/libultra"
  "${CMAKE_SOURCE_DIR}/src/libultra/audio"
  "${CMAKE_SOURCE_DIR}/port/include"
  "${CMAKE_BINARY_DIR}/port/include"
)

# ----------------------------------------------------------------------------
# Dependencies: SDL2, zlib, OpenGL
# ----------------------------------------------------------------------------
set(LIBS "")

find_package(SDL2 REQUIRED)
include_directories(SYSTEM "${SDL2_INCLUDE_DIR}")
list(APPEND LIBS "${SDL2_LIBRARY}")

find_package(ZLIB REQUIRED)
if(ZLIB_INCLUDE_DIR)
  include_directories(SYSTEM "${ZLIB_INCLUDE_DIR}")
endif()
list(APPEND LIBS "${ZLIB_LIBRARY}")

# Real <zlib.h> for port/src/rzdecomp.c (which backs decompressdata() with real
# zlib). The game's own src/game/zlib.h sits on the -I path and shadows a plain
# `#include <zlib.h>` in a port TU, so generate an absolute-path include (same
# trick as hoststring.h). Prefer find_package(ZLIB)'s dir; fall back to the
# toolchain include dir (MinGW layout: bin/../include), where
# mingw-w64-x86_64-zlib installs zlib.h.
set(PORT_REAL_ZLIB_H "${ZLIB_INCLUDE_DIR}/zlib.h")
if(NOT EXISTS "${PORT_REAL_ZLIB_H}")
  set(PORT_REAL_ZLIB_H "${_PORT_GCC_DIR}/../include/zlib.h")
endif()
if(NOT EXISTS "${PORT_REAL_ZLIB_H}")
  message(FATAL_ERROR "Cannot locate the host zlib.h needed by port/src/rzdecomp.c")
endif()
configure_file(
  "${CMAKE_SOURCE_DIR}/port/include/realzlib.h.in"
  "${CMAKE_BINARY_DIR}/port/include/realzlib.h" @ONLY)

if(NOT GL_LIBRARY)
  if(WIN32)
    set(GL_LIBRARY opengl32)
  elseif(APPLE)
    set(GL_LIBRARY "-framework OpenGL")
  else()
    set(GL_LIBRARY GL)
  endif()
endif()
list(APPEND LIBS "${GL_LIBRARY}")

if(NOT EXTRA_LIBRARIES)
  if(MINGW)
    # MinGW: pthreads for the real-OS-thread kernel (port/src/libultra.c).
    set(EXTRA_LIBRARIES dbghelp -static-libstdc++ pthread)
  elseif(WIN32)
    set(EXTRA_LIBRARIES dbghelp -static-libstdc++)
  else()
    set(EXTRA_LIBRARIES stdc++ m dl pthread)
  endif()
endif()
list(APPEND LIBS "${EXTRA_LIBRARIES}")

# ----------------------------------------------------------------------------
# Sources
# ----------------------------------------------------------------------------
# Port layer (shims + software RSP).
file(GLOB_RECURSE SRC_PORT      "${CMAKE_SOURCE_DIR}/port/src/*.c")
file(GLOB_RECURSE SRC_PORT_CXX  "${CMAKE_SOURCE_DIR}/port/src/*.cpp")
file(GLOB_RECURSE SRC_FAST3D    "${CMAKE_SOURCE_DIR}/port/fast3d/*.cpp"
                               "${CMAKE_SOURCE_DIR}/port/fast3d/glad/*.c")

# Generated absolute ROM asset symbols for this region (see
# scripts/gen_romassets.py). Defines every obseg/ramrom/music label and the
# ge007.ld segment markers as 0x10000000 + rom_offset, so `&symbol` / romCopy()
# work once romdata.c maps the .z64 at 0x10000000.
#
# Mach-O needs a different encoding (`.data` section, `_`-prefixed symbols), so
# macOS uses the committed `.darwin.s` variant generated with
# `--darwin --base <PORT_ADDR_BASE>`. The base is currently 0 (the shifted-
# window address model is item M1 in docs/dev/MACOS-ARM64-PLAN.md); when it
# becomes non-zero, regenerate both `.darwin.s` files with the new `--base`.
if(APPLE)
  set(SRC_ROMASSETS "${CMAKE_SOURCE_DIR}/port/src/romassets_${ASSET_REGION}.darwin.s")
  set(SRC_DRAM_SYMS "${CMAKE_SOURCE_DIR}/port/src/dram_syms.darwin.s")
  set(_GEN_ASSET_HINT "python3 scripts/gen_romassets.py ${ASSET_REGION} --darwin --base 0")
else()
  set(SRC_ROMASSETS "${CMAKE_SOURCE_DIR}/port/src/romassets_${ASSET_REGION}.s")
  set(SRC_DRAM_SYMS "${CMAKE_SOURCE_DIR}/port/src/dram_syms.s")
  set(_GEN_ASSET_HINT "python3 scripts/gen_romassets.py ${ASSET_REGION}")
endif()
if(NOT EXISTS ${SRC_ROMASSETS})
  message(FATAL_ERROR "Missing ${SRC_ROMASSETS} — run: ${_GEN_ASSET_HINT}")
endif()

# Game code — compiled unmodified.
file(GLOB_RECURSE SRC_GAME "${CMAKE_SOURCE_DIR}/src/game/*.c")

# EXCLUDED from src/game (replaced by port/src/rzdecomp.c, which backs
# decompressdata()/rzipGetSomething() with real zlib):
#   decompress.c + zlib.c — GE's custom gzip-1.2.4 inflate builds its Huffman
#   tables contiguously into a caller-supplied fixed buffer of `struct huft`.
#   That struct is 8 bytes on MIPS (union holds a 32-bit pointer) but 16 bytes
#   on x86-64 (union holds a 64-bit pointer), so on a 64-bit host the tables
#   overflow load_resource's `u8 buffer[0x2100]` and clobber the caller's return
#   address -> SIGSEGV in langInit()'s first file load (finding D32). Real zlib
#   manages its own table memory, removing the fixed-buffer assumption. Mirrors
#   the PD port, which swaps its assembly rzip for a real-zlib-backed rzip_c.c.
#   The only externally-referenced symbols from these two files are
#   decompressdata() and rzipGetSomething(); both are provided by
#   port/src/rzdecomp.c (see the symbol sweep in docs/internals.md §F).
list(REMOVE_ITEM SRC_GAME
  "${CMAKE_SOURCE_DIR}/src/game/decompress.c"
  "${CMAKE_SOURCE_DIR}/src/game/zlib.c")

# libaudio (AL) — runs on the CPU, platform independent.
file(GLOB SRC_LIBAUDIO "${CMAKE_SOURCE_DIR}/src/libultra/audio/*.c")
# Rare's audio "New" driver (drvrNew/env/reverb) — synthesizer.c calls
# alSaveNew/alAuxBusNew/alMainBusNew/alLoadNew/alResampleNew/alEnvmixerNew/
# alEnvmixerParam/alFxParam, all defined here. Runs on the CPU; must be linked.
file(GLOB SRC_LIBULTRARE_AUDIO "${CMAKE_SOURCE_DIR}/src/libultrare/audio/*.c")
# gu matrix helpers.
file(GLOB SRC_GU "${CMAKE_SOURCE_DIR}/src/libultra/gu/*.c")

# VI mode table (DATA, not a driver): fr.c references osViModeTable ~15x.
# This is the only libultra/io file we compile — the rest are hardware
# drivers that get shimmed in port/src/libultra.c.
set(SRC_LIBULTRARE_DATA
  src/libultrare/io/vitbl.c
)
list(PREPEND SRC_LIBULTRARE_DATA "${CMAKE_SOURCE_DIR}")

# IDO printf engine: src/sprintf.c's sprintf() calls _Printf(prout, dst, fmt,
# va_list), the internal formatter from libultrare/libc (pure C, no hardware).
# xlitob/xldtob are its integer-conversion helpers (_Litob/_Ldtob). The
# _Printf prototype clash between xstdio.h and xprintf.c is resolved by the
# port/shim/libc/xstdio.h shadow header.
set(SRC_LIBULTRARE_LIBC
  src/libultrare/libc/xprintf.c
  src/libultra/libc/xlitob.c
  src/libultra/libc/xldtob.c
)
list(PREPEND SRC_LIBULTRARE_LIBC "${CMAKE_SOURCE_DIR}")

# ----------------------------------------------------------------------------
# Asset data — decompiled ROM arrays (pure data, no logic).
#
# Single-file assets compile unmodified. The per-model / per-level .c files
# under assets/obseg/{bg,brief,setup,stan} are NOT compiled: each one defines
# file-local globals with generic names (`header`, `room_data_table`,
# `intro`, ...) that the N64 build keeps apart by linking every file into its
# own ELF (see assets/obseg/Makefile.*). In a single host link they collide.
# The game loads those blobs from ROM at runtime anyway; their top-level
# symbols (C*Z, G*Z, P*Z, Tbg_*Z, U*Z, L*E/L*J, bg_*_all_p_seg, ...) are now
# absolute cart addresses in the generated romassets_<region>.s (see
# scripts/gen_romassets.py), backed by the ROM image that romdata.c maps at
# 0x10000000. The per-model data itself (chr/*/Model.c etc.) is gitignored
# ROM-derived and absent from this checkout. See docs/internals.md §11.
# D34 (docs/internals.md): animationtable_{data,entries}.c are NOT
# compiled on PC. Their arrays exist only so that &ANIM_DATA_x evaluates to
# the record's offset within the animation_data segment (N64 links it at VMA
# 0); on x86-64 assets/animationtable_data.h redefines them as lvalues at
# g_pc_animdata_base + offset instead. The data itself comes from ROM via
# romCopy, exactly like every other asset.
set(SRC_ASSETS
  assets/font_chardatae.c
  assets/font_chardataj.c
  assets/font_dl.c
  assets/oddtextures.c
  assets/rarewarelogo.c
)
list(PREPEND SRC_ASSETS "${CMAKE_SOURCE_DIR}")

# Engine files in src/ that are platform independent.
# NOTE: this list is a starting point and must be curated during Phase 1.
#
# INCLUDED here (platform-independent or CPU-side):
#   init.c    — provides mainproc() (the game's real main entry) + the thread
#               setup helpers. Its N64-only init() is compiled but NOT called;
#               the N64 symbols init() references are stubbed in
#               port/src/n64stubs.c (see A3 in docs/internals.md).
#   cfb.c     — defines cfb_16 (the framebuffer), referenced by fr.c and game
#               code (bondview2.c, front.c). Pure data.
#   stacks.c  — thread-stack arrays (sp_boot/sp_rmon/sp_idle/sp_shed/sp_main/
#               sp_audi + sp_debug under LEFTOVERDEBUG). Pure data; referenced
#               by init.c (thread setup) and audi.c (sp_audi). The SP_*_SZ
#               sizes in bondconstants.h match these exactly, so sizeof() is
#               correct. No duplicate-definition risk (all other refs are
#               extern decls in bondgame.h / audi.c).
#
# EXCLUDED (hardware drivers, replaced by port/ shims):
#   crash.c rmon.c usb.c tlb_*.c pi.c vi.c motor.c
#   (there is no src/spectrum.c in this decomp)
#   All of src/libultra/io/*.c and src/libultrare/io/*.c EXCEPT vitbl.c.
#   motor.c / pfsinit.c / pfsisplug.c are NOT compiled: joy.c calls
#   osMotorInit/Start/Stop and osPfsInit/osPfsIsPlug only for accessory
#   (Rumble Pak / Memory Pak) detection, so they are shimmed as no-ops in
#   port/src/libultra.c (see B2 — saves are EEPROM, not PFS).
#
# sched.c IS compiled (Phase 2): the game's real scheduler runs on the
# cooperative thread kernel; its hardware leaf calls (osSpTaskLoad/StartGo,
# osDpSetNextBuffer, osVi*) are shimmed in port/src/libultra.c, which runs
# the software RSP inline and posts the RSP_DONE/RDP_DONE messages.
set(SRC_ENGINE
  src/init.c
  src/sched.c
  src/audi.c
  src/snd.c
  src/music.c
  src/joy.c
  src/fr.c
  # src/cfb.c — cfb_16 now lives at 0x80000000 in the reserved DRAM region
  # (port/src/dram_syms.s) so OS_K0_TO_PHYSICAL(cfb_16[0]) is a small offset
  # that fast3d can map back; a host-BSS array would truncate to garbage.
  src/stacks.c
  src/boss.c
  src/mema.c
  src/memp.c
  src/deb.c
  src/debugmenu.c
  src/speed_graph.c
  src/token.c
  src/str.c
  src/sprintf.c
  src/ramrom.c
)
list(PREPEND SRC_ENGINE "${CMAKE_SOURCE_DIR}")

set(SRC
  ${SRC_GAME}
  ${SRC_PORT}
  ${SRC_ROMASSETS}
  ${SRC_DRAM_SYMS}
  ${SRC_PORT_CXX}
  ${SRC_FAST3D}
  ${SRC_LIBAUDIO}
  ${SRC_LIBULTRARE_AUDIO}
  ${SRC_LIBULTRARE_DATA}
  ${SRC_LIBULTRARE_LIBC}
  ${SRC_ASSETS}
  ${SRC_GU}
  ${SRC_ENGINE}
)

# ----------------------------------------------------------------------------
# Target
# ----------------------------------------------------------------------------
set(BIN_NAME "ge007${BIN_REGION}.${TARGET_ARCH}")
add_executable(ge007 ${SRC})
target_link_libraries(ge007 ${LIBS})
set_target_properties(ge007 PROPERTIES OUTPUT_NAME "${BIN_NAME}")
if(WIN32)
  set_target_properties(ge007 PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

message(STATUS "Output binary: ${BIN_NAME}")
