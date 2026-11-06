"""
1176-style FET compressor, single channel: Python proof of concept.

Same algorithm as FetCompressor in Source/AmpCore.h, written for clarity:

  input ─► peak envelope follower ─► dB ─► soft-knee gain computer (4:1)
        ─► attack/release smoothing in the dB domain ─► gain ─► output

1176-style behaviour modelled:
  * very fast attack, set in microseconds (20 µs .. 800 µs)
  * release set in milliseconds (50 ms .. 1100 ms)
  * fixed threshold: the Input control drives the signal into it, Output
    sets make-up gain (no threshold knob)
  * fixed 4:1 ratio with a soft knee

Run:  python3 prototypes/fet_compressor.py [--plot out.png]
"""
from __future__ import annotations

import argparse
import math

import numpy as np


class FETCompressor:
    """Single-channel 1176-style feed-forward FET compressor."""

    def __init__(
        self,
        sample_rate: float = 48_000.0,
        attack_us: float = 250.0,
        release_ms: float = 250.0,
        ratio: float = 4.0,
        threshold_db: float = -24.0,
        knee_db: float = 6.0,
        input_db: float = 0.0,
        output_db: float = 0.0,
        detector_release_ms: float = 5.0,
    ) -> None:
        self.fs = float(sample_rate)
        self.ratio = float(ratio)
        self.threshold_db = float(threshold_db)
        self.knee_db = float(knee_db)
        self.input_gain = 10.0 ** (input_db / 20.0)
        self.output_gain = 10.0 ** (output_db / 20.0)
        self.set_attack_us(attack_us)
        self.set_release_ms(release_ms)
        # peak detector: instant rise, short exponential fall
        self._det_coef = self._coef(detector_release_ms * 1e-3)
        self.reset()

    # ------------------------------------------------------------------ setup
    def _coef(self, seconds: float) -> float:
        """One-pole coefficient: reaches 1 - 1/e (63 %) after `seconds`."""
        return math.exp(-1.0 / (self.fs * max(seconds, 1e-9)))

    def set_attack_us(self, attack_us: float) -> None:
        self.attack_us = float(attack_us)
        self._att = self._coef(attack_us * 1e-6)

    def set_release_ms(self, release_ms: float) -> None:
        self.release_ms = float(release_ms)
        self._rel = self._coef(release_ms * 1e-3)

    def reset(self) -> None:
        self._env = 0.0          # detector envelope (linear)
        self._gr_db = 0.0        # smoothed gain change (<= 0 dB)

    # ------------------------------------------------------- static curve
    def gain_computer(self, level_db: float) -> float:
        """Soft-knee static curve. Returns the gain change in dB (<= 0)."""
        t, w, r = self.threshold_db, self.knee_db, self.ratio
        over = level_db - t
        if 2.0 * over < -w:                       # below the knee
            out = level_db
        elif 2.0 * abs(over) <= w:                # inside the knee: quadratic blend
            out = level_db + (1.0 / r - 1.0) * (over + w / 2.0) ** 2 / (2.0 * w)
        else:                                     # above the knee
            out = t + over / r
        return out - level_db

    # ------------------------------------------------------------ process
    def process(self, x: np.ndarray, return_gr: bool = False):
        """Compress a mono float array. Optionally also return gain reduction (dB)."""
        x = np.asarray(x, dtype=np.float64) * self.input_gain
        y = np.empty_like(x)
        gr = np.empty_like(x)
        env, g, att, rel, det = self._env, self._gr_db, self._att, self._rel, self._det_coef
        for i, s in enumerate(x):
            a = abs(s)
            env = a if a > env else det * env + (1.0 - det) * a
            target = self.gain_computer(20.0 * math.log10(env + 1e-12))
            # more reduction needed -> attack time constant; less -> release
            coef = att if target < g else rel
            g = coef * g + (1.0 - coef) * target
            y[i] = s * 10.0 ** (g / 20.0) * self.output_gain
            gr[i] = g
        self._env, self._gr_db = env, g
        return (y, gr) if return_gr else y


# ---------------------------------------------------------------- demo
def _tone_bursts(fs: float) -> np.ndarray:
    """Quiet note, loud note, quiet note: shows attack and release."""
    t = np.arange(int(fs * 1.5)) / fs
    level = np.where((t >= 0.5) & (t < 1.0), 10 ** (-6 / 20), 10 ** (-30 / 20))
    return (level * np.sin(2 * np.pi * 220.0 * t)).astype(np.float64)


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--plot", help="save a plot of input / output / gain reduction to this PNG")
    args = ap.parse_args()

    fs = 48_000.0
    comp = FETCompressor(fs, attack_us=250.0, release_ms=250.0)

    print("Static curve (4:1, threshold -24 dBFS, 6 dB soft knee):")
    for lvl in (-40, -30, -27, -24, -21, -18, -12, -6, 0):
        g = comp.gain_computer(lvl)
        print(f"  in {lvl:+4d} dBFS -> out {lvl + g:+7.2f} dBFS   (gain {g:+6.2f} dB)")

    x = _tone_bursts(fs)
    y, gr = comp.process(x, return_gr=True)

    def rms_db(a: np.ndarray) -> float:
        return 20 * math.log10(math.sqrt(float(np.mean(a * a))) + 1e-12)

    loud = slice(int(0.75 * fs), int(0.95 * fs))
    print("\nTone bursts through the compressor:")
    print(f"  loud section in  {rms_db(x[loud]):6.2f} dBFS RMS")
    print(f"  loud section out {rms_db(y[loud]):6.2f} dBFS RMS")
    print(f"  steady gain reduction on the loud note: {gr[loud].mean():.2f} dB")

    # Timing: use a clean level step (a square-wave envelope), because a sine
    # only reaches its peak a quarter-cycle after the note starts.
    step = np.concatenate([np.full(int(0.2 * fs), 0.03), np.full(int(0.4 * fs), 0.5), np.full(int(0.8 * fs), 0.03)])
    timing = FETCompressor(fs, attack_us=comp.attack_us, release_ms=comp.release_ms)
    _, g2 = timing.process(step, return_gr=True)
    on, off = int(0.2 * fs), int(0.6 * fs)
    final = g2[off - 10]
    reached = np.argmax(g2[on:] <= 0.632 * final)
    recovered = np.argmax(g2[off:] >= 0.368 * final)
    print(f"  attack:  63 % of the reduction after {reached / fs * 1e6:6.0f} µs (setting {comp.attack_us:.0f} µs)")
    print(f"  release: 63 % recovered after      {recovered / fs * 1e3:6.0f} ms (setting {comp.release_ms:.0f} ms)")

    if args.plot:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt

        t = np.arange(len(x)) / fs
        fig, ax = plt.subplots(2, 1, figsize=(9, 5), sharex=True)
        ax[0].plot(t, x, lw=0.4, label="input", alpha=0.6)
        ax[0].plot(t, y, lw=0.4, label="output")
        ax[0].legend(loc="upper right")
        ax[0].set_ylabel("amplitude")
        ax[1].plot(t, gr, color="tab:purple")
        ax[1].set_ylabel("gain change (dB)")
        ax[1].set_xlabel("time (s)")
        fig.suptitle("1176-style FET compressor prototype: 4:1, 250 µs / 250 ms")
        fig.tight_layout()
        fig.savefig(args.plot, dpi=120)
        print(f"\nplot saved to {args.plot}")


if __name__ == "__main__":
    main()
