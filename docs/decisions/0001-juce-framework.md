# ADR 0001 — Use JUCE as the plugin framework

**Date:** 2026-05-25
**Status:** Accepted

## Context

We need a C++ plugin framework that handles VST3 packaging, audio I/O plumbing, MIDI, parameter automation, preset save/load, and a GUI toolkit. The developer is an audio engineer with limited C++ background, so the maturity of docs and community matters as much as raw capability.

## Options considered

- **JUCE.** Dominant framework. Multi-format from one codebase (VST3, AU, CLAP, standalone). Includes DSP utilities, GUI, MIDI, parameter system. Huge community, abundant tutorials. Dual-licensed: free for open-source under GPL, commercial license for closed-source distribution.
- **iPlug2.** Leaner, MIT-licensed, also multi-format. Smaller community, thinner docs, more DIY for things JUCE provides.
- **CLAP SDK + clap-wrapper.** Modern, fully open plugin format wrapped to VST3. Cutting-edge but documentation is thin; we'd be solving infrastructure problems instead of synth-building problems.

## Decision

Use JUCE.

## Why

The developer is new to C++/DSP. The deciding factor is learning-curve management: JUCE has by far the most tutorials, books (e.g. the Pirkle and Tarr books), and community Q&A. Time spent learning JUCE is reusable on every future audio project, and most problems we hit will already have answers.

iPlug2's smaller community is a real handicap when stuck on a build or threading issue. CLAP-first is technically appealing but is the wrong place to spend learning bandwidth right now.

The license question is deferred — fine for personal/learning use under GPL; if commercial distribution ever becomes the goal, we revisit then.

## Consequences

- All C++ code is JUCE-flavored (JUCE's container types, threading model, GUI components).
- The project depends on a specific JUCE version, pinned as a git submodule.
- We get VST3, AU, CLAP, and standalone for almost free when we want them, even though v1 only ships VST3.
