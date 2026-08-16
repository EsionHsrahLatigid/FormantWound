# FormantWound

FormantWound is an EsionHsrahLatigid real-time LPC/source-filter destruction effect for JUCE.

It analyzes the incoming signal as a bounded linear-predictive spectral envelope, corrupts the residual/excitation path, warps the reconstructed tract, and keeps hostile settings audible without allowing NaN, DC rail, clipped-constant, denormal, or ultrasonic-only failure modes.

## Identity

- Manufacturer: `EsionHsrahLatigid`
- Manufacturer code: `EHL_`
- Product: `FormantWound`
- Bundle ID: `jp.ehl.formantwound`
- Plug-in code: `FmWd`
- Formats: VST3, Standalone, and AU on Apple platforms

## Controls

- `LPC Resolution`: envelope order from coarse four-pole damage to dense sixteen-coefficient tracking.
- `Excitation Corruption`: blends the LPC residual with deterministic noise and impulse wounds.
- `Formant Warp`: folds the envelope coefficient field away from a neutral source-filter response.
- `Freeze Hold`: stops envelope analysis while the exciter continues to move.
- `Reseed`: deterministically changes the corruption stream and impulse spacing.
- `Damage`: increases coefficient quantization, drive, and filter collapse pressure.
- `Feedback`: reinjects bounded wet energy into the source-filter path.
- `Mix`: dry/wet balance.
- `Output`: final trim before the bounded ceiling.

## Research Grounding

The implementation is intentionally not a generic pitch/formant shifter. It is based on source-filter speech coding and synthesis ideas: LPC estimates a vocal-tract-like envelope, excitation carries residual source energy, and corruption is applied to both sides of that split.

Primary references used for the design:

- ITU-T Recommendation G.729 (06/2012), *Coding of speech at 8 kbit/s using conjugate-structure algebraic-code-excited linear prediction (CS-ACELP)*, https://www.itu.int/rec/T-REC-G.729-201206-I/en
- B. S. Atal and M. R. Schroeder, "Adaptive Predictive Coding of Speech Signals", *Bell System Technical Journal*, 1970, DOI `10.1002/j.1538-7305.1970.tb04297.x`
- A. V. Oppenheim, "Speech Analysis-Synthesis System Based on Homomorphic Filtering", *Journal of the Acoustical Society of America*, 1969, DOI `10.1121/1.1911395`
- DAFX chapter 9, "Source-filter Processing", https://www.dafx.de/DAFX_Book_Page/chapter9.html
- JUCE `AudioProcessor` lifecycle and callback contract, https://docs.juce.com/master/classjuce_1_1AudioProcessor.html

## Build

Use a local JUCE checkout to avoid network fetches:

```sh
cmake --preset engine-debug
cmake --build --preset engine-debug --parallel
ctest --preset engine-debug --output-on-failure

cmake --preset plugin-release -DEHL_JUCE_SOURCE_DIR=/absolute/path/to/JUCE
cmake --build --preset plugin-release --parallel
ctest --preset plugin-release --output-on-failure
```

Local macOS builds default `EHL_COPY_PLUGIN_AFTER_BUILD` to `ON` for VST3 and AU developer convenience. Set `-DEHL_COPY_PLUGIN_AFTER_BUILD=OFF` to disable user-folder copying.

Release-staged products are written under:

```text
artifacts/plugin-release/<platform>/
```
