BASEMENTAL FG 250 - NAM + IR PACK
==================================

The FG 250 plug-in split into the two pieces every NAM loader uses:
  amp head  -> NAM model (.nam)
  cabinet   -> impulse response (.wav IR)
Load one of each, the same way you'd pair a real amp capture with a cab IR.

What's in the folders
---------------------
1 NAM models (A2)        10 amp-head captures. NAM A2 format: each file holds the Full and
                         the Lite size, so the loader picks the one it can run
                         (LAVA STUDIO runs A2-Lite).
2 IRs                    The 8 built-in cabinets x 3 mic positions (On-Axis, Off-Axis,
                         Close). Mono, 24-bit, 2048 samples. Use the 48 kHz folder for
                         LAVA STUDIO and most loaders; 44.1 kHz is there for DAW projects
                         at 44.1.
3 TONE3000 sweep uploads Each capture rendered through NAM's standard v3 sweep (48 kHz,
                         24-bit, mono). Upload these to TONE3000 if you'd rather have
                         TONE3000 train and host the tones itself.
Training reports         Accuracy (ESR: lower is better, under 0.01 is excellent) and a
                         plot for every model.

The captures are the AMP ONLY: no cabinet, reverb, FET Stack compressor or guitar voice.
They're taken at Output = "Power only", so they include the power-amp stage.

Suggested pairs
---------------
  Clean Init / Glassy / Jazz Neck     IR 01 Fusion 1x12 On-Axis, or IR 07 1x12 Jazz Combo
  Edge of Breakup                     IR 02 1x12 Open Back On-Axis
  Lead Init / Fusion Crunch           IR 03 2x12 Closed Back On-Axis
  Sweep / Ballad / Singing Sustain    IR 01 Fusion 1x12 On-Axis (the plug-in's default)
  Fat Round Neck                      IR 04 4x12 Closed Back Close
Add reverb and compression in the unit itself (they aren't in the captures).

LAVA STUDIO (lavaOS 1.7 or later)
---------------------------------
Option A - put your own files on the unit
  1. Copy a .nam file from "1 NAM models (A2)" and an IR from "2 IRs/48 kHz" to the
     unit, following LAVA's instructions for importing your own NAM and IR files.
  2. In a preset, load the .nam into the amp (NAM) block and the IR into the cab/IR block.
Option B - through TONE3000 (no file copying)
  1. Sign in at tone3000.com and upload the .nam files (Upload > NAM) and the IRs
     (Upload > IR). You can keep them private.
  2. On LAVA STUDIO, open the TONE3000 browser and load them from your account.
     Or: tone3000.com > Create Tone > Capture > "sweep" method, and upload a file from
     "3 TONE3000 sweep uploads" to have TONE3000 train it in A2 for you.

NAM plug-in / other loaders
---------------------------
Load the .nam in the Neural Amp Modeler plug-in (or any loader with A2 support) and the
IR in its IR slot. Loaders that only run A1 (older pedals) need the TONE3000 route: upload
a sweep file and pick the size the unit needs.

Levels
------
The captures are made with the plug-in's digital levels: 0 dBFS into the model = 0 dBFS
into the plug-in. If a capture was louder than -1 dBFS it was turned down to -1 dBFS
(volume only, the tone is unchanged). Set your input so the guitar peaks around -12 dBFS,
then balance with the loader's output level.

These are captures of the Basemental FG 250 plug-in, not of any hardware amplifier.
