// Tests for the FET Stack compressor and Class-A drive (Source/AmpCore.h).
// Build: g++ -O2 -std=c++17 -I../Source comp_test.cpp -o comp_test
#include "AmpCore.h"
#include <chrono>
#include <cstdio>
#include <vector>

using namespace fusion;
static int fails = 0;
static void check (bool ok, const char* what, double got, double want, const char* unit)
{
    if (! ok) ++fails;
    std::printf ("  %-58s got %9.3f %-3s want %9.3f  %s\n", what, got, unit, want, ok ? "OK" : "FAIL");
}

// magnitude of harmonic k of f0 (Goertzel over whole periods)
static double harmonic (const std::vector<double>& y, double fs, double f0, int k)
{
    const double w = 2 * kPi * k * f0 / fs;
    double re = 0, im = 0;
    for (size_t i = 0; i < y.size(); ++i) { re += y[i] * std::cos (w * i); im -= y[i] * std::sin (w * i); }
    return 2.0 * std::sqrt (re * re + im * im) / y.size();
}

int main()
{
    const double fs = 48000;

    std::printf ("--- static curve: 4:1, threshold -24 dBFS, 6 dB soft knee (steady 1 kHz sine)\n");
    for (double peakDb : { -36.0, -24.0, -18.0, -12.0, -6.0, 0.0 })
    {
        FetCompressor c; c.prepare (fs); c.setAttackUs (250); c.setReleaseMs (250);
        const double a = std::pow (10.0, peakDb / 20.0);
        for (int i = 0; i < (int) fs; ++i) c.process (a * std::sin (2 * kPi * 1000.0 * i / fs));
        const double want = c.gainComputer (peakDb);
        char label[96]; std::snprintf (label, sizeof label, "gain reduction at %+.0f dBFS peak", peakDb);
        check (std::fabs (c.grDb - want) < 0.3, label, c.grDb, want, "dB");
    }
    {
        FetCompressor c; c.prepare (fs);
        const double slope = (c.gainComputer (0) + 0) - (c.gainComputer (-12) - 12);
        check (std::fabs (slope - 3.0) < 1e-9, "output rises 3 dB per 12 dB above the knee (4:1)", slope, 3.0, "dB");
    }

    std::printf ("--- attack / release time constants (level step)\n");
    for (double atkUs : { 20.0, 250.0, 800.0 })
        for (double relMs : { 50.0, 250.0, 1100.0 })
        {
            FetCompressor c; c.prepare (fs); c.setAttackUs (atkUs); c.setReleaseMs (relMs);
            for (int i = 0; i < (int) (0.2 * fs); ++i) c.process (0.01);
            std::vector<double> g;
            for (int i = 0; i < (int) (2.5 * fs); ++i) { c.process (0.5); g.push_back (c.grDb); }
            const double fin = g.back();
            int ia = 0; while (g[(size_t) ia] > 0.632 * fin) ++ia;
            std::vector<double> r;
            for (int i = 0; i < (int) (6 * fs); ++i) { c.process (0.01); r.push_back (c.grDb); }
            int ir = 0; while (r[(size_t) ir] < 0.368 * fin) ++ir;
            const double gotA = (ia + 1) / fs * 1e6, gotR = ir / fs * 1e3;
            char la[96], lr[96];
            std::snprintf (la, sizeof la, "attack %4.0f us (release %4.0f ms)", atkUs, relMs);
            std::snprintf (lr, sizeof lr, "release %4.0f ms (attack %4.0f us)", relMs, atkUs);
            check (std::fabs (gotA - atkUs) <= std::max (0.15 * atkUs, 1e6 / fs + 1), la, gotA, atkUs, "us");
            check (std::fabs (gotR - relMs) <= 0.15 * relMs, lr, gotR, relMs, "ms");
        }

    std::printf ("--- stacked pair, thru, dry mix, bypass\n");
    auto run = [&] (StackedFetComp::Settings s, double amp, double& outRms, double& inRms, float& g1, float& g2, double& maxDiff)
    {
        StackedFetComp sc; sc.prepare (fs, s);
        double so = 0, si = 0; maxDiff = 0; g1 = g2 = 0;
        for (int i = 0; i < (int) fs; ++i)
        {
            const double x = amp * std::sin (2 * kPi * 220.0 * i / fs);
            const double y = sc.process (x);
            if (i > fs / 2) { so += y * y; si += x * x; maxDiff = std::max (maxDiff, std::fabs (y - x)); }
            if (! std::isfinite (y)) ++fails;
        }
        g1 = sc.gr1; g2 = sc.gr2;
        outRms = 10 * std::log10 (so / (fs / 2)); inRms = 10 * std::log10 (si / (fs / 2));
    };
    double o, i, d; float g1, g2;
    StackedFetComp::Settings s; s.on = true; s.inputDb = 12; s.stacked = false;
    run (s, 0.3, o, i, g1, g2, d);
    check (g1 < -6 && g2 == 0, "single: Comp 1 reduces, Comp 2 idle (dB)", g1, -6, "dB");
    s.stacked = true; s.thruDb = 0;
    run (s, 0.3, o, i, g1, g2, d);
    const double stackedOut = o, gr2At0 = g2;
    check (g2 < -0.5, "stacked: Comp 2 also reduces", g2, -0.5, "dB");
    s.thruDb = 10;
    run (s, 0.3, o, i, g1, g2, d);
    check (g2 < gr2At0 - 4, "Thru +10 dB drives Comp 2 harder", g2, gr2At0 - 4, "dB");
    s.thruDb = 0; s.dryMix = 1;
    run (s, 0.3, o, i, g1, g2, d);
    check (d < 1e-6, "Dry 100%: output equals input", d, 0, "");
    s.dryMix = 0.5;
    run (s, 0.3, o, i, g1, g2, d);
    check (o > stackedOut + 1, "Dry 50%: louder than fully compressed (parallel)", o, stackedOut + 1, "dB");
    s.on = false; s.dryMix = 0;
    run (s, 0.3, o, i, g1, g2, d);
    check (d == 0.0, "off: bit-exact bypass", d, 0, "");

    std::printf ("--- Class-A drive\n");
    {
        double prevThd = -1;
        for (double drive : { 0.0, 0.25, 0.5, 0.75, 1.0 })
        {
            std::vector<double> y;
            for (int n = 0; n < 4800; ++n) y.push_back (classASaturate (0.5 * std::sin (2 * kPi * 1000.0 * n / fs), drive));
            const double h1 = harmonic (y, fs, 1000, 1), h2 = harmonic (y, fs, 1000, 2), h3 = harmonic (y, fs, 1000, 3);
            const double thd = std::sqrt (h2 * h2 + h3 * h3) / h1 * 100;
            std::printf ("    drive %.2f: fundamental %6.2f dB, 2nd %7.1f dB, 3rd %7.1f dB, THD %6.2f %%\n",
                         drive, 20 * std::log10 (h1 / 0.5), 20 * std::log10 (h2 / h1 + 1e-12), 20 * std::log10 (h3 / h1 + 1e-12), thd);
            if (drive == 0) check (thd < 1e-6, "drive 0 is perfectly clean", thd, 0, "%");
            else
            {
                if (thd <= prevThd) ++fails;
                if (h2 / h1 < 0.003) { ++fails; std::printf ("    FAIL: no even harmonic\n"); }
                if (std::fabs (20 * std::log10 (h1 / 0.5)) > 2.0) { ++fails; std::printf ("    FAIL: level jump\n"); }
            }
            prevThd = thd;
        }
        // Saturation must grow gradually with level, not switch on at a clip point:
        // THD at drive 0.5 measured every 3 dB from -36 to 0 dBFS must rise steadily
        // and never jump by more than 2.5x between neighbouring levels.
        double prev = -1, worstJump = 0; bool monotonic = true;
        std::printf ("    THD vs input level at drive 0.5:");
        for (double lvl = -36; lvl <= 0.01; lvl += 3)
        {
            std::vector<double> y;
            const double amp = std::pow (10.0, lvl / 20.0);
            for (int n = 0; n < 4800; ++n) y.push_back (classASaturate (amp * std::sin (2 * kPi * 1000.0 * n / fs), 0.5));
            const double h1 = harmonic (y, fs, 1000, 1), h2 = harmonic (y, fs, 1000, 2), h3 = harmonic (y, fs, 1000, 3);
            const double thd = std::sqrt (h2 * h2 + h3 * h3) / h1;
            std::printf (" %.1f%%", thd * 100);
            if (prev > 0) { if (thd < prev) monotonic = false; worstJump = std::max (worstJump, thd / prev); }
            prev = thd;
        }
        std::printf ("\n");
        check (monotonic, "harmonics rise steadily with input level (no early clip)", monotonic ? 1 : 0, 1, "");
        check (worstJump < 2.5, "largest THD step between 3 dB levels", worstJump, 2.5, "x");
        double minStep = 1e9;   // the transfer curve keeps rising across the whole range
        for (double x = -1.0; x < 1.0; x += 0.001) minStep = std::min (minStep, classASaturate (x + 0.001, 1.0) - classASaturate (x, 1.0));
        check (minStep > 0, "curve strictly rising to full scale at max drive", minStep * 1e3, 0, "e-3");
    }

    std::printf ("--- CPU\n");
    {
        StackedFetComp::Settings st; st.on = true; st.stacked = true; st.drive = 0.6f; st.inputDb = 10;
        StackedFetComp sc; sc.prepare (fs, st);
        const int N = (int) fs * 10;
        double acc = 0;
        const auto t0 = std::chrono::steady_clock::now();
        for (int n = 0; n < N; ++n) acc += sc.process (0.3 * std::sin (2 * kPi * 220.0 * n / fs));
        const double sec = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        std::printf ("  10 s of audio processed in %.3f s (%.1f%% of one core)  [%g]\n", sec, sec / 10 * 100, acc * 0);
    }

    std::printf ("\n%s (%d failures)\n", fails ? "FAILED" : "ALL PASSED", fails);
    return fails ? 1 : 0;
}
