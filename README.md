# 400Scalper

A transparent soft-knee clipper, as a CLAP plugin. Made for Always 400.

- **In / Out gain, Threshold, Knee**: per-sample soft-knee clipping, which becomes a hard clip at Knee 0.
- **Clean clipping**: the clipper runs at 4× oversampling with antiderivative anti-aliasing (ADAA), which removes the harsh-sounding artifacts clipping normally causes. In tests, these artifacts sit about 105 dB below the signal, compared with about 26 dB for a plain clipper.
- **Hard ceiling**: a stereo-linked, 0.7 ms look-ahead stage follows the clipper. It stops the small overshoots that oversampling adds from passing the threshold, without putting the artifacts back.
- **Optional multiband**: up to 4 bands, with crossovers at 6, 12 or 24 dB/oct (first-order, Linkwitz-Riley 2 or Linkwitz-Riley 4). The bands sum back flat, within 0.02 dB. Each band has its own Threshold, Knee and Solo.
- **UI**: the v5 interface runs in an embedded web view (WebView2 on Windows):
  - live scope with the orange input behind the grey output, plus TIME/ZOOM, FOLLOW and FREEZE
  - IN/OUT meters
  - spectrum and band editor
  - accent-color picker (click the wordmark)
- **Latency**: 68 samples at 48 kHz (1.4 ms), reported to the host so it can compensate.

## Get the plugin (Windows)

Each push to `main` builds the plugin on GitHub Actions.

1. Open **Actions → Build → latest run**.
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

- **Windows**: needs Visual Studio 2022 and CMake 3.21 or newer. WebView2 ships with Windows 10 and 11.
- **macOS/Linux**: builds without the UI for now.

## Layout

| Path | What |
|---|---|
| `src/dsp/Clipper.h` | Soft-knee curve, its antiderivative, ADAA, droop compensation |
| `src/dsp/Oversampler.h` | 4× linear-phase polyphase halfband oversampler |
| `src/dsp/Crossover.h` | 6/12/24 dB multiband splitter with allpass alignment |
| `src/dsp/Engine.h` | Per-channel engine and the look-ahead ceiling |
| `src/Plugin.*` | CLAP plugin: params, state, latency, processing |
| `src/Gui.cpp`, `src/GuiJson.cpp` | Web view window and the JSON bridge to the UI |
| `ui/index.html` | The UI. It also opens in a browser, where a simulated engine stands in for preview. |
| `tests/` | DSP tests and a minimal CLAP host test |

## Notes

- **Spectral mode**: the SPECTRAL switch is in the UI, but spectral clipping is planned for a later version.
- **Solo**: Solo is a monitoring control and is not saved with the session.
- **Accent color**: the accent color is a global preference, stored in `%APPDATA%\400Scalper\settings.txt`.
- **Font license**: the UI font (Sui Generis, Typodermic) is embedded in the binary. Check that its license allows embedding before a public release.
