# The Huggett Filter in the Novation Summit & Peak — A Research Dossier for a JUCE/C++ Behavioral Replica

> **Provenance:** external research dossier, ingested 2026-06-17. Companion to the primary [huggett-filter.md](huggett-filter.md) and the [moog-ladder.md](moog-ladder.md) brief. It is the **source material for Plan 2** (Huggett nonlinear stages). Body verbatim except this header.

TL;DR


The Summit/Peak filter is a per-voice analog state-variable filter built around OTAs (operational transconductance amplifiers, almost certainly the LM13700), directly derived from the Novation Bass Station II "Classic" filter, which is itself a descendant of Chris Huggett's OSCar dual-filter design. It offers switchable LP/BP/HP at 12 dB and 24 dB/oct, self-oscillates in all modes, and gets its character from a pre-filter overdrive and a post-filter distortion stage. The correct DSP modeling approach is a nonlinear zero-delay-feedback (TPT) state-variable filter with saturating (tanh/asymmetric) nonlinearities in the integrators and resonance path, plus oversampling.
No patents exist. An exhaustive search (Google Patents, Justia, USPTO full-text, Espacenet/UKIPO web records) found no patent or application naming Chris Huggett as inventor for any filter or oscillator, and nothing covering the filter circuit assigned to Oxford Synthesiser Company, Novation, or Focusrite. The only synth-related Focusrite IP is industrial-design (enclosure) patents D939620/D939621 and one unrelated keyboard-button utility patent (US 12,211,658, inventor Ryan Gray). Huggett's designs are, on all available evidence, entirely unpatented.
To "sound like the original," the spec must reproduce three things beyond a textbook linear filter: (1) the two-pole-per-section state-variable topology with mode switching and the Summit's dual-filter routings with Separation; (2) the resonance behavior (musical self-oscillation, pitch-tracking over four-plus octaves, increasingly spiky/aggressive resonance especially in 24 dB LP); and (3) the multi-stage analog nonlinearities (pre-filter overdrive, post-filter distortion, VCA/master distortion), modeled with antialiasing.


Key Findings

1. Patent / IP status — definitively unpatented


A systematic search across Google Patents, Justia, USPTO full-text, and web-indexed Espacenet/UKIPO records returned zero patents or published applications naming Christopher/Chris Huggett as inventor for any synthesizer filter, oscillator, or the "Oxford Oscillator."
No filter/oscillator IP is assigned to Oxford Synthesiser Company, Novation Digital Music Systems, or Focusrite Audio Engineering. The entire Focusrite patent portfolio (per the Justia assignee listing) consists of one utility patent — US 12,211,658, "Audio keyboard button with varying output" (inventor Ryan Gray; filed Oct 9, 2020, granted Jan 28, 2025) — plus roughly 19 design patents covering enclosure appearance, including two synthesizer-housing designs (D939620 and D939621, both granted Dec 28, 2021, naming the industrial-design team Krischa Tobias, Peter Phillips, et al. — not Huggett, and not the circuit). justiajustia
Unrelated namesakes correctly excluded: Anthony Richard Huggett (imaging/semiconductors), Raymond Huggett (vibration monitoring), David J. Huggett (fluid filters). Justia Patents
This is consistent with biographical accounts: collaborator Anthony Harrison-Griffin described the Wasp as "designed on the back of a fag packet… The only drawing that was ever done was Chris's of the circuitry," and the Sound On Sound obituary recounts Huggett freely providing the Wasp schematic. The Wasp filter was later openly cloned commercially (Doepfer A-124, Behringer's Wasp reissue) without any patent obstacle. Reverb News
Implication for the user: there is no patent document to mine for the exact circuit, and equally no patent barrier to building a behavioral replica. Reverse-engineering must rely on service manuals, teardowns, the closely-related Bass Station II circuit, and DSP literature.


2. Lineage — one continuous filter philosophy, 1978 → 2019

Chris Huggett (1949–22 October 2020) carried a single recognizable filter philosophy across four decades: Wikipedia


EDP Wasp (1978): a state-variable filter implemented with OTAs (CA3080) as the cutoff-control elements and CMOS inverters (CD4069) used in lieu of op-amps, on a unipolar supply — famous for "dirty," erratic nonlinear behavior. This is documented rigorously in Köper, Holters, Esqueda & Parker, "A Virtual Analog Model of the EDP Wasp VCF," Proc. 25th Int. Conf. on Digital Audio Effects (DAFx20in22), Vienna, September 2022, which describes "a state variable filter topology implemented using operational transconductance amplifiers (OTAs) as the cutoff-control elements and CMOS inverters in lieu of operational amplifiers, all powered by a unipolar power supply," with a schematic "based on the Doepfer version… [that] follows Huggett's original design." The paper measures (Fig. 6) the "nonlinear current-voltage relationship of the CA3080 OTA… typically modeled using a hyperbolic tangent." Dafx + 3
OSC OSCar (1983): two 12 dB/oct analog filters that can run in series (for 24 dB) with a unique Separation control offsetting the two cutoff frequencies, plus a filter overdrive that produced the OSCar's famously aggressive lead tone (e.g., Ultravox's Billy Currie patches modeled on an ARP Odyssey through a distortion pedal). This is the direct conceptual ancestor of the Summit's dual filter. Sound On Sound
Novation Bass Station / Bass Station II (1993 / 2013): the BS2 "Classic" filter is a state-variable, switchable LP/BP/HP, 12/24 dB design "designed by Chris Huggett of Wasp and OSCar fame," built on LM13700 OTAs with a pre-filter overdrive and a separate post-filter distortion. (A second "Acid" filter — a TB‑303‑style diode ladder — is BS2‑only and is not present in Peak/Summit.)
Novation Peak (2017) / Summit (2019): Novation and reviewers state explicitly that the Peak/Summit filter is based on the Bass Station II filter topology, with "roots in the legendary OSCar, via Bass Station II." B&H's hands-on Peak review states verbatim: "Peak's 100% analog multimode filter is based on the Bass Station II filter, but with increased resonance and adjustable key tracking." B&H eXplora


3. Summit/Peak filter — verified technical details


Topology: Analog state-variable filter (SVF) using OTAs, one independent filter per voice (8 in Peak, 16 in Summit). Novation's own specification (as carried by B&H) describes "an analog, multimode, state-variable OTA filter with high-pass, band-pass, low-pass, and dual-filter selections with 12/24 dB slopes."
Modes & slopes: Low-pass, band-pass, high-pass; selectable 12 dB/oct and 24 dB/oct. The 24 dB mode is two 12 dB stages in series (consistent with BS2 and OSCar).
Summit's dual filter: Summit adds a dual-filter mode with nine LP/HP/BP combinations and separation between the two 12 dB filters' cutoffs. Novation's spec lists the routings verbatim: "LP to HP, LP to BP, HP to BP, LP + HP, LP + BP, HP + BP, LP + LP, BP + BP, and HP + HP" (series "→/to" and parallel "+" routings). This is the OSCar Separation concept generalized.
Self-oscillation: Yes, in all modes. Sound On Sound's Peak review: "In low-pass mode at 24dB per octave, its high resonance is scarier than that of the Bass Station II — it spits like an acidic cobra!" and "Resonance happily self-oscillates with all mixer levels at zero, the resulting tone tracking pretty well over at least four octaves." The same review notes the 12 dB option "supplies a more even response… [with] less intimidating resonance," while band- and high-pass at 24 dB are praised for "liquid and musical resonance." Sound On Sound + 2
Keyboard tracking: Filter cutoff tracks the keyboard; resonance self-oscillation tracks pitch over four-plus octaves; Peak/BS2 expose adjustable filter tracking.
Drive/nonlinearity — three analog distortion stages per voice:

Pre-filter overdrive (post-mixer, front-panel "Overdrive/Distortion" into the filter input) — ranges from mild thickening to serious analog distortion.
Post-filter distortion ("Filter Post Drive," between filter and VCA).
Post-VCA / master analog distortion at the end of the analog chain; the VCA itself can be gained up to overdrive.



Community testing (Gearspace) notes the resonance interacts with drive: at high resonance + low cutoff, audible "blown out" distortion appears in the resonance peak, increasing as oscillator/VCA levels are cranked. Novation confirmed master volume/VCA gain scale level (and noise floor) but the tonal distortion comes from the dedicated drive stages and internal clipping.



Cutoff modulation: Oscillator 3 can FM the filter cutoff at audio rate (per voice); noise can modulate cutoff via the mod matrix. A per-voice Diverge parameter randomizes each voice's cutoff slightly to emulate analog spread (paired with oscillator Drift).
Oscillators (context, not the filter): the "New Oxford Oscillators" are digital NCOs generated on an FPGA. Per Novation's spec (via B&H), they are "FPGA-based numerically controlled oscillators (NCOs) running at 24 MHz," and "Each of Peak's 16 voices has an independent oversampling digital-to-analog converter (DAC)… oversampling at over 24 MHz," feeding a simple RC reconstruction filter into the analog filter. The analog filter and VCAs are the only analog parts of the voice.


4. The exact filter IC — strong inference, not teardown-confirmed


No published photographic teardown of the Peak/Summit voice card naming the filter chip was located. This is the single most important unverified hardware fact.
Strongest available evidence: Novation states the Peak/Summit filter is based on the Bass Station II filter; the BS2 "Classic" filter core is built on LM13700 dual OTAs (documented in detailed BS2 mod/repair write-ups, with HC4051 analog switches for mode selection, op-amps for mixing/buffering, and resonance realized per‑LM13700). MOD Wiggler forum consensus is explicit: "Bass Station 2 uses LM13700, I'd be surprised if Peak/Summit are different… Novation says the filter is based on the Bass Station filter." Maffez
Conclusion: The Peak/Summit filter very likely uses LM13700 (or an equivalent dual OTA such as the behaviorally similar CA3280 class) in a 2‑pole SVF per stage, two stages cascaded for 24 dB, with CMOS analog-switch mode selection — exactly as in BS2. Treat this as the best-supported hypothesis, not as teardown-verified fact.


5. The OSCar reference filter


The OSCar uses two 12 dB/oct analog filters, cascadable in series (24 dB) and offset via the Separation control, plus a filter overdrive that defines its aggressive lead character.
Chip identity: Community schematic redraws exist (the Osamu/Sam Hoshuyama OSCar VCF redraw, referenced by BS2 modders). G‑Storm Electro's "GODSPEar" Eurorack clone of the OSCar filter is, per G‑Storm's own description, built to retain "the original core topology, a full analog signal path, NOS NJM13600 OTAs, as well as V/Oct filter tracking… It is actually TWO FILTERS each w/ 2‑Pole cutoff slope" with adjustable Separation. The NJM13600 is an LM13600/LM13700‑class dual OTA — strong corroboration that the OSCar filter core is OTA-based state-variable, not a CEM/SSM-chip filter. Claims online that the OSCar filter used CEM/Curtis or SSM chips are not supported by the clone evidence; the OTA (13600-family) attribution is better supported. MATRIXSYNTH
Software model precedent: GForce's impOSCar (2004; now impOSCar3) models "two 12dB analog filters running in series," with Drive, Cutoff, Q, and Separation across nine filter combinations. GForce describes the filter modeling as exceptionally difficult: "The OSCar, with its two 12dB analog filters running in series, its filter overdrive and sublime separation control was not a job for some… normal off-the-shelf filter algorithms." GForce further states these modeled filters were respected enough that "Spectrasonics licensed them for use in their wonderful Stylus RMX" (and the Omnisphere "Power Filter" is "based on their famous impOSCar filter design"). This validates that an authentic recreation hinges on the series-12 dB structure, Separation, and the overdrive nonlinearity. zZounds + 6


6. PWM Mantis (Huggett's last design) — a useful cross-check


The Mantis (2023; completed by PWM after Huggett's death) uses a per-voice analog multimode state-variable filter "based on the OSCar." Per PWM's official spec: "The Filter is a multi-mode state variable VCA Filter with overdrive. It is built using the SSI2164 chip in special configuration designed by Chris Huggett. There are two filters — one for each analogue signal path… Low Pass / Band Pass / High Pass in 12dB and 24dB configurations — Additional WBAND (series) and WSTOP (parallel) types." This confirms Huggett's late SVF implementations could be realized with SSI2164 VCA cells (an OTA-like building block) — it confirms the topology family (OTA/VCA-cell state-variable with series/parallel dual filtering and overdrive) but does not by itself prove the Peak/Summit's specific silicon. zZounds


Details — DSP Modeling and JUCE/C++ Implementation Guidance

Recommended core: nonlinear zero-delay-feedback (TPT) SVF

The analog circuit is a 2‑pole state-variable filter; the correct digital analog is a topology-preserving transform (TPT) / zero-delay-feedback (ZDF) SVF solved with trapezoidal integration. Primary references:


Vadim Zavalishin, The Art of VA Filter Design — the definitive text. The 2‑pole/SVF chapter gives the linear TPT SVF; later sections cover nonlinear zero-delay-feedback equations, iterative vs. approximate solving, saturation in filters, antisaturators, asymmetric saturation, and antialiasing of waveshaping — precisely the toolkit for the drive stages and nonlinear resonance.
Andrew Simper / Cytomic technical papers — "Linear Trapezoidal Integrated State Variable Filter" (SvfLinearTrapOptimised / SvfLinearTrapOptimised2), plus the simultaneous-all-outputs and input-mixing variants. These give compact, proven coefficient formulas (g = tan(π·fc/fs); the SVF computes LP/BP/HP simultaneously). A ready C++ port exists (Fred Anton Corvest, "Common-DSP" on GitHub).
JUCE built-in: juce::dsp::StateVariableTPTFilter is a 12 dB/oct TPT SVF (LP/BP/HP) explicitly "based on the analog state variable filter circuit" and on Zavalishin's TPT structure — usable as the linear core or reference, but it is linear, so it must be extended with nonlinearities to capture Huggett character. JUCE
Hal Chamberlin SVF and the arXiv paper "Improving the Chamberlin Digital State Variable Filter" are useful background, but the TPT/ZDF form is preferred for stability under fast modulation and high resonance.


Mapping circuit → DSP


Two-pole SVF core: Implement one TPT SVF yielding LP, BP, HP simultaneously. The mode switch selects the output (or blends); 24 dB modes cascade two cores in series (mirroring the two hardware 12 dB stages).
Separation (Summit dual filter): Run two independent 2‑pole cores with independently offset cutoffs (Separation = the offset/ratio between the two cutoffs) and provide the nine series/parallel routings. Series routings feed core1 → core2; parallel routings sum the two outputs.
Resonance / self-oscillation: In the ZDF SVF, resonance is set by the damping term (R, where Q = 1/(2R)). For authentic self-oscillation, push R toward 0 and place a saturating nonlinearity in the resonance/feedback path so oscillation amplitude limits gracefully instead of diverging — reproducing the OTA's tanh-like limiting. Base this on Zavalishin's nonlinear ZDF treatment and the OTA tanh model below.
OTA nonlinearity: OTAs (LM13700/CA3080/NJM13600 class) have a tanh transconductance characteristic; the DAFx Wasp paper measured the CA3080's saturating I–V curve and models it with a hyperbolic tangent. Apply tanh-style saturation (e.g., iout = Is·tanh(vin/(2·Vt))) on the integrator inputs and in the resonance feedback to capture the "spitting"/aggressive resonance and the way resonance distorts as it approaches self-oscillation.
Drive stages (the heart of the "sound"): Implement three separately addressable waveshaping stages:

Pre-filter overdrive: asymmetric soft-clipper (tanh or a diode-style asymmetric shaper) on the mixer sum before the SVF input; drive maps to input gain into the shaper.
Post-filter distortion: second waveshaper between filter output and VCA.
Post-VCA / master distortion: final clipper; also model that high resonance + chords can internally clip (a shared stage overdriven by the sum of voices).



Asymmetry matters — Huggett-family filters are described as "dirty"/edgy rather than symmetric/warm; use asymmetric saturation (Zavalishin's "Asymmetric saturation" section).



Antialiasing: Nonlinear stages generate aliasing harmonics. Use juce::dsp::Oversampling (2×–4× for subtle drive, 8× when drive is heavy) around the nonlinear filter+drive block; choose FIR equiripple for linear phase or polyphase IIR for low latency. Zavalishin's "antialiasing of waveshaping" and antiderivative antialiasing (ADAA) are complements for the static shapers.
Per-voice variation (Diverge/Drift): Randomize each voice's cutoff and resonance (and optionally each voice's drive trim) by small amounts to reproduce the analog spread Novation deliberately added.
Cutoff range & tracking: Map cutoff across the full audio range (~20 Hz to ~16–20 kHz) with an exponential/voltage-like control law; implement keytrack as a 0–100% scaling against MIDI note; implement audio-rate cutoff FM from oscillator 3 and noise → cutoff. Use prewarped g per sample for clean fast modulation (TPT handles this well).


Suggested signal chain for the replica (per voice)

mixer (osc1..3 + noise + ring) → pre-filter overdrive (asymmetric shaper) → SVF stage 1 (2‑pole, TPT, nonlinear feedback) → [for 24 dB or dual modes: SVF stage 2 with independent cutoff offset = Separation] → mode select / route (series or parallel) → post-filter distortion → VCA (with optional overdrive) → master distortion → FX. Wrap the overdrive + SVF region in oversampling.

Parameters to expose (from hardware)


Cutoff: full-range, exponential mapping; keytrack 0–100%.
Resonance: 0 → self-oscillation (clamp damping so it sings rather than diverges).
Slope: 12 / 24 dB (one vs. two cascaded stages).
Mode: LP / BP / HP (single), plus Summit's nine dual routings + Separation.
Drive (pre), Distortion (post-filter), and master/VCA drive: independent.
Diverge/Drift: per-voice cutoff/resonance randomization.


Recommendations

Stage 1 — Build the linear skeleton (validate first).
Implement a TPT SVF per Cytomic/Zavalishin (or start from juce::dsp::StateVariableTPTFilter). Verify LP/BP/HP magnitude responses and 12/24 dB slopes against analytic curves; the 24 dB LP should show a ~−24 dB/oct stopband and a resonant peak reaching self-oscillation. Get the dual-filter routings and Separation working. Change trigger: if modulation produces zipper noise or instability at high resonance, you have not implemented true ZDF — fix before adding nonlinearity.

Stage 2 — Add the nonlinearities (this is where it becomes a Huggett filter).
Insert tanh/asymmetric saturation into the integrator inputs and resonance feedback; add the three drive shapers. Tune the resonance-path saturation so 24 dB LP self-oscillation is aggressive/"spitting" while 12 dB is tamer and BP/HP at 24 dB are "liquid/musical" — matching the Sound On Sound descriptions. Add oversampling (start at 4×). Benchmark: A/B against your own Summit — single saw, cutoff ~50%, resonance to self-oscillation, sweep; then add pre-drive and listen for the "blown-out" resonance at low cutoff with chords.

Stage 3 — Calibrate against the real instrument (you own a Summit).
Capture reference recordings: (a) self-oscillation pitch vs. note (verify four-plus-octave tracking), (b) resonance sweeps at 12 vs. 24 dB in each mode, (c) drive at increasing levels with fixed cutoff/resonance, (d) dual-filter Separation sweeps. Match cutoff calibration, the onset point of self-oscillation, and the spectral signature of the drive stages. Change trigger: if your model's distortion is symmetric/"warm" where the Summit is edgy/asymmetric, increase shaper asymmetry; if aliasing appears on bright drive, raise oversampling to 8×.

Stage 4 — Per-voice realism & polish.
Add Diverge/Drift randomization, audio-rate osc3 → cutoff FM and noise → cutoff, and the master distortion. Expose all parameters above with smoothing.

On the chip question: Proceed on the LM13700-class OTA SVF assumption (best-supported by the BS2 lineage). If you want certainty, photograph your own Summit's voice card / filter ICs (or consult a Novation service manual) before finalizing the OTA saturation constants — this is the one fact worth verifying physically. The GForce impOSCar and the G‑Storm GODSPEar (NJM13600) are the best external sonic references for the OSCar end of the lineage; the Bass Station II circuit (LM13700, HC4051) is the best circuit reference for the Peak/Summit end.

Caveats


Exact filter IC is inferred, not teardown-verified. No photographic teardown naming the Peak/Summit filter chip was found. The LM13700 attribution rests on Novation's "based on Bass Station II" statements plus the documented BS2 circuit and forum consensus. Treat as high-probability, not certain.
"State-variable OTA" is well-corroborated across Novation's own copy, Vintage Synth Explorer, Polynominal, B&H, MusicRadar, and Sound On Sound — but several of these partly echo Novation's marketing; the deepest independent circuit detail comes from the closely-related Bass Station II, not the Peak/Summit board itself.
OSCar filter chip: community and clone evidence (Hoshuyama redraw; GODSPEar's NJM13600) points to OTA (13600-family) cores, contradicting some online claims of CEM/SSM chips. Treat the CEM/SSM claims as likely incorrect for the OSCar filter.
No patents means no authoritative circuit disclosure exists; all topology detail is reverse-engineered or inferred from related products.
Marketing vs. measured: specifics such as "NCOs running at 24 MHz / oversampling DACs over 24 MHz" and "self-oscillates in all modes" come from Novation and reviewers; the qualitative resonance/drive descriptions ("spits like an acidic cobra," "blown out") are reviewer/user impressions — valuable as voicing targets but not laboratory measurements.
The PWM Mantis SSI2164 detail and the OSCar NJM13600 detail confirm the topology family (OTA/VCA-cell state-variable with overdrive and series/parallel dual filtering) but do not by themselves prove the Peak/Summit's specific silicon