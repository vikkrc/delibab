# Installing Délibáb

Délibáb is a granular synthesizer plugin (VST3, AU, CLAP) plus a standalone app.

## macOS (Apple Silicon and Intel)

Copy each plugin to its folder (create the folder if it doesn't exist):

| File                | Folder                                   | Used by                    |
|---------------------|------------------------------------------|----------------------------|
| `Delibab.component` | `~/Library/Audio/Plug-Ins/Components/`   | Logic Pro, GarageBand      |
| `Delibab.vst3`      | `~/Library/Audio/Plug-Ins/VST3/`         | Ableton Live, FL Studio    |
| `Delibab.clap`      | `~/Library/Audio/Plug-Ins/CLAP/`         | FL Studio, Bitwig, Reaper  |
| `Delibab.app`       | `/Applications/`                         | Standalone                 |

(`~/Library` is hidden in Finder: press Cmd+Shift+G and paste the path.)

These development builds are not notarized by Apple yet, so macOS will block
them at first. Open Terminal and run, once:

```
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/Components/Delibab.component \
  ~/Library/Audio/Plug-Ins/VST3/Delibab.vst3 ~/Library/Audio/Plug-Ins/CLAP/Delibab.clap \
  /Applications/Delibab.app
```

Then restart your DAW and rescan plugins. In Logic, if Délibáb doesn't show
up, open Logic Pro > Settings > Plug-in Manager and click "Reset & Rescan Selection".

## Windows (64-bit)

| File            | Folder                                 | Used by                 |
|-----------------|----------------------------------------|-------------------------|
| `Delibab.vst3`  | `C:\Program Files\Common Files\VST3\`  | Ableton Live, FL Studio |
| `Delibab.clap`  | `C:\Program Files\Common Files\CLAP\`  | FL Studio, Bitwig       |
| `Delibab.exe`   | anywhere                               | Standalone              |

Restart your DAW and rescan. In FL Studio: Options > Manage plugins > Find more plugins.

## First steps

1. Drag any audio file (WAV, AIFF, FLAC, OGG, MP3) onto the window, or click **Load sample**.
2. Play notes. Layer 1 (Cloud) is on by default.
3. Drag across the waveform to move the selected layer's grain position.
4. Switch on more layers: Pulse (rhythmic, synced to your DAW tempo), Tonal
   (a playable lead/bass whose pitch comes from the grain rate), Slice (beats
   snapped to the sample's transients).

With **Embed** on, the sample is saved inside your project, so it opens on any
computer even if the original file moves.
