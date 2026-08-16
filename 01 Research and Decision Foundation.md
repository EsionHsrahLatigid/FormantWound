# FormantWound Research and Decision Foundation

## Research question

How should FormantWound mutate source-filter speech cues into a harsh, controllable audio effect without collapsing into silence, DC lock, or unstable feedback?

## Request type

Comprehensive research: implementation reference lookup plus current best-practice evidence for a chosen JUCE effect design.

## Primary sources

- ITU-T Recommendation G.729 (06/2012), "Coding of speech at 8 kbit/s using conjugate-structure algebraic-code-excited linear prediction (CS-ACELP)". https://www.itu.int/rec/T-REC-G.729-201206-I/en
  - Establishes a production speech-coding source/filter precedent around linear prediction, algebraic excitation, bounded frame updates, and reference implementation/test-vector discipline.
- B. S. Atal and M. R. Schroeder, "Adaptive Predictive Coding of Speech Signals", *Bell System Technical Journal* (1970). DOI: https://doi.org/10.1002/j.1538-7305.1970.tb04297.x
  - Establishes adaptive linear prediction for speech signals, periodic coefficient readjustment, and residual transmission as the conceptual basis for exposing LPC order/resolution.
- A. V. Oppenheim, "Speech Analysis-Synthesis System Based on Homomorphic Filtering", *The Journal of the Acoustical Society of America* (1969). DOI: https://doi.org/10.1121/1.1911395
  - Supports separating broad spectral envelope from excitation-like detail through cepstral/homomorphic analysis-synthesis.
- DAFX chapter 9, "Source-filter Processing". https://www.dafx.de/DAFX_Book_Page/chapter9.html
  - Frames source-filter separation, LPC, cepstrum, spectral envelope mutation, and formant changing as audio-effect techniques.
- Lawrence R. Rabiner, "Digital-formant synthesizer for speech-synthesis studies", *The Journal of the Acoustical Society of America* (1968). PubMed: https://pubmed.ncbi.nlm.nih.gov/5645831/ DOI: https://doi.org/10.1121/1.1910901
  - Establishes the classic digital formant synthesizer as a source-filter system and explicitly discusses serial vs. parallel synthesizer tradeoffs.
  - The summary notes voiced and unvoiced excitation, a voice-bar path for stop closures, and a simple higher-pole correction network.
- "Learning and controlling the source-filter representation of speech with a variational autoencoder" (*Speech Communication*, 2023). https://www.sciencedirect.com/science/article/pii/S0167639323000304
  - Confirms that source-filter speech can be treated as independently controlled latent factors, with the fundamental and formant frequencies represented in orthogonal subspaces.
  - Useful as a modern control-model reference for separating excitation from vocal-tract mutation.
- "Formant normalisation for speech recognition and vowel studies" (*Speech Communication*, 1991). https://www.sciencedirect.com/science/article/abs/pii/0167639391900504
  - Shows that Bark-scaled formants are a standard way to reason about speaker-normalized vowel space.
  - Supports Bark-domain interpolation rather than direct Hz interpolation for the mutated tract view.
- "Non-hexagonal neural dynamics in vowel space" (2020). https://pmc.ncbi.nlm.nih.gov/articles/PMC7519971/
  - Gives an explicit Bark conversion and demonstrates vowel trajectories in a Bark-space geometry.
  - Useful for continuous vowel-path interpolation and for a compact visual display of the formant map.
- Ranniery Maia, Masami Akamine, Mark J. F. Gales, "Complex cepstrum analysis based on the minimum mean squared error" (ICASSP 2013). https://doi.org/10.1109/ICASSP.2013.6639217
  - Supports cepstral source/filter separation and accurate reconstruction at fixed periods.
  - Good evidence for a cepstral smoothing / envelope mutation stage that stays deterministic.
- JUCE `AudioProcessor` reference. https://docs.juce.com/master/classjuce_1_1AudioProcessor.html
  - Documents `prepareToPlay`, `processBlock`, `processBlockBypassed`, `setLatencySamples`, and the realtime warning that the audio callback must not touch the UI.

## Decision map

### 1. Core topology

Use a frame-based source/filter mutator:

1. analyze the input with a bounded LPC / envelope stage;
2. derive a cepstral or smoothed spectral-envelope view;
3. warp the envelope in Bark space;
4. rebuild the result through a small stable resonator bank;
5. feed it with the input residual plus a bounded internal exciter when the active-input path would otherwise collapse.

The product should read as "speech anatomy pushed into violent terrain", not as a generic vocoder.

### 2. LPC and resonator shape

- Start from a shallow LPC order, then collapse the result to a small set of dominant formant poles.
- Prefer 3 or 4 formant bands plus one optional antiresonant notch over a large direct-form lattice.
- Keep every pole radius below 1.0 with a measurable guard band; the default pole clamp should leave headroom for time-varying jitter.
- Treat the tract as a mutable shape, not as a fixed speech identity. The plugin should work on speech, drums, noise, and mixed program material.

### 3. Cepstral mutation

- Use cepstral smoothing or liftering to separate broad envelope from fine excitation detail.
- Interpolate the mutation in a perceptual space first, then convert back to Hz for the resonator implementation.
- Keep the cepstral path fixed-size and allocation-free in the audio callback.
- If an FFT-backed helper is used, it must be proven safe for the target platforms and must not introduce hidden locks or allocations.

### 4. Bark-domain interpolation

- Warp the formant centers and spacing in Bark space instead of plain Hertz.
- This preserves a smoother perceived motion when the tract is moved between neutral, nasal, and exaggerated states.
- Use the Bark-space form as the UI-facing "shape" abstraction and convert back to filter coefficients at frame boundaries.

### 5. Safety and audibility

The plugin must not fail by:

- collapsing into silence after active input;
- pinning to DC or a single rail;
- producing NaN/Inf;
- becoming ultrasonic-only;
- masking all motion behind over-smoothing.

The output strategy should therefore include:

- bounded pole radii and bounded bandwidth;
- a DC blocker on the reconstructed path;
- finite guards on every coefficient update;
- a recent-input activity latch that can inject a low-level bounded exciter when the active path collapses;
- a final output trim or ceiling.

## Implemented parameter set

This is the current implementation contract.

| Parameter | Range | Default | Role |
|---|---:|---:|---|
| `resolution` | `0.0 .. 1.0` | `0.55` | LPC order from 4 to 16 coefficients |
| `excitation` | `0.0 .. 1.0` | `0.45` | Residual versus deterministic noise/impulse corruption |
| `warp` | `0.0 .. 1.0` | `0.52` | Formant-envelope coefficient fold around neutral |
| `freeze` | `off/on` | `off` | Hold the last analyzed tract while excitation continues |
| `reseed` | `0.0 .. 1.0` | `0.0` | Deterministically reseed excitation corruption |
| `damage` | `0.0 .. 1.0` | `0.35` | Coefficient quantization, saturation, and collapse pressure |
| `feedback` | `0.0 .. 0.92` | `0.18` | Bounded wet feedback into the source-filter path |
| `mix` | `0.0 .. 1.0` | `1.0` | Wet mix |
| `output` | `-24 dB .. +12 dB` | `0 dB` | Final trim |

## UI direction

- Use the shared `juce-ehl-design-module`.
- Keep the editor monochrome, compact, and operational.
- Use the canonical short `ehl` mark in the header.
- Prefer one compact tract display and one small residual activity readout over decorative meters.
- Keep the formant map legible at the minimum editor size; the 8-bit UI should communicate motion and state, not atmosphere.

## Anti-collapse test plan

The plugin should be validated against:

- impulse and burst input at `44.1`, `48`, and `96 kHz`;
- voiced speech, vowel sweeps, sibilant noise, pink noise, and sparse transient material;
- block sizes `1`, `17`, `64`, `256`, and `512`;
- repeated state restore / reset cycles;
- extreme parameter combinations;
- deterministic same-seed render equality;
- RMS, peak, crest, DC share, zero-crossing count, and active-window counts;
- finite-output and non-silent-output assertions under active input;
- bypass and latency-compensation checks via `processBlockBypassed` and `setLatencySamples`.

## Risks and open questions

- Exact LPC order and window length are not locked yet.
- Whether the active-input recovery exciter should be fully automatic or exposed as a user-visible control.
- Whether the final tract should report a latency value if the analysis path adds an alignment delay.
- Whether the antiresonance should be static or adapt to the evolving tract.
- Whether the cepstral path should be implemented with a custom fixed-size helper or a separately proven FFT backend.

## Reusable takeaway

FormantWound should be a source-filter mutator: LPC for tract extraction, cepstrum for envelope shaping, Bark-space interpolation for perceived movement, and a bounded exciter / trim path so aggressive settings stay audible without turning into dead silence or unstable rails.
