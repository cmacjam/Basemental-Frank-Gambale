// Offline test: plucked-string "guitar" through every channel/output mode.
// Build: g++ -O2 -std=c++17 -I../Source render_test.cpp -o render_test
#include "AmpCore.h"
#include <cstdio>
#include <cstdint>
#include <random>
#include <string>

static std::vector<float> pluckedGuitar (double fs, double seconds)
{
    std::vector<float> out ((size_t) (fs * seconds), 0.0f);
    const double notes[] = { 110.0, 146.83, 196.0, 246.94, 329.63, 440.0, 329.63, 246.94 };
    std::mt19937 rng (1);
    std::uniform_real_distribution<float> d (-1, 1);
    const size_t step = (size_t) (fs * seconds / 8);
    for (int n = 0; n < 8; ++n)
    {
        const int len = (int) (fs / notes[n]);
        std::vector<float> line ((size_t) len);
        for (auto& v : line) v = d (rng) * 0.4f;
        int idx = 0;
        for (size_t i = n * step; i < out.size(); ++i)
        {
            const float a = line[(size_t) idx], b = line[(size_t) ((idx + 1) % len)];
            line[(size_t) idx] = 0.996f * 0.5f * (a + b);
            out[i] += a;
            idx = (idx + 1) % len;
        }
    }
    return out;
}

static void writeWav (const std::string& path, const std::vector<float>& L, const std::vector<float>& R, int fs)
{
    FILE* f = std::fopen (path.c_str(), "wb");
    const uint32_t n = (uint32_t) L.size(), dataBytes = n * 4;
    auto u32 = [&] (uint32_t v) { std::fwrite (&v, 4, 1, f); };
    auto u16 = [&] (uint16_t v) { std::fwrite (&v, 2, 1, f); };
    std::fwrite ("RIFF", 1, 4, f); u32 (36 + dataBytes); std::fwrite ("WAVEfmt ", 1, 8, f);
    u32 (16); u16 (1); u16 (2); u32 ((uint32_t) fs); u32 ((uint32_t) fs * 4); u16 (4); u16 (16);
    std::fwrite ("data", 1, 4, f); u32 (dataBytes);
    for (uint32_t i = 0; i < n; ++i)
        for (float v : { L[i], R[i] })
        {
            int16_t s = (int16_t) std::max (-32767.0f, std::min (32767.0f, v * 32767.0f));
            std::fwrite (&s, 2, 1, f);
        }
    std::fclose (f);
}

int main (int argc, char** argv)
{
    const std::string outDir = argc > 1 ? argv[1] : ".";
    int failures = 0;
    for (double fs : { 44100.0, 48000.0, 96000.0 })
    {
        auto in = pluckedGuitar (fs, 6.0);
        struct Case { const char* name; int ch; int out; float gain; float rev; };
        const Case cases[] = { { "clean_cab", 0, 1, 4, 3 }, { "clean_preout", 0, 0, 4, 3 },
                               { "lead_cab", 1, 1, 6, 2.5f }, { "lead_max_gain", 1, 1, 10, 0 },
                               { "lead_poweronly", 1, 2, 7, 2 } };
        for (auto& c : cases)
        {
            fusion::AmpCore amp;
            fusion::Params p;
            p.channel = c.ch; p.output = c.out; p.reverb = c.rev;
            p.clean[0] = c.gain; p.lead[0] = c.gain;
            amp.prepare (fs);
            amp.setParams (p);
            std::vector<float> L (in.size()), R (in.size());
            const int block = 256;
            for (size_t i = 0; i < in.size(); i += block)
            {
                const int n = (int) std::min<size_t> (block, in.size() - i);
                amp.process (in.data() + i, L.data() + i, R.data() + i, n);
            }
            double peak = 0, sum = 0; bool bad = false;
            for (size_t i = 0; i < L.size(); ++i)
            {
                if (! std::isfinite (L[i]) || ! std::isfinite (R[i])) bad = true;
                peak = std::max (peak, (double) std::fabs (L[i]));
                sum += (double) L[i] * L[i];
            }
            const double rmsDb = 10 * std::log10 (sum / L.size() + 1e-20), peakDb = 20 * std::log10 (peak + 1e-20);
            const bool ok = ! bad && peakDb < 3.0 && rmsDb > -50.0;
            if (! ok) ++failures;
            std::printf ("%6.0f Hz  %-16s rms %6.1f dBFS  peak %6.1f dBFS  %s\n", fs, c.name, rmsDb, peakDb, ok ? "OK" : "FAIL");
            if (fs == 48000.0) writeWav (outDir + "/" + c.name + ".wav", L, R, (int) fs);
        }
    }
    // channel-switch click test
    {
        const double fs = 48000; auto in = pluckedGuitar (fs, 2.0);
        fusion::AmpCore amp; fusion::Params p; amp.prepare (fs); amp.setParams (p);
        std::vector<float> L (in.size()), R (in.size());
        double maxJump = 0; float prev = 0;
        for (size_t i = 0; i < in.size(); i += 256)
        {
            p.channel = ((i / 256) % 40 < 20) ? 0 : 1; amp.setParams (p);
            const int n = (int) std::min<size_t> (256, in.size() - i);
            amp.process (in.data() + i, L.data() + i, R.data() + i, n);
            for (int k = 0; k < n; ++k) { maxJump = std::max (maxJump, (double) std::fabs (L[i + k] - prev)); prev = L[i + k]; }
        }
        std::printf ("channel switching: max sample jump %.3f %s\n", maxJump, maxJump < 0.5 ? "OK" : "FAIL");
        if (maxJump >= 0.5) ++failures;
    }
    std::printf (failures ? "\n%d FAILURES\n" : "\nALL PASSED\n", failures);
    return failures ? 1 : 0;
}
