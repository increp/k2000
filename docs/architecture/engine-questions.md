# Engine architecture — decisions & open questions (living register)

A living register of architectural decisions and open questions for the K2061/Summit engine. Per `feedback_question_heavy_design`: questions are raised constantly, recorded here with the phase that resolves them, and **groomed for internal consistency before any ADR / spec / roadmap is written**. Resolved entries keep the answer so the rationale survives.

Source of the model: [v4.5(C) re-positioning spec](../specs/2026-06-16-v4.5-k2061-repositioning-design.md).

## Locked decisions

| # | Decision | Where |
|---|---|---|
| L1 | Engine north star is **K2061/K2088 VAST**, not K2000. Surface + analog voice stay **Summit**. | C spec |
| L2 | **Constant Summit spine**: a **selectable, live-switchable filter model** (Huggett default) + drive → VCA, plus the modulation system (amp/mod envelopes, LFOs, mod matrix, voice modes), always present. | C spec + v5 |
| L3 | **Sources are VAST blocks** — Summit oscillators are subsumed as one (default) source block; KVA/FM/wavetable/sample/noise are others. | C spec |
| L4 | **Dynamic VAST graph** (variable-I/O blocks, serial + parallel) is the variable source/DSP stage feeding the spine. v3's fixed `AlgorithmLibrary` = "factory" presets (the floor). | C spec |
| L5 | **GUI grows with the engine** — B (v4.5) is the load-bearing UI foundation (before v5); each phase ships its feature UI; constant spine = permanent panel, source/DSP region = dynamic knob-clusters. | C spec |
| L6 | Repo codename stays **k2000**. Tuning deferred to **v11**. | C spec |
| L7 | **Spine filter is a `FilterModel` library** — append-only, stable-ID registry (Huggett v5, Moog v5.1, Oberheim+ later), selected per-Layer by an **automatable** `spine.filterModel`. **Live click-free switching** via a per-voice equal-power **crossfade** of two **heap-free, in-place** model instances. Param shape: a **common core** (cutoff/res/drive/output, always front-panel + mod-targetable) + **per-model namespaced banks** `spine.<modelId>.<param>` (the ADR-0008 idiom). | v5 |

## Open questions

Status: 🔴 open · 🟡 in discussion · 🟢 resolved (answer recorded).

| # | Question | Why it matters | Resolve at | Status |
|---|---|---|---|---|
| Q1 | **Stereo vs mono signal path.** **Resolved: full stereo throughout** — every block (sources, DSP graph, spine) is stereo from v5. *Deliberate modernization, confirmed against the hardware:* the real Summit is **mono through the dual filter + both drive stages, splitting to L/R only at the VCA** (Summit User Guide pp. 21–22 block diagram), and K2000 VAST is **mono-per-layer + pan to stereo outputs** (Musician's Guide p. 366). We knowingly depart from that to be stereo end-to-end, accepting the ~2× per-voice filter cost (gated by Q11/Q12). | Touches every block, filter, voice mix, CPU. | v5 | 🟢 |
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
| Q12 | **Filter oversampling policy at 256 voices.** **Direction resolved (constrained by L7):** the live-switch in-place/heap-free rule **forbids a per-voice `juce::dsp::Oversampling` object**. Policy — linear TPT core runs un-oversampled; nonlinear drive stages use **cheap, fixed per-voice anti-aliasing, conditional on drive being engaged**; exact factor/scheme chosen **behind the perf gate** (Q11), measured at the voice target. | Largest v5 CPU risk; gates fidelity-vs-budget. | v5 | 🟡 |
| Q13 | **Dual-section vs single SVF at v5. Resolved: ship the dual** two-linked-12 dB-section + separation core from v5 — it is the Summit identity and the [deep research](huggett-filter.md) backs it. Internal milestones de-risk: linear single cell → dual + separation → nonlinear → calibrate. | Core fidelity vs v5 scope/cost. | v5 | 🟢 |
| Q14 | **Filter mode set exposed at v5. Resolved: Summit dual modes first** (LP/BP/HP, 12/24 dB, series/parallel); OSCar separation-law modes are a marked **stretch/later**. | Scope of v5 filter UI + DSP. | v5 | 🟢 |
| Q15 | **White-box OTA vs gray-box tanh.** v5 ships **gray-box** — three tanh-class nonlinear stages (pre-drive, resonance-loop saturator, post-drive) with calibrated laws. Component-accurate **white-box OTA** modeling stays future work with no committed phase. | Fidelity ceiling vs effort; later phase. | v5 (start) / later | 🟡 |
| Q16 | **Filter promotion + preset migration. Resolved (approach):** a cumulative shim migrates v1–v4 presets onto the spine with `filterModel = Huggett`; the old optional `SVFFilter` cutoff/res map onto the **common core**; per-model banks default. A migration test guards it. | Preset stability across the v5 promotion. | v5 | 🟢 |
| Q17 | **Crossfade duration + selector-automation debounce.** How long is the equal-power model-switch fade, and how do we bound cost when the (automatable) `spine.filterModel` is stepped rapidly (debounce / ignore no-op reselect / cap concurrent fades)? | Live-switch cost + click-free guarantee. | v5 | 🔴 |
| Q18 | **Per-voice slot size budget governance.** The two in-place model slots are sized to the **largest** `FilterModel`. As models are appended (Oberheim+), what is the size cap, and what is the process when a new model would exceed it (bump size + migrate vs reject)? | Keeps L7's heap-free invariant as the library grows. | v5 (set) / ongoing | 🔴 |
| Q19 | **Live model-selector automation semantics.** `spine.filterModel` is a stepped choice automated live — confirm stepped (not interpolated) VST3 semantics and how the host/automation lane represents it; is the selector itself a mod-matrix destination? | Automation correctness for the selector. | v5 | 🔴 |
