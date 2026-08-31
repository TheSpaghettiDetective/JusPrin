# CLAUDE.md

OrcaSlicer — open-source C++17 3D slicer. wxWidgets GUI, CMake build system.

## Error handling discipline

- Do not catch an error merely because it can be caught. Catch only when the code can recover, translate the error at an abstraction boundary, or add context that is essential for diagnosis. Logging alone is not recovery.
- Represent expected states with normal control flow, not exceptions.
- Never turn an invariant violation or an "impossible" state into success or a silent no-op. Fix the invalid transition at the layer that owns the decision and let unexpected failures propagate to Sentry.
- Avoid duplicating defensive checks across layers. Keep lower-level operations strict, with their preconditions enforced by the caller that owns the workflow.
- If concurrency makes an invalid state reachable, fix the ordering or synchronization instead of suppressing the resulting error.
- Suppress an error only when it is genuinely expected and safely recoverable; document the reason and cover it with a test. Never broadly swallow unexpected errors.
- Prefer removing the root cause over reducing the visibility of its symptoms. Tests should encode valid transitions and assert that invalid ones remain visible.

## Build Commands

```bash
# macOS
cmake --build build/arm64 --config RelWithDebInfo --target all --

# Linux
cmake --build build --config RelWithDebInfo --target all --

# Windows (replace %build_type% with Debug/Release/RelWithDebInfo)
cmake --build . --config %build_type% --target ALL_BUILD -- -m
```

## Testing

Catch2 framework. Tests in `tests/` directory.

```bash
cd build && ctest --output-on-failure           # all tests
ctest --test-dir ./tests/libslic3r              # individual suite
ctest --test-dir ./tests/fff_print
```

## Code Style

- C++17, selective C++20. PascalCase classes, snake_case functions/variables
- `#pragma once` for headers. Smart pointers and RAII preferred
- Parallelization via TBB — be mindful of shared state

## Key Entry Points

- App startup: `src/OrcaSlicer.cpp`
- Slicing pipeline: `src/libslic3r/Print.cpp`
- All print/printer/material settings: `src/libslic3r/PrintConfig.cpp`
- GUI: `src/slic3r/GUI/`
- Core algorithms: `src/libslic3r/` (GCode/, Fill/, Support/, Geometry/, Format/, Arachne/)
- Printer profiles: `resources/profiles/[manufacturer].json`

## JusPrin Production UI

- Start with `agent-docs/jusprin/README.md` for the canonical product definition, production architecture, verification method, Orca integration guidance, and design system.
- Before editing any file outside `src/slic3r/GUI/JusPrin/`, read `agent-docs/jusprin/fork-stewardship.md`. Every line changed in an OrcaSlicer-owned file is a line this fork re-resolves at every rebase, so those changes must be small, product-neutral, additive where possible, and accompanied by the rebase evidence that document requires.
- Experimental plans, probes, screenshots, logs, and temporary UI code remain on `jusprin-v2-poc`. Use `agent-docs/jusprin/poc-reference.md` for exact commit and path pointers; do not cherry-pick the POC wholesale.
- Curated brand and semantic UI assets live under `resources/jusprin/`.

## Critical Constraints

- **Backward compatibility required** for .3mf project files and printer profiles
- **Cross-platform** — all changes must work on Windows, macOS, and Linux
- Profile/format changes need version migration handling
- Dependencies built separately in `deps/build/`, then linked to main app

## Code review focus areas

- Changes must not cause regressions in existing functionality, defaults, profiles, or project compatibility.
- Features gated by options must not affect existing behavior when those options are disabled.
- Changes should follow the existing code style and architecture. Architectural changes should be justified in code comments and the PR description.
- Add helper functions or utilities only when existing code cannot reasonably be reused. Avoid duplication.
- Changes to OrcaSlicer-owned files must be additive where possible. Rewriting an upstream function, or reimplementing upstream behavior beside it, needs explicit justification in the PR description.
- When an upstream line is deleted or relocated, say where it went and what a future rebase must carry into the new home.
- Keep code concise and clear. Manually simplify AI generated bloated codes before review.
- Include targeted tests or documented verification for behavior changes, especially in slicing logic, profiles, formats, and GUI defaults.
