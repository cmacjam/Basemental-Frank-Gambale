// Real-world stress tests for the strobe tuner:
// bends that settle, mains hum, clipped/distorted input, DC offset,
// 7-string and bass low notes, 88.2/192 kHz, and time-to-lock.
// Build: g++ -O2 -std=c++17 -I../Source tuner_stress_test.cpp -o tuner_stress_test
#include "StrobeTuner.h"
#include <cstdio>
#include <functional>
#include <random>

static const double kPi = 3.14159265358979323846;

struct Opts
{
    double humDb = -200;       // hum level relative to the note (dB)
    double humHz = 60;
    bool distort = false;
    double dc = 0;
    double bendCents = 0;      // start this far off, glide to target
    double bendStart = 0.15, bendLen = 0.08;
    bool weakFund = false;
};

// harmonic plucked tone whose fundamental follows f(t)
static std::vector<float> synth (double fs, double f0, double seconds, const Opts& o, std::mt19937& rng)
{
    std::uniform_real_distribution<double> ph (0, 2 * kPi);
    std::normal_distribution<double> noise (0, 1);
    const double B = 1.0e-4;
    const double amps[6] = { o.weakFund ? 0.15 : 1.0, 0.7, 0.45, 0.3, 0.2, 0.12 };
    double phs[6]; for (auto& p : phs) p = ph (rng);
    std::vector<float> x ((size_t) (fs * seconds));
    double humPh = ph (rng);
    for (size_t i = 0; i < x.size(); ++i)
    {
        const double t = i / fs;
        double c = 0;
        if (o.bendCents != 0)
        {
            if (t < o.bendStart) c = o.bendCents;
            else if (t < o.bendStart + o.bendLen) c = o.bendCents * (1.0 - (t - o.bendStart) / o.bendLen);
        }
        const double f = f0 * std::pow (2.0, c / 1200.0);
        double s = 0;
        for (int k = 1; k <= 6; ++k)
        {
            const double fk = k * f * std::sqrt (1 + B * k * k);
            phs[k - 1] += 2 * kPi * fk / fs;
            if (fk > fs * 0.45) continue;
            s += amps[k - 1] * std::exp (-t * (0.6 + 0.4 * k)) * std::sin (phs[k - 1]);
        }
        s *= 0.25;
        if (o.distort) s = std::tanh (4.0 * s) * 0.3;
        const double humA = 0.25 * std::pow (10.0, o.humDb / 20.0);
        s += humA * (std::sin (2 * kPi * o.humHz * t + humPh) + 0.5 * std::sin (2 * kPi * 3 * o.humHz * t + humPh));
        s += o.dc + 0.0003 * noise (rng);
        x[i] = (float) s;
    }
    return x;
}

static int fails = 0;
static double worst = 0;

static void check (const char* label, double fs, double nominal, double detune, const Opts& o, double atSec, std::mt19937& rng, double tol = 0.1)
{
    const double f0 = nominal * std::pow (2.0, detune / 1200.0);
    auto x = synth (fs, f0, atSec + 0.05, o, rng);
    fusion::StrobeTunerCore t;
    const auto r = t.analyse (x.data(), (int) (atSec * fs), fs, 440.0);
    const double trueF = f0 * std::sqrt (1 + 1.0e-4);
    const int expect = (int) std::lround (69 + 12 * std::log2 (trueF / 440.0));
    const double trueC = 1200 * std::log2 (trueF / r.refFreq);
    const double err = std::fabs (r.cents - trueC);
    const bool ok = r.valid && r.midiNote == expect && err < tol;
    if (! ok) ++fails; else worst = std::max (worst, err);
    std::printf ("%-34s fs=%6.0f  %7.2f Hz %+6.1fc -> %s%d %+8.3fc  err %.4f  %s\n", label, fs, nominal, detune,
                 fusion::StrobeTunerCore::noteName (r.midiNote), fusion::StrobeTunerCore::octaveOf (r.midiNote),
                 r.cents, err, ok ? "OK" : "FAIL");
}

// time from pluck until the reading is valid and within 0.1 cent
static double timeToLock (double fs, double nominal, double detune, std::mt19937& rng)
{
    Opts o;
    const double f0 = nominal * std::pow (2.0, detune / 1200.0);
    auto x = synth (fs, f0, 2.0, o, rng);
    const double trueF = f0 * std::sqrt (1 + 1.0e-4);
    fusion::StrobeTunerCore t;
    for (double at = 0.05; at < 2.0; at += 0.02)
    {
        const auto r = t.analyse (x.data(), (int) (at * fs), fs, 440.0);
        if (r.valid && std::fabs (r.cents - 1200 * std::log2 (trueF / r.refFreq)) < 0.1) return at;
    }
    return -1;
}

int main()
{
    std::mt19937 rng (11);
    const double E2 = 82.4069, A2 = 110.0, E4 = 329.628, A4 = 440.0;

    std::printf ("--- high sample rates\n");
    for (double fs : { 88200.0, 192000.0 })
        for (double f : { E2, A2, 196.0, E4, A4, 880.0 })
            for (double d : { -23.4, -0.3, 0.2, 11.0 })
                check ("clean pluck", fs, f, d, {}, 0.9, rng);

    std::printf ("--- low notes (7-string, bass)\n");
    for (double fs : { 44100.0, 48000.0, 96000.0 })
    {
        check ("B0 (5-string bass)", fs, 30.8677, 0.4, {}, 1.4, rng);
        check ("E1 (4-string bass)", fs, 41.2034, -1.2, {}, 1.2, rng);
        check ("A1", fs, 55.0, 0.7, {}, 1.0, rng);
        check ("B1 (7-string)", fs, 61.7354, -0.6, {}, 1.0, rng);
        Opts w; w.weakFund = true;
        check ("B1 weak fundamental", fs, 61.7354, 2.5, w, 1.0, rng);
    }

    std::printf ("--- string bends that settle\n");
    for (double f : { E2, A2, E4, A4 })
        for (double b : { -30.0, 25.0 })
        {
            Opts o; o.bendCents = b;
            check (b < 0 ? "bend up, then settle" : "bend down, then settle", 48000, f, 0.3, o, 0.85, rng);
        }

    std::printf ("--- mains hum (-40 dB, 50/60 Hz + 3rd harmonic)\n");
    for (double hz : { 50.0, 60.0 })
        for (double f : { 55.0, E2, A2, 146.832, E4 })
        {
            Opts o; o.humDb = -40; o.humHz = hz;
            check (hz == 50 ? "50 Hz hum" : "60 Hz hum", 48000, f, -0.4, o, 0.9, rng);
        }

    std::printf ("--- distorted input, DC offset\n");
    for (double f : { E2, A2, E4, A4 })
    {
        Opts d; d.distort = true;
        check ("hard-clipped (fuzz into tuner)", 48000, f, 0.8, d, 0.9, rng);
        Opts c; c.dc = 0.05;
        check ("DC offset 0.05", 48000, f, -0.8, c, 0.9, rng);
    }

    std::printf ("--- temperaments, global offset, manual note\n");
    {
        struct Case { const char* label; fusion::TunerSettings s; double playHz; int expectNote; double expectCents; };
        auto S = [] (int temp, int root, double off, bool man, int mn)
        { fusion::TunerSettings s; s.temperament = temp; s.root = root; s.offsetCents = off; s.manual = man; s.manualNote = mn; return s; };
        const double et = 1.0;
        const Case cases[] = {
            { "Just, root C: pure major 3rd E4",   S (1, 0, 0, false, 0), E4 * std::pow (2.0, -13.69 / 1200) * et, 64, 0.0 },
            { "Just, root C: E4 played ET",        S (1, 0, 0, false, 0), E4, 64, 13.69 },
            { "Pythagorean, root G: B3",           S (2, 7, 0, false, 0), 246.942 * std::pow (2.0, 7.82 / 1200), 59, 0.0 },
            { "1/4-comma meantone, root C: G3",    S (3, 0, 0, false, 0), 196.0 * std::pow (2.0, -3.42 / 1200), 55, 0.0 },
            { "Werckmeister III, root C: F#3",     S (4, 0, 0, false, 0), 184.997 * std::pow (2.0, -11.73 / 1200), 54, 0.0 },
            { "Global offset +7.5c: A4 at +7.5c",  S (0, 0, 7.5, false, 0), A4 * std::pow (2.0, 7.5 / 1200), 69, 0.0 },
            { "Global offset -3.2c: E2 ET",        S (0, 0, -3.2, false, 0), E2, 40, 3.2 },
            { "Manual A: A2 played +150c",         S (0, 0, 0, true, 9), A2 * std::pow (2.0, 150.0 / 1200), 45, 150.0 },
            { "Manual E: E4 played -210c",         S (0, 0, 0, true, 4), E4 * std::pow (2.0, -210.0 / 1200), 64, -210.0 },
            { "Manual G: G3 in tune",              S (0, 0, 0, true, 7), 196.0, 55, 0.0 },
        };
        for (auto& c : cases)
        {
            auto x = synth (48000, c.playHz / std::sqrt (1 + 1.0e-4), 0.95, {}, rng);
            fusion::StrobeTunerCore t;
            const auto r = t.analyse (x.data(), (int) (0.9 * 48000), 48000, c.s);
            const double err = std::fabs (r.cents - c.expectCents);
            const bool ok = r.valid && r.midiNote == c.expectNote && err < 0.1;
            if (! ok) ++fails; else worst = std::max (worst, err);
            std::printf ("%-38s -> %s%d %+9.3fc (expect %+.2f)  err %.4f  %s\n", c.label,
                         fusion::StrobeTunerCore::noteName (r.midiNote), fusion::StrobeTunerCore::octaveOf (r.midiNote),
                         r.cents, c.expectCents, err, ok ? "OK" : "FAIL");
        }
    }

    std::printf ("--- time to lock within 0.1 cent after the pluck\n");
    for (double f : { 41.2034, 55.0, E2, A2, 196.0, E4, A4, 880.0 })
    {
        const double t = timeToLock (48000, f, 0.4, rng);
        std::printf ("  %7.2f Hz: %s\n", f, t < 0 ? "never" : (std::to_string ((int) std::lround (t * 1000)) + " ms").c_str());
        if (t < 0 || t > 1.5) ++fails;
    }

    std::printf ("\n%d failures, worst error on passing cases %.4f cents\n", fails, worst);
    return fails ? 1 : 0;
}
