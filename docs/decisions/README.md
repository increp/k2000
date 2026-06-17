# Architecture Decision Records (ADRs)

Each ADR captures one non-obvious decision: what was decided, what alternatives were considered, and *why* the chosen option won. ADRs are append-only history — when a decision is reversed, write a new ADR that supersedes the old one rather than editing it.

Filename convention: `NNNN-<short-slug>.md`, zero-padded sequential.

## Index

| # | Title | Status |
|---|---|---|
| [0001](0001-juce-framework.md) | Use JUCE as the plugin framework | Accepted |
| [0002](0002-polymorphic-dsp-slots.md) | Polymorphic DSP slots over hardcoded chain | Accepted |
| [0003](0003-windows-via-github-actions.md) | Windows builds via GitHub Actions, not local cross-compile | Accepted |
| [0004](0004-defer-photoreal-ui.md) | Defer photoreal UI to a later phase | Accepted |
| [0005](0005-voice-layer-split.md) | Voice/Layer split | Accepted (v2) |
| [0006](0006-algorithm-as-passive-data.md) | Algorithm as passive data | Accepted (v2) |
| [0007](0007-param-namespace-and-v1-preset-shim.md) | Param namespace + v1 preset shim | Accepted (v2) |
| [0008](0008-algorithm-selection-and-param-namespace.md) | Algorithm selection: palette + semantic param namespace | Accepted (v3) |
| [0009](0009-multi-layer-program.md) | Multi-layer Program: shared pool, range routing | Accepted (v4) |
| [0010](0010-k2061-repositioning-constant-summit-spine.md) | Re-position to K2061/K2088 VAST + constant Summit spine | Accepted (v4.5) |
| [0011](0011-selectable-spine-filter-library.md) | Selectable spine filter: a curated library of analog filter models | Accepted (v5, 5.01) |
