#pragma once
// ============================================================================
// Tape Space Rack — DSP core (C++). Line-by-line port of web/dsp.js (no JUCE dependency).
//   Tape   : record EQ → saturation at 2x (polyphase IIR halfband) → playback EQ / head bump → wow & flutter → hiss
//   Delay  : Digital HQ / Tape Echo (multi-head) / Analog BBD / Vintage Digital; ping-pong, reverse, wet EQ
//   Reverb : 16-line FDN or zero-latency partitioned convolution (double precision); reverse reverb
// Everything runs in double precision. Audio-thread methods never allocate.
// ============================================================================
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#if defined (__clang__) || defined (__GNUC__)
 #pragma GCC diagnostic push
 #pragma GCC diagnostic ignored "-Wfloat-equal"
 #pragma GCC diagnostic ignored "-Wsign-conversion"
 #pragma GCC diagnostic ignored "-Wshadow"
 #pragma GCC diagnostic ignored "-Wconversion"
#endif

namespace ts
{
static constexpr double PI = 3.14159265358979323846;
inline double dbToLin (double d) { return std::pow (10.0, d / 20.0); }
inline double linToDb (double l) { return 20.0 * std::log10 (std::max (l, 1e-9)); }
template <typename T> inline T clampv (T v, T a, T b) { return std::min (b, std::max (a, v)); }
inline int ival (double v) { return (int) v; }                 // JS "v | 0" for the small integers used here
inline long jsRound (double v) { return (long) std::floor (v + 0.5); }
static constexpr double REF = -18.0;

enum Type { kTape = 0, kDelay = 1, kReverb = 2, kNumTypes = 3 };
inline const char* typeKey (int t) { static const char* k[] = { "tape", "delay", "reverb" }; return k[clampv (t, 0, 2)]; }
inline int typeFromKey (const std::string& k) { for (int t = 0; t < kNumTypes; ++t) if (k == typeKey (t)) return t; return -1; }

// ---------------------------------------------------------------- tables shared with the UI
static const double SYNC_BEATS[14] = { 0.125, 1.0 / 6, 0.25, 0.375, 1.0 / 3, 0.5, 0.75, 2.0 / 3, 1, 1.5, 4.0 / 3, 2, 3, 4 };
static const char* const SYNC_NAMES[14] = { "1/32", "1/16T", "1/16", "1/16.", "1/8T", "1/8", "1/8.", "1/4T", "1/4", "1/4.", "1/2T", "1/2", "1/2.", "1/1" };
static const double TAPE_SPEEDS[4] = { 3.75, 7.5, 15, 30 };
static const double DELAY_MAX[4] = { 4000, 1600, 1200, 2000 };
static const double HEAD_RATIO[3] = { 1, 1.9, 2.8 };
static const int HEADMASK[7] = { 1, 2, 4, 3, 6, 5, 7 };
static constexpr double REV_MAX = 2000;

struct Electronics { const char* name; double ceil, b; };
struct Heads { const char* name; double hf, bump; };
struct Align { const char* name; double lvl; };
static const Electronics ELECTRONICS[3] = { { "Tube electronics", 1.4, 0.10 }, { "Solid-state electronics", 2.2, 0.03 }, { "Modern transformerless", 3.2, 0.0 } };
static const Heads HEADS[3] = { { "New heads", 0, 0 }, { "Worn heads", -2.5, 0.6 }, { "Relapped (bright)", 0.8, -0.3 } };
static const Align ALIGN[3] = { { "Standard level", 0 }, { "Hot (+3 dB)", 3 }, { "Very hot (+6 dB)", 6 } };
struct Formula { double mol, b, hiss; };
static const Formula FORMULA[3] = { { 12, 0.02, -86 }, { 9, 0.04, -83 }, { 6, 0.07, -79 } };

// ---------------------------------------------------------------- parameters (one union set, real units)
struct Params
{
    double variant = 0, mix = 100, output = 0, analog = 1, c1 = 0, c2 = 0, c3 = 0, age = 0, headroom = 24;
    double drive = 0, speed = 15, eqStd = 0, formula = 0, bias = 0, wow = 0, flutter = 0, hiss = 0;
    double time = 375, sync = 0, feedback = 35, stereo = 1, ratioLR = 75, lowcut = 80, highcut = 12000, modRate = 0.5, modDepth = 0, duck = 0, heads = 0;
    double source = 0, irIndex = 0, predelay = 20, size = 100, decay = 2.2, hiMult = 0.55, loMult = 1.2, diffusion = 80, er = 50, modulation = 30, width = 100;
    double reverse = 0, revLen = 1500, irLen = 100;
    double eqLo = 0, eqMid = 0, eqMidF = 1500, eqHi = 0;
    bool operator== (const Params& o) const { return std::memcmp (this, &o, sizeof (Params)) == 0; }
    bool operator!= (const Params& o) const { return ! (*this == o); }
};
inline Params defaultsFor (int type)
{
    Params p;
    if (type == kTape) p.mix = 100;
    if (type == kDelay) { p.mix = 30; p.lowcut = 80; p.highcut = 12000; }
    if (type == kReverb) { p.mix = 25; p.lowcut = 100; p.highcut = 12000; }
    return p;
}

// ---------------------------------------------------------------- biquads (RBJ) + first-order sections
struct Coefs { double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0; };
inline Coefs bqPeak (double fs, double f, double q, double gDb)
{
    const double A = std::pow (10.0, gDb / 40), w = 2 * PI * f / fs, al = std::sin (w) / (2 * q), c = std::cos (w), a0 = 1 + al / A;
    return { (1 + al * A) / a0, -2 * c / a0, (1 - al * A) / a0, -2 * c / a0, (1 - al / A) / a0 };
}
inline Coefs bqHighShelf (double fs, double f, double q, double gDb)
{
    const double A = std::pow (10.0, gDb / 40), w = 2 * PI * f / fs, al = std::sin (w) / (2 * q), c = std::cos (w), s = 2 * std::sqrt (A) * al;
    const double a0 = (A + 1) - (A - 1) * c + s;
    return { A * ((A + 1) + (A - 1) * c + s) / a0, -2 * A * ((A - 1) + (A + 1) * c) / a0,
             A * ((A + 1) + (A - 1) * c - s) / a0, 2 * ((A - 1) - (A + 1) * c) / a0, ((A + 1) - (A - 1) * c - s) / a0 };
}
inline Coefs bqLowShelf (double fs, double f, double q, double gDb)
{
    const double A = std::pow (10.0, gDb / 40), w = 2 * PI * f / fs, al = std::sin (w) / (2 * q), c = std::cos (w), s = 2 * std::sqrt (A) * al;
    const double a0 = (A + 1) + (A - 1) * c + s;
    return { A * ((A + 1) - (A - 1) * c + s) / a0, 2 * A * ((A - 1) - (A + 1) * c) / a0,
             A * ((A + 1) - (A - 1) * c - s) / a0, -2 * ((A - 1) + (A + 1) * c) / a0, ((A + 1) + (A - 1) * c - s) / a0 };
}
inline Coefs bqHP (double fs, double f, double q)
{
    const double w = 2 * PI * f / fs, al = std::sin (w) / (2 * q), c = std::cos (w), a0 = 1 + al;
    return { (1 + c) / 2 / a0, -(1 + c) / a0, (1 + c) / 2 / a0, -2 * c / a0, (1 - al) / a0 };
}
inline Coefs bqLP (double fs, double f, double q)
{
    const double w = 2 * PI * f / fs, al = std::sin (w) / (2 * q), c = std::cos (w), a0 = 1 + al;
    return { (1 - c) / 2 / a0, (1 - c) / a0, (1 - c) / 2 / a0, -2 * c / a0, (1 - al) / a0 };
}
inline Coefs fo_ZP (double fs, double fz, double fp, double k = 1)
{
    const double c = 2 * fs, wz = c * std::tan (PI * std::min (fz, fs * 0.49) / fs), wp = c * std::tan (PI * std::min (fp, fs * 0.49) / fs), d = c + wp;
    return { k * (c + wz) / d, k * (wz - c) / d, 0, (wp - c) / d, 0 };
}
inline Coefs foLowShelf (double fs, double fc, double g) { const double r = std::sqrt (g); return fo_ZP (fs, fc * r, fc / r, 1); }
inline Coefs foHighShelf (double fs, double fc, double g) { const double r = std::sqrt (g); return fo_ZP (fs, fc / r, fc * r, g); }
inline double bqMagDb (const Coefs& c, double fs, double f)
{
    const double w = 2 * PI * f / fs;
    const double cr = std::cos (w), ci = -std::sin (w), c2r = std::cos (2 * w), c2i = -std::sin (2 * w);
    const double nr = c.b0 + c.b1 * cr + c.b2 * c2r, ni = c.b1 * ci + c.b2 * c2i;
    const double dr = 1 + c.a1 * cr + c.a2 * c2r, di = c.a1 * ci + c.a2 * c2i;
    return 10 * std::log10 ((nr * nr + ni * ni) / (dr * dr + di * di) + 1e-30);
}
// a fixed-capacity list of sections (no allocation)
struct Secs
{
    static constexpr int kMax = 10;
    std::array<Coefs, kMax> c {}; int n = 0;
    void push (const Coefs& x) { if (n < kMax) c[(size_t) n++] = x; }
    void append (const Secs& o) { for (int i = 0; i < o.n; ++i) push (o.c[(size_t) i]); }
    double magDb (double fs, double f) const { double s = 0; for (int i = 0; i < n; ++i) s += bqMagDb (c[(size_t) i], fs, f); return s; }
};
struct Biquad
{
    Coefs c; double z1 = 0, z2 = 0;
    void set (const Coefs& x) { c = x; }
    double process (double x) { const double y = c.b0 * x + z1; z1 = c.b1 * x - c.a1 * y + z2; z2 = c.b2 * x - c.a2 * y; return y; }
    void reset() { z1 = z2 = 0; }
};
struct Chain
{
    std::array<Biquad, Secs::kMax> b {}; int n = 0;
    void set (const Secs& s)
    {
        for (int i = n; i < s.n; ++i) b[(size_t) i] = Biquad();      // new sections start from rest (as in the JS)
        n = s.n; for (int i = 0; i < n; ++i) b[(size_t) i].set (s.c[(size_t) i]);
    }
    double run (double x) { for (int k = 0; k < n; ++k) x = b[(size_t) k].process (x); return x; }
};

// ---------------------------------------------------------------- helpers
struct DCBlock
{
    double R = 0, x1 = 0, y1 = 0;
    void init (double fs) { R = 1 - 2 * PI * 5 / fs; x1 = y1 = 0; }
    double process (double x) { const double y = x - x1 + R * y1; x1 = x; y1 = y; return y; }
};
inline double sat (double x, double ceil, double bias) { const double u = x / ceil, tb = std::tanh (bias); return ceil * (std::tanh (u + bias) - tb) / (1 - tb * tb); }
struct Rng
{
    uint32_t s; explicit Rng (uint32_t seed = 1) : s (seed) {}
    double next() { s = s * 1664525u + 1013904223u; return (double) (s >> 8) / 16777216.0 * 2 - 1; }
};
struct Hiss
{
    Rng r; double n = 0, a = 0, g = 0;
    void init (double fs, uint32_t seed) { r = Rng (seed); n = 0; a = 1 - std::exp (-2 * PI * 7000 / fs); g = 0; }
    void set (double levelDb, bool on) { const double norm = 0.57735 * std::sqrt (a / (2 - a)); g = on ? dbToLin (levelDb) / norm : 0; }
    double next() { if (g == 0) return 0; n += a * (r.next() - n); return n * g; }
};
inline double hermite (const double* buf, int mask, int w, double d)
{
    const int di = (int) std::floor (d); const double f = d - di;
    const int i0 = (w - di) & mask;
    const double xm1 = buf[(i0 + 1) & mask], x0 = buf[i0], x1 = buf[(i0 - 1) & mask], x2 = buf[(i0 - 2) & mask];
    const double c0 = x0, c1 = 0.5 * (x1 - xm1), c2 = xm1 - 2.5 * x0 + 2 * x1 - 0.5 * x2, c3 = 0.5 * (x2 - xm1) + 1.5 * (x0 - x1);
    return ((c3 * f + c2) * f + c1) * f + c0;
}
inline int pow2 (int n) { int p = 1; while (p < n) p <<= 1; return p; }

// ---------------------------------------------------------------- 2x oversampling (polyphase IIR halfband, elliptic design)
inline std::vector<double> halfbandCoefs (int n, double tbw)
{
    double k = std::tan ((1 - tbw * 2) * PI / 4); k *= k;
    const double kk = std::pow (1 - k * k, 0.25), e = 0.5 * (1 - kk) / (1 + kk), e4 = std::pow (e, 4);
    const double q = e * (1 + e4 * (2 + e4 * (15 + 150 * e4)));
    const int order = n * 2 + 1; std::vector<double> c;
    for (int idx = 0; idx < n; ++idx)
    {
        const int cc = idx + 1; int i = 0, j = 1; double nm = 0, t;
        do { t = std::pow (q, i * (i + 1)) * std::sin ((i * 2 + 1) * cc * PI / order) * j; nm += t; j = -j; ++i; } while (std::abs (t) > 1e-100);
        nm *= std::pow (q, 0.25);
        i = 1; j = -1; double dn = 0;
        do { t = std::pow (q, i * i) * std::cos (i * 2 * cc * PI / order) * j; dn += t; j = -j; ++i; } while (std::abs (t) > 1e-100);
        dn += 0.5;
        const double ww = nm / dn, wwsq = ww * ww, x = std::sqrt ((1 - wwsq * k) * (1 - wwsq / k)) / (1 + wwsq);
        c.push_back ((1 - x) / (1 + x));
    }
    return c;
}
inline const std::vector<double>& HB() { static const std::vector<double> c = halfbandCoefs (10, 0.03); return c; }
struct Halfband
{
    std::array<double, 10> x {}, y {}; double o0 = 0, o1 = 0;
    void chain (double a, double b)
    {
        const auto& c = HB();
        for (size_t i = 0; i < c.size(); i += 2)
        {
            double t = (a - y[i]) * c[i] + x[i]; x[i] = a; y[i] = t; a = t;
            t = (b - y[i + 1]) * c[i + 1] + x[i + 1]; x[i + 1] = b; y[i + 1] = t; b = t;
        }
        o0 = a; o1 = b;
    }
    void up (double v) { chain (v, v); }
    double down (double s0, double s1) { chain (s1, s0); return 0.5 * (o0 + o1); }
};

// ---------------------------------------------------------------- FFT (radix-2, in place) + real FFT
struct FFTPlan
{
    int n; std::vector<uint32_t> rev; std::vector<double> cs, sn;
    explicit FFTPlan (int N) : n (N), rev ((size_t) N), cs ((size_t) N / 2), sn ((size_t) N / 2)
    {
        int bits = 0; while ((1 << bits) < n) ++bits;
        for (int i = 0; i < n; ++i) { uint32_t r = 0; for (int b = 0; b < bits; ++b) r |= (uint32_t) ((i >> b) & 1) << (bits - 1 - b); rev[(size_t) i] = r; }
        for (int i = 0; i < n / 2; ++i) { cs[(size_t) i] = std::cos (2 * PI * i / n); sn[(size_t) i] = -std::sin (2 * PI * i / n); }
    }
};
inline const FFTPlan& fftPlan (int n)
{
    // plans for every size used (64 … 8192) are made once, thread-safe static initialisation
    static const std::array<std::unique_ptr<FFTPlan>, 15> plans = [] {
        std::array<std::unique_ptr<FFTPlan>, 15> a; for (int b = 1; b < 15; ++b) a[(size_t) b] = std::make_unique<FFTPlan> (1 << b); return a; }();
    int b = 0; while ((1 << b) < n) ++b;
    return *plans[(size_t) b];
}
inline void fft (double* re, double* im, int n, bool inv)
{
    const auto& P = fftPlan (n);
    for (int i = 0; i < n; ++i) { const int j = (int) P.rev[(size_t) i]; if (j > i) { std::swap (re[i], re[j]); std::swap (im[i], im[j]); } }
    for (int size = 2; size <= n; size <<= 1)
    {
        const int half = size >> 1, step = n / size;
        for (int s = 0; s < n; s += size)
            for (int k = 0; k < half; ++k)
            {
                const double wr = P.cs[(size_t) (k * step)], wi = inv ? -P.sn[(size_t) (k * step)] : P.sn[(size_t) (k * step)];
                const int a = s + k, b = a + half;
                const double tr = re[b] * wr - im[b] * wi, ti = re[b] * wi + im[b] * wr;
                re[b] = re[a] - tr; im[b] = im[a] - ti; re[a] += tr; im[a] += ti;
            }
    }
}
inline void rfft (const double* x, int n, double* outRe, double* outIm, double* wr, double* wi)
{
    const int h = n / 2;
    for (int i = 0; i < h; ++i) { wr[i] = x[2 * i]; wi[i] = x[2 * i + 1]; }
    fft (wr, wi, h, false);
    const auto& P = fftPlan (n);
    for (int k = 0; k <= h; ++k)
    {
        const double ar = wr[k % h], ai = wi[k % h], br = wr[(h - k) % h], bi = -wi[(h - k) % h];
        const double er = 0.5 * (ar + br), ei = 0.5 * (ai + bi), orr = 0.5 * (ai - bi), oi = -0.5 * (ar - br);
        const double c = k < h ? P.cs[(size_t) k] : -1, s = k < h ? P.sn[(size_t) k] : 0;
        outRe[k] = er + (orr * c - oi * s); outIm[k] = ei + (orr * s + oi * c);
    }
}
inline void irfft (const double* inRe, const double* inIm, int n, double* x, double* wr, double* wi)
{
    const int h = n / 2; const auto& P = fftPlan (n);
    for (int k = 0; k < h; ++k)
    {
        const double ar = inRe[k], ai = inIm[k], br = inRe[h - k], bi = -inIm[h - k];
        const double er = 0.5 * (ar + br), ei = 0.5 * (ai + bi);
        const double dr = 0.5 * (ar - br), di = 0.5 * (ai - bi);
        const double c = P.cs[(size_t) k], s = -P.sn[(size_t) k];
        const double orr = dr * c - di * s, oi = dr * s + di * c;
        wr[k] = er - oi; wi[k] = ei + orr;
    }
    fft (wr, wi, h, true);
    for (int i = 0; i < h; ++i) { x[2 * i] = wr[i] / h; x[2 * i + 1] = wi[i] / h; }
}

// ---------------------------------------------------------------- zero-latency partitioned convolution (one channel)
static constexpr int HEAD = 128, B1 = 128, B2 = 2048, SUB = B2 / B1;
struct Stage
{
    bool full; int B, N, bins, P, head = 0;
    std::vector<std::vector<double>> Hr, Hi, Fr, Fi;
    std::vector<double> inBuf, out, accR, accI, t, wr, wi;
    Stage (const float* h, int hn, int from, int to, int b, bool isFull)
        : full (isFull), B (b), N (2 * b), bins (b + 1)
    {
        P = std::max (0, (int) std::ceil ((double) (std::min (to, hn) - from) / B));
        t.assign ((size_t) N, 0); wr.assign ((size_t) B, 0); wi.assign ((size_t) B, 0);
        for (int p = 0; p < P; ++p)
        {
            std::fill (t.begin(), t.end(), 0.0);
            for (int k = 0; k < B; ++k) { const int i = from + p * B + k; t[(size_t) k] = i < to && i < hn ? (double) h[i] : 0; }
            Hr.emplace_back ((size_t) bins); Hi.emplace_back ((size_t) bins);
            rfft (t.data(), N, Hr.back().data(), Hi.back().data(), wr.data(), wi.data());
        }
        for (int p = 0; p < P; ++p) { Fr.emplace_back ((size_t) bins, 0.0); Fi.emplace_back ((size_t) bins, 0.0); }
        inBuf.assign ((size_t) N, 0); out.assign ((size_t) B, 0); accR.assign ((size_t) bins, 0); accI.assign ((size_t) bins, 0);
    }
    void accumulate (int p0, int p1)
    {
        for (int p = p0; p < p1 && p < P; ++p)
        {
            if (p < 1) continue;
            const size_t xi = (size_t) ((head + p - 1) % P);
            const double* xr = Fr[xi].data(); const double* xim = Fi[xi].data(); const double* hr = Hr[(size_t) p].data(); const double* hi = Hi[(size_t) p].data();
            for (int k = 0; k < bins; ++k) { accR[(size_t) k] += xr[k] * hr[k] - xim[k] * hi[k]; accI[(size_t) k] += xr[k] * hi[k] + xim[k] * hr[k]; }
        }
    }
    void finish()
    {
        if (P == 0) { std::fill (out.begin(), out.end(), 0.0); return; }
        head = (head + P - 1) % P;
        double* xr = Fr[(size_t) head].data(); double* xim = Fi[(size_t) head].data();
        rfft (inBuf.data(), N, xr, xim, wr.data(), wi.data());
        const double* hr = Hr[0].data(); const double* hi = Hi[0].data();
        for (int k = 0; k < bins; ++k) { accR[(size_t) k] += xr[k] * hr[k] - xim[k] * hi[k]; accI[(size_t) k] += xr[k] * hi[k] + xim[k] * hr[k]; }
        if (full)
            for (int p = 1; p < P; ++p)
            {
                const size_t yi = (size_t) ((head + p) % P);
                const double* yr = Fr[yi].data(); const double* yim = Fi[yi].data(); const double* gr = Hr[(size_t) p].data(); const double* gi = Hi[(size_t) p].data();
                for (int k = 0; k < bins; ++k) { accR[(size_t) k] += yr[k] * gr[k] - yim[k] * gi[k]; accI[(size_t) k] += yr[k] * gi[k] + yim[k] * gr[k]; }
            }
        irfft (accR.data(), accI.data(), N, t.data(), wr.data(), wi.data());
        for (int i = 0; i < B; ++i) out[(size_t) i] = t[(size_t) (B + i)];
        std::fill (accR.begin(), accR.end(), 0.0); std::fill (accI.begin(), accI.end(), 0.0);
        std::copy (inBuf.begin() + B, inBuf.end(), inBuf.begin());
    }
};
class Convolver
{
public:
    Convolver (const float* h, int n)
        : s1 (h, n, HEAD, B2, B1, true), s2 (h, n, B2, n, B2, false), len (n)
    {
        for (int i = 0; i < HEAD && i < n; ++i) headH[(size_t) i] = h[i];
    }
    double process (double x)
    {
        hist[(size_t) hp] = x; hist[(size_t) (hp + HEAD)] = x;
        double y = 0;
        for (int k = 0; k < HEAD; ++k) y += headH[(size_t) k] * hist[(size_t) (hp + HEAD - k)];
        hp = (hp + 1) % HEAD;
        y += s1.out[(size_t) pos1] + s2.out[(size_t) pos2];
        s1.inBuf[(size_t) (B1 + pos1)] = x; s2.inBuf[(size_t) (B2 + pos2)] = x;
        if (++pos1 == B1)
        {
            pos1 = 0; s1.finish();
            const int P = s2.P, per = (int) std::ceil (std::max (0, P - 1) / (double) SUB);
            s2.accumulate (1 + sub * per, 1 + (sub + 1) * per);
            ++sub;
        }
        if (++pos2 == B2) { pos2 = 0; sub = 0; s2.finish(); }
        return y;
    }
private:
    std::array<double, HEAD> headH {};
    std::array<double, HEAD * 2> hist {};
    int hp = 0;
    Stage s1, s2;
    int pos1 = 0, pos2 = 0, sub = 0;
public:
    int len;
};
struct ConvPair
{
    ConvPair (const std::vector<float>& L, const std::vector<float>& R) : l (L.data(), (int) L.size()), r (R.data(), (int) R.size()) {}
    Convolver l, r;
    int gen = 0;                       // which module instance this IR was built for
};
using IR = std::array<std::vector<float>, 2>;

// ---------------------------------------------------------------- built-in impulse responses (synthesised, deterministic)
struct IRSpecT { double len, rt, lo, hi, ramp; int er; double erSpan, erGain, pre, w; bool spring; };
static const IRSpecT IRSPEC[7] = {
    { 3.4, 2.3, 1.25, 0.5, 60, 10, 70, 0.35, 18, 0.9, false },
    { 1.2, 0.55, 1.1, 0.6, 8, 14, 28, 0.55, 4, 0.7, false },
    { 2.6, 1.9, 0.9, 0.85, 3, 0, 0, 0, 1, 1.0, false },
    { 2.2, 1.4, 1.15, 0.45, 20, 12, 45, 0.45, 8, 0.8, false },
    { 0.5, 0.22, 1.0, 0.7, 2, 10, 12, 0.6, 1, 0.5, false },
    { 7.0, 5.2, 1.35, 0.4, 120, 8, 140, 0.3, 35, 1.0, false },
    { 3.0, 2.0, 0.8, 0.6, 1, 0, 0, 0, 1, 0.6, true },
};
static const char* const IR_NAMES[7] = { "Concert Hall", "Wood Room", "Bright Plate", "Stone Chamber", "Vocal Booth", "Large Church", "Spring Tank" };
inline IR& normalizeIR (IR& ir)
{
    double e = 0; for (auto& c : ir) for (float v : c) e += (double) v * (double) v;
    e /= 2; const double g = e > 0 ? 0.6 / std::sqrt (e) : 1;
    for (auto& c : ir) for (auto& v : c) v = (float) (v * g);
    return ir;
}
inline IR springIR (const IRSpecT& S, double fs, int n)
{
    IR out { std::vector<float> ((size_t) n), std::vector<float> ((size_t) n) };
    const double periods[2] = { 0.0283, 0.0361 };
    for (int ch = 0; ch < 2; ++ch)
    {
        std::vector<double> x ((size_t) n, 0.0); const double T = periods[ch], k = -6.9078 / (S.rt * fs);
        for (int r = 0; r * T * fs < n; ++r) { const long i = jsRound (r * T * fs) + 20; if (i < n) x[(size_t) i] += (r % 2 ? -1 : 1) * std::exp (k * i); }
        std::vector<double> ap (160, 0.0); const double a = -0.62;
        for (int i = 0; i < n; ++i)
        {
            double v = x[(size_t) i];
            for (int s = 0; s < 80; ++s) { const double y = a * v + ap[(size_t) (2 * s)]; ap[(size_t) (2 * s)] = v - a * y; v = y; }
            x[(size_t) i] = v;
        }
        Biquad bq[4]; bq[0].set (bqHP (fs, 120, 0.707)); bq[1].set (bqLP (fs, std::min (4500.0, fs * 0.45), 0.707)); bq[2].set (bqLP (fs, std::min (4500.0, fs * 0.45), 0.707)); bq[3].set (bqPeak (fs, 1800, 1.2, 4));
        Rng rng (777u + (uint32_t) ch);
        for (int i = 0; i < n; ++i) { double v = x[(size_t) i] + 0.02 * rng.next() * std::exp (k * i); for (auto& b : bq) v = b.process (v); out[(size_t) ch][(size_t) i] = (float) v; }
        const int f0 = (int) std::floor (n * 0.9); for (int i = f0; i < n; ++i) out[(size_t) ch][(size_t) i] = (float) (out[(size_t) ch][(size_t) i] * (0.5 + 0.5 * std::cos (PI * (i - f0) / (n - f0))));
    }
    return normalizeIR (out);
}
inline IR synthIR (int index, double fs)
{
    index = clampv (index, 0, 6);
    const IRSpecT& S = IRSPEC[index]; const int n = (int) jsRound (S.len * fs);
    if (S.spring) return springIR (S, fs, n);
    IR out { std::vector<float> ((size_t) n), std::vector<float> ((size_t) n) };
    const double bands[4][3] = { { 0, 250, S.lo }, { 250, 2000, 1 }, { 2000, 6000, (1 + S.hi) / 2 }, { 6000, 0, S.hi } };
    for (int ch = 0; ch < 2; ++ch)
    {
        Rng rng ((uint32_t) (9001 + 77 * index + 13 * ch)); std::vector<double> acc ((size_t) n, 0.0);
        for (auto& bd : bands)
        {
            const double f0 = bd[0], f1 = bd[1], mult = bd[2];
            Chain bq; Secs sc;
            if (f0 > 0) { sc.push (bqHP (fs, f0, 0.707)); sc.push (bqHP (fs, f0, 0.707)); }
            if (f1 > 0) { sc.push (bqLP (fs, std::min (f1, fs * 0.45), 0.707)); sc.push (bqLP (fs, std::min (f1, fs * 0.45), 0.707)); }
            bq.set (sc);
            const double rt = S.rt * mult, k = -6.9078 / (rt * fs);
            for (int i = 0; i < n; ++i)
            {
                const double t = i / fs, env = std::exp (k * i) * (1 - std::exp (-t * 1000 / S.ramp));
                acc[(size_t) i] += bq.run (rng.next()) * env;
            }
        }
        Rng r2 ((uint32_t) (4242 + 31 * index + ch));
        for (int e = 0; e < S.er; ++e)
        {
            const double t = S.pre + S.erSpan * std::pow ((e + 0.5 + 0.3 * r2.next()) / S.er, 1.4); const long i = jsRound (t * fs / 1000);
            if (i < n) { const double a = S.erGain * (0.6 + 0.4 * r2.next()); const double sg = r2.next() > 0 ? 1 : -1; acc[(size_t) i] += a * sg * std::pow (1 - (double) e / (S.er + 2), 1.5); }
        }
        const long sh = jsRound (S.pre * fs / 1000);
        for (int i = n - 1; i >= 0; --i) out[(size_t) ch][(size_t) i] = i >= sh ? (float) acc[(size_t) (i - sh)] : 0.f;
        const int f0 = (int) std::floor (n * 0.9); for (int i = f0; i < n; ++i) out[(size_t) ch][(size_t) i] = (float) (out[(size_t) ch][(size_t) i] * (0.5 + 0.5 * std::cos (PI * (i - f0) / (n - f0))));
    }
    const double w = S.w;
    for (int i = 0; i < n; ++i)
    {
        const double m = 0.5 * ((double) out[0][(size_t) i] + out[1][(size_t) i]), s = 0.5 * ((double) out[0][(size_t) i] - out[1][(size_t) i]) * w;
        out[0][(size_t) i] = (float) (m + s); out[1][(size_t) i] = (float) (m - s);
    }
    return normalizeIR (out);
}
inline IR prepareIR (const IR& src, const Params& p, double fs)
{
    const auto& L = src[0]; const auto& R = src[1].empty() ? src[0] : src[1];
    int n = (int) std::max (64L, jsRound (L.size() * clampv (p.irLen, 5.0, 100.0) / 100));
    if (p.reverse != 0) n = (int) std::min ((long) L.size(), std::max (64L, jsRound (clampv (p.revLen, 100.0, 6000.0) * fs / 1000)));
    IR out { std::vector<float> ((size_t) n), std::vector<float> ((size_t) n) };
    const int fade = (int) std::min ((long) (n >> 2), jsRound (0.05 * fs));
    for (int ch = 0; ch < 2; ++ch)
    {
        const auto& s = ch ? R : L;
        for (int i = 0; i < n; ++i)
        {
            double v = i < (int) s.size() ? (double) s[(size_t) i] : 0;
            if (i >= n - fade) v *= 0.5 + 0.5 * std::cos (PI * (i - (n - fade)) / fade);
            out[(size_t) ch][(size_t) i] = (float) v;
        }
        if (p.reverse != 0) std::reverse (out[(size_t) ch].begin(), out[(size_t) ch].end());
    }
    return normalizeIR (out);
}

// ================================================================ module base
struct Module
{
    virtual ~Module() = default;
    virtual int type() const = 0;
    virtual void setParams (const Params& p) = 0;
    virtual void process (float* L, float* R, int n) = 0;
    virtual void setBpm (double) {}
    double inMeter = 0, outMeter = 0, wetMeter = 0;
    Params p;
    int gen = 0;                       // instance id (set by the host)
};

// ================================================================ TAPE
inline double tapeTau (int eq, double speed)
{
    if (speed >= 30) return 17.5;
    if (speed >= 15) return eq ? 35 : 50;
    if (speed >= 7.5) return eq ? 70 : 50;
    return eq ? 70 : 90;
}
inline double tapeSpeed (const Params& p) { return ival (p.variant) == 2 ? 1.875 : clampv (p.speed, 1.875, 30.0); }
inline Secs designTapeTone (const Params& p, double fs)
{
    Secs s; const double sp = tapeSpeed (p); const auto& heads = HEADS[clampv (ival (p.c2), 0, 2)];
    const int v = ival (p.variant);
    if (v == 2)
    {
        s.push (bqHP (fs, 22, 0.6)); s.push (bqPeak (fs, 45, 1.1, 1.0 + heads.bump));
        s.push (bqHighShelf (fs, std::min (9000.0, fs * 0.45), 0.6, -1.8 + heads.hf)); s.push (bqLP (fs, std::min (14500.0, fs * 0.45), 0.6));
    }
    else
    {
        const double fb = 3.5 * sp * (v == 1 ? 0.9 : 1);
        const double bumpT[4] = { 2.4, 2.1, 1.6, 1.0 };
        const double bump = bumpT[sp >= 30 ? 3 : sp >= 15 ? 2 : sp >= 7.5 ? 1 : 0] + (v == 1 ? 0.5 : 0) + heads.bump;
        s.push (bqHP (fs, fb * 0.4, 0.6)); s.push (bqPeak (fs, fb, 1.2, bump)); s.push (bqPeak (fs, fb * 1.9, 1.5, -0.6 * bump));
        const double fc = std::min (4200 * std::pow (sp, 0.55) * (v == 1 ? 0.93 : 1), fs * 0.45);
        s.push (bqLP (fs, fc, 0.62));
        if (heads.hf != 0) s.push (bqHighShelf (fs, std::min (8000.0, fs * 0.45), 0.6, heads.hf));
    }
    const double bias = p.bias / 100;
    if (bias != 0) s.push (bqHighShelf (fs, std::min (9000.0, fs * 0.45), 0.6, bias > 0 ? -3 * bias : -1.6 * bias));
    const double age = p.age / 100; if (age > 0) s.push (bqHighShelf (fs, std::min (7000.0, fs * 0.45), 0.6, -2 * age));
    return s;
}
class Tape : public Module
{
public:
    explicit Tape (double sampleRate) : fs (sampleRate)
    {
        p = defaultsFor (kTape);
        for (auto& d : dc) d.init (fs);
        hiss[0].init (fs, 1234); hiss[1].init (fs, 5678);
        N = pow2 ((int) std::ceil (fs * 0.012) + 8); mask = N - 1;
        buf[0].assign ((size_t) N, 0); buf[1].assign ((size_t) N, 0);
        driftK = 1 / (fs * 0.4);
        update();
    }
    int type() const override { return kTape; }
    void setParams (const Params& np) override { p = np; update(); }
    void update()
    {
        const double fs2 = 2 * fs;
        const auto& el = ELECTRONICS[clampv (ival (p.c1), 0, 2)]; const double align = ALIGN[clampv (ival (p.c3), 0, 2)].lvl; const auto& F = FORMULA[clampv (ival (p.formula), 0, 2)];
        const double age = p.age / 100, V = std::pow (p.headroom / 24, 1.5);
        analog = p.analog != 0;
        inG = dbToLin (p.drive + align);
        outG = dbToLin (p.output - align);
        const double sp = tapeSpeed (p), ft = 1e6 / (2 * PI * (ival (p.variant) == 2 ? 70 : tapeTau (ival (p.eqStd), sp)));
        const Coefs pre = fo_ZP (fs2, ft, std::min (ft * 4, fs2 * 0.45)); const double k = (1 + pre.a1) / (pre.b0 + pre.b1);
        const Coefs preK { pre.b0 * k, pre.b1 * k, 0, pre.a1, 0 };
        const Coefs de { 1 / preK.b0, preK.a1 / preK.b0, 0, preK.b1 / preK.b0, 0 };
        for (int c = 0; c < 2; ++c) { preF[c].set (preK); deF[c].set (de); }
        const double bias = p.bias / 100;
        satCeil = dbToLin (REF + F.mol - (ival (p.variant) == 2 ? 3 : 0)) * (1 + 0.25 * bias) * (1 - 0.2 * age);
        satB = F.b * (1 - 0.5 * bias) + 0.03 * age;
        ampCeil = 1.4 * el.ceil * V * (1 - 0.2 * age); ampB = el.b;
        const Secs t = designTapeTone (p, fs); tone[0].set (t); tone[1].set (t);
        const double sr = ival (p.variant) == 2 ? 1.6 : 1;
        wowD = analog ? p.wow / 100 * 0.004 * sr * fs / (2 * PI * 0.55) : 0;
        flD = analog ? p.flutter / 100 * 0.0015 * sr * fs / (2 * PI * 7.3) : 0;
        base = wowD + flD > 0 ? 1.25 * (wowD * 1.4 + flD * 1.6) + 3 : 0;
        dph[0] = 2 * PI * 0.55 / fs; dph[1] = 2 * PI * 7.3 / fs; dph[2] = 2 * PI * 11.7 / fs;
        for (int c = 0; c < 2; ++c)
            hiss[c].set (F.hiss + (ival (p.variant) == 2 ? 10 : 0) - 6 * std::log2 (std::max (sp, 1.875) / 15) * 0.5 + (p.hiss - 50) * 0.3 + 6 * age, analog && p.hiss > 0);
        const double m = clampv (p.mix, 0.0, 100.0) / 100; wetG = m; dryG = 1 - m;
    }
    double tapeSample (int c, double x)
    {
        auto& o = os[c]; o[0].up (x * inG);
        double a = o[0].o0, b = o[0].o1;
        if (analog)
        {
            a = sat (a, ampCeil, ampB); b = sat (b, ampCeil, ampB);
            a = preF[c].process (a); a = sat (a, satCeil, satB); a = deF[c].process (a);
            b = preF[c].process (b); b = sat (b, satCeil, satB); b = deF[c].process (b);
        }
        return o[1].down (a, b);
    }
    void process (float* L, float* R, int n) override
    {
        double pi = 0, po = 0;
        for (int i = 0; i < n; ++i)
        {
            const double dl = L[i], dr = R[i];
            pi = std::max ({ pi, std::abs (dl), std::abs (dr) });
            double yl = tone[0].run (tapeSample (0, dl)), yr = tone[1].run (tapeSample (1, dr));
            if (base > 0)
            {
                ph[0] += dph[0]; ph[1] += dph[1]; ph[2] += dph[2];
                for (int k = 0; k < 3; ++k) if (ph[k] > 2 * PI) ph[k] -= 2 * PI;
                if (--driftT <= 0) { driftT = (int) jsRound (fs * 0.25); driftTarget = rng.next(); }
                drift += (driftTarget - drift) * driftK;
                const double d = base + wowD * (std::sin (ph[0]) + 0.4 * drift) + flD * (std::sin (ph[1]) + 0.6 * std::sin (ph[2]));
                buf[0][(size_t) w] = yl; buf[1][(size_t) w] = yr;
                yl = hermite (buf[0].data(), mask, w, d); yr = hermite (buf[1].data(), mask, w, d);
                w = (w + 1) & mask;
            }
            yl = dc[0].process (yl + hiss[0].next()) * outG; yr = dc[1].process (yr + hiss[1].next()) * outG;
            yl = dl * dryG + yl * wetG; yr = dr * dryG + yr * wetG;
            L[i] = (float) yl; R[i] = (float) yr; po = std::max ({ po, std::abs ((double) L[i]), std::abs ((double) R[i]) });
        }
        inMeter = pi; outMeter = po;
    }
private:
    double fs; bool analog = true;
    Halfband os[2][2]; Biquad preF[2], deF[2]; Chain tone[2]; DCBlock dc[2]; Hiss hiss[2];
    int N = 0, mask = 0, w = 0; std::vector<double> buf[2];
    double ph[3] {}, dph[3] {}, drift = 0, driftTarget = 0, driftK = 0; int driftT = 0; Rng rng { 99 };
    double inG = 1, outG = 1, satCeil = 1, satB = 0, ampCeil = 1, ampB = 0, wowD = 0, flD = 0, base = 0, wetG = 1, dryG = 0;
};

// ================================================================ DELAY
inline double delayMs (const Params& p, double bpm)
{
    const int mode = clampv (ival (p.variant), 0, 3); const double mx = DELAY_MAX[mode];
    const double t = ival (p.sync) > 0 ? SYNC_BEATS[clampv (ival (p.sync) - 1, 0, 13)] * 60000 / clampv (bpm > 0 ? bpm : 120.0, 20.0, 300.0) : p.time;
    return clampv (t, 1.0, p.reverse != 0 ? std::min (mx, REV_MAX) : mx);
}
inline double bbdCutoff (double ms) { return std::min (9000.0, 0.45 * 4096 / (2 * ms / 1000)); }
inline Secs designDelayLoop (const Params& p, double fs, double bpm)
{
    Secs s; const int mode = ival (p.variant);
    if (p.lowcut >= 20) s.push (bqHP (fs, std::min (p.lowcut, fs * 0.45), 0.707));
    if (p.highcut >= 200 && p.highcut < 20000) s.push (bqLP (fs, std::min (p.highcut, fs * 0.45), 0.707));
    if (mode == 1 && p.analog != 0) { s.push (bqPeak (fs, 90, 1.0, 1.5)); s.push (bqHighShelf (fs, std::min (6000.0, fs * 0.45), 0.6, -2.5)); }
    if (mode == 2) { const double fc = std::min (bbdCutoff (delayMs (p, bpm)), fs * 0.45); s.push (bqLP (fs, fc, 0.6)); s.push (bqLP (fs, fc, 0.9)); }
    if (mode == 3) { const double fc = std::min (9000.0, fs * 0.45); s.push (bqLP (fs, fc, 0.54)); s.push (bqLP (fs, fc, 1.31)); }
    return s;
}
inline Secs design3Band (const Params& p, double fs)
{
    Secs s;
    if (p.eqLo != 0) s.push (bqLowShelf (fs, 180, 0.7, p.eqLo));
    if (p.eqMid != 0) s.push (bqPeak (fs, std::min (clampv (p.eqMidF, 200.0, 8000.0), fs * 0.45), 0.9, p.eqMid));
    if (p.eqHi != 0) s.push (bqHighShelf (fs, std::min (6000.0, fs * 0.45), 0.7, p.eqHi));
    return s;
}
class Delay : public Module
{
public:
    explicit Delay (double sampleRate) : fs (sampleRate)
    {
        p = defaultsFor (kDelay);
        N = pow2 ((int) std::ceil (fs * 4.6) + 16); mask = N - 1;
        buf[0].assign ((size_t) N, 0); buf[1].assign ((size_t) N, 0);
        for (auto& d : dc) d.init (fs);
        hiss[0].init (fs, 4321); hiss[1].init (fs, 8765);
        update();
    }
    int type() const override { return kDelay; }
    void setBpm (double b) override { if (b != bpm) { bpm = b; update(); } }
    void setParams (const Params& np) override { p = np; update(); }
    void snap() { double t[2]; targets (t); for (int c = 0; c < 2; ++c) { cur[c] = nxt[c] = smooth[c] = tgt[c] = t[c]; } xf = 1; }
    void targets (double* t) const
    {
        const double ms = delayMs (p, bpm), d = ms * fs / 1000; const int st = ival (p.stereo);
        t[0] = d; t[1] = st == 3 ? std::max (1.0, d * clampv (p.ratioLR, 10.0, 200.0) / 100) : d;
    }
    void update()
    {
        mode = clampv (ival (p.variant), 0, 3); analog = p.analog != 0;
        g = clampv (p.feedback, 0.0, 110.0) / 100;
        st = clampv (ival (p.stereo), 0, 3);
        double t[2]; targets (t);
        if (first) { for (int c = 0; c < 2; ++c) cur[c] = nxt[c] = smooth[c] = t[c]; first = false; }
        if (mode == 0)
        {
            if (std::abs (t[0] - nxt[0]) > 0.5 || std::abs (t[1] - nxt[1]) > 0.5)
            {
                if (xf < 1) { cur[0] = nxt[0]; cur[1] = nxt[1]; }
                nxt[0] = t[0]; nxt[1] = t[1]; xf = 0;
            }
        }
        tgt[0] = t[0]; tgt[1] = t[1];
        xfStep = 1 / (0.05 * fs);
        glide = 1 - std::exp (-1 / (0.18 * fs));
        const Secs lp = designDelayLoop (p, fs, bpm);
        for (int c = 0; c < 2; ++c) { loop[c].set (lp); rloop[c].set (lp); }
        const Secs we = design3Band (p, fs); weq[0].set (we); weq[1].set (we);
        rev = p.reverse != 0;
        lastBbd = delayMs (p, bpm);
        drive = dbToLin (p.drive);
        const double ceils[4] = { 1.0, 0.7, 0.55, 1.0 }; satCeil = ceils[mode];
        heads = HEADMASK[clampv (ival (p.heads), 0, 6)];
        const int nh = (heads & 1) + ((heads >> 1) & 1) + ((heads >> 2) & 1); headNorm = 1 / std::sqrt ((double) nh);
        modD = p.modDepth / 100 * 0.003 * fs; dph = 2 * PI * clampv (p.modRate, 0.05, 10.0) / fs;
        wowD = analog && mode == 1 ? (0.0006 + 0.002 * p.age / 100) * fs / (2 * PI * 0.7) : analog && mode == 2 ? 0.0002 * fs / (2 * PI * 0.4) : 0;
        wdph = 2 * PI * (mode == 1 ? 0.7 : 0.4) / fs;
        const double hissDb = mode == 1 ? -78 : mode == 2 ? -84 : -200;
        for (int c = 0; c < 2; ++c) hiss[c].set (hissDb + (p.hiss - 50) * 0.3, analog && p.hiss > 0 && hissDb > -150);
        duck = clampv (p.duck, 0.0, 100.0) / 100; envA = 1 - std::exp (-1 / (0.005 * fs)); envR = 1 - std::exp (-1 / (0.25 * fs));
        const double m = clampv (p.mix, 0.0, 100.0) / 100; wetG = std::min (1.0, 2 * m); dryG = std::min (1.0, 2 * (1 - m));
        width = clampv (p.width, 0.0, 200.0) / 100;
        outG = dbToLin (p.output);
    }
    double tap (int c, double d) const
    {
        const double* b = buf[c].data();
        if (mode != 1 || heads == 1) return hermite (b, mask, w, d);
        double y = 0;
        for (int h = 0; h < 3; ++h) if (heads & (1 << h)) y += hermite (b, mask, w, std::min (d * HEAD_RATIO[h], (double) (N - 4)));
        return y * headNorm;
    }
    double revRead (int c, int T) const
    {
        const double* b = buf[c].data(); const int w0 = (w - 1) & mask;
        const int k1 = (int) (rn % T), k2 = (int) ((rn + (T >> 1)) % T);
        const double g1 = std::sin (PI * k1 / T), g2 = std::sin (PI * k2 / T);
        return b[(w0 - 2 * k1) & mask] * g1 * g1 + b[(w0 - 2 * k2) & mask] * g2 * g2;
    }
    double record (double x) const
    {
        x *= drive;
        if (mode == 3) { x = std::max (-1.0, std::min (1.0, x)); return (double) jsRound (x * 2048) / 2048; }
        if (! analog) return sat (x, 2.0, 0);
        if (mode == 1) return sat (x, satCeil, 0.04);
        if (mode == 2) return sat (x, satCeil, 0.08);
        return sat (x, 2.0, 0);
    }
    void process (float* L, float* R, int n) override
    {
        double pi = 0, po = 0, pw = 0;
        for (int i = 0; i < n; ++i)
        {
            const double dl = L[i], dr = R[i];
            pi = std::max ({ pi, std::abs (dl), std::abs (dr) });
            double rl, rr;
            ph += dph; if (ph > 2 * PI) ph -= 2 * PI;
            wph += wdph; if (wph > 2 * PI) wph -= 2 * PI;
            const double ml = modD * (1 + std::sin (ph)) + wowD * (1 + std::sin (wph)), mr = modD * (1 + std::cos (ph)) + wowD * (1 + std::sin (wph + 0.3));
            if (mode == 0)
            {
                if (xf < 1)
                {
                    xf = std::min (1.0, xf + xfStep);
                    const double a = 0.5 - 0.5 * std::cos (PI * xf);
                    rl = (1 - a) * tap (0, cur[0] + ml) + a * tap (0, nxt[0] + ml);
                    rr = (1 - a) * tap (1, cur[1] + mr) + a * tap (1, nxt[1] + mr);
                    if (xf >= 1) { cur[0] = nxt[0]; cur[1] = nxt[1]; }
                }
                else { rl = tap (0, cur[0] + ml); rr = tap (1, cur[1] + mr); }
            }
            else
            {
                smooth[0] += (tgt[0] - smooth[0]) * glide; smooth[1] += (tgt[1] - smooth[1]) * glide;
                rl = tap (0, smooth[0] + ml); rr = tap (1, smooth[1] + mr);
                if (mode == 2 && ++cnt >= 64)
                {
                    cnt = 0; const double ms = smooth[0] * 1000 / fs;
                    if (std::abs (ms - lastBbd) > lastBbd * 0.01)
                    {
                        lastBbd = ms; Params q = p; q.sync = 0; q.time = ms;
                        const Secs s = designDelayLoop (q, fs, bpm);
                        for (int c = 0; c < 2; ++c) for (int k = 0; k < s.n && k < loop[c].n; ++k) { loop[c].b[(size_t) k].set (s.c[(size_t) k]); rloop[c].b[(size_t) k].set (s.c[(size_t) k]); }
                    }
                }
            }
            const double zl = loop[0].run (rl) + hiss[0].next(), zr = loop[1].run (rr) + hiss[1].next();
            double wl, wr;
            if (st == 0) { const double m = 0.5 * (dl + dr); wl = m + zl * g; wr = m + zr * g; }
            else if (st == 2) { const double m = 0.5 * (dl + dr); wl = m + zr * g; wr = zl * g; }
            else { wl = dl + zl * g; wr = dr + zr * g; }
            buf[0][(size_t) w] = dc[0].process (record (wl)); buf[1][(size_t) w] = dc[1].process (record (wr));
            w = (w + 1) & mask;
            double ol = zl, orr = zr;
            if (rev)
            {
                const int T = (int) std::max (64L, jsRound (mode == 0 ? cur[0] : smooth[0])), TR = (int) std::max (64L, jsRound (mode == 0 ? cur[1] : smooth[1]));
                ol = revRead (0, T); orr = revRead (1, TR);
                ol = rloop[0].run (ol); orr = rloop[1].run (orr);
                ++rn;
            }
            const double a = std::max (std::abs (dl), std::abs (dr));
            env += (a - env) * (a > env ? envA : envR);
            const double dg = 1 - duck * std::min (1.0, env / 0.25);
            double el = weq[0].run (ol) * dg, er = weq[1].run (orr) * dg;
            const double mid = 0.5 * (el + er), side = 0.5 * (el - er) * width; el = mid + side; er = mid - side;
            pw = std::max ({ pw, std::abs (el), std::abs (er) });
            const double yl = (dl * dryG + el * wetG) * outG, yr = (dr * dryG + er * wetG) * outG;
            L[i] = (float) yl; R[i] = (float) yr; po = std::max ({ po, std::abs ((double) L[i]), std::abs ((double) R[i]) });
        }
        inMeter = pi; outMeter = po; wetMeter = pw;
    }
    double wowD = 0;
private:
    double fs, bpm = 120;
    int N = 0, mask = 0, w = 0; std::vector<double> buf[2];
    Chain loop[2], rloop[2], weq[2]; DCBlock dc[2]; Hiss hiss[2];
    double cur[2] {}, nxt[2] {}, tgt[2] {}, smooth[2] {}; double xf = 1; bool first = true;
    double ph = 0, wph = 0, env = 0; int cnt = 0; long long rn = 0;
    int mode = 0, st = 1, heads = 1; bool analog = true, rev = false;
    double g = 0, xfStep = 0, glide = 0, lastBbd = 0, drive = 1, satCeil = 1, headNorm = 1, modD = 0, dph = 0, wdph = 0;
    double duck = 0, envA = 0, envR = 0, wetG = 1, dryG = 1, width = 1, outG = 1;
};

// ================================================================ REVERB
static constexpr int FDN_N = 16;
static const double FDN_MS[FDN_N] = { 31.3, 37.1, 41.9, 47.3, 53.9, 59.1, 64.7, 70.3, 76.9, 83.1, 89.7, 95.3, 101.9, 109.1, 116.3, 123.7 };
struct VariantT { double scale, er, erSpan, erPre, ld; };
static const VariantT VARIANT[5] = { { 1.0, 1.0, 60, 8, 1.0 }, { 0.36, 1.4, 22, 2, 0.8 }, { 0.55, 0.0, 0, 0, 1.0 }, { 0.62, 1.1, 35, 4, 0.9 }, { 1.65, 0.8, 110, 15, 1.0 } };
static const double AP_MS[4] = { 4.77, 3.59, 12.73, 9.31 };
inline bool isPrime (long n) { if (n < 2) return false; for (long k = 2; k * k <= n; ++k) if (n % k == 0) return false; return true; }
inline long nextPrime (double x) { long n = std::max (2L, jsRound (x)); while (! isPrime (n)) ++n; return n; }
inline void fdnLengths (const Params& p, double fs, long* out)
{
    const auto& V = VARIANT[clampv (ival (p.variant), 0, 4)]; const double s = V.scale * clampv (p.size, 20.0, 200.0) / 100;
    for (int k = 0; k < FDN_N; ++k) out[k] = nextPrime (FDN_MS[k] * s * fs / 1000);
}
struct LineGains { double mid, low, high; };
inline LineGains lineGains (long len, const Params& p, double fs)
{
    const double rt = clampv (p.decay, 0.1, 30.0), lo = clampv (p.loMult, 0.25, 4.0), hi = clampv (p.hiMult, 0.05, 2.0);
    auto g = [&] (double r) { return -60.0 * len / (r * fs); };
    return { g (rt), g (rt * lo), g (rt * hi) };
}
static constexpr double LS_F = 250, HS_F = 3500;
inline std::vector<double> rtCurve (const Params& p, double fs, const std::vector<double>& freqs)
{
    long lens[FDN_N]; fdnLengths (p, fs, lens);
    std::vector<double> out;
    for (double f : freqs)
    {
        double acc = 0;
        for (long L : lens)
        {
            const auto G = lineGains (L, p, fs);
            const double db = G.mid + bqMagDb (foLowShelf (fs, LS_F, dbToLin (G.low - G.mid)), fs, f) + bqMagDb (foHighShelf (fs, HS_F, dbToLin (G.high - G.mid)), fs, f);
            acc += -60.0 * L / (fs * std::min (db, -1e-6));
        }
        out.push_back (acc / FDN_N);
    }
    return out;
}
inline Secs designWetEQ (const Params& p, double fs)
{
    Secs s;
    if (p.lowcut >= 20) s.push (bqHP (fs, std::min (p.lowcut, fs * 0.45), 0.707));
    if (p.highcut >= 200 && p.highcut < 20000) s.push (bqLP (fs, std::min (p.highcut, fs * 0.45), 0.707));
    s.append (design3Band (p, fs));
    return s;
}
struct Allpass
{
    std::vector<double> b; int n = 0, i = 0; double g = 0.5;
    void reserve (int mx) { b.assign ((size_t) mx, 0.0); }
    void reset (int len) { n = std::min (len, (int) b.size()); std::fill (b.begin(), b.end(), 0.0); i = 0; g = 0.5; }
    double process (double x) { const double d = b[(size_t) i], y = -g * x + d; b[(size_t) i] = x + g * y; i = (i + 1) % n; return y; }
};
class Reverb : public Module
{
public:
    Reverb (double sampleRate, bool isOffline = false) : fs (sampleRate), offline (isOffline)
    {
        p = defaultsFor (kReverb);
        const int maxLine = pow2 ((int) std::ceil (123.7 * 1.65 * 2 * fs / 1000) + 64);
        for (auto& l : lines) l.assign ((size_t) maxLine, 0.0);
        lmask = maxLine - 1;
        for (int k = 0; k < FDN_N; ++k) { lphase[k] = k * 0.71; ldph[k] = 2 * PI * (0.13 + 0.061 * k) / fs; }
        PN = pow2 ((int) std::ceil (0.5 * fs) + 8); pmask = PN - 1; pre[0].assign ((size_t) PN, 0); pre[1].assign ((size_t) PN, 0);
        erN = pow2 ((int) std::ceil (0.2 * fs * 1.7) + 8); erMask = erN - 1; erBuf.assign ((size_t) erN, 0);
        const int apMax = (int) nextPrime (12.73 * std::sqrt (1.65 * 2) * fs / 1000 * 1.07 + 8) + 8;
        for (auto& c : ap) for (auto& a : c) a.reserve (apMax);
        for (auto& d : dc) d.init (fs);
        update();
    }
    ~Reverb() override = default;
    int type() const override { return kReverb; }
    void setParams (const Params& np) override { p = np; update(); }
    bool usesIR() const { return ival (p.source) > 0 || p.reverse != 0; }
    // install a new convolver pair (audio thread). Returns the pair that is no longer needed (to be freed elsewhere).
    ConvPair* setIR (ConvPair* c)
    {
        ConvPair* drop = nullptr;
        if (conv) { drop = convOld; convOld = conv; xf = 0; } else xf = 1;
        conv = c;
        return drop;
    }
    ConvPair* takeFinished() { ConvPair* d = finished; finished = nullptr; return d; }
    ConvPair* releaseAll() { ConvPair* a = conv; conv = nullptr; return a; }
    ConvPair* releaseOld() { ConvPair* a = convOld; convOld = nullptr; return a; }
    void update()
    {
        const auto& V = VARIANT[clampv (ival (p.variant), 0, 4)];
        preD = clampv (p.predelay, 0.0, 490.0) * fs / 1000;
        fdnLengths (p, fs, lens);
        for (int k = 0; k < FDN_N; ++k)
        {
            const auto G = lineGains (lens[k], p, fs);
            v[k] = dbToLin (G.mid);
            lp[k].set (foLowShelf (fs, LS_F, dbToLin (G.low - G.mid)));
            hp[k].set (foHighShelf (fs, HS_F, dbToLin (G.high - G.mid)));
        }
        modD = offline ? 0 : clampv (p.modulation, 0.0, 100.0) / 100 * 10 * fs / 48000;
        const double s = V.scale * clampv (p.size, 20.0, 200.0) / 100, dg = clampv (p.diffusion, 0.0, 100.0) / 100 * 0.72;
        for (int c = 0; c < 2; ++c)
        {
            int want[4]; bool diff = ! apInit;
            for (int k = 0; k < 4; ++k) { want[k] = (int) nextPrime (AP_MS[k] * std::sqrt (s) * fs / 1000 * (c ? 1.07 : 1) + k); if (ap[c][(size_t) k].n != want[k]) diff = true; }
            if (diff) for (int k = 0; k < 4; ++k) ap[c][(size_t) k].reset (want[k]);
            for (auto& a : ap[c]) a.g = dg;
        }
        apInit = true;
        nTaps = 0;
        const double erAmt = clampv (p.er, 0.0, 100.0) / 100 * V.er;
        if (erAmt > 0)
        {
            Rng r ((uint32_t) (31337 + ival (p.variant)));
            for (int k = 0; k < 24; ++k)
            {
                const double t = (V.erPre + V.erSpan * std::sqrt (clampv (p.size, 20.0, 200.0) / 100) * std::pow ((k + 0.5 + 0.4 * r.next()) / 24, 1.3)) * fs / 1000;
                Tap& tp = taps[(size_t) nTaps++];
                tp.d = std::min ((double) (erN - 4), std::max (1.0, t));
                const double a = erAmt * 0.7 * (0.55 + 0.45 * r.next()) * std::pow (1 - k / 30.0, 1.2);
                tp.g = a * (r.next() > 0 ? 1 : -1); tp.ch = k & 1;
            }
        }
        for (int c = 0; c < 2; ++c) erLP[c].set (bqLP (fs, std::min (7000.0, fs * 0.45), 0.6));
        lateG = 1.0 / std::sqrt ((double) FDN_N) * 0.9;
        const Secs e = designWetEQ (p, fs); eq[0].set (e); eq[1].set (e);
        width = clampv (p.width, 0.0, 200.0) / 100;
        const double m = clampv (p.mix, 0.0, 100.0) / 100; wetG = std::min (1.0, 2 * m); dryG = std::min (1.0, 2 * (1 - m));
        outG = dbToLin (p.output);
        xfStep = 1 / (0.08 * fs);
    }
    void algo (double xl, double xr, double* out)
    {
        for (int k = 0; k < 4; ++k) { xl = ap[0][(size_t) k].process (xl); xr = ap[1][(size_t) k].process (xr); }
        erBuf[(size_t) erW] = 0.5 * (xl + xr);
        double el = 0, er = 0;
        for (int k = 0; k < nTaps; ++k) { const auto& t = taps[(size_t) k]; const double vv = erBuf[(size_t) ((erW - (int) std::floor (t.d)) & erMask)] * t.g; if (t.ch) er += vv; else el += vv; }
        erW = (erW + 1) & erMask;
        el = erLP[0].process (el); er = erLP[1].process (er);
        double s[FDN_N];
        for (int k = 0; k < FDN_N; ++k)
        {
            double d = (double) lens[k];
            if (modD > 0) { lphase[k] += ldph[k]; if (lphase[k] > 2 * PI) lphase[k] -= 2 * PI; d += modD * (1 + std::sin (lphase[k])); }
            const double y = modD > 0 ? hermite (lines[k].data(), lmask, lw, d) : lines[k][(size_t) ((lw - (int) lens[k]) & lmask)];
            s[k] = hp[k].process (lp[k].process (y * v[k]));
        }
        double ol = 0, orr = 0;
        for (int k = 0; k < FDN_N; ++k) { ol += (k & 1 ? -s[k] : s[k]); orr += ((k >> 1) & 1 ? -s[k] : s[k]); }
        for (int h = 1; h < FDN_N; h <<= 1) for (int i = 0; i < FDN_N; i += h << 1) for (int j = i; j < i + h; ++j) { const double a = s[j], b = s[j + h]; s[j] = a + b; s[j + h] = a - b; }
        for (int k = 0; k < FDN_N; ++k) lines[k][(size_t) lw] = s[k] * 0.25 + (k & 1 ? xr : xl) * (k & 2 ? -0.5 : 0.5);
        lw = (lw + 1) & lmask;
        out[0] = ol * lateG + el; out[1] = orr * lateG + er;
    }
    void process (float* L, float* R, int n) override
    {
        double pi = 0, po = 0, pw = 0, o[2]; const bool ir = usesIR();
        for (int i = 0; i < n; ++i)
        {
            const double dl = L[i], dr = R[i];
            pi = std::max ({ pi, std::abs (dl), std::abs (dr) });
            pre[0][(size_t) pw0] = dl; pre[1][(size_t) pw0] = dr;
            const double xl = preD > 0 ? hermite (pre[0].data(), pmask, pw0, std::max (1.0, preD)) : dl;
            const double xr = preD > 0 ? hermite (pre[1].data(), pmask, pw0, std::max (1.0, preD)) : dr;
            pw0 = (pw0 + 1) & pmask;
            double wl = 0, wr = 0;
            if (ir)
            {
                if (conv)
                {
                    wl = conv->l.process (xl); wr = conv->r.process (xr);
                    if (xf < 1 && convOld)
                    {
                        const double a = xf; wl = a * wl + (1 - a) * convOld->l.process (xl); wr = a * wr + (1 - a) * convOld->r.process (xr);
                        xf = std::min (1.0, xf + xfStep); if (xf >= 1) { finished = convOld; convOld = nullptr; }
                    }
                }
            }
            else { algo (xl, xr, o); wl = o[0]; wr = o[1]; }
            wl = dc[0].process (eq[0].run (wl)); wr = dc[1].process (eq[1].run (wr));
            const double mid = 0.5 * (wl + wr), side = 0.5 * (wl - wr) * width; wl = mid + side; wr = mid - side;
            pw = std::max ({ pw, std::abs (wl), std::abs (wr) });
            const double yl = (dl * dryG + wl * wetG) * outG, yr = (dr * dryG + wr * wetG) * outG;
            L[i] = (float) yl; R[i] = (float) yr; po = std::max ({ po, std::abs ((double) L[i]), std::abs ((double) R[i]) });
        }
        inMeter = pi; outMeter = po; wetMeter = pw;
    }
private:
    struct Tap { double d = 1, g = 0; int ch = 0; };
    double fs; bool offline;
    std::array<std::vector<double>, FDN_N> lines; int lmask = 0, lw = 0;
    Biquad lp[FDN_N], hp[FDN_N]; double v[FDN_N] {}, lphase[FDN_N] {}, ldph[FDN_N] {}; long lens[FDN_N] {};
    int PN = 0, pmask = 0, pw0 = 0; std::vector<double> pre[2];
    std::array<std::array<Allpass, 4>, 2> ap; bool apInit = false;
    int erN = 0, erMask = 0, erW = 0; std::vector<double> erBuf; std::array<Tap, 24> taps {}; int nTaps = 0;
    Biquad erLP[2]; Chain eq[2]; DCBlock dc[2];
    ConvPair* conv = nullptr; ConvPair* convOld = nullptr; ConvPair* finished = nullptr; double xf = 1;
    double preD = 0, modD = 0, lateG = 0, width = 1, wetG = 1, dryG = 1, outG = 1, xfStep = 0;
};

inline std::unique_ptr<Module> create (int type, double fs)
{
    if (type == kTape) return std::make_unique<Tape> (fs);
    if (type == kDelay) return std::make_unique<Delay> (fs);
    return std::make_unique<Reverb> (fs);
}

// render the algorithmic reverb's impulse response (for reverse reverb in ALGO mode)
inline IR renderAlgoIR (const Params& p, double fs, double lenMs)
{
    Reverb m (fs, true);
    Params q = p; q.source = 0; q.reverse = 0; q.predelay = 0; q.mix = 100; q.width = 100; q.lowcut = 0; q.highcut = 0; q.output = 0; q.modulation = 0; q.eqLo = 0; q.eqMid = 0; q.eqHi = 0;
    m.setParams (q);
    const int n = (int) jsRound (lenMs * fs / 1000);
    IR out { std::vector<float> ((size_t) n, 0.f), std::vector<float> ((size_t) n, 0.f) };
    out[0][0] = out[1][0] = 1;
    for (int i = 0; i < n; i += 512) { const int k = std::min (512, n - i); m.process (out[0].data() + i, out[1].data() + i, k); }
    return out;
}
// the IR a reverb module needs: built-in / user / rendered algorithm, then trim + reverse. Empty when not needed.
inline IR buildIR (const Params& p, double fs, const IR* userIR)
{
    const int src = ival (p.source);
    if (src == 0 && p.reverse == 0) return {};
    IR raw;
    if (src == 1) raw = synthIR (ival (p.irIndex), fs);
    else if (src == 2) { if (! userIR || userIR->at (0).empty()) return {}; raw = *userIR; }
    else raw = renderAlgoIR (p, fs, clampv (p.revLen, 100.0, 6000.0) + 50);
    return prepareIR (raw, p, fs);
}

// ================================================================ previews (editor)
inline std::array<std::vector<double>, 2> echoPattern (const Params& p, double bpm, double durMs, double binMs, double fs = 24000)
{
    Delay m (fs); m.setBpm (bpm > 0 ? bpm : 120);
    Params q = p; q.hiss = 0; q.modDepth = 0; q.duck = 0; q.mix = 100; q.output = 0; q.width = 100;
    m.setParams (q); m.wowD = 0; m.snap();
    const int n = (int) jsRound (fs * durMs / 1000), step = (int) std::max (1L, jsRound (fs * binMs / 1000));
    std::vector<float> L ((size_t) step), R ((size_t) step); std::array<std::vector<double>, 2> out;
    for (int i0 = 0; i0 + step <= n; i0 += step)
    {
        std::fill (L.begin(), L.end(), 0.f); std::fill (R.begin(), R.end(), 0.f);
        if (i0 == 0) { L[0] = 1; R[0] = ival (p.stereo) == 2 ? 0.f : 1.f; }
        m.process (L.data(), R.data(), step);
        double a = 0, b = 0; for (int j = 0; j < step; ++j) { a = std::max (a, (double) std::abs (L[(size_t) j])); b = std::max (b, (double) std::abs (R[(size_t) j])); }
        out[0].push_back (a); out[1].push_back (b);
    }
    return out;
}
inline std::vector<double> reverbEnvelope (const Params& p, const IR* ir, double durMs, double binMs, double fs = 24000)
{
    Reverb m (fs, false); Params q = p; q.mix = 100; q.output = 0; m.setParams (q);
    std::unique_ptr<ConvPair> cp;
    if (m.usesIR() && ir && ! ir->at (0).empty()) { cp = std::make_unique<ConvPair> ((*ir)[0], (*ir)[1]); m.setIR (cp.get()); }
    const int n = (int) jsRound (fs * durMs / 1000), step = (int) std::max (1L, jsRound (fs * binMs / 1000));
    std::vector<float> L ((size_t) step), R ((size_t) step); std::vector<double> env;
    for (int i0 = 0; i0 + step <= n; i0 += step)
    {
        std::fill (L.begin(), L.end(), 0.f); std::fill (R.begin(), R.end(), 0.f);
        if (i0 == 0) L[0] = R[0] = 1;
        m.process (L.data(), R.data(), step);
        double e = 0; for (int j = 0; j < step; ++j) e += (double) L[(size_t) j] * L[(size_t) j] + (double) R[(size_t) j] * R[(size_t) j];
        env.push_back (10 * std::log10 (e / (2 * step) + 1e-14));
    }
    m.releaseAll();
    return env;
}
inline std::vector<double> tapeCurve (const Params& p, double fs, const std::vector<double>& levels)
{
    std::vector<double> out;
    for (double lv : levels)
    {
        Tape m (fs); Params q = p; q.wow = 0; q.flutter = 0; q.hiss = 0; q.mix = 100; m.setParams (q);
        const int n = 4096; const double A = dbToLin (lv); std::vector<float> L ((size_t) n), R ((size_t) n);
        for (int i = 0; i < n; ++i) L[(size_t) i] = R[(size_t) i] = (float) (A * std::sin (2 * PI * 1000 * i / fs));
        m.process (L.data(), R.data(), n);
        double pk = 0; for (int i = n / 2; i < n; ++i) pk = std::max (pk, (double) std::abs (L[(size_t) i]));
        out.push_back (linToDb (pk));
    }
    return out;
}
inline std::vector<double> tapeLevelResponse (const Params& p, double fs, const std::vector<double>& freqs, double lvDb)
{
    std::vector<double> out;
    for (double f : freqs)
    {
        Tape m (fs); Params q = p; q.wow = 0; q.flutter = 0; q.hiss = 0; q.mix = 100; q.output = 0; m.setParams (q);
        const int n = (int) std::max (4096L, jsRound (fs * 8 / f)); const double A = dbToLin (lvDb);
        std::vector<float> L ((size_t) n), R ((size_t) n);
        for (int i = 0; i < n; ++i) L[(size_t) i] = R[(size_t) i] = (float) (A * std::sin (2 * PI * f * i / fs));
        m.process (L.data(), R.data(), n);
        double re = 0, im = 0; const int h = n / 2;
        for (int i = h; i < n; ++i) { const double ph = 2 * PI * f * i / fs; re += L[(size_t) i] * std::cos (ph); im += L[(size_t) i] * std::sin (ph); }
        out.push_back (linToDb (2 * std::hypot (re, im) / (n - h)) - lvDb - p.output);
    }
    return out;
}
struct Harm { double h[6]; double thd; };
inline Harm harmonics (int type, const Params& p, double fs, double f0, double levelDb)
{
    auto m = create (type, fs); Params q = p; q.hiss = 0; q.wow = 0; q.flutter = 0; q.mix = 100; m->setParams (q);
    const int n = (int) jsRound (fs * 0.3), N = (int) jsRound (fs * 0.1); const double A = dbToLin (levelDb);
    std::vector<float> L ((size_t) n), R ((size_t) n);
    for (int i = 0; i < n; ++i) L[(size_t) i] = R[(size_t) i] = (float) (A * std::sin (2 * PI * f0 * i / fs));
    for (int i = 0; i < n; i += 256) { const int k = std::min (256, n - i); m->process (L.data() + i, R.data() + i, k); }
    auto mag = [&] (double f) { double re = 0, im = 0; for (int i = 0; i < N; ++i) { const double w = 0.5 - 0.5 * std::cos (2 * PI * i / (N - 1)), x = L[(size_t) (n - N + i)] * w, ph = 2 * PI * f * i / fs; re += x * std::cos (ph); im += x * std::sin (ph); } return std::hypot (re, im); };
    double h1 = mag (f0); if (h1 == 0) h1 = 1e-12;
    Harm r {}; double sum = 0;
    for (int k = 2; k <= 7; ++k) { const double v = f0 * k < fs * 0.45 ? mag (f0 * k) : 0; sum += v * v; r.h[k - 2] = 20 * std::log10 (std::max (v / h1, 1e-7)); }
    r.thd = std::sqrt (sum) / h1 * 100;
    return r;
}
inline std::vector<double> responseTone (int type, const Params& p, double fs, const std::vector<double>& freqs, double bpm)
{
    Secs s;
    if (type == kTape) s = designTapeTone (p, fs);
    else if (type == kDelay) { s = designDelayLoop (p, fs, bpm); s.append (design3Band (p, fs)); }
    else s = designWetEQ (p, fs);
    std::vector<double> out; for (double f : freqs) out.push_back (s.magDb (fs, f));
    return out;
}
} // namespace ts

#if defined (__clang__) || defined (__GNUC__)
 #pragma GCC diagnostic pop
#endif
