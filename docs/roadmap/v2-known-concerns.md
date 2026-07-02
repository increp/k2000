# v2 known concerns (carried from the v1 final review)

> **Point-in-time record (v2 era).** Not groomed since; several items are since resolved. Surviving concerns belong in the living register `docs/architecture/engine-questions.md` — check there before acting on anything below.

These are issues identified in v1 that don't block v1 shipping but must be addressed in v2 or a later phase. Captured here so they aren't lost between specs.

## Structural

### Single oscillator in Voice
`Voice` owns exactly one `Oscillator osc_`. The v2 vision (Peak character) requires 3 NCO oscillators per voice with per-oscillator detune and mix. The v2 spec must restructure `Voice` to hold an array of oscillators and combine their outputs before the DSP slot chain.

### Voice stealing ignores midiNote
`VoiceManager::pickVoiceFor(int)` takes a MIDI note argument but doesn't use it. v1 always steals the oldest active voice. v2's per-voice tuning state and v3's legato/mono modes will need this argument — the steal strategy may want to prefer voices already playing the same note (for retrigger), or voices in a related pitch range.

### Stealing causes a click
When a voice is stolen, `reset()` zeros oscillator phase AND all DSP slot state in the same call (`Voice::noteOn`). At high filter resonance the filter's stored state is meaningful, and instantly zeroing it produces a click on the stolen voice's new note. Options when this becomes audibly annoying: (a) a few-millisecond fade between stolen-note release and new-note attack, (b) keeping filter state across steal, (c) a "voice retrigger" mode that's not a full reset.

## Architectural

### `DSPBlock::getParamSpecs()` is half-implemented
Both `SVFFilter` and `Waveshaper` return `{}` from `getParamSpecs()`, and `PluginProcessor` doesn't call it — the APVTS layout is hand-coded in `params/Parameters.cpp`. This is the "split-brain" state that's risky: if a v4 contributor adds a new block expecting `getParamSpecs()` to drive registration, they'll discover at runtime that their parameters aren't bound.

Decide before v4 (when slot types become user-selectable):
- **Option A:** Wire `getParamSpecs()` into APVTS registration; delete the parallel hand-coded layout for slot params.
- **Option B:** Remove `getParamSpecs()` from the interface; add it back when block-driven layout actually arrives.

Leaving both paths half-open is the dangerous state.

### `ParamSnapshot` will grow large
v1 has 13 fields in a flat POD. v2 adds ~10 (3 oscillators × {coarse, fine, level, waveform} + detune); v3 adds 8+ for mod matrix; v4 adds variable-per-slot. By v3 this is a 40+ field struct that's hard to navigate.

Suggest splitting into sub-structs by v2: `OscSnapshot[3]`, `FilterSnapshot`, `ShaperSnapshot`, `AmpSnapshot`, `ModSnapshot`. Easier to do at v2 size than retrofit at v3 size.

## RT-safety

### No parameter smoothing
The v1 spec defers smoothing ("smoothing happens at the parameter layer"). At v1 sizes (13 params, no automation) this is inaudible. By v3 (mod matrix writes per-sample to filter cutoff at LFO rate), unsmoothed parameter writes will zipper audibly. Smoothing must arrive no later than v3.

## Documentation/test debt

### Preset state slot metadata isn't tested
`PluginLifecycleTests` verifies that an APVTS parameter (`svfCutoff`) round-trips through `getStateInformation` → `setStateInformation`, but doesn't verify the `<Slots>` XML block is written. A regression in the state-save XML structure would break v4 preset loading without v1 tests catching it. Add a test that parses the XML and asserts `<Slot index="0" type="svf_filter"/>` is present.

### Preset compatibility across APVTS param renames
A general APVTS risk, not specific to k2000: renaming a parameter ID (`slot0.cutoff` → `filter.cutoff`) in a future version makes old presets silently lose that value. When a refactor renames any ID, the v2+ spec should include a one-time migration path or at minimum a documented deprecation.
