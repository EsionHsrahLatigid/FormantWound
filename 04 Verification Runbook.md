# FormantWound Verification Runbook

## Goal

Prove that the implementation is deterministic, realtime-safe, audible at extreme settings, and compatible with the EHL release shape.

## Local checks

1. Run the JUCE metadata/structure checker for identity validation.
2. Build the JUCE-independent DSP tests first.
3. Build the plug-in targets with the pinned JUCE source path.
4. Run the processor and editor tests.
5. Run staged artifact checks.
6. Run host smoke or plugin tester validation after the binaries exist.

## Evidence to capture

- compiler and CTest output;
- artifact paths under `artifacts/plugin-release/<platform>`;
- macOS codesign status if applicable;
- deterministic render comparisons for repeated seeds;
- RMS, peak, crest, DC, activity, and formant-band energy metrics for the hostile-input set;
- final GitHub Actions CI run URL and release URL once published.

## Expected commands

```sh
python3 /Users/2bit/.codex/skills/develop-juce-plugins/scripts/check_juce_project.py /Users/2bit/prog/juce/FormantWound \
  --expect-product FormantWound \
  --expect-manufacturer-code EHL_ \
  --expect-bundle-prefix jp.ehl.

cmake --preset engine-debug
cmake --build --preset engine-debug --parallel
ctest --preset engine-debug --output-on-failure

cmake --preset plugin-release -DEHL_JUCE_SOURCE_DIR=/absolute/path/to/JUCE
cmake --build --preset plugin-release --parallel
ctest --preset plugin-release --output-on-failure
```

## Public copy gate

Before any public commit, release, or website update, run the bundled public-text guard from the shared EHL skill against the consumer repository history.

## Stop condition

The note set is complete only when the implementation has:

- deterministic DSP tests;
- plugin-format builds;
- staged artifacts;
- public copy guard pass;
- host validation evidence;
- no unresolved public-surface wording issues.
