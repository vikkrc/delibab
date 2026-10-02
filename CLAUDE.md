# Délibáb: notes for working on this repo

Granular synth plugin (JUCE 9, C++20, CMake). Owner: Viktor (vikkrc). He tests
by ear in FL Studio, Ableton Live and Logic Pro; code changes are made here and
verified with the headless tests before pushing.

## Build & test

```
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release   # add -DFETCHCONTENT_SOURCE_DIR_JUCE=<path> to reuse a local JUCE
cmake --build build
xvfb-run -a ./build/DelibabTests_artefacts/Release/DelibabTests build/test-output   # Linux; omit xvfb-run elsewhere
```

The test tool writes WAV renders and `editor.png` to the output folder. Look at
`editor.png` after any UI change. Run pluginval on the VST3 before pushing
DSP or state changes (CI also does this on all three OSes, plus `auval` on macOS).

## Hard rules

- **Parameters** (`src/Parameters.*`): IDs are permanent. Never rename, remove
  or reuse an ID; never reorder or insert into a choice list (append only).
  Add new parameters with a new ID. Breaking this silently breaks users'
  automation and saved projects in every DAW.
- **Audio thread** (`processBlock`, everything in `GrainEngine::render`): no
  allocation, no locks that can block, no file or console I/O, no
  `MessageManager`. Samples are swapped in with a try-lock and freed on the
  message thread via the release pool in `DelibabProcessor`.
- **State**: `getStateInformation` must stay cheap (hosts call it often); the
  embedded FLAC is encoded once at load time. `setStateInformation` restores
  the sample synchronously so offline bounces work right after project load.
- Keep each `GrainEvent` cheap; it feeds the UI particles and will feed
  OSC/Syphon/Spout output later.

## Roadmap (agreed so far)

1. ~~Skeleton: parameter pool, state, sample loading, 4 grain modes, UI, CI~~ (v0.1)
2. Modulation: LFOs, envelopes, random, drag-to-modulate, macros, scenes/XY morph
3. Note brain: harmonizer, arpeggiator, chord memory, MPE
4. Timbre map (grains placed by brightness/noisiness, XY playable), gesture paths
5. AV output: OSC grain events, Syphon/Spout texture
6. Groove transfer, time-machine capture, multi-out per layer, presets
