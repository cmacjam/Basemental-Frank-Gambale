// Exports the Basemental FG 250 as files for NAM loaders (TONE3000, LAVA STUDIO, NAM plug-in…)
//
//   nam_export list                         list the capture presets
//   nam_export capture <input.wav> <outdir> reamp a NAM input file (e.g. v3_0_0.wav)
//                                           through every amp preset: <outdir>/<name>.wav
//   nam_export irs <outdir>                 write the 8 cabinets x 3 mic positions as IR .wav files
//
// Captures are the AMP HEAD only (Output = Power only: no cabinet, reverb, compressor or
// guitar voice) so they pair with any IR, just like a real amp capture.
// Build: g++ -O2 -std=c++17 -ISource tools/nam_export.cpp -o nam_export
#include "AmpCore.h"
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>
#include <algorithm>

// ------------------------------------------------------------------ WAV i/o
struct Wav { int fs = 48000; std::vector<float> x; };

static bool readWav (const std::string& path, Wav& w)
{
    FILE* f = std::fopen (path.c_str(), "rb");
    if (! f) return false;
    std::vector<uint8_t> d;
    { uint8_t buf[65536]; size_t n; while ((n = std::fread (buf, 1, sizeof buf, f)) > 0) d.insert (d.end(), buf, buf + n); }
    std::fclose (f);
    if (d.size() < 12 || std::memcmp (d.data(), "RIFF", 4) || std::memcmp (d.data() + 8, "WAVE", 4)) return false;
    auto u16 = [&] (size_t p) { return (uint32_t) d[p] | ((uint32_t) d[p + 1] << 8); };
    auto u32 = [&] (size_t p) { return u16 (p) | (u16 (p + 2) << 16); };
    int fmt = 0, ch = 0, bits = 0;
    size_t p = 12;
    while (p + 8 <= d.size())
    {
        const size_t len = u32 (p + 4), body = p + 8;
        if (! std::memcmp (d.data() + p, "fmt ", 4))
        {
            fmt = (int) u16 (body); ch = (int) u16 (body + 2); w.fs = (int) u32 (body + 4); bits = (int) u16 (body + 14);
            if (fmt == 0xFFFE && len >= 26) fmt = (int) u16 (body + 24);   // WAVE_FORMAT_EXTENSIBLE
        }
        else if (! std::memcmp (d.data() + p, "data", 4))
        {
            const int bps = bits / 8;
            const size_t frames = std::min (len, d.size() - body) / (size_t) (bps * ch);
            w.x.resize (frames);
            for (size_t i = 0; i < frames; ++i)
            {
                const uint8_t* s = d.data() + body + i * (size_t) (bps * ch);   // first channel
                float v = 0;
                if (fmt == 3 && bits == 32) std::memcpy (&v, s, 4);
                else if (bits == 16) v = (float) (int16_t) (s[0] | (s[1] << 8)) / 32768.0f;
                else if (bits == 24) v = (float) ((int32_t) ((uint32_t) s[0] << 8 | (uint32_t) s[1] << 16 | (uint32_t) s[2] << 24) >> 8) / 8388608.0f;
                else if (bits == 32) v = (float) ((double) (int32_t) (s[0] | s[1] << 8 | s[2] << 16 | (uint32_t) s[3] << 24) / 2147483648.0);
                w.x[i] = v;
            }
            return true;
        }
        p = body + len + (len & 1);
    }
    return false;
}

static void writeWav24 (const std::string& path, const std::vector<float>& x, int fs)
{
    FILE* f = std::fopen (path.c_str(), "wb");
    if (! f) { std::fprintf (stderr, "can't write %s\n", path.c_str()); std::exit (1); }
    const uint32_t n = (uint32_t) x.size(), dataBytes = n * 3;
    auto u32 = [&] (uint32_t v) { std::fwrite (&v, 4, 1, f); };
    auto u16 = [&] (uint16_t v) { std::fwrite (&v, 2, 1, f); };
    std::fwrite ("RIFF", 1, 4, f); u32 (36 + dataBytes); std::fwrite ("WAVEfmt ", 1, 8, f);
    u32 (16); u16 (1); u16 (1); u32 ((uint32_t) fs); u32 ((uint32_t) fs * 3); u16 (3); u16 (24);
    std::fwrite ("data", 1, 4, f); u32 (dataBytes);
    for (float v : x)
    {
        const int32_t s = (int32_t) std::lrint (std::clamp ((double) v, -1.0, 8388607.0 / 8388608.0) * 8388608.0);
        const uint8_t b[3] = { (uint8_t) s, (uint8_t) (s >> 8), (uint8_t) (s >> 16) };
        std::fwrite (b, 1, 3, f);
    }
    std::fclose (f);
}

// ------------------------------------------------------------- capture list
// Same knob settings as the plug-in's amp presets (PluginProcessor.cpp), one capture per
// channel actually used. Clean = { Gain, Low, Mid, High, Level }, Lead the same.
struct Capture { const char* file; const char* name; int channel; float k[5]; };
static const Capture captures[] = {
    { "FG250_Clean_Init",             "FG 250 Clean (Init)",              0, { 4.0f, 5.0f, 5.0f, 5.0f, 5.0f } },
    { "FG250_Clean_Glassy",           "FG 250 Glassy Clean",              0, { 3.5f, 4.5f, 5.0f, 6.2f, 5.0f } },
    { "FG250_Clean_Jazz_Neck",        "FG 250 Jazz Neck Clean",           0, { 4.5f, 5.0f, 5.5f, 4.0f, 5.0f } },
    { "FG250_Clean_Edge_of_Breakup",  "FG 250 Edge of Breakup",           0, { 7.5f, 4.5f, 6.0f, 5.5f, 3.0f } },
    { "FG250_Lead_Init",              "FG 250 Lead (Init, gain 0.51)",    1, { 0.51f, 4.5f, 6.5f, 5.0f, 5.0f } },
    { "FG250_Lead_Fusion_Crunch",     "FG 250 Fusion Crunch Rhythm",      1, { 3.5f, 4.5f, 6.0f, 5.5f, 5.4f } },
    { "FG250_Lead_Sweep",             "FG 250 Sweep Lead",                1, { 5.5f, 4.0f, 6.5f, 5.0f, 5.0f } },
    { "FG250_Lead_Ballad",            "FG 250 Ballad Lead",               1, { 6.5f, 4.5f, 6.5f, 4.5f, 5.0f } },
    { "FG250_Lead_Fat_Round_Neck",    "FG 250 Fat Round Lead",            1, { 7.0f, 5.0f, 7.0f, 3.8f, 5.0f } },
    { "FG250_Lead_Singing_Sustain",   "FG 250 Singing Sustain",           1, { 8.5f, 4.5f, 7.5f, 4.2f, 5.0f } },
};

static fusion::Params captureParams (const Capture& c)
{
    fusion::Params p;
    p.channel = c.channel;
    for (int i = 0; i < 5; ++i) (c.channel ? p.lead : p.clean)[i] = c.k[i];
    p.reverbOn = false; p.reverb = 0;
    p.guitar = 0;               // Off: the capture is the amp, not a guitar EQ
    p.output = 2;               // Power only: no cabinet (pair the capture with an IR)
    p.comp.on = false;          // FET Stack off: straight into the amp
    p.inDb = 0; p.outDb = 0;
    return p;
}

static std::vector<float> render (const Capture& c, const std::vector<float>& in, int fs)
{
    fusion::AmpCore amp;
    amp.setParams (captureParams (c));
    amp.prepare (fs);
    std::vector<float> L (in.size()), R (in.size());
    const int block = 512;
    for (size_t s = 0; s < in.size(); s += block)
    {
        const int n = (int) std::min<size_t> (block, in.size() - s);
        amp.process (in.data() + s, L.data() + s, R.data() + s, n);
    }
    return L;   // reverb is off, so L == R
}

// ---------------------------------------------------------------- IR export
static std::vector<float> cabIR (int cab, int mic, int fs, int len)
{
    fusion::CabSim c;
    c.setup (fs, cab, mic);
    c.reset();
    std::vector<float> ir ((size_t) len);
    for (int i = 0; i < len; ++i) ir[(size_t) i] = (float) c.process (i == 0 ? 1.0 : 0.0);
    const int fade = len / 8;   // short raised-cosine fade so the truncated tail can't click
    for (int i = 0; i < fade; ++i) ir[(size_t) (len - fade + i)] *= (float) (0.5 + 0.5 * std::cos (M_PI * i / fade));
    return ir;
}

int main (int argc, char** argv)
{
    const std::string cmd = argc > 1 ? argv[1] : "";
    if (cmd == "list")
    {
        for (auto& c : captures) std::printf ("%s\t%s\n", c.file, c.name);
        return 0;
    }
    if (cmd == "capture" && argc >= 4)
    {
        Wav in;
        if (! readWav (argv[2], in)) { std::fprintf (stderr, "can't read %s\n", argv[2]); return 1; }
        const std::string only = argc > 4 ? argv[4] : "";
        for (auto& c : captures)
        {
            if (! only.empty() && only != c.file) continue;
            auto y = render (c, in.x, in.fs);
            float pk = 0; for (float v : y) pk = std::max (pk, std::fabs (v));
            // NAM wants an unclipped recording: if the head is louder than -1 dBFS, turn the
            // return down (a level change only; the tone is untouched).
            const float lim = std::pow (10.0f, -1.0f / 20.0f);
            const float g = pk > lim ? lim / pk : 1.0f;
            for (auto& v : y) v *= g;
            writeWav24 (std::string (argv[3]) + "/" + c.file + ".wav", y, in.fs);
            std::printf ("%-30s peak %6.1f dBFS  return trim %5.1f dB\n", c.file, 20 * std::log10 (pk + 1e-12f), 20 * std::log10 (g));
        }
        return 0;
    }
    if (cmd == "irs" && argc >= 3)
    {
        const int fsList[] = { 48000, 44100 };
        static const char* micFile[] = { "OnAxis", "OffAxis", "Close" };
        for (int fs : fsList)
        {
            const int len = 2048;   // 43 ms at 48 kHz: the cabinet curves have died away by then
            // one common scale: every IR has the loudness of the Fusion 1x12 on-axis, peak <= -1 dBFS
            std::vector<std::vector<float>> irs;
            std::vector<std::string> names;
            for (int cab = 0; cab < fusion::kNumCabs; ++cab)
                for (int mic = 0; mic < 3; ++mic)
                {
                    irs.push_back (cabIR (cab, mic, fs, len));
                    std::string n = fusion::cabName (cab);
                    for (auto& ch : n) if (ch == ' ') ch = '_';
                    char buf[200];
                    std::snprintf (buf, sizeof buf, "%s/%d/FG250_IR_%02d_%s_%s_%dk.wav", argv[2], fs / 1000, cab + 1, n.c_str(), micFile[mic], fs / 1000);
                    names.push_back (buf);
                }
            auto energy = [] (const std::vector<float>& v) { double e = 0; for (float s : v) e += (double) s * s; return std::sqrt (e); };
            const double ref = energy (irs[0]);
            double worst = 0;
            for (auto& ir : irs) { const double g = ref / energy (ir); for (auto& s : ir) s *= (float) g; for (float s : ir) worst = std::max (worst, (double) std::fabs (s)); }
            const double g = std::pow (10.0, -1.0 / 20.0) / worst;
            for (size_t i = 0; i < irs.size(); ++i)
            {
                for (auto& s : irs[i]) s *= (float) g;
                writeWav24 (names[i], irs[i], fs);
            }
            std::printf ("%zu IRs at %d Hz, %d samples\n", irs.size(), fs, len);
        }
        return 0;
    }
    std::fprintf (stderr, "usage: nam_export list | capture <input.wav> <outdir> [name] | irs <outdir>\n");
    return 1;
}
