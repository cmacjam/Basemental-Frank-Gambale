#pragma once
// =============================================================================
//  Fusion 250 — strobe tuner core (header-only, no JUCE dependency)
//
//  How it works (the same principle as a mechanical / electronic strobe):
//   1. A coarse pitch detector (YIN on a decimated copy) picks the nearest
//      equal-tempered note.
//   2. The input is heterodyned against a perfect reference oscillator at that
//      note (or one of its harmonics) in short Hann-windowed blocks.
//   3. The reference-relative phase of each block is unwrapped and a weighted
//      least-squares line is fitted. Its slope is the frequency error:
//      a stationary strobe = zero slope = in tune.
//
//  Resolution comes from the phase fit over ~0.35 s, not from FFT bins, so a
//  steady note is resolved far finer than 0.1 cent. Absolute accuracy is
//  ultimately bounded by the audio interface's clock (typ. +/-20-50 ppm,
//  i.e. +/-0.03-0.09 cent).
// =============================================================================

#include <algorithm>
#include <atomic>
#include <cmath>
#include <complex>
#include <cstdint>
#include <vector>

namespace fusion
{

// ------------------------------------------- single-producer sample ring
class SampleRing
{
public:
    explicit SampleRing (int log2Size = 18) : buf ((size_t) 1 << log2Size, 0.0f), mask (((uint64_t) 1 << log2Size) - 1) {}

    // audio thread
    void push (const float* x, int n)
    {
        uint64_t w = writePos.load (std::memory_order_relaxed);
        for (int i = 0; i < n; ++i) buf[(size_t) ((w + (uint64_t) i) & mask)] = x[i];
        writePos.store (w + (uint64_t) n, std::memory_order_release);
    }
    // reader thread: copies the most recent n samples (oldest first)
    int readLatest (float* dst, int n) const
    {
        const uint64_t w = writePos.load (std::memory_order_acquire);
        n = (int) std::min<uint64_t> ((uint64_t) n, std::min<uint64_t> (w, mask + 1 - 4096));
        const uint64_t start = w - (uint64_t) n;
        for (int i = 0; i < n; ++i) dst[i] = buf[(size_t) ((start + (uint64_t) i) & mask)];
        return n;
    }
    int capacity() const { return (int) (mask + 1 - 4096); }
    // call only while the writer is not pushing (e.g. before enabling the tap)
    void clear() { std::fill (buf.begin(), buf.end(), 0.0f); }

private:
    std::vector<float> buf;
    uint64_t mask;
    std::atomic<uint64_t> writePos { 0 };
};

// ---------------------------------------------------------------- tuner
struct TunerResult
{
    bool valid = false;
    int midiNote = 69;          // nearest note
    double cents = 0;           // deviation from the note, cents
    double freq = 0;            // measured fundamental, Hz
    double refFreq = 0;         // ideal frequency of the note, Hz
    double deltaHz = 0;         // freq - refFreq (drives the strobe speed)
    double levelDb = -120;
    int harmonic = 1;           // which harmonic the strobe locked to
    double confidence = 0;      // 0..1 from phase-fit residual
};

class StrobeTunerCore
{
public:
    static const char* noteName (int midi)
    {
        static const char* n[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        return n[((midi % 12) + 12) % 12];
    }
    static int octaveOf (int midi) { return midi / 12 - 1; }

    // x: most recent samples (oldest first)
    TunerResult analyse (const float* x, int n, double fs, double refA)
    {
        TunerResult r;
        if (n < 4096 || fs < 8000) return r;

        // ---- level gate (last ~50 ms)
        const int lw = std::min (n, (int) (fs * 0.05));
        double e = 0;
        for (int i = n - lw; i < n; ++i) e += (double) x[i] * x[i];
        r.levelDb = 10 * std::log10 (e / lw + 1e-24);
        if (r.levelDb < -62.0) { lastNote = -1; return r; }

        // ---- 1. coarse pitch (YIN, decimated to ~8 kHz)
        double f0 = coarsePitch (x, n, fs);
        if (f0 <= 0) { lastNote = -1; return r; }

        // ---- 2. nearest note, with hysteresis so the display doesn't flicker
        auto noteFor = [refA] (double f) { return 69.0 + 12.0 * std::log2 (f / refA); };
        int note = (int) std::lround (noteFor (f0));
        if (lastNote >= 0 && std::fabs (noteFor (f0) - lastNote) < 0.62) note = lastNote;

        // ---- 3. strobe: phase-slope measurement against the reference
        Fine fine;
        for (int pass = 0; pass < 2; ++pass)
        {
            const double fr = refA * std::pow (2.0, (note - 69) / 12.0);
            fine = strobe (x, n, fs, fr);
            if (! fine.ok) { lastNote = -1; return r; }
            const double c = 1200.0 * std::log2 ((fr + fine.deltaHz) / fr);
            if (std::fabs (c) <= 60.0 || pass == 1) { r.refFreq = fr; break; }
            note = (int) std::lround (noteFor (fr + fine.deltaHz)); // crossed a note boundary
        }

        r.midiNote = note;
        r.deltaHz = fine.deltaHz;
        r.freq = r.refFreq + fine.deltaHz;
        r.cents = 1200.0 * std::log2 (r.freq / r.refFreq);
        r.harmonic = fine.harmonic;
        r.confidence = fine.confidence;
        r.valid = fine.confidence > 0.35 && std::fabs (r.cents) <= 60.0;
        lastNote = r.valid ? note : -1;
        return r;
    }

private:
    int lastNote = -1;
    std::vector<double> dec, d;

    // ------------------------------------------------------------ YIN
    double coarsePitch (const float* x, int n, double fs)
    {
        const int D = std::max (1, (int) std::lround (fs / 8000.0));
        const double fsd = fs / D;
        const int tauMin = std::max (2, (int) (fsd / 1600.0));
        const int tauMax = (int) (fsd / 25.0);
        const int W = (int) (tauMax * 1.2);
        const int M = W + tauMax + 2;
        const int warm = 64;
        const int need = (M + warm) * D;
        if (n < need) return -1;

        // 4th-order Butterworth low-pass then decimate
        dec.assign ((size_t) M, 0.0);
        const double fc = 0.21 * fsd;   // ~1.7 kHz at 8 kHz: passes every guitar fundamental
        double z[2][2] = { { 0, 0 }, { 0, 0 } };
        double b[2][5];
        const double qs[2] = { 0.54119610, 1.3065630 };
        for (int s = 0; s < 2; ++s)
        {
            const double w = 2 * 3.14159265358979323846 * fc / fs, c = std::cos (w), al = std::sin (w) / (2 * qs[s]), a0 = 1 + al;
            b[s][0] = (1 - c) / 2 / a0; b[s][1] = (1 - c) / a0; b[s][2] = (1 - c) / 2 / a0; b[s][3] = -2 * c / a0; b[s][4] = (1 - al) / a0;
        }
        int k = 0;
        for (int i = n - need; i < n; ++i)
        {
            double v = x[i];
            for (int s = 0; s < 2; ++s)
            {
                const double y = b[s][0] * v + z[s][0];
                z[s][0] = b[s][1] * v - b[s][3] * y + z[s][1];
                z[s][1] = b[s][2] * v - b[s][4] * y;
                v = y;
            }
            const int j = i - (n - need);
            if (j >= warm * D && (j - warm * D) % D == 0 && k < M) dec[(size_t) k++] = v;
        }

        // cumulative-mean-normalised difference
        d.assign ((size_t) tauMax + 2, 1.0);
        double run = 0;
        for (int tau = 1; tau <= tauMax + 1; ++tau)
        {
            double s = 0;
            for (int i = 0; i < W; ++i) { const double df = dec[(size_t) i] - dec[(size_t) (i + tau)]; s += df * df; }
            run += s;
            d[(size_t) tau] = run > 0 ? s * tau / run : 1.0;
        }
        int best = -1;
        for (int tau = tauMin; tau <= tauMax; ++tau)
            if (d[(size_t) tau] < 0.15)
            {
                while (tau + 1 <= tauMax && d[(size_t) tau + 1] < d[(size_t) tau]) ++tau;
                best = tau; break;
            }
        if (best < 0)
        {
            double mn = 1e9;
            for (int tau = tauMin; tau <= tauMax; ++tau) if (d[(size_t) tau] < mn) { mn = d[(size_t) tau]; best = tau; }
            if (mn > 0.45) return -1;
        }
        double t = best;
        if (best > 1 && best < tauMax)
        {
            const double a = d[(size_t) best - 1], bb = d[(size_t) best], c = d[(size_t) best + 1];
            const double den = a - 2 * bb + c;
            if (std::fabs (den) > 1e-12) t += 0.5 * (a - c) / den;
        }
        return fsd / t;
    }

    // ------------------------------------------------- heterodyne strobe
    struct Fine { bool ok = false; double deltaHz = 0; int harmonic = 1; double confidence = 0; };

    static std::complex<double> demod (const float* x, int start, int L, double omega)
    {
        // 4-term Blackman-Harris windowed correlation with exp(-j*omega*t)
        // (-92 dB sidelobes keep neighbouring harmonics out of the phase)
        std::complex<double> acc (0, 0);
        std::complex<double> ph = std::polar (1.0, -omega * start);
        const std::complex<double> rot = std::polar (1.0, -omega);
        const double wk = 2 * 3.14159265358979323846 / (L - 1);
        for (int i = 0; i < L; ++i)
        {
            const double w = 0.35875 - 0.48829 * std::cos (wk * i) + 0.14128 * std::cos (2 * wk * i) - 0.01168 * std::cos (3 * wk * i);
            acc += ph * (w * x[start + i]);
            ph *= rot;
        }
        return acc;
    }

    // measure against the reference, then re-centre on the measured pitch and
    // measure the (now tiny) residual: removes window-leakage bias
    Fine strobe (const float* x, int n, double fs, double fr)
    {
        Fine a = strobeAt (x, n, fs, fr, 0);
        if (! a.ok) return a;
        for (int it = 0; it < 2; ++it)
        {
            Fine b = strobeAt (x, n, fs, fr + a.deltaHz, a.harmonic);
            if (! b.ok) break;
            a.deltaHz += b.deltaHz;
            a.confidence = b.confidence;
        }
        return a;
    }

    Fine strobeAt (const float* x, int n, double fs, double fr, int forceHarmonic)
    {
        Fine out;
        const double twoPi = 2 * 3.14159265358979323846;
        const int L = std::max (64, (int) std::lround (6.0 * fs / fr));   // 6 periods
        const int hop = L / 2;
        const int span = std::max ((int) (0.35 * fs), 8 * hop);
        const int total = std::min (n, span + L);
        const int base = n - total;
        const int blocks = (total - L) / hop + 1;
        if (blocks < 4) return out;

        // choose harmonic: fundamental unless it's much weaker than 2nd/3rd
        const int probe = n - L;
        double mag[4] = { 0, 0, 0, 0 };
        for (int h = 1; h <= 3; ++h)
            if (h * fr < fs * 0.45) mag[h] = std::abs (demod (x, probe, L, twoPi * h * fr / fs));
        int h = 1;
        const double strongest = std::max (mag[2], mag[3]);
        if (mag[1] < 0.2 * strongest) h = mag[2] >= mag[3] ? 2 : 3;
        if (forceHarmonic > 0) h = forceHarmonic;
        out.harmonic = h;

        const double omega = twoPi * h * fr / fs;
        std::vector<double> t ((size_t) blocks), ph ((size_t) blocks), w ((size_t) blocks);
        double prev = 0, unwrap = 0;
        for (int k = 0; k < blocks; ++k)
        {
            const auto z = demod (x + base, k * hop, L, omega);   // phase vs. a reference started at the segment start
            const double p = std::arg (z);
            if (k > 0)
            {
                double dp = p - prev;
                while (dp > 3.14159265358979323846) dp -= twoPi;
                while (dp < -3.14159265358979323846) dp += twoPi;
                unwrap += dp;
            }
            prev = p;
            t[(size_t) k] = (k * hop + 0.5 * (L - 1)) / fs;
            ph[(size_t) k] = unwrap;
            w[(size_t) k] = std::norm (z);
        }

        double sw = 0, st = 0, sp = 0;
        for (int k = 0; k < blocks; ++k) { sw += w[(size_t) k]; st += w[(size_t) k] * t[(size_t) k]; sp += w[(size_t) k] * ph[(size_t) k]; }
        if (sw <= 0) return out;
        const double mt = st / sw, mp = sp / sw;
        double num = 0, den = 0;
        for (int k = 0; k < blocks; ++k)
        {
            const double dt = t[(size_t) k] - mt;
            num += w[(size_t) k] * dt * (ph[(size_t) k] - mp);
            den += w[(size_t) k] * dt * dt;
        }
        if (den <= 0) return out;
        const double slope = num / den;   // rad/s at harmonic h
        double res = 0;
        for (int k = 0; k < blocks; ++k)
        {
            const double e = ph[(size_t) k] - (mp + slope * (t[(size_t) k] - mt));
            res += w[(size_t) k] * e * e;
        }
        res = std::sqrt (res / sw);
        out.deltaHz = slope / twoPi / h;
        out.confidence = std::exp (-res / 0.35);
        out.ok = true;
        return out;
    }
};

} // namespace fusion
