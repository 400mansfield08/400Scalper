# 400Scalper

A transparent soft-knee clipper, as a CLAP plugin. Made for Always 400.

- **In / Out gain, Threshold, Knee**: per-sample soft-knee clipping, which becomes a hard clip at Knee 0.
- **Clean clipping**: the clipper runs at 4× oversampling with antiderivative anti-aliasing (ADAA), which removes the harsh-sounding artifacts clipping normally causes. In tests, these artifacts sit about 105 dB below the signal, compared with about 26 dB for a plain clipper.
- **Hard ceiling**: a stereo-linked, 0.7 ms look-ahead stage follows the clipper. It stops the small overshoots that oversampling adds from passing the threshold, without putting the artifacts back.
- **Two modes**, switched with SAMPLE / SPECTRAL at the top:
  - **Sample**: the per-sample clipper described above.
  - **Spectral**: a detail-preserving clipper. A 26-band ear-shaped (Mel) filter bank works out which parts of the sound are loud and which are quiet detail. Only the loud part is clipped; the **DETAIL** knob (0–100 %) sets how much of the quiet detail rides through untouched. Nothing passes the threshold. In tests, 100 % of a quiet 4 kHz tone survives under a hard-clipped 60 Hz tone, against 26 % with the plain clipper. Delay: 579 samples at 48 kHz (12 ms).
  - Only one mode runs at a time. The audio switches immediately with a 10 ms fade out and in, and the other engine is stopped and reset. The two modes have different delays, so the plugin also asks the host to restart it to update its delay compensation.
  - The Spectral **SPECTRUM** display sits left of OUT. The accent color shows what's being clipped, and the opposite hue shows the detail being kept. The DETAIL knob uses the same opposite hue.
- **BYPASS** (top right): switches to the untouched input, delayed by the same latency as the processed signal so nothing shifts in time, with a 10 ms crossfade. Hosts that support CLAP's bypass flag use it as the plugin's own bypass. While bypassed, the scope shows the input in grey.
- **AUTO GAIN** (above IN): turns the output down by whatever In Gain adds, so In only sets how hard the clipper is driven. With nothing clipping, the level stays the same; what you hear changing is only the clipping.
- **Δ DELTA** (above THRESHOLD, both modes): listen to only what the clipper removes. Each engine runs an unclipped twin of the signal through exactly the same filters, so Delta is truly silent (−156 dBFS in tests) when nothing is clipping. Delta is a monitoring control and isn't saved.
- **Optional multiband** (Sample mode): up to 4 bands, with crossovers at 6, 12 or 24 dB/oct (first-order, Linkwitz-Riley 2 or Linkwitz-Riley 4). The bands sum back flat, within 0.02 dB. Each band has its own Threshold, Knee and Solo.
- **UI**: a native interface built with [iPlug2](https://github.com/iPlug2/iPlug2) IGraphics (NanoVG on OpenGL), no web view. It reproduces the approved v7 mockup:
  - live scope with the orange input behind the grey output, draggable threshold, TIME, FOLLOW, FREEZE and mouse-wheel zoom (Shift = time)
  - ZOOM from 0.25× (zoomed out, shows up to +12 dB, for hot inputs) to 16×; it scales evenly around the centre line. FIT zooms so the loudest part of the visible signal fills the scope
  - IN/OUT meters
  - spectrum and band editor (drag in from the ‹ › tabs or right-click to add a band, double-click a crossover to remove it, S = solo)
  - accent-color picker (click the 400SCALPER wordmark)
  - resizable window (minimum 640 × 440), with a compact layout below 820 px
  - footer with the version number and Always400.com
- **Latency**: Sample mode 68 samples at 48 kHz (1.4 ms); Spectral mode 579 samples (12 ms). Both are reported to the host so it can compensate.

## Get the plugin (Windows)

Each push to `main` builds the plugin on GitHub Actions.

1. Open **Actions → Build → latest run** (or the latest run of your pull request).
2. Download the **400Scalper-windows** artifact.
3. Unzip it and copy `400Scalper.clap` to `C:\Program Files\Common Files\CLAP\`.
4. In Bitwig, rescan plugins, or restart Bitwig.

Tagged versions (`v0.1.0`, …) also attach the `.clap` to a GitHub Release.

## Build it yourself

```
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release
```

- **Windows**: needs Visual Studio 2022 and CMake 3.21 or newer. CMake downloads iPlug2 and the CLAP SDK (pinned versions) on the first configure. A local build also copies the plugin to `%LOCALAPPDATA%\Programs\Common\CLAP`; turn that off with `-DIPLUG_DEPLOY_PLUGINS=OFF`.
- **macOS**: iPlug2 supports it; not set up or tested yet.
- **Linux**: iPlug2 has no Linux support, so only the engine tests build there.

**Formats**: only CLAP is built for now. iPlug2 can also build VST3, AU and AAX from the same code. To add one later, add it to `SCALPER_FORMATS` in `plugin/CMakeLists.txt` and provide the SDK iPlug2 needs for it. `plugin/config.h` already has the VST3 / AU / AAX fields filled in.

## Layout

| Path | What |
|---|---|
| `src/dsp/Clipper.h` | Soft-knee curve, its antiderivative, ADAA, droop compensation |
| `src/dsp/Oversampler.h` | 4× linear-phase polyphase halfband oversampler |
| `src/dsp/Crossover.h` | 6/12/24 dB multiband splitter with allpass alignment |
| `src/dsp/Engine.h` | Per-channel engine and the look-ahead ceiling |
| `src/dsp/Spectral.h` | Spectral mode: STFT ear filter bank, loud/detail split, detail-preserving clip |
| `src/core/Processor.h` | The whole audio engine, independent of the plugin format: parameter snapshot in, audio out, mode switching, Delta, scope/meter taps |
| `src/core/Params.h` | Parameter table (the index is the host parameter id) |
| `plugin/Scalper.*` | iPlug2 plugin class: parameters, state, latency reporting |
| `plugin/ui/` | Native UI controls: header, scope, meters, knobs, band panel, spectrum, picker, footer |
| `plugin/config.h` | iPlug2 project settings (name, ids, window size, CLAP features) |
| `tests/` | DSP tests, Spectral mode tests and engine tests |
| `prototypes/html-ui/` | The v7 HTML UI that the native UI reproduces (opens in a browser with a simulated engine) |
| `prototypes/spectral/` | The JavaScript prototype of Spectral mode, its tests and the v7 review page |

## Notes

- **Solo**: Solo is a monitoring control and is not saved with the session.
- **Upgrading from v0.1 / v0.2**: the plugin id is unchanged, so hosts treat 0.3.0 as an update. Sessions saved with v0.2 still load their settings (the old parameter ids are mapped on load). Host automation lanes from v0.2 have to be re-assigned, because the parameter ids changed.
- **Accent color**: the accent color is a global preference, stored in `%APPDATA%\400Scalper\settings.txt`.
- **Font license**: the UI font (Sui Generis, Typodermic) is embedded in the binary. Check that its license allows embedding before a public release.
