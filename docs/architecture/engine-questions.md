# Engine architecture — decisions & open questions (living register)

A living register of architectural decisions and open questions for the K2061/Summit engine. Per `feedback_question_heavy_design`: questions are raised constantly, recorded here with the phase that resolves them, and **groomed for internal consistency before any ADR / spec / roadmap is written**. Resolved entries keep the answer so the rationale survives.

Source of the model: [v4.5(C) re-positioning spec](../specs/2026-06-16-v4.5-k2061-repositioning-design.md).

## Locked decisions

| # | Decision | Where |
|---|---|---|
| L1 | Engine north star is **K2061/K2088 VAST**, not K2000. Surface + analog voice stay **Summit**. | C spec |
| L2 | **Constant Summit spine**: Huggett filter + drive → VCA, plus the modulation system (amp/mod envelopes, LFOs, mod matrix, voice modes) is always present. | C spec |
| L3 | **Sources are VAST blocks** — Summit oscillators are subsumed as one (default) source block; KVA/FM/wavetable/sample/noise are others. | C spec |
| L4 | **Dynamic VAST graph** (variable-I/O blocks, serial + parallel) is the variable source/DSP stage feeding the spine. v3's fixed `AlgorithmLibrary` = "factory" presets (the floor). | C spec |
| L5 | **GUI grows with the engine** — B (v4.5) is the load-bearing UI foundation (before v5); each phase ships its feature UI; constant spine = permanent panel, source/DSP region = dynamic knob-clusters. | C spec |
| L6 | Repo codename stays **k2000**. Tuning deferred to **v11**. | C spec |

## Open questions

Status: 🔴 open · 🟡 in discussion · 🟢 resolved (answer recorded).

| # | Question | Why it matters | Resolve at | Status |
|---|---|---|---|---|
| Q1 | **Stereo vs mono signal path.** **Resolved: full stereo throughout** — every block (sources, DSP graph, spine) is stereo from v5. | Touches every block, filter, voice mix, CPU. | v5 | 🟢 |
| Q2 | **Voice-count target.** **Resolved: target the full 256 voices** (with full stereo) — fidelity over a reduced budget. Makes Q11 (CPU) critical. | Feasibility + allocation design. | v9 (intent set) | 🟢 |
| Q3 | **Spine / modulation scope.** **Resolved: per-Layer** — each Layer has its own spine (filter/VCA) + modulation. No program-level tier for now. | Where filter/env/LFO/mod live; instance count. | v5 | 🟢 |
| Q4 | **Sample/keymap sources.** **Resolved: synth-only now** — sources = oscillators/FM/DSP; sample/keymap playback deferred to v12+. | Avoids pulling a sampler subsystem in early. | v12+ | 🟢 |
| Q5 | **Variable-graph parameter model** — a fixed APVTS layout can't describe an arbitrary-topology graph (variable blocks/wiring). How do we parameterize / automate / save / migrate a Dynamic-VAST graph? | The central technical challenge of Dynamic VAST. | v6 | 🔴 |
| Q6 | **Graph → spine interface** — single mono/stereo signal into the filter? K2088 has "double-output" algorithms — does our graph have one output to the spine, or multiple? | Defines the boundary contract. | v6 | 🔴 |
| Q7 | **Mod-matrix addressing across changing graphs** — the constant mod matrix must target both spine params and arbitrary graph-block params with a stable scheme as the graph changes. | Mod routing stability across edits/presets. | v7 (depends on Q5) | 🔴 |
| Q8 | **Summit FX vs KDFX** — KDFX is the routing (per-layer insert + common + 2 aux); Summit effects are the effect types. Is a Summit-style master FX section a *constant* (always-present common chain) or just one KDFX config? | FX architecture + whether FX joins the "constant" identity. | v8 | 🔴 |
| Q9 | **Dynamic control panels** — how much of the variable source/DSP region is front-panel vs pages; how knob-clusters swap without losing state; how the graph editor surfaces block params. | Core of the GUI foundation. | B (v4.5) | 🔴 |
| Q10 | **Mixer placement** — with sources as blocks, the mixer (osc/ring/noise levels) is graph-side. Confirm it's a DSP block, and how multiple sources sum into the one voice output the spine expects. | Graph semantics. | v6/v7 | 🔴 |
| Q11 | **CPU/voice budget & block cost** at 256 × full stereo × Dynamic-VAST graphs. **Strategy resolved: performance is a cross-cutting constraint** — each phase meets a per-voice CPU budget with profiling as a gate (like the test gate); perf-friendly data structures designed in from v5 (SIMD-friendly buffers, denormals, efficient graph execution). The specific per-phase budgets remain to be set. | Bounds the whole design; feasibility of Q1+Q2. | every phase (ongoing) | 🟡 |
