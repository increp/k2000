# ADR 0003 — Windows builds via GitHub Actions, not local cross-compile

**Date:** 2026-05-25
**Status:** Accepted

## Context

Developer works on Linux but wants to test the plugin in Ableton 12 on Windows. Three viable paths to a Windows VST3 from a Linux dev machine:

## Options considered

- **GitHub Actions CI.** Push to GitHub; CI builds Linux + Windows in parallel using real MSVC on the Windows runner; download the `.vst3` artifact from each commit.
- **MinGW cross-compile locally.** `x86_64-w64-mingw32-g++` from Linux. Fast iteration. JUCE supports it, but historically MinGW-built VST3s have shown occasional compatibility quirks in commercial hosts (manifest/signing, ABI edge cases).
- **Local Windows VM with Visual Studio.** Most reliable Windows build, slowest iteration, requires a Windows license and ~80 GB of disk.

## Decision

GitHub Actions CI.

## Why

The Windows build is a sanity check ("does it sound right in Ableton"), not a tight inner-loop activity. CI latency (a few minutes per push) is fine for that cadence. The benefits compound:

- Windows binary is built with the same toolchain real Ableton plugins use (MSVC). No MinGW-specific compatibility risk.
- Linux build also runs on a clean machine, catching "works on my laptop" issues early.
- Tests run on both platforms automatically — Windows-only regressions get caught before manual DAW testing.
- Zero local toolchain setup for Windows.
- Free for public repos; cheap for private.

MinGW would save round-trip time but trades it for a class of compatibility bugs we don't want to debug. The VM is overkill for v1's "occasional Ableton check" need.

## Consequences

- `.github/workflows/build.yml` exists from day one with a `{ ubuntu-latest, windows-latest }` build matrix.
- Every push produces downloadable Windows + Linux VST3 artifacts.
- The project lives in a GitHub repo from early in the project (not strictly required for v1's first lines of code, but the longer we wait the more painful the migration).
- We do not maintain a local MinGW toolchain or a Windows VM.
- If CI ever becomes too slow or expensive to be useful, we revisit (e.g. add a local MinGW path as a quick check, while keeping CI as the trusted-artifact source).
