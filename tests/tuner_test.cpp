// Accuracy test for the strobe tuner: synthetic plucked notes with known
// detuning, decay, noise, inharmonicity and random partial phases.
// Build: g++ -O2 -std=c++17 -I../Source tuner_test.cpp -o tuner_test
#include "StrobeTuner.h"
#include <cstdio>
#include <random>

struct Tone { double f; double detune; bool weakFund; };

static std::vector<float> synth (double fs, double f0, bool weakFund, std::mt19937& rng, double seconds)
{
    std::uniform_real_distribution<double> ph (0, 2 * 3.14159265358979323846);
    std::normal_distribution<double> noise (0, 1);
    const double B = 1.0e-4;                       // string inharmonicity
    const double amps[6] = { weakFund ? 0.15 : 1.0, 0.7, 0.45, 0.3, 0.2, 0.12 };
    double phs[6]; for (auto& p : phs) p = ph (rng);
    std::vector<float> x ((size_t) (fs * seconds));
    for (size_t i = 0; i < x.size(); ++i)
    {
        const double t = i / fs;
        double s = 0;
        for (int k = 1; k <= 6; ++k)
        {
            const double fk = k * f0 * std::sqrt (1 + B * k * k);
            if (fk > fs * 0.45) break;
            s += amps[k - 1] * std::exp (-t * (0.8 + 0.4 * k)) * std::sin (2 * 3.14159265358979323846 * fk * t + phs[k - 1]);
        }
        x[i] = (float) (0.25 * s + 0.0003 * noise (rng));   // noise ~ -70 dBFS
    }
    return x;
}

int main()
{
    const double detunes[] = { -44.9, -12.5, -3.2, -0.7, -0.1, 0.0, 0.1, 0.4, 1.3, 7.7, 23.1, 48.0 };
    // A1 (low tunings), E2, A2 (Gambale low A), D3, E3/A3 (Gambale top strings), C4, E4, A4, E5, A5, C6
    const double notes[] = { 55.0, 82.4069, 110.0, 146.832, 164.814, 220.0, 261.626, 329.628, 440.0, 659.255, 880.0, 1046.5 };
    std::mt19937 rng (7);
    fusion::StrobeTunerCore tuner;
    double worst = 0; int count = 0, fails = 0;

    for (double fs : { 44100.0, 48000.0, 96000.0 })
        for (double refA : { 440.0, 432.0 })
            for (double nf : notes)
                for (double dc : detunes)
                    for (bool weak : { false, true })
                    {
                        if (weak && nf > 200) continue;             // weak fundamental only on low strings
                        const double ideal = nf * refA / 440.0;
                        const double f0 = ideal * std::pow (2.0, dc / 1200.0);
                        auto x = synth (fs, f0, weak, rng, 1.2);
                        // true pitch of the fundamental partial (includes inharmonicity)
                        const double trueF = f0 * std::sqrt (1 + 1.0e-4);
                        const int n = (int) (0.9 * fs);           // analyse 0.9 s after the pluck
                        const auto r = tuner.analyse (x.data(), n, fs, refA);
                        // reference: cents from measured freq to the note it reported
                        const double trueCents = 1200.0 * std::log2 (trueF / r.refFreq);
                        const double err = std::fabs (r.cents - trueCents);
                        const int expectNote = (int) std::lround (69 + 12 * std::log2 (trueF / refA));
                        const bool ok = r.valid && err < 0.1 && r.midiNote == expectNote;
                        ++count;
                        if (! ok)
                        {
                            ++fails;
                            std::printf ("FAIL fs=%.0f A=%.0f f=%.2f det=%+.1f weak=%d -> valid=%d note=%d(exp %d) cents=%+.3f true=%+.3f conf=%.2f h=%d\n",
                                         fs, refA, nf, dc, weak, r.valid, r.midiNote, expectNote, r.cents, trueCents, r.confidence, r.harmonic);
                        }
                        else worst = std::max (worst, err);
                    }

    std::printf ("%d cases, %d failures, worst error on passing cases: %.4f cents\n", count, fails, worst);

    // silence and noise must not produce a reading
    std::vector<float> silence (48000, 0.0f), noise (48000);
    std::normal_distribution<double> g (0, 0.05);
    for (auto& v : noise) v = (float) g (rng);
    const auto rs = tuner.analyse (silence.data(), 48000, 48000, 440);
    const auto rn = tuner.analyse (noise.data(), 48000, 48000, 440);
    std::printf ("silence -> %s, white noise -> %s\n", rs.valid ? "FALSE READING" : "no reading (ok)", rn.valid ? "FALSE READING" : "no reading (ok)");
    if (rs.valid || rn.valid) ++fails;
    return fails ? 1 : 0;
}
