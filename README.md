# Fusion 250 — two-channel fusion amp plug-in

A guitar amp plug-in modeled on the signal chain of the DV Mark FG Little 250
Amplitude (Frank Gambale signature), with optional Kiesel FG1/FG2 guitar voicings.

**Formats:** VST3 (Mac/Win/Linux), AU (Mac), LV2 (Linux), Standalone app (all),
AAX (Pro Tools, needs Avid's SDK), and a REAPER JSFX that runs without compiling.

## Signal chain

Taken from the block diagram in the FG Little 250 Amplitude user manual:

```
Input -> [Guitar Voice] -> CLEAN: Gain -> Low/Mid/High -> Level --+
                        -> LEAD : Gain -> Low/Mid/High -> Level --+-> Reverb
      -> [Pre Out, 0/-6 dB] -> Class-D power stage -> Cab (built-in or your IR) -> Output
```

| Control | What it does |
|---|---|
| Channel | Clean / Lead (15 ms crossfade, no clicks) |
| Gain | Clean: -6 to +18 dB into a soft, high-headroom stage. Lead: 12-55 dB across two clipping stages, 4x oversampled |
| Low / Mid / High | Post-gain 3-band EQ, 5 = flat. Shelves +/-15 dB, broad mid +/-12 dB |
| Level | Feed to the power stage. Above ~7 it starts pushing the power stage into soft limiting, like a cranked amp |
| Reverb + On/Off | Shared digital reverb (the hardware footswitch toggles it) |
| Guitar Voice | Off, FG1 Neck, FG1 Bridge, FG2. Gentle EQ curves that nudge a DI'd guitar toward those instruments. Leave Off for your own guitar's sound |
| Output | Pre Out (no power amp or cab, like the rear jack), Built-in Cab, or User IR (load any cabinet .wav) |
| -6 dB | Pre Out pad, same as the hardware switch |
| Input / Master | Input trim and master output (dB) |

### Gambale-style presets

Glassy Clean, Jazz Neck Clean, Edge of Breakup, Sweep Lead, Fat Round Lead
(neck), Singing Sustain, Fusion Crunch Rhythm, Ballad Lead (wet). Each sets
both channels, so the channel button/footswitch gives a matched clean/lead pair.

Frank Gambale hasn't published his amp settings. These are interpretations of
the tones he describes: glassy, expressive cleans and a lead he calls "fat,
round and warm" with long sustain. Lead presets keep gain moderate so sweep
arpeggios stay note-separated. They're for personal use; rename them before
distributing the plug-in, since his name implies an endorsement.

### Strobe tuner

Click **TUNER** in the header. It works like a strobe tuner: the input is
compared against a perfect reference oscillator and the phase drift is
measured, so the bands stand still when you're in tune and drift right
(sharp) or left (flat) at a speed proportional to the error. The x2 and x4
bands magnify small errors.

- Readout: note, cents to 0.1, frequency to 0.01 Hz, flat / in tune / sharp
  lights (green within +/-0.5 cent)
- Reference A: 415-466 Hz in 0.1 Hz steps
- String guide: Chromatic, Standard, **Gambale (A2 D3 G3 C4 E3 A3)**, Half
  Step Down, Drop D, 7-string
- Mute output while tuning (on by default)
- The tuner reads the raw input, ahead of the amp, like a pedal at the front

Measured accuracy (`tests/tuner_test.cpp`): 1,224 synthetic plucked notes from
55 Hz to 1 kHz, +/-50 cents detuned, with decay, noise, string inharmonicity
and weak fundamentals, at 44.1/48/96 kHz and A=440/432. Worst error is
**0.02 cent**. Real-world accuracy is limited by your audio interface's clock,
typically +/-0.03 to 0.09 cent.

**FX loop:** a plug-in can't host other plug-ins inside itself. To match the
hardware loop (after the reverb, before the power amp), set Output to
*Pre Out*, insert your delays/modulation after it, then add a cab IR loader.

## What's measured vs. estimated

DV Mark publishes the chain order and control set but not the circuit.
These values in `Source/AmpCore.h` are marked `[VOICING]` and are engineering
estimates, not measurements:

- EQ centre frequencies (clean 110 / 750 / 3200 Hz, lead 100 / 800 / 2800 Hz)
- the lead channel's pre-clip shaping (110 Hz tight high-pass, +5 dB at 850 Hz,
  -4 dB shelf at 3 kHz to soften pick attack) and the clipping curves
- the built-in cab and the guitar voicings

If you can get time with a real FG Little 250, sweep each knob with a sine and
compare. Every one of those numbers is a single constant to change.

## Logic Pro (Audio Unit)

Logic only loads Audio Units, which must be compiled on a Mac:

1. Unzip this folder anywhere (e.g. Documents).
2. Right-click `build-mac.command` > **Open** (the first time macOS asks to confirm).
   If Apple's Command Line Tools aren't installed, it opens their installer:
   click Install, wait, then run the script again.
3. It builds a universal AU + VST3, installs them to
   `~/Library/Audio/Plug-Ins/`, and runs `auval`, the same validation Logic
   runs. Look for **AU VALIDATION SUCCEEDED**.
4. In Logic: a track's Audio FX slot > Audio Units > Fusion Audio > Fusion 250.
   If it isn't listed: Logic Pro > Settings > Plug-in Manager > Reset & Rescan Selection.

The REAPER JSFX version has no tuner (JSFX can't host this kind of GUI);
use the VST3/AU for the tuner.

## REAPER (no build needed)

1. In REAPER: Options > Show REAPER resource path.
2. Copy `JSFX/Fusion250.jsfx` into the `Effects` folder there.
3. On a track: FX > JS > Fusion 250.
4. For your own cab IR, set Output to *Power only* and add ReaVerb after it.

## Getting Mac / Windows / Linux builds without a dev setup

1. Create a free GitHub repository and upload this whole folder.
2. Open the **Actions** tab. The included workflow builds macOS (universal AU +
   VST3), Windows (VST3) and Linux (VST3 + LV2) automatically.
3. Download the `Fusion250-macOS`, `-Windows` and `-Linux` artifacts from the run.

### Installing

| | VST3 | AU | LV2 |
|---|---|---|---|
| macOS | `~/Library/Audio/Plug-Ins/VST3/` | `~/Library/Audio/Plug-Ins/Components/` | - |
| Windows | `C:\Program Files\Common Files\VST3\` | - | - |
| Linux | `~/.vst3/` | - | `~/.lv2/` |

On macOS, unsigned plug-ins downloaded from the internet are blocked. After
copying, run once in Terminal:
`xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/Components/"Fusion 250.component"`
(same for the `.vst3`). For distribution to others you'd sign and notarize
with an Apple Developer account.

## Building locally

Requires CMake 3.22+ and a C++17 compiler (Xcode on Mac, Visual Studio 2022 on
Windows, GCC/Clang on Linux). JUCE is downloaded automatically.

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

Output goes to `build/Fusion250_artefacts/Release/`.

- Mac universal binary: add `-DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"`
- Linux packages: `libasound2-dev libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxext-dev libfreetype6-dev libfontconfig1-dev`
- **Pro Tools (AAX):** download the AAX SDK from Avid's developer portal and add
  `-DAAX_SDK_PATH=/path/to/aax-sdk`. Running AAX in a retail Pro Tools also
  requires PACE/iLok signing (free for developers via Avid).

DSP regression test: `build/render_test <folder>` renders test riffs through
every mode at 44.1/48/96 kHz and writes WAVs.

## Licensing

JUCE is free under AGPLv3 or its commercial licence (free "Personal" tier below
a revenue limit). Selling closed-source requires a JUCE licence. The plug-in
name and graphics are original; "DV Mark", "Amplitude" and "Kiesel" are their
owners' trademarks and aren't used in the product name.
