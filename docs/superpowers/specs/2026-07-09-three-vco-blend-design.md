# Three-VCO Blend Oscillator — Design

**Version:** 5.29 (artifact) · **Date:** 2026-07-09 · **Status:** Approved in brainstorm; build follows
**Depends on:** `2026-07-09-vco-prework-removals-design.md` (Drive/Mix knob removal touches the same Source-section layout this rebuild replaces)

---

## 1. Problem

Each voice currently has exactly one oscillator (`Voice::osc_`, single-waveform enum: Saw/Square/Triangle/Sine). The Source section is being rebuilt around three independent VCOs per voice, each capable of dialing in a custom waveform as a proportional blend of Sine/Triangle/Saw/Pulse (plus a duty-cycle control for Pulse), with a new Mixer section balancing the three VCOs' levels.

## 2. Naming

Code/param identifiers keep the existing `osc` root (matching the current `Oscillator` class and `osc.*` param namespace) — **`osc1`/`osc2`/`osc3`**, not `vco1/2/3`, to stay consistent with what's already in the codebase. GUI labels say "VCO 1/2/3", matching how you've been describing them. The new per-VCO waveform control is called **Blend** everywhere (code and UI) — deliberately not "Shaper" (already the Drive/Mix waveshaper) or "Mixer"/"Mix" (already the VCO-balance section and the waveshaper's wet/dry knob, respectively).

## 3. Params

Extends `buildIds()`/`LayerIds` in `Parameters.cpp`, same per-layer `"layer{N}."` prefix pattern as everything else. Replaces the single `osc.waveform` param (retired, no back-compat) with, for `n` in `{1,2,3}`:

| param | range | default |
|---|---|---|
| `osc{n}.coarse` | existing Coarse range | existing default (osc1 only; osc2/osc3 default to unison, i.e. 0) |
| `osc{n}.fine` | existing Fine range | 0 |
| `osc{n}.blend.sine` / `.triangle` / `.saw` / `.pulse` | 0–100% | osc1: saw=100, rest 0 (matches today's default patch). osc2/osc3: saw=100, rest 0 (audible-if-turned-up, silent via Mixer level — see §6) |
| `osc{n}.blend.pulseDuty` | 1–99% | 50% |
| `mixer.osc{n}.level` | 0–100% | osc1=100, osc2=0, osc3=0 |

## 4. Blend math (the core DSP change)

All four waveform shapes are evaluated at **one shared phase accumulator per oscillator** — this is what makes the result a genuinely new single-cycle waveform rather than three/four independently-clocked oscillators summed (which would beat/chorus instead of reshape). Per sample, per VCO:

```
total = wSine + wTri + wSaw + wPulse
out   = total > 0
        ? (wSine*sine(t) + wTri*tri(t) + wSaw*saw(t,dt) + wPulse*pulse(t,dt,duty)) / total
        : 0.0
```

- **Proportional, not additive**: dividing by `total` means the sliders set *ratios*, never loudness — 5%/19% sine/triangle sounds identical in level to 50%/190% sine/triangle (same 5:19 ratio), always at full resultant amplitude. All four at max = 25% each. This is a deliberate, explicit design choice (not a headroom safeguard) — the blend never touches level; level lives entirely in the new Mixer section (§6).
- **Zero-sum = silence**: all four weights at 0 divides by nothing meaningful → output 0. This is a low-stakes edge case, not a mute mechanism — muting a VCO is the Mixer level knob's job, so blend-all-zero is a rare, deliberately-reached state, not a common one.
- Classic single-knob wavetable-style morph (sine→tri→saw→pulse, crossfading only two adjacent neighbors) is a special case of this formula (only two weights nonzero, already summing to `total=1`) — nothing forecloses adding a "morph position" macro control on top later.

## 5. DSP implementation (`src/dsp/Oscillator.h/.cpp`)

`Oscillator` is used only by `Voice` (confirmed via grep) — free to change its public interface:

- Replace `enum class Waveform` + `setWaveform(Waveform)` with `setBlend(float sine, float tri, float saw, float pulse)` and `setPulseDuty(float duty)`.
- `processSample()` computes the four components per §4. Skip computing a component whose weight is exactly 0 (cheap, avoids the trig/polyBLEP call entirely for unused shapes — not a design requirement, just a free efficiency win worth taking at implementation time).
- **Pulse duty cycle** generalizes the existing fixed-50%-duty Square polyBLEP: rising edge at `t=0` (`polyBLEP(t, dt)`), falling edge at `t=duty` (`polyBLEP(fmod(t - duty + 1.0, 1.0), dt)`), replacing the current hardcoded `0.5`.
- **Triangle stays derived from its own internal fixed-50% square** (the existing leaky-integrator implementation) — it does **not** read `pulseDuty`. That parameter affects only the separate Pulse blend component. Worth a comment at the implementation site since the coupling would be a natural but wrong assumption to make.

## 6. Voice architecture (`src/Voice.h/.cpp`)

`Voice` goes from one `osc_` to three (`osc1_, osc2_, osc3_`, or an equivalent `std::array<Oscillator,3>`). In `render()`: each VCO gets its own coarse/fine → own Hz, its own blend+duty from the snapshot, processes into its own buffer, gets scaled by its `mixer.osc{n}.level`, and the three are summed into the same mono `scratch_` buffer that feeds today's unchanged algorithm-block graph — everything downstream of oscillator generation (blocks, spine filter, envelope) is untouched.

## 7. GUI (`src/PluginEditor.h/.cpp`)

- Source section rebuilds as **three stacked VCO rows** (your chosen full-visibility layout: every VCO's full state visible at once, no tab-switching) — each row: Coarse, Fine, four Blend sliders (Sine/Triangle/Saw/Pulse), Pulse Duty control positioned under the Pulse slider. This needs materially more panel space than today's single-VCO row; exact sizing gets tuned live against the running Standalone build, not pre-specified to the pixel.
- The currently-reserved, dimmed `mixerSection_` gets built out for real: three level knobs, one per VCO, replacing its placeholder state.
- `oscWave_` combo box is removed (fully superseded by the Blend sliders).

## 8. Non-goals

- No modulation routing for Blend weights or duty cycle in this pass (Mod Matrix section stays reserved/untouched) — purely static per-voice controls for now.
- No attempt to pre-optimize the up-to-12x oscillator-stage CPU increase (3 VCOs × up to 4 components vs. today's 1×1) beyond the free zero-weight skip in §5. Measure with the existing characterization tooling after implementation; optimize only if the 64-voice target is actually at risk.
- No change to anything downstream of oscillator generation (algorithm blocks, spine filter, envelope, waveshaper) — this spec is oscillator-stage only.

## 9. Verification

Extends `tests/OscillatorTests.cpp`: blend-ratio correctness (proportional math, not additive), zero-sum silence, duty-cycle edge placement for Pulse, Triangle independence from `pulseDuty`. `cmake --build build --target k2000_tests -j4 && ./build/tests/k2000_tests` green. Live check in the running Standalone (per the usual GUI rule — unit tests don't substitute for looking at/hearing the actual panel): three VCO rows visible and independently playable, Mixer section balances them audibly, default patch sounds identical to today's single-Saw-VCO patch (osc1 at Mixer 100%, osc2/osc3 at 0%).
