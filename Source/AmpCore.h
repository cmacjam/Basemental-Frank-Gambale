#pragma once
// =============================================================================
//  Fusion 250 — DSP core (header-only, no JUCE dependency)
//
//  Signal chain follows the FG Little 250 Amplitude manual's block diagram:
//
//    Input -> [Guitar voice] -> CLEAN: Gain -> Tones -> Level  --+
//                            -> LEAD : Gain -> Tones -> Level  --+-> Reverb
//         -> (Pre Out tap, 0/-6 dB) -> Power amp (Class-D style) -> Cab -> Out
//
//  Everything marked [VOICING] is an engineering estimate: DV Mark has not
//  published EQ frequencies, clipping topology or the "Amplitude" filtering.
//  Tune those constants against measurements of a real unit.
// =============================================================================

#include <algorithm>
#include <cmath>
#include <vector>

namespace fusion
{
static constexpr double kPi = 3.14159265358979323846;

inline double dbToLin (double db) { return std::pow (10.0, db / 20.0); }

// ---------------------------------------------------------------- Biquad (RBJ)
struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;

    inline double process (double x)
    {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
    void reset() { z1 = z2 = 0; }

    void set (double B0, double B1, double B2, double A0, double A1, double A2)
    {
        b0 = B0 / A0; b1 = B1 / A0; b2 = B2 / A0; a1 = A1 / A0; a2 = A2 / A0;
    }
    static double clampF (double fs, double f) { return std::min (std::max (f, 5.0), fs * 0.49); }

    void lowpass (double fs, double f, double q)
    {
        f = clampF (fs, f);
        const double w = 2 * kPi * f / fs, c = std::cos (w), al = std::sin (w) / (2 * q);
        set ((1 - c) / 2, 1 - c, (1 - c) / 2, 1 + al, -2 * c, 1 - al);
    }
    void highpass (double fs, double f, double q)
    {
        f = clampF (fs, f);
        const double w = 2 * kPi * f / fs, c = std::cos (w), al = std::sin (w) / (2 * q);
        set ((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + al, -2 * c, 1 - al);
    }
    void peak (double fs, double f, double q, double db)
    {
        f = clampF (fs, f);
        const double A = std::pow (10.0, db / 40.0);
        const double w = 2 * kPi * f / fs, c = std::cos (w), al = std::sin (w) / (2 * q);
        set (1 + al * A, -2 * c, 1 - al * A, 1 + al / A, -2 * c, 1 - al / A);
    }
    void lowShelf (double fs, double f, double db, double q = 0.707)
    {
        f = clampF (fs, f);
        const double A = std::pow (10.0, db / 40.0);
        const double w = 2 * kPi * f / fs, c = std::cos (w), al = std::sin (w) / (2 * q);
        const double sq = 2 * std::sqrt (A) * al;
        set (A * ((A + 1) - (A - 1) * c + sq), 2 * A * ((A - 1) - (A + 1) * c),
             A * ((A + 1) - (A - 1) * c - sq), (A + 1) + (A - 1) * c + sq,
             -2 * ((A - 1) + (A + 1) * c), (A + 1) + (A - 1) * c - sq);
    }
    void highShelf (double fs, double f, double db, double q = 0.707)
    {
        f = clampF (fs, f);
        const double A = std::pow (10.0, db / 40.0);
        const double w = 2 * kPi * f / fs, c = std::cos (w), al = std::sin (w) / (2 * q);
        const double sq = 2 * std::sqrt (A) * al;
        set (A * ((A + 1) + (A - 1) * c + sq), -2 * A * ((A - 1) + (A + 1) * c),
             A * ((A + 1) + (A - 1) * c - sq), (A + 1) - (A - 1) * c + sq,
             2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - sq);
    }
    void identity() { set (1, 0, 0, 1, 0, 0); }
};

// ------------------------------------------------------------ one-pole smoother
struct Smoother
{
    double cur = 0, target = 0, coef = 0.999;
    void prepare (double fs, double ms) { coef = std::exp (-1.0 / (fs * ms * 0.001)); }
    void snap (double v) { cur = target = v; }
    inline double next() { cur = target + coef * (cur - target); return cur; }
};

// ---------------------------------------------- 4x IIR oversampler (8th-order)
struct Oversampler4x
{
    static constexpr int F = 4;
    Biquad up[4], dn[4];

    void prepare (double fs)
    {
        // 8th-order Butterworth as 4 cascaded biquads
        static const double qs[4] = { 0.50979558, 0.60134489, 0.89997622, 2.56291545 };
        const double fo = fs * F, fc = std::min (fs * 0.45, 20000.0);
        for (int i = 0; i < 4; ++i) { up[i].lowpass (fo, fc, qs[i]); dn[i] = up[i]; }
        reset();
    }
    void reset() { for (auto& b : up) b.reset(); for (auto& b : dn) b.reset(); }

    template <typename Fn>
    inline double process (double x, Fn&& fn)
    {
        double out = 0;
        for (int k = 0; k < F; ++k)
        {
            double s = (k == 0) ? x * F : 0.0;
            for (auto& b : up) s = b.process (s);
            s = fn (s);
            for (auto& b : dn) s = b.process (s);
            out = s;
        }
        return out;
    }
};

// ------------------------------------------------------------- waveshapers
// Near-linear below ~0.5, smooth knee, asymptote +/-1 (solid-state, op-amp-ish)
inline double softSat (double x) { return x / std::sqrt (std::sqrt (1.0 + x * x * x * x)); }
// Asymmetric soft clip (adds even harmonics), DC removed downstream
inline double asymClip (double x, double bias) { return std::tanh (x + bias) - std::tanh (bias); }

// ------------------------------------------------------------- 3-band "Tones"
struct ToneStack
{
    Biquad low, mid, high;
    double lowF = 110, midF = 750, highF = 3000, midQ = 0.7;

    void setup (double fs, double lowKnob, double midKnob, double highKnob)
    {
        // knob 0..10, 5 = flat.  Shelves +/-15 dB, broad mid +/-12 dB [VOICING]
        low.lowShelf (fs, lowF, (lowKnob - 5.0) * 3.0, 0.6);
        mid.peak (fs, midF, midQ, (midKnob - 5.0) * 2.4);
        high.highShelf (fs, highF, (highKnob - 5.0) * 3.0, 0.6);
    }
    inline double process (double x) { return high.process (mid.process (low.process (x))); }
    void reset() { low.reset(); mid.reset(); high.reset(); }
};

inline double levelKnobToGain (double k)
{
    // 0 = silent, 5 = -4 dB, 10 = +14 dB (top of the range drives the power stage)
    if (k <= 0.02) return 0.0;
    const double g = dbToLin (-22.0 + 3.6 * k);
    return k < 0.5 ? g * (k / 0.5) : g;
}

// =============================================================== Clean channel
struct CleanChannel
{
    double fs = 48000;
    Biquad hpIn, glass;
    Oversampler4x os;
    ToneStack tones;
    Smoother gain, level;

    void prepare (double sr)
    {
        fs = sr;
        hpIn.highpass (fs, 40, 0.707);
        glass.peak (fs, 3200, 1.0, 1.5); // [VOICING] slight "glassy" presence
        os.prepare (fs);
        tones.lowF = 110; tones.midF = 750; tones.highF = 3200;
        gain.prepare (fs, 20); level.prepare (fs, 20);
        reset();
    }
    void reset() { hpIn.reset(); glass.reset(); os.reset(); tones.reset(); }

    void set (const float* k, bool snap)
    {
        tones.setup (fs, k[1], k[2], k[3]);
        const double g = dbToLin (-6.0 + 2.4 * k[0]);   // -6 .. +18 dB of headroom use
        const double l = levelKnobToGain (k[4]);
        if (snap) { gain.snap (g); level.snap (l); } else { gain.target = g; level.target = l; }
    }

    inline double process (double x)
    {
        const double g = gain.next();
        double y = os.process (hpIn.process (x), [g] (double s) { return softSat (s * g); });
        y = tones.process (glass.process (y));
        return y * level.next();
    }
};

// ================================================================ Lead channel
struct LeadChannel
{
    double fs = 48000;
    Biquad hpIn, preMid, preHigh, interHP, interLP, dcBlock, postLP1, postLP2;
    Oversampler4x os;
    ToneStack tones;
    Smoother g1, g2, level;

    void prepare (double sr)
    {
        fs = sr;
        const double fo = fs * Oversampler4x::F;
        // [VOICING] "Amplitude" pre-shaping: tight lows, forward mids,
        //            softened pick transients before the clipping stages
        hpIn.highpass (fs, 110, 0.707);
        preMid.peak (fs, 850, 0.8, 5.0);
        preHigh.highShelf (fs, 3000, -4.0, 0.7);
        interHP.highpass (fo, 90, 0.6);
        interLP.lowpass (fo, 7000, 0.7);
        dcBlock.highpass (fs, 20, 0.707);
        postLP1.lowpass (fs, 5800, 0.6);   // de-fizz, round top end
        postLP2.lowpass (fs, 9500, 0.707);
        os.prepare (fs);
        tones.lowF = 100; tones.midF = 800; tones.highF = 2800;
        g1.prepare (fs, 25); g2.prepare (fs, 25); level.prepare (fs, 20);
        reset();
    }
    void reset()
    {
        for (auto* b : { &hpIn, &preMid, &preHigh, &interHP, &interLP, &dcBlock, &postLP1, &postLP2 }) b->reset();
        os.reset(); tones.reset();
    }

    void set (const float* k, bool snap)
    {
        tones.setup (fs, k[1], k[2], k[3]);
        const double gainDb = 12.0 + 4.3 * k[0];           // 12 .. 55 dB total
        const double a = dbToLin (gainDb * 0.55), b = dbToLin (gainDb * 0.45);
        const double l = levelKnobToGain (k[4]) * 0.19;     // match clean loudness at equal Level
        if (snap) { g1.snap (a); g2.snap (b); level.snap (l); }
        else { g1.target = a; g2.target = b; level.target = l; }
    }

    inline double process (double x)
    {
        const double a = g1.next(), b = g2.next();
        double y = preHigh.process (preMid.process (hpIn.process (x)));
        y = os.process (y, [&] (double s)
        {
            s = asymClip (s * a, 0.25);                       // stage 1
            s = interLP.process (interHP.process (s));
            return softSat (std::tanh (s * b));               // stage 2
        });
        y = postLP2.process (postLP1.process (dcBlock.process (y)));
        y = tones.process (y);
        return y * level.next();
    }
};

// ===================================================== shared digital reverb
struct Reverb
{
    struct Comb
    {
        std::vector<float> buf; int idx = 0; float store = 0;
        inline float process (float in, float fb, float damp)
        {
            const float out = buf[(size_t) idx];
            store = out * (1.0f - damp) + store * damp;
            buf[(size_t) idx] = in + store * fb;
            if (++idx >= (int) buf.size()) idx = 0;
            return out;
        }
    };
    struct Allpass
    {
        std::vector<float> buf; int idx = 0;
        inline float process (float in)
        {
            const float b = buf[(size_t) idx];
            buf[(size_t) idx] = in + b * 0.5f;
            if (++idx >= (int) buf.size()) idx = 0;
            return b - in;
        }
    };

    Comb combs[8]; Allpass aps[4];
    Biquad inHP, inLP;
    float fb = 0.83f, damp = 0.35f;

    void prepare (double fs, int spread)
    {
        static const int cl[8] = { 1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617 };
        static const int al[4] = { 556, 441, 341, 225 };
        const double sc = fs / 44100.0;
        for (int i = 0; i < 8; ++i) combs[i].buf.assign ((size_t) std::max (8, (int) ((cl[i] + spread) * sc)), 0.0f);
        for (int i = 0; i < 4; ++i) aps[i].buf.assign ((size_t) std::max (8, (int) ((al[i] + spread) * sc)), 0.0f);
        inHP.highpass (fs, 150, 0.707);   // warm: keep lows and fizz out of the tank
        inLP.lowpass (fs, 5000, 0.707);
        reset();
    }
    void reset()
    {
        for (auto& c : combs) { std::fill (c.buf.begin(), c.buf.end(), 0.0f); c.store = 0; c.idx = 0; }
        for (auto& a : aps) { std::fill (a.buf.begin(), a.buf.end(), 0.0f); a.idx = 0; }
        inHP.reset(); inLP.reset();
    }
    inline float process (float x)
    {
        const float in = (float) inLP.process (inHP.process (x)) * 0.015f;
        float acc = 0;
        for (auto& c : combs) acc += c.process (in, fb, damp);
        for (auto& a : aps) acc = a.process (acc);
        return acc;
    }
};

// ======================================================= built-in cabinet sim
struct CabSim
{
    Biquad f[6];
    void prepare (double fs)
    {
        // [VOICING] generic closed-back 12" character
        f[0].highpass (fs, 75, 0.9);
        f[1].peak (fs, 110, 1.2, 4.0);      // low resonance
        f[2].peak (fs, 450, 1.0, -2.5);     // de-box
        f[3].peak (fs, 2400, 1.4, 3.0);     // presence / cone peak
        f[4].lowpass (fs, 5200, 0.7);
        f[5].lowpass (fs, 6500, 0.55);
        reset();
    }
    void reset() { for (auto& b : f) b.reset(); }
    inline double process (double x) { for (auto& b : f) x = b.process (x); return x * 0.9; }
};

// ================================================== guitar voicing (approx.)
struct GuitarVoice
{
    Biquad f[3]; int mode = 0;
    void setup (double fs, int m)
    {
        mode = m;
        for (auto& b : f) b.identity();
        switch (m)
        {
            case 1: // FG1 neck: chambered mahogany, warm humbucker
                f[0].peak (fs, 220, 0.8, 2.0); f[1].peak (fs, 2300, 1.8, 1.5); f[2].highShelf (fs, 3500, -3.0); break;
            case 2: // FG1 bridge: Beryllium humbucker
                f[0].lowShelf (fs, 150, -1.5); f[1].peak (fs, 3200, 1.8, 2.5); f[2].highShelf (fs, 6000, 1.0); break;
            case 3: // FG2: alder body, Twinblade / Beryllium, brighter
                f[0].lowShelf (fs, 120, -2.0); f[1].peak (fs, 4000, 2.0, 3.0); f[2].highShelf (fs, 5000, 1.5); break;
            default: break;
        }
    }
    void reset() { for (auto& b : f) b.reset(); }
    inline double process (double x) { if (mode == 0) return x; for (auto& b : f) x = b.process (x); return x; }
};

// ================================================================== the amp
struct Params
{
    int channel = 0;                          // 0 = Clean, 1 = Lead
    float clean[5] = { 4, 5, 5, 5, 5 };       // Gain, Low, Mid, High, Level (0..10)
    float lead[5]  = { 6, 4.5f, 6.5f, 5, 5 };
    float reverb = 2.5f;                      // 0..10
    bool reverbOn = true;                     // footswitch
    int guitar = 0;                           // 0 Off, 1 FG1 neck, 2 FG1 bridge, 3 FG2
    int output = 1;                           // 0 Pre Out, 1 Power + built-in cab, 2 Power only (external IR)
    bool pad6 = false;                        // Pre Out 0/-6 dB
    float inDb = 0, outDb = 0;
};

class AmpCore
{
public:
    void prepare (double sampleRate)
    {
        fs = sampleRate;
        clean.prepare (fs); lead.prepare (fs);
        revL.prepare (fs, 0); revR.prepare (fs, 23);
        for (auto& c : cab) c.prepare (fs);
        xfade.prepare (fs, 15); wet.prepare (fs, 30); outG.prepare (fs, 20); inG.prepare (fs, 20);
        first = true;
        setParams (p);
    }

    void reset()
    {
        clean.reset(); lead.reset(); revL.reset(); revR.reset(); voice.reset();
        for (auto& c : cab) c.reset();
    }

    void setParams (const Params& np)
    {
        p = np;
        clean.set (p.clean, first);
        lead.set (p.lead, first);
        if (p.guitar != voice.mode) { voice.setup (fs, p.guitar); voice.reset(); }
        const double w = p.reverbOn ? std::pow (p.reverb / 10.0, 1.5) * 0.9 : 0.0;
        const double o = dbToLin (p.outDb) * ((p.output == 0 && p.pad6) ? 0.5 : 1.0);
        const double x = p.channel ? 1.0 : 0.0, i = dbToLin (p.inDb);
        if (first) { wet.snap (w); outG.snap (o); xfade.snap (x); inG.snap (i); first = false; }
        else { wet.target = w; outG.target = o; xfade.target = x; inG.target = i; }
    }

    // mono in, stereo out (stereo comes only from the reverb tails)
    void process (const float* in, float* outL, float* outR, int n)
    {
        for (int s = 0; s < n; ++s)
        {
            const double x = voice.process (in[s] * inG.next());
            const double mix = xfade.next();
            double m = 0;
            if (mix < 0.9999) m += clean.process (x) * (1.0 - mix); else clean.process (x);
            if (mix > 0.0001) m += lead.process (x) * mix;           else lead.process (x);

            const double w = wet.next();
            double L = m + revL.process ((float) m) * w;
            double R = m + revR.process ((float) m) * w;

            if (p.output != 0)            // MPT Class-D: clean, soft limit near rail
            {
                L = softSat (L * 0.8) / 0.8;
                R = softSat (R * 0.8) / 0.8;
                if (p.output == 1) { L = cab[0].process (L); R = cab[1].process (R); }
            }
            const double g = outG.next();
            outL[s] = (float) (L * g);
            outR[s] = (float) (R * g);
        }
    }

private:
    double fs = 48000;
    Params p;
    bool first = true;
    CleanChannel clean; LeadChannel lead;
    Reverb revL, revR;
    CabSim cab[2];
    GuitarVoice voice;
    Smoother xfade, wet, outG, inG;
};

} // namespace fusion
