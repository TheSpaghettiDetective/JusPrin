# macOS build troubleshooting

Recipes for build failures on this fork that are environment/toolchain issues,
not code regressions. Kept separate from `agent-docs/jusprin/` because these
apply to the OrcaSlicer build system generally, not to JusPrin product code.

## `liblibslic3r_gui.a` exceeds 4 GiB: Apple `ar`/`ranlib` assertion

### Symptom

`./build_release_macos.sh -s -x -b -c RelWithDebInfo` (or any RelWithDebInfo
build of the `slicer` target) fails while archiving, not compiling:

```
[36/42] Linking CXX static library src/slic3r/RelWithDebInfo/liblibslic3r_gui.a
FAILED: [code=1] src/slic3r/RelWithDebInfo/liblibslic3r_gui.a
Assertion failed: (memberOffset < 0xFFFFFFFF), function update_block_invoke, file ArchiveWriter.cpp, line 276.
fatal error: .../usr/bin/ar: fatal error in .../usr/bin/ranlib
```

All `.cpp`/`.mm` files for `libslic3r_gui` compiled fine; only the final
archive step fails.

### Root cause

Apple's stock `ar`/`ranlib` (from the Xcode toolchain) use 32-bit member
offsets internally and assert once an archive crosses 4 GiB
(`0xFFFFFFFF` bytes). `liblibslic3r_gui.a` in `RelWithDebInfo` carries full
debug info at `-O0` — the root `CMakeLists.txt` (around line 544-548)
deliberately rewrites `RelWithDebInfo` from `-O2` to `-O0` for easier
debugging — and as the JusPrin fork has added more GUI code (`Agent/`,
`PrinterSetup/`, `Home/`, `Mcp/`, `Shell/`, `Workspace/`, etc.), the archive
has grown past that limit. This first hit on `jusprin-newui` on 2026-09-13 at
commit `770bd2b0a2`; it will recur, and get worse, as the fork keeps growing.
It is not disk space (plenty was free) and not a code bug — the same source
links fine once a 64-bit-capable archiver is used.

### Fix: use LLVM's `ar`/`ranlib` instead of Apple's

LLVM's archiver (installed via `brew install llvm`, present at
`/opt/homebrew/opt/llvm/bin/llvm-ar` and `llvm-ranlib`) has no such limit and
accepted a 4,288,175,376-byte archive without complaint.

**The trap:** `-DCMAKE_AR=...` on an *existing* build directory does nothing.
CMake only auto-detects the archiver once, the first time it determines the
C/C++ compiler, and bakes the result into
`build/arm64/CMakeFiles/<cmake-version>/CMakeCXXCompiler.cmake` (and the `C`
equivalent) as a literal `set(CMAKE_AR "...")`. That generated file also sets
`CMAKE_CXX_COMPILER_ID_RUN 1`, and every later `cmake ..` reconfigure loads it
first and skips compiler/archiver detection entirely because that variable is
already set — so a `-DCMAKE_AR=` flag on a reconfigure is silently ignored.
(Verified 2026-09-13 by reading CMake 3.29's own
`CMakeDetermineCXXCompiler.cmake` and `CMakeCXXCompiler.cmake.in`.)

To make the override take effect, CMake has to redo compiler detection from
scratch. Two ways to get there:

**Option A — targeted reset (cheaper, not yet verified end-to-end).** Delete
just the compiler-detection cache files, then reconfigure:

```bash
rm -rf build/arm64/CMakeFiles/3.29.0/CMakeCXXCompiler.cmake \
       build/arm64/CMakeFiles/3.29.0/CMakeCCompiler.cmake \
       build/arm64/CMakeFiles/3.29.0/CMakeDetermineCompilerABI_CXX.bin \
       build/arm64/CMakeFiles/3.29.0/CMakeDetermineCompilerABI_C.bin \
       build/arm64/CMakeFiles/3.29.0/CompilerIdC \
       build/arm64/CMakeFiles/3.29.0/CompilerIdCXX
cd build/arm64
cmake . -DCMAKE_AR=/opt/homebrew/opt/llvm/bin/llvm-ar -DCMAKE_RANLIB=/opt/homebrew/opt/llvm/bin/llvm-ranlib
```

`ar`/`ranlib` only matter for the final archive step, not for compiling each
`.cpp`, so this is expected to make Ninja only relink static-library targets
rather than recompile every translation unit — but this has not been proven
in practice. Adjust `3.29.0` to match your CMake version
(`build/arm64/CMakeFiles/` has the exact directory name).

**Option B — full clean reconfigure (guaranteed, expensive; this is what was
actually run and verified on 2026-09-13).** Wipe the whole build directory —
it is pure generated output, safe to delete — and reconfigure from nothing:

```bash
rm -rf build/arm64
mkdir -p build/arm64
cd build/arm64
cmake /Users/kenneth/Projects/JusPrin \
  -G "Ninja Multi-Config" \
  -DORCA_TOOLS=ON \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=11.3 \
  -DCMAKE_IGNORE_PREFIX_PATH="/opt/local:/usr/local:/opt/homebrew" \
  -DCMAKE_AR=/opt/homebrew/opt/llvm/bin/llvm-ar \
  -DCMAKE_RANLIB=/opt/homebrew/opt/llvm/bin/llvm-ranlib \
  -DCMAKE_PREFIX_PATH="/Users/kenneth/Projects/JusPrin/deps/build/arm64/OrcaSlicer_dep/usr/local;/Users/kenneth/Projects/OrcaSlicer/deps/build/arm64/OrcaSlicer_dep/usr/local"
cmake --build . --config RelWithDebInfo --target all
```

This pays for a full rebuild of the entire slicer target (every `.cpp`/`.mm`
file, not just `libslic3r_gui`) at `-O0` with full debug info, so budget real
time for it (order of an hour on Apple Silicon).

**Deps-path gotcha, easy to miss:** on this machine, `JusPrin/deps/build/arm64/OrcaSlicer_dep/usr/local`
is an empty skeleton — no Boost, no TBB, nothing — kept that way to save
disk. The real, already-built dependencies for this fork
(Boost 1.84.0, TBB, OpenSSL, CURL, Freetype, libpng, NLopt, OpenVDB, GMP/MPFR,
OpenCV, JPEG, wxWidgets, etc.) live in the sibling checkout at
`~/Projects/OrcaSlicer/deps/build/arm64/OrcaSlicer_dep/usr/local`. A fresh
configure that only uses `build_release_macos.sh`'s own computed
`CMAKE_PREFIX_PATH` (JusPrin's own deps dir) fails immediately with `Could
NOT find Boost`. Always include the OrcaSlicer sibling path too, as shown
above — this mirrors the worktree build pattern already used for
`jusprin-newui` worktrees.

### Once fixed

`build/arm64`'s CMake cache now has `llvm-ar`/`llvm-ranlib` and the correct
`CMAKE_PREFIX_PATH` baked in, so ordinary incremental builds — including
`./build_release_macos.sh -s -x -b -c RelWithDebInfo` (`-b` = don't
reconfigure) — keep using them without any special handling. This is
deliberately *not* wired into `build_release_macos.sh` itself: the script has
no flag for a custom archiver or for the sibling deps path, so recreating
`build/arm64` from scratch again means repeating the manual `cmake` steps
above rather than just re-running the script.

### Evidence

Verified 2026-09-13 on `jusprin-newui` at `770bd2b0a2`: full clean rebuild via
Option B completed with exit code 0, zero `error:` lines in the build log,
final `liblibslic3r_gui.a` at 4,288,175,376 bytes (past the 4 GiB point that
broke Apple's `ar`), confirmed a valid `current ar archive` via `file`, and
`OrcaSlicer.app`'s binary linked successfully.
