# FormantWound Design

FormantWound uses the shared `juce-ehl-design-module` for the canonical compact `ehl` mark, palette, typography, and 8-bit JUCE component styling.

## UI Direction

- Compact monochrome operational surface, `560 x 344`.
- Header: product title plus effect class; canonical short `ehl` mark rendered by the shared module.
- Primary display: quantized LPC envelope matrix with hold/rescue striping.
- Controls: two dense rows covering LPC resolution, excitation corruption, formant warp, reseed, freeze hold, damage, feedback, mix, and output.
- No chromatic accents, waveform logos, fake hardware, glow, blur, RGB split, or decorative noise fields.

## Production Boundaries

- The consumer plugin does not copy logo SVG paths or local logo assets.
- The UI uses only `#050505`, `#2A2A2A`, `#8A8A86`, and `#F2F2F0` via the shared design module.
- Operational labels are clean text; corruption appears in the data display and source-filter behavior, not in legibility-critical copy.
