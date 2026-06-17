# Filter dossiers — Oberheim SEM & Moog ladder

> **Version:** 5.01 · **Ingested:** 2026-06-17 · External research, body verbatim except this header.
> **Purpose:** source material for two `FilterModel` library entries in the selectable Summit spine slot ([ADR-0011](../decisions/0011-selectable-spine-filter-library.md), register L7). **Dossier 1 (SEM)** is new; **Dossier 2 (Moog)** corroborates and sharpens [moog-ladder.md](moog-ladder.md). Both topologies are legally clear (SEM unpatented; Moog patent expired 1986).

---

# DOSSIER 1 — THE OBERHEIM SEM FILTER
*Specification dossier for a behavioral, analog-modeled DSP replica in JUCE/C++*

## TL;DR
- The SEM filter is a **2-pole (12 dB/oct) multimode state-variable filter** (Kerwin-Huelsman-Newcomb two-integrator topology) designed by Dennis Colin, built from **CA3080 OTAs** as the voltage-controlled elements with discrete JFET buffers; it is **unpatented** and the topology is public-domain academic work, so a DSP replica is legally clear.
- Its sonic signature comes from being **2-pole (gentle) and non-self-oscillating** (resonance is implemented as *damping* with limiting diodes, and there is insufficient loop phase shift to reach oscillation), with a continuous **LP→notch→HP morph** plus a separate bandpass output.
- Recommended DSP approach: a **linear topology-preserving-transform (TPT/ZDF) state-variable filter** (Zavalishin / Cytomic-Simper), exposing simultaneous LP/BP/HP, deriving notch/peak by summation, with a Mode-morph crossfade and a **clamped resonance that never self-oscillates**, plus mild OTA tanh saturation. In JUCE, start from `juce::dsp::StateVariableTPTFilter`.

## Key Findings

### Patent / IP status
- **No patent exists on the SEM filter or its state-variable circuit.** The complete list of patents assigned to Oberheim Electronics Inc. (verified via Justia and Google Patents) is three, and none covers the SEM SVF:
  1. **US 3,986,423 "Polyphonic music synthesizer"** — inventor **David P. Rossum**, filed Dec 11 1974, granted Oct 19 1976 — a keyboard voice-assignment multiplexer, *not* a filter.
  2. **US 4,185,531 "Music synthesizer programmer"** — inventors **Thomas E. Oberheim and James L. Cooper** — the PSP patch-storage programmer.
  3. **US 3,969,682 "Circuit for dynamic control of phase shift"** — inventor **David P. Rossum** — an OTA phase-shift/filter cell (SSM2040 lineage), *distinct from* the SEM's two-integrator SVF.
- The state-variable topology itself originates in the public-domain academic paper **Kerwin, Huelsman & Newcomb, "State-variable synthesis for insensitive integrated circuit transfer functions," IEEE J. Solid-State Circuits 2 (1967), pp. 87–92.** The related OTA filter *cell* was patented by Dennis Colin / ARP (**US 3,805,091**, "Frequency sensitive circuit employing variable transconductance circuit," Apr 16 1974), assigned to ARP — not Oberheim. **Implication: cloning the SEM filter behavior is completely unencumbered.**

### Lineage & history
- The SEM (Synthesizer Expander Module) was released in **1974**, Tom Oberheim's first instrument. Oberheim (verbatim, Perfect Circuit interview): *"I designed what we now call the SEM Synthesizer Expander Module. We sold them for that purpose—or if somebody wanted to beef up their ARP 2600 or Odyssey or Minimoog, you could get a fatter sound."* It also accompanied the Oberheim DS-2 sequencer.
- The design was a collaboration. Oberheim (via GForce/CDM): *"I came up with the feature set and user interface and Jim [Cooper] designed much of the circuitry. In addition, Dave Rossum of E-Mu systems helped with the VCO design and Dennis Colin of ARP designed the multimode filter."* And on the choice of slope (Perfect Circuit): *"I got Dennis Colin, who designed the 2600, to design the two-pole filter… I specifically put in a two-pole filter because it was different from what everybody had. Not because it was better or worse—it was a different sound."*
- **Dennis Colin** (Tonus/ARP) designed the ARP 2500 model 1047 Multimode Filter/Resonator and the ARP 2600; his foundational AES paper "Electrical Design and Musical Applications of an Unconditionally Stable Combination Voltage Controlled Filter/Resonator" (presented Apr 29 1971, 40th AES Convention) describes the ARP 1047 — the direct ancestor of the SEM filter.
- The SEM became the voice building block of the Oberheim Two-Voice (1975), Four-Voice (FVS) and Eight-Voice polyphonics. **Transition to later Oberheim synths:** the OB-1 and OB-X retained discrete SEM-derived 2-pole state-variable VCFs (Curtis chips used only for envelopes on the OB-X). The **OB-Xa (1980) and OB-8 moved to Curtis CEM3320 ICs**, wired as a state-variable filter but tapping only the lowpass output, switchable 2-pole/4-pole, and borrowing "the resonance limiting diodes found in the SEM SVF design." The Matrix-12/Xpander used the CEM3372.
- Modern recreations: Tom Oberheim's own reissue SEM (2009–), the **OB-X8** (2022, offering both a discrete SEM-style VCF and a CEM3320 model), and the OB-6 (2016, SEM-style VCO/VCF by Oberheim with Dave Smith control).

### Verified technical / circuit details
- **Topology:** two-integrator state-variable filter (Kerwin-Huelsman-Newcomb / Tow-Thomas family), **2-pole, 12 dB/octave**, with simultaneous LP/BP/HP outputs and Q independent of cutoff.
- **Exact components (original SEM — confirmed by Synthesis Technology e430, the licensed full-original-parts clone, and DIY clones built from the synthfool.com schematics):**
  - **CA3080-class OTAs** as the voltage-controlled elements (two)
  - **LM301** audio input op-amp
  - **LM741** in the resonance path (Synthesis Technology lists one; the original-schematic DIY BOM calls for three)
  - **RC4558** audio summing op-amps
  - **Discrete JFET source-follower current buffers between filter stages** (original part: **2N4302** N-channel JFET)
  - Canonical original BOM: 2×CA3080, 1×LM301, 3×LM741, 2×2N4302.
- **Resonance / self-oscillation:** The SEM **does NOT self-oscillate in stock form.** Resonance is implemented as **damping** — reducing damping increases resonance ("omitting the resonance circuitry means maximum of resonance") — with **limiting diodes in the feedback loop.** Authoritative reason (Sound Semiconductor AN701): *"by the lack of sufficient phase shift in the resonance feedback loop, they are incapable of self-oscillating."* A 2-pole SVF cannot reach the 360° loop phase + >unity gain (Barkhausen) condition that a 4-pole ladder meets. Studio Electronics describes the mechanism as an analog pendulum model: increasing resonance decreases damping until, if hit hard, it "sticks" ("bonk-out"). Users can coax pseudo-oscillation only by external self-patching.
- **Mode control:** a single front-panel knob continuously morphs **LP → notch → HP**, with **bandpass on a separate switched position/output.** The **notch is created by summing the LP and HP outputs** in the VCA/mixer (ModWiggler clone builder: *"The 'NOTCH' filter function of the original SEM module is created by its VCA by addition of the high- and lowpass VCF outputs."*).
- **Sonic character:** smooth, creamy, "silky and open," wide with lots of movement; ideal for pads, strings, brass and sustained sounds; gentler than a 24 dB Moog. Subtle OTA input-stage distortion plus the FET buffers and feedback-loop diodes shape its character.

### Existing emulations
- **Arturia SEM V** (synth) and **Arturia Filter SEM** (FX plugin) — model the 12 dB/oct SVF with crossfadable LP/notch/HP + bandpass. *Sound On Sound* confirmed the SEM V does not self-oscillate (and flagged Arturia's manual erroneously claiming it does).
- **GForce Oberheim SEM** (2022, made with Tom Oberheim & Marcus Ryle) and **GForce OB-E** — praised for capturing the analog nonlinearities.
- **u-he Diva** — two filter models are SEM-derived: the "Multimode" and "Uhbie," both modeled on the 2-pole Oberheim SEM SVF, using realtime circuit simulation + zero-delay feedback (ZDF).
- **Hardware Eurorack clones:** Synthesis Technology e430 Morphing SVF (full original circuit; individual LP/BP/notch/HP outputs + VC morph), Studio Electronics Boomstar SEM, G-Storm Electro SEMSVF (NOS FETs, CA3080), mLab SEM 12dB.

### DSP modeling references
- **Andrew Simper / Cytomic — "Linear Trapezoidal Integrated State Variable Filter With Low Noise Optimisation"** (cytomic.com/files/dsp/SvfLinearTrapOptimised2.pdf): trapezoidal integration, closed-form delay-free-loop solution, all outputs (LP/BP/HP/notch/peak/allpass) simultaneously. See also SvfLinearTrapAllOutputs.pdf.
- **Vadim Zavalishin — "The Art of VA Filter Design," Chapter 5 (2-pole filters)** — linear and nonlinear ZDF/TPT state-variable filter and multimode outputs; canonical TPT reference.
- **Hal Chamberlin — "Musical Applications of Microprocessors"** — the classic naive digital SVF; unstable above ~fs/6, superseded by TPT.
- **`juce::dsp::StateVariableTPTFilter`** — JUCE's built-in TPT state-variable filter (LP/BP/HP) — the natural starting core.
- "Improving the Chamberlin Digital State Variable Filter" (arXiv 2111.05592) explicitly references the SEM/ARP SVF lineage.

## Details — DSP modeling & JUCE/C++ implementation

**Core architecture.** Implement the linear core as a TPT/ZDF SVF (Zavalishin Ch. 5 or Cytomic). The standard one-sample update from input `v0` with integrator states `ic1eq`, `ic2eq`:

```cpp
g  = std::tan(juce::MathConstants<float>::pi * cutoff / fs); // prewarped
k  = 1.0f / Q;                       // damping (higher Q => more resonance)
a1 = 1.0f / (1.0f + g*(g + k));
a2 = g * a1;
a3 = g * a2;
v3 = v0 - ic2eq;
v1 = a1*ic1eq + a2*v3;               // BP
v2 = ic2eq + a2*ic1eq + a3*v3;       // LP
ic1eq = 2.0f*v1 - ic1eq;
ic2eq = 2.0f*v2 - ic2eq;
float HP = v0 - k*v1 - v2;
float BP = v1;
float LP = v2;
```

**Deriving the other modes.** Notch = LP + HP (= `v0 − k·BP`); Peak = LP − HP; Bandpass = `k·BP` (constant-peak) or `BP` (constant-skirt). This mirrors the SEM hardware, where the notch is the LP+HP sum.

**Continuous Mode morph.** Expose one `mode` parameter [0..1] mapping LP→notch→HP via a weighted sum of the simultaneous outputs (crossfade LP→notch for 0→0.5, notch→HP for 0.5→1). Provide a separate switch for the dedicated bandpass output, matching the SEM panel. Smooth `mode` with `SmoothedValue` to avoid zipper noise.

**Resonance — the key authenticity detail.** The SEM does NOT self-oscillate. Map resonance so maximum gives a strong but **finite** peak (Q clamped below threshold). Model the **feedback limiting diodes** as a gentle saturator (tanh or soft diode clipper) on the resonance/feedback signal — this both prevents oscillation and gives the SEM's characteristic resonance limiting/"bonk" under hard drive. Because it is 2-pole, there is **no bass-thinning gain-compensation issue** like the Moog.

**Nonlinearity.** Add subtle OTA-style saturation (`tanh`) at the integrator inputs and/or input stage, scaled to be mild at nominal levels. The feedback-loop diodes are arguably the dominant nonlinearity at high resonance.

**Oversampling.** The linear SVF needs none. With the tanh/diode nonlinearities, use **2×–4× oversampling** (`juce::dsp::Oversampling`); the SEM's gentle nonlinearity aliases far less than a saturating 4-pole Moog.

**JUCE specifics.** Start from `juce::dsp::StateVariableTPTFilter<float>` (LP/BP/HP) or roll your own to get simultaneous outputs + morph + nonlinearity (the built-in class exposes only one mode at a time). Use `prepare()/process()/reset()`, `SmoothedValue` for cutoff/resonance/mode, and `dsp::ProcessorChain`/`dsp::Oversampling` for the saturated path.

## Recommendations (staged build plan)
1. **Linear core (1–2 days):** Cytomic/Zavalishin TPT SVF; expose simultaneous LP/BP/HP. Verify 12 dB/oct slopes and cutoff against an analytic reference.
2. **Modes & morph:** Add notch (LP+HP) and peak; implement the continuous LP→notch→HP Mode knob + separate BP switch. Smooth parameters.
3. **Authentic resonance:** Clamp Q below self-oscillation; add the feedback-path diode/tanh limiter so resonance peaks and limits but never oscillates. A/B against SEM V / GForce SEM / e430 demos.
4. **Nonlinearity & oversampling:** Add mild OTA tanh saturation; add 2×–4× oversampling; tune drive to stay "creamy."
5. **Polish:** Input drive, optional component drift/detune for "vintage" feel, key-tracking, CV-style mod inputs for cutoff and mode.

**Thresholds that change the plan:** If it sounds too "clean/digital," the fix is almost always (a) the feedback-diode nonlinearity and (b) input-stage OTA saturation — not more poles. If it self-oscillates, your Q clamp/damping is wrong.

## Caveats
- **Verified:** topology (2-pole KHN SVF), 12 dB/oct, CA3080 OTAs + LM301/LM741/RC4558 + 2N4302 JFET buffers, non-self-oscillation, notch=LP+HP, no SEM filter patent, Colin as filter designer. Sourced to Synthesis Technology e430 docs, Sound Semiconductor AN701, Justia/Google Patents, Sound On Sound, ModWiggler/Gearspace/Studio Electronics, and Tom Oberheim interviews (Perfect Circuit, CDM/GForce).
- **Inference/community:** The CA3080 "clean" input range (~10–100 mV) is an engineer's forum recollection, not a measured SEM datapoint. IC substitutions across SEM revisions 1974–1979 are not well documented — treat "the SEM filter" as one canonical circuit; consult dated synthfool.com schematics for revision granularity. The relative weight of OTA distortion vs feedback-diode clipping in the SEM "sound" is debated.

---

# DOSSIER 2 — THE MOOG TRANSISTOR LADDER FILTER
*Specification dossier for a behavioral, analog-modeled DSP replica in JUCE/C++*

## TL;DR
- The Moog ladder is a **4-pole (24 dB/oct) transistor-ladder lowpass VCF**, patented by Robert A. Moog as **US 3,475,623** (filed Oct 10 1966, granted Oct 28 1969, **expired Oct 28 1986**) — the patent expiry unleashed the countless ladder clones; a DSP replica is legally clear.
- It uses four cascaded one-pole stages built from **NPN transistor differential pairs with shunt capacitors**, the transistor base-emitter junctions acting as voltage-variable resistors; cutoff is set by a control current (exponential CV-to-cutoff); a global feedback path sets resonance and **the filter DOES self-oscillate** (near-sine) at maximum resonance.
- Recommended DSP approach: a **nonlinear 4-stage model with per-stage tanh saturation and zero-delay feedback** — the **Huovilainen (DAFx 2004)** model or **Zavalishin** ZDF ladder (Art of VA Filter Design Ch. 4), with **2×–8× oversampling** and gain compensation for resonance bass-loss. In JUCE, `juce::dsp::LadderFilter` is a ready Moog-style multimode filter; roll your own for finer nonlinear control.

## Key Findings

### Patent / IP status
- **US Patent 3,475,623 — "Electronic High-Pass and Low-Pass Filters Employing the Base to Emitter Diode Resistance of Bipolar Transistors,"** inventor **Robert A. Moog.** Application US585601A filed **Oct 10, 1966**; granted **Oct 28, 1969**; US Cl. 307-233; **16 claims.** Under the pre-1995 17-year-from-grant term it **expired Oct 28, 1986** (now long expired). Wikipedia notes it was Moog's only patent.
- Abstract (verbatim): *"The dynamic base to emitter resistance of bipolar transistors is programmable over a wide range by varying the standing current in the transistors. Voltage programmable high-pass and low-pass RC filters are formed using a plurality of bipolar transistors connected to use the base to emitter diode resistance as the Rs of the filters."*
- **Effect of expiry:** the ladder became "the most-cloned filter in history." *During* the patent term, ARP's 4012 filter was found to infringe and was replaced (after a Moog–Pearlman conversation) by the redesigned 4072 filter in 1976/77; designers built diode-ladder and OTA variants partly to design around the patent. Per Moog Music's May 1, 2013 press release, Moog was *"posthumously inducted into the United States Patent and Trademark Office National Inventors Hall of Fame… for Patent No. 3475623, more commonly known to Moog fans as the Moog Ladder Filter."*
- Patent scans hosted at till.com (Don Tillman); full text at Google Patents (patents.google.com/patent/US3475623A).

### Verified technical / circuit details
- **Topology:** four cascaded one-pole (6 dB/oct each → **24 dB/oct** total) RC lowpass stages. Each stage is a transistor differential pair sharing a base voltage, with a **capacitor shunting the emitters**; the transistors' base-emitter ("diode") transconductance forms the variable R. The bottom **differential pair (Q1/Q2) is driven by a bias/control current `Ibias`** setting cutoff for the whole ladder; audio enters one side, **resonance feedback enters the other (Q2).** Output is taken across the top capacitor and recovered by an amplifier.
- **CV-to-cutoff:** cutoff is set by the bias current; with an exponential converter this yields the familiar exponential V/oct control. Per-stage cutoff `fc = Ibias / (8·π·VT·C)`.
- **Resonance & self-oscillation:** a global feedback path returns the (inverted) output to the input with gain `k`. At **k = 4** (100% feedback — needed because the feedback energy is spread across four 1-pole stages reaching 180° + inversion = 360°) the filter **self-oscillates, producing a near-perfect sine** at cutoff. Below 4 it gives classic resonant Q-enhancement.
- **Bass-thinning:** as resonance rises, low frequencies (which receive little phase shift) are cancelled by the fed-back inverted signal, so the **low end thins out** — a defining Moog trait. Designers add gain compensation to counter it (which also tames self-oscillation).
- **Minimoog "nonlinear resonance" trait:** on the original Minimoog/modular 904A, **resonance drops off at low cutoff frequencies**, keeping bass full and "spoinky." Moog viewed this as a "fault" and "fixed" it in later designs (Source, Voyager), which is partly why later Moogs sound different. The current-recovery amplifier in the feedback path (BJT in Model D vs JFET in early Model C) strongly affects resonant character.
- **Transistors:** the original 904A/Minimoog used matched NPN pairs; modern clones commonly use **2N3904** NPNs (and CA3046/CA3086 transistor arrays for matching). Matching is critical for symmetric response and clean resonance. **Temperature-dependent tuning:** because cutoff depends on `VT` (thermal voltage), the filter drifts with temperature — real units need tempco compensation.
- **Sound:** warm, fat, "creamy" — the canonical "Moog sound."

### Lineage
- **Moog modular 904A lowpass filter (mid-1960s)** → **Minimoog Model D filter (1970, slightly different/discrete)** → modern Moog reissues (Voyager, Sub 37, Minimoog reissue, Moog "The Ladder" pedal/Eurorack).
- The transistor ladder is the **most-cloned filter in history.** Contrasts: **diode-ladder** filters (EMS VCS-3/Synthi, Roland TB-303) where poles interact more (rubbery, edgy) vs the transistor ladder's better-isolated poles; **OTA/IC** lowpass cascades (Roland IR3109, CEM/SSM chips) popularized the 4-pole-LP sound in 80s polysynths. The Oberheim SEM (Dossier 1) is the 2-pole state-variable contrast.

### Existing emulations & DSP modeling (heavily studied)
- **Antti Huovilainen, "Non-Linear Digital Implementation of the Moog Ladder Filter," DAFx-04 (Naples, Oct 5–8 2004), pp. 61–64.** Abstract (verbatim): *"The analog circuit is analyzed to produce a differential equation. This equation is solved using Euler's method, and the result is shown to be equivalent to a cascade of first order IIR sections with embedded non-linearities. Finally, the filter structure is modified to improve tuning."* The seminal nonlinear "white-box" model, with **per-stage tanh** nonlinearities.
- **Tim Stilson & Julius O. Smith, "Analyzing the Moog VCF with Considerations for Digital Implementation," ICMC 1996.** Continuous-time analysis; notes the bilinear/backward-difference transforms create a delay-free loop needing an ad-hoc unit delay; identifies the constant-Q / decoupled-cutoff properties to preserve. Idealized/linear — the foundation everyone builds on.
- **Stefano D'Angelo & Vesa Välimäki, "An Improved Virtual Analog Model of the Moog Ladder Filter," ICASSP 2013** (Vancouver), and **"Generalized Moog Ladder Filter," Parts I & II (IEEE/ACM TASLP 22(12), 2014)** — a circuit-derived model discretized via bilinear transform, validated against SPICE, with a novel **delay-free-loop** explicit nonlinear implementation (Part II); the best-matching white-box model, surpassing Huovilainen.
- **Välimäki & Huovilainen, "Oscillator and Filter Algorithms for Virtual Analog Synthesis,"** Computer Music Journal — the simplified single-tanh nonlinear model + antialiasing.
- **Vadim Zavalishin, "The Art of VA Filter Design," Chapter 4 (Ladder filter)** — linear analog/digital model, feedback shaping, multimode ladder, simple & advanced nonlinear models, diode ladder, and the **nonlinear zero-delay-feedback equation** (iterative vs approximate). The canonical TPT/ZDF treatment.
- **Tim Stinchcombe, "Analysis of the Moog Transistor Ladder and Derivative Filters" (2008)** — exhaustive circuit analysis including nonlinearities.
- **ddiakopoulos/MoogLadders (GitHub)** — collected C++ implementations comparing Huovilainen, Stilson, Simplified (Välimäki/Huovilainen), Oberheim variation (Will Pirkle), RKSimulation (Runge-Kutta, Miller Puckette's bob~), Microtracker (Magnus Jonsson), Krajeski, Improved (D'Angelo-Välimäki), MusicDSP, Hyperion. The single best practical starting point; differential equations `y1' = k·(S(x − r·y4) − S(y1))`, etc.
- **Will Pirkle, "The Moog Ladder Filter (Biquad Style)" / Addendum A11** — practical notes on K=4 self-oscillation, bass decimation, and using a hard peak-limiter on the output (instead of waveshapers) to get pure sinusoids without oversampling.
- Commercial: **Arturia Mini V / Filter Mini**, **u-he Diva "Ladder"** (Minimoog-style, switchable 24/12 dB, ZDF + realtime circuit simulation), **Cytomic, Native Instruments**, and many more.

### DSP modeling reference — recommendation
For a behavioral replica that "sounds like the original," use the **Huovilainen nonlinear model (per-stage tanh) with 2× minimum, ideally 4–8×, oversampling**, OR the **Zavalishin nonlinear ZDF ladder** (better cutoff accuracy under modulation, no half-sample tuning hacks). Use **D'Angelo-Välimäki** for the closest match to a measured/SPICE ladder including distortion. The **linear Stilson-Smith / simplified models** are cheaper but sound more "digital."

## Details — DSP modeling & JUCE/C++ implementation

**Four cascaded one-pole stages.** Each stage is a leaky integrator (one-pole LP). In Huovilainen's formulation each embeds a `tanh` modeling the transistor pair's compression. Differential-equation form (per MoogLadders):
```
y1' = k·(S(x − r·y4) − S(y1))
y2' = k·(S(y1) − S(y2))
y3' = k·(S(y2) − S(y3))
y4' = k·(S(y3) − S(y4))
```
where `k` sets cutoff, `r` is feedback (≤4 for stability), `S(·)` is a saturator (tanh). Output is `y4` (24 dB/oct LP); intermediate taps give 6/12/18 dB/oct and pole-mixing modes.

**Zero-delay feedback.** The feedback `x − r·y4` is an instantaneous loop. Either (a) insert a unit delay (Stilson-Smith ad-hoc delay — simple, tuning shifts at high res), or (b) resolve the delay-free loop properly (Zavalishin ZDF: closed-form linear solve each sample, or Newton iteration when nonlinear). The ZDF/TPT route gives accurate cutoff under fast modulation and stable high-resonance behavior.

**tanh placement — per-stage vs input-only.** Per-stage tanh (Huovilainen) is most authentic (each transistor pair saturates) but costs 4+ `tanh`/sample; the simplified Välimäki-Huovilainen model uses a single nonlinearity (cheaper, still "warm"); input-only saturation is cheapest but least authentic.

**Resonance & self-oscillation.** Scale feedback so `k`/`r` reaches the self-oscillation threshold (≈4) at max resonance, giving a clean near-sine. Pirkle's tip: a hard peak-limiter on the output tames oscillation to a pure sinusoid without oversampling; alternatively let the tanh limit (needs oversampling).

**Gain compensation (bass-loss).** As resonance rises the passband/bass drops. Add optional input gain compensation (e.g. scale input by `1 + resonance` or a tuned curve) — but note real Minimoogs don't fully compensate (bass thins), and the low-cutoff resonance roll-off is part of the charm. Make compensation optional/tunable to choose "Minimoog" vs "corrected" character.

**Exponential cutoff mapping.** Map MIDI note / cutoff knob exponentially to Hz (V/oct), then to the coefficient `g = tan(π·fc/fs)` (TPT) or `k`. Prewarp the cutoff for correct frequency at high fc.

**Thermal/tuning.** Optional: model slight cutoff drift / per-stage detune ("spread" parameter, per D'Angelo) for analog realism; not required for core sound.

**Oversampling.** Nonlinear tanh stages generate harmonics that alias. Use **2×–8× oversampling** (`juce::dsp::Oversampling`); 4× is a good default for self-oscillation and overdrive. Higher fc + drive needs more.

**JUCE: built-in vs roll-your-own.**
- **`juce::dsp::LadderFilter<SampleType>`** is a ready Moog-style multimode filter with modes **LPF12, HPF12, BPF12, LPF24, HPF24, BPF24**, plus **`setCutoffFrequencyHz()`, `setResonance()` (0–1), `setDrive()` (saturation), `setMode()`, `setEnabled()`** and `prepare()/process()/reset()`. It is a TPT/ZDF Moog-style ladder with a drive parameter — excellent and CPU-cheap. (Cutoff and resonance are internally smoothed but drive is not — smooth drive yourself to avoid zipper noise.)
- **Roll your own** (Huovilainen/Zavalishin) when you need: authentic per-stage tanh voicing, true self-oscillation to a clean sine, the Minimoog low-cutoff resonance roll-off, custom gain-compensation curves, or pole-mixing/multimode beyond the six built-in modes.

## Recommendations (staged build plan)
1. **Baseline (hours):** Drop in `juce::dsp::LadderFilter`, mode LPF24, wire cutoff/resonance/drive with `SmoothedValue`. Confirm 24 dB/oct slope, self-oscillation near max resonance, Moog-ish drive. Your reference + fallback.
2. **Custom linear ladder:** Implement four TPT one-pole stages + ZDF feedback (Zavalishin Ch. 4 linear). Match cutoff/Q to step 1.
3. **Nonlinear voicing:** Add per-stage tanh (Huovilainen) or single-tanh (simplified). Add 4× oversampling. Tune drive. A/B against Minimoog/Diva Ladder demos.
4. **Self-oscillation & bass character:** Calibrate feedback to clean self-oscillation (≈k=4); implement optional gain compensation and an optional "Minimoog" low-cutoff resonance roll-off.
5. **Polish:** Exponential V/oct cutoff + key tracking, optional thermal drift/stage spread, pole-mixing multimode outputs, input drive stage.

**Thresholds that change the plan:** If CPU-bound, drop to single-tanh + 2× OS or use the built-in LadderFilter. If it sounds "digital/cold," add per-stage tanh and ensure ZDF (not ad-hoc delay). If self-oscillation aliases, raise oversampling or add Pirkle's output limiter. If bass feels weak vs a real Minimoog, reduce/disable gain compensation.

## Caveats
- **Verified:** patent 3,475,623 details (number, Oct 10 1966 filing, Oct 28 1969 grant, Oct 28 1986 expiry, 16 claims, abstract, Moog's only patent), 4-pole/24 dB transistor-ladder topology, base-emitter-as-resistor mechanism, control-current cutoff, k≈4 self-oscillation to sine, resonance bass-thinning, Minimoog low-cutoff resonance roll-off, 2N3904/CA3046 in clones, the DSP papers (Huovilainen, Stilson-Smith, D'Angelo-Välimäki, Zavalishin) and JUCE LadderFilter API. Sourced to Google Patents/till.com, Moog Music press release, All About Circuits, Sound On Sound, DAFx/ICASSP/IEEE papers, MoogLadders repo, JUCE docs, Will Pirkle.
- **Inference/community:** Exact original Minimoog transistor part numbers vary by source/era; "2N3904" is the common modern-clone choice, not necessarily the 1970 original. The ARP 4012→4072 timeline is from secondary sources. The relative audibility of per-stage vs single tanh is a modeling judgment, not a measured fact.