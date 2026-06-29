# ADR-005 — Meson as Build System

## Status
Accepted

## Date
2026-05-02

## Context
The proxy must build on Linux (GCC/Clang) and Windows (MSVC/MinGW-w64). It has two external dependencies: a TOML parser (`tomlc99`) and a unit test framework (`unity`). The build system must:
- Support cross-platform compilation
- Manage external dependencies without requiring system-level installation
- Integrate test execution (`meson test`)
- Support coverage instrumentation (`-Db_coverage=true`)
- Be reproducible across developer machines and CI environments

Options evaluated:
- **CMake:** most widely used; excellent cross-platform support; verbose syntax; wraps/FetchContent less ergonomic than Meson's wrap system
- **Make/Makefile:** universal but no portable dependency management; platform differences require `ifeq/ifdef` chains; no built-in test runner
- **Meson (chosen):** modern Python-based build system; wrap files for reproducible dependency fetching; built-in test runner with JUnit XML output; native coverage support; cross-compilation via cross-file; concise syntax

## Decision
Use Meson ≥ 1.3 as the build system. External dependencies are managed via `.wrap` files in `subprojects/`:
- `unity.wrap` — downloads and builds the Unity test framework as a subproject
- `tomlc99.wrap` (if/when added) — would manage the TOML parser similarly

The root `meson.build` detects the platform (`host_machine.system()`) and selects the correct platform backend (`platform/epoll.c` or `platform/iocp.c`).

## Consequences

### Positive
- Single command setup: `meson setup builddir` downloads, builds, and links all dependencies
- `meson test` integrates the unit test runner with structured output and JUnit XML for CI
- `meson setup builddir -Db_coverage=true` + `ninja coverage-html` gives full lcov HTML coverage without additional scripts
- Cross-compilation to Windows from Linux: `meson setup --cross-file mingw64.ini` works without Makefile conditionals
- Wrap files are version-pinned (hash in `.wrap` file) — reproducible builds across environments

### Negative / Trade-offs
- Requires Python ≥ 3.8 as a runtime dependency of Meson itself (usually present on developer machines and CI runners, but adds one implicit requirement)
- Less familiar than CMake or Make to some developers and evaluators
- Some Meson idioms (e.g., `dependency()` vs. `find_library()`) have subtle differences that require reading documentation
- The `meson compile -C builddir format` custom target requires clang-format to be in PATH; it silently skips if not found

### Neutral
- `meson.options` file exposes configurable build options (e.g., `log_default_level`) that can be overridden per build without editing `meson.build`
- The Unity subproject is excluded from `.gitignore` once downloaded; `git submodule` is not needed
