// ============================================================================
// Tape Space Rack — DSP core (JavaScript)
//   tape   : tape machine (record EQ → saturation at 2x → playback EQ / head bump → wow & flutter → hiss)
//   delay  : Digital HQ / Tape Echo (multi-head) / Analog BBD / Vintage Digital
//   reverb : 16-line FDN (Hall / Room / Plate / Chamber / Cathedral) or zero-latency IR convolution; reverse reverb
// All three share one parameter set (real units). /Source/DSP.h is a line-by-line C++ port.
// ============================================================================
var TS = (function () {
  'use strict';
  const PI = Math.PI;
  const dbToLin = (d) => Math.pow(10, d / 20);
  const linToDb = (l) => 20 * Math.log10(Math.max(l, 1e-9));
  const clamp = (v, a, b) => Math.min(b, Math.max(a, v));
  const num = (v, d) => (v === undefined || v === null ? d : +v);
  const REF = -18;                                  // 0 VU = −18 dBFS
  const TYPES = ['tape', 'delay', 'reverb'];

  // ---------------------------------------------------------------- positions / names
  const SYNC = {
    names: ['1/32', '1/16T', '1/16', '1/16.', '1/8T', '1/8', '1/8.', '1/4T', '1/4', '1/4.', '1/2T', '1/2', '1/2.', '1/1'],
    beats: [0.125, 1 / 6, 0.25, 0.375, 1 / 3, 0.5, 0.75, 2 / 3, 1, 1.5, 4 / 3, 2, 3, 4],
  };
  const SPEC = {
    tape: { variants: ['Studio 2-Track', 'Multitrack', 'Cassette'], speed: [3.75, 7.5, 15, 30], formula: ['Modern', 'Classic', 'Vintage'], eq: ['NAB', 'CCIR'] },
    delay: { variants: ['Digital HQ', 'Tape Echo', 'Analog BBD', 'Vintage Digital'], stereo: ['MONO', 'STEREO', 'PING-PONG', 'DUAL'],
             heads: ['1', '2', '3', '1+2', '2+3', '1+3', '1+2+3'], headRatio: [1, 1.9, 2.8], maxTime: [4000, 1600, 1200, 2000] },
    reverb: { variants: ['Hall', 'Room', 'Plate', 'Chamber', 'Cathedral'], source: ['ALGO', 'IR', 'USER IR'],
              irs: ['Concert Hall', 'Wood Room', 'Bright Plate', 'Stone Chamber', 'Vocal Booth', 'Large Church', 'Spring Tank'] },
  };
  const HEADMASK = [1, 2, 4, 3, 6, 5, 7];

  // ---------------------------------------------------------------- circuit parts (tape only)
  const CIRCUIT = {
    tape: {
      c1: { label: 'Electronics', list: [
        { name: 'Tube electronics', ceil: 1.4, b: 0.10 },
        { name: 'Solid-state electronics', ceil: 2.2, b: 0.03 },
        { name: 'Modern transformerless', ceil: 3.2, b: 0.0 }] },
      c2: { label: 'Heads', list: [
        { name: 'New heads', hf: 0, bump: 0 },
        { name: 'Worn heads', hf: -2.5, bump: 0.6 },
        { name: 'Relapped (bright)', hf: 0.8, bump: -0.3 }] },
      c3: { label: 'Alignment', list: [
        { name: 'Standard level', lvl: 0 },
        { name: 'Hot (+3 dB)', lvl: 3 },
        { name: 'Very hot (+6 dB)', lvl: 6 }] },
    },
  };
  const FORMULA = [{ mol: 12, b: 0.02, hiss: -86 }, { mol: 9, b: 0.04, hiss: -83 }, { mol: 6, b: 0.07, hiss: -79 }];

  // ---------------------------------------------------------------- defaults
  const BASE = {
    variant: 0, mix: 100, output: 0, analog: 1, c1: 0, c2: 0, c3: 0, age: 0, headroom: 24,
    drive: 0, speed: 15, eqStd: 0, formula: 0, bias: 0, wow: 0, flutter: 0, hiss: 0,
    time: 375, sync: 0, feedback: 35, stereo: 1, ratioLR: 75, lowcut: 80, highcut: 12000, modRate: 0.5, modDepth: 0, duck: 0, heads: 0,
    source: 0, irIndex: 0, predelay: 20, size: 100, decay: 2.2, hiMult: 0.55, loMult: 1.2, diffusion: 80, er: 50, modulation: 30, width: 100,
    reverse: 0, revLen: 1500, irLen: 100,
    eqLo: 0, eqMid: 0, eqMidF: 1500, eqHi: 0,
  };
  const DEFAULTS = {
    tape: Object.assign({}, BASE, { mix: 100 }),
    delay: Object.assign({}, BASE, { mix: 30, lowcut: 80, highcut: 12000 }),
    reverb: Object.assign({}, BASE, { mix: 25, lowcut: 100, highcut: 12000 }),
  };

  // ---------------------------------------------------------------- biquads (RBJ) + first-order sections
  function bqPeak(fs, f, q, gDb) {
    const A = Math.pow(10, gDb / 40), w = 2 * PI * f / fs, al = Math.sin(w) / (2 * q), c = Math.cos(w), a0 = 1 + al / A;
    return { b0: (1 + al * A) / a0, b1: -2 * c / a0, b2: (1 - al * A) / a0, a1: -2 * c / a0, a2: (1 - al / A) / a0 };
  }
  function bqHighShelf(fs, f, q, gDb) {
    const A = Math.pow(10, gDb / 40), w = 2 * PI * f / fs, al = Math.sin(w) / (2 * q), c = Math.cos(w), s = 2 * Math.sqrt(A) * al;
    const a0 = (A + 1) - (A - 1) * c + s;
    return { b0: A * ((A + 1) + (A - 1) * c + s) / a0, b1: -2 * A * ((A - 1) + (A + 1) * c) / a0,
             b2: A * ((A + 1) + (A - 1) * c - s) / a0, a1: 2 * ((A - 1) - (A + 1) * c) / a0, a2: ((A + 1) - (A - 1) * c - s) / a0 };
  }
  function bqLowShelf(fs, f, q, gDb) {
    const A = Math.pow(10, gDb / 40), w = 2 * PI * f / fs, al = Math.sin(w) / (2 * q), c = Math.cos(w), s = 2 * Math.sqrt(A) * al;
    const a0 = (A + 1) + (A - 1) * c + s;
    return { b0: A * ((A + 1) - (A - 1) * c + s) / a0, b1: 2 * A * ((A - 1) - (A + 1) * c) / a0,
             b2: A * ((A + 1) - (A - 1) * c - s) / a0, a1: -2 * ((A - 1) + (A + 1) * c) / a0, a2: ((A + 1) + (A - 1) * c - s) / a0 };
  }
  function bqHP(fs, f, q) {
    const w = 2 * PI * f / fs, al = Math.sin(w) / (2 * q), c = Math.cos(w), a0 = 1 + al;
    return { b0: (1 + c) / 2 / a0, b1: -(1 + c) / a0, b2: (1 + c) / 2 / a0, a1: -2 * c / a0, a2: (1 - al) / a0 };
  }
  function bqLP(fs, f, q) {
    const w = 2 * PI * f / fs, al = Math.sin(w) / (2 * q), c = Math.cos(w), a0 = 1 + al;
    return { b0: (1 - c) / 2 / a0, b1: (1 - c) / a0, b2: (1 - c) / 2 / a0, a1: -2 * c / a0, a2: (1 - al) / a0 };
  }
  // first-order (s + wz) / (s + wp), pre-warped bilinear, scaled by k
  function fo_ZP(fs, fz, fp, k) {
    const c = 2 * fs, wz = c * Math.tan(PI * Math.min(fz, fs * 0.49) / fs), wp = c * Math.tan(PI * Math.min(fp, fs * 0.49) / fs), d = c + wp;
    k = k === undefined ? 1 : k;
    return { b0: k * (c + wz) / d, b1: k * (wz - c) / d, b2: 0, a1: (wp - c) / d, a2: 0 };
  }
  // first-order shelves: gain g (linear) below / above the corner, 1 on the other side
  function foLowShelf(fs, fc, g) { const r = Math.sqrt(g); return fo_ZP(fs, fc * r, fc / r, 1); }          // DC = g, HF = 1
  function foHighShelf(fs, fc, g) { const r = Math.sqrt(g); return fo_ZP(fs, fc / r, fc * r, g); }          // DC = 1, HF = g
  function bqMagDb(c, fs, f) {
    const w = 2 * PI * f / fs;
    const cr = Math.cos(w), ci = -Math.sin(w), c2r = Math.cos(2 * w), c2i = -Math.sin(2 * w);
    const nr = c.b0 + c.b1 * cr + c.b2 * c2r, ni = c.b1 * ci + c.b2 * c2i;
    const dr = 1 + c.a1 * cr + c.a2 * c2r, di = c.a1 * ci + c.a2 * c2i;
    return 10 * Math.log10((nr * nr + ni * ni) / (dr * dr + di * di) + 1e-30);
  }
  const secsMagDb = (secs, fs, f) => secs.reduce((s, c) => s + bqMagDb(c, fs, f), 0);
  class Biquad {
    constructor(c) { this.c = c || { b0: 1, b1: 0, b2: 0, a1: 0, a2: 0 }; this.z1 = 0; this.z2 = 0; }
    set(c) { this.c = c; }
    process(x) { const c = this.c, y = c.b0 * x + this.z1; this.z1 = c.b1 * x - c.a1 * y + this.z2; this.z2 = c.b2 * x - c.a2 * y; return y; }
    reset() { this.z1 = this.z2 = 0; }
  }
  function setSecs(chs, coefs) {
    for (const arr of chs) { while (arr.length < coefs.length) arr.push(new Biquad()); arr.length = coefs.length; coefs.forEach((c, i) => arr[i].set(c)); }
  }
  function runSecs(arr, x) { for (let k = 0; k < arr.length; k++) x = arr[k].process(x); return x; }

  // ---------------------------------------------------------------- helpers
  class DCBlock { constructor(fs) { this.R = 1 - 2 * PI * 5 / fs; this.x1 = 0; this.y1 = 0; }
    process(x) { const y = x - this.x1 + this.R * this.y1; this.x1 = x; this.y1 = y; return y; } }
  function sat(x, ceil, bias) { const u = x / ceil, tb = Math.tanh(bias); return ceil * (Math.tanh(u + bias) - tb) / (1 - tb * tb); }
  // deterministic noise
  class Rng { constructor(seed) { this.s = seed >>> 0; } next() { this.s = (Math.imul(this.s, 1664525) + 1013904223) >>> 0; return (this.s >>> 8) / 16777216 * 2 - 1; } }
  class Hiss {
    constructor(fs, seed) { this.r = new Rng(seed); this.n = 0; this.a = 1 - Math.exp(-2 * PI * 7000 / fs); this.g = 0; }
    set(levelDb, on) { const norm = 0.57735 * Math.sqrt(this.a / (2 - this.a)); this.g = on ? dbToLin(levelDb) / norm : 0; }
    next() { if (this.g === 0) return 0; this.n += this.a * (this.r.next() - this.n); return this.n * this.g; }
  }
  // 4-point Hermite interpolation; buf is a power-of-two ring, w = write index, d = delay in samples (≥ 1)
  function hermite(buf, mask, w, d) {
    const di = Math.floor(d), f = d - di;
    const i0 = (w - di) & mask;
    const xm1 = buf[(i0 + 1) & mask], x0 = buf[i0], x1 = buf[(i0 - 1) & mask], x2 = buf[(i0 - 2) & mask];
    const c0 = x0, c1 = 0.5 * (x1 - xm1), c2 = xm1 - 2.5 * x0 + 2 * x1 - 0.5 * x2, c3 = 0.5 * (x2 - xm1) + 1.5 * (x0 - x1);
    return ((c3 * f + c2) * f + c1) * f + c0;
  }
  const pow2 = (n) => { let p = 1; while (p < n) p <<= 1; return p; };

  // ---------------------------------------------------------------- 2x oversampling (polyphase IIR halfband, elliptic design)
  function halfbandCoefs(n, tbw) {
    let k = Math.tan((1 - tbw * 2) * PI / 4); k *= k;
    const kk = Math.pow(1 - k * k, 0.25), e = 0.5 * (1 - kk) / (1 + kk), e4 = Math.pow(e, 4);
    const q = e * (1 + e4 * (2 + e4 * (15 + 150 * e4))), order = n * 2 + 1, c = [];
    for (let idx = 0; idx < n; idx++) {
      const cc = idx + 1; let i = 0, j = 1, nm = 0, t;
      do { t = Math.pow(q, i * (i + 1)) * Math.sin((i * 2 + 1) * cc * PI / order) * j; nm += t; j = -j; i++; } while (Math.abs(t) > 1e-100);
      nm *= Math.pow(q, 0.25);
      i = 1; j = -1; let dn = 0;
      do { t = Math.pow(q, i * i) * Math.cos(i * 2 * cc * PI / order) * j; dn += t; j = -j; i++; } while (Math.abs(t) > 1e-100);
      dn += 0.5;
      const ww = nm / dn, wwsq = ww * ww, x = Math.sqrt((1 - wwsq * k) * (1 - wwsq / k)) / (1 + wwsq);
      c.push((1 - x) / (1 + x));
    }
    return c;
  }
  const HB = halfbandCoefs(10, 0.03);
  class Halfband {
    constructor() { this.x = new Float64Array(HB.length); this.y = new Float64Array(HB.length); this.o0 = 0; this.o1 = 0; }
    chain(a, b) {
      const c = HB, x = this.x, y = this.y;
      for (let i = 0; i < c.length; i += 2) {
        let t = (a - y[i]) * c[i] + x[i]; x[i] = a; y[i] = t; a = t;
        t = (b - y[i + 1]) * c[i + 1] + x[i + 1]; x[i + 1] = b; y[i + 1] = t; b = t;
      }
      this.o0 = a; this.o1 = b;
    }
    up(v) { this.chain(v, v); }                       // → o0, o1 (two samples at 2x)
    down(s0, s1) { this.chain(s1, s0); return 0.5 * (this.o0 + this.o1); }
  }

  // ---------------------------------------------------------------- FFT (radix-2, in place) + real FFT
  const fftCache = new Map();
  function fftPlan(n) {
    let p = fftCache.get(n); if (p) return p;
    const rev = new Uint32Array(n), bits = Math.log2(n) | 0;
    for (let i = 0; i < n; i++) { let r = 0; for (let b = 0; b < bits; b++) r |= ((i >> b) & 1) << (bits - 1 - b); rev[i] = r; }
    const cs = new Float64Array(n / 2), sn = new Float64Array(n / 2);
    for (let i = 0; i < n / 2; i++) { cs[i] = Math.cos(2 * PI * i / n); sn[i] = -Math.sin(2 * PI * i / n); }
    p = { n, rev, cs, sn }; fftCache.set(n, p); return p;
  }
  function fft(re, im, n, inv) {
    const P = fftPlan(n), rev = P.rev;
    for (let i = 0; i < n; i++) { const j = rev[i]; if (j > i) { let t = re[i]; re[i] = re[j]; re[j] = t; t = im[i]; im[i] = im[j]; im[j] = t; } }
    for (let size = 2; size <= n; size <<= 1) {
      const half = size >> 1, step = n / size;
      for (let s = 0; s < n; s += size) {
        for (let k = 0; k < half; k++) {
          const wr = P.cs[k * step], wi = inv ? -P.sn[k * step] : P.sn[k * step];
          const a = s + k, b = a + half;
          const tr = re[b] * wr - im[b] * wi, ti = re[b] * wi + im[b] * wr;
          re[b] = re[a] - tr; im[b] = im[a] - ti; re[a] += tr; im[a] += ti;
        }
      }
    }
  }
  // real FFT of x (length n) → X[0..n/2] (re, im), via one complex FFT of n/2 points
  function rfft(x, n, outRe, outIm, wr, wi) {
    const h = n / 2;
    for (let i = 0; i < h; i++) { wr[i] = x[2 * i]; wi[i] = x[2 * i + 1]; }
    fft(wr, wi, h, false);
    const P = fftPlan(n);
    for (let k = 0; k <= h; k++) {
      const ar = wr[k % h], ai = wi[k % h], br = wr[(h - k) % h], bi = -wi[(h - k) % h];
      const er = 0.5 * (ar + br), ei = 0.5 * (ai + bi), or = 0.5 * (ai - bi), oi = -0.5 * (ar - br);
      const c = k < h ? P.cs[k] : -1, s = k < h ? P.sn[k] : 0;          // e^{-2πik/n}
      outRe[k] = er + (or * c - oi * s); outIm[k] = ei + (or * s + oi * c);
    }
  }
  // inverse of rfft: X[0..n/2] → x (length n), scaled by 1/n
  function irfft(inRe, inIm, n, x, wr, wi) {
    const h = n / 2, P = fftPlan(n);
    for (let k = 0; k < h; k++) {
      const ar = inRe[k], ai = inIm[k], br = inRe[h - k], bi = -inIm[h - k];
      const er = 0.5 * (ar + br), ei = 0.5 * (ai + bi);
      const dr = 0.5 * (ar - br), di = 0.5 * (ai - bi);
      const c = P.cs[k], s = -P.sn[k];                                      // e^{+2πik/n}
      const or = dr * c - di * s, oi = dr * s + di * c;
      wr[k] = er - oi; wi[k] = ei + or;
    }
    fft(wr, wi, h, true);
    for (let i = 0; i < h; i++) { x[2 * i] = wr[i] / h; x[2 * i + 1] = wi[i] / h; }
  }

  // ---------------------------------------------------------------- zero-latency partitioned convolution (one channel)
  // h[0..127]: direct FIR; h[128..2047]: 128-sample partitions (FFT 256); h[2048..]: 2048-sample partitions (FFT 4096),
  // tail partitions accumulated a little every 128 samples so no single block carries the whole cost.
  const HEAD = 128, B1 = 128, B2 = 2048, SUB = B2 / B1;
  class Stage {
    constructor(h, from, to, B, full) {
      this.full = !!full; this.B = B; this.N = 2 * B; this.bins = B + 1;
      this.P = Math.max(0, Math.ceil((Math.min(to, h.length) - from) / B));
      this.H = [];
      const t = new Float64Array(this.N), wr = new Float64Array(B), wi = new Float64Array(B);
      for (let p = 0; p < this.P; p++) {
        t.fill(0); for (let k = 0; k < B; k++) { const i = from + p * B + k; t[k] = i < to && i < h.length ? h[i] : 0; }
        const re = new Float64Array(this.bins), im = new Float64Array(this.bins); rfft(t, this.N, re, im, wr, wi); this.H.push([re, im]);
      }
      this.fdl = []; for (let p = 0; p < this.P; p++) this.fdl.push([new Float64Array(this.bins), new Float64Array(this.bins)]);
      this.head = 0;                                        // fdl[head] = newest spectrum
      this.inBuf = new Float64Array(this.N); this.out = new Float64Array(B);
      this.accR = new Float64Array(this.bins); this.accI = new Float64Array(this.bins);
      this.t = t; this.wr = wr; this.wi = wi; this.done = 0;
    }
    // accumulate partitions [p0, p1) that use spectra already in the FDL (newest = index 0 → partition 2)
    accumulate(p0, p1) {
      const bins = this.bins, aR = this.accR, aI = this.accI;
      for (let p = p0; p < p1 && p < this.P; p++) {
        if (p < 1) continue;
        const X = this.fdl[(this.head + p - 1) % this.P], H = this.H[p];
        const xr = X[0], xi = X[1], hr = H[0], hi = H[1];
        for (let k = 0; k < bins; k++) { aR[k] += xr[k] * hr[k] - xi[k] * hi[k]; aI[k] += xr[k] * hi[k] + xi[k] * hr[k]; }
      }
    }
    // a block of B new samples has been written to inBuf[B..2B): produce the next B output samples
    finish() {
      const B = this.B, N = this.N, bins = this.bins;
      if (this.P === 0) { this.out.fill(0); return; }
      // new input spectrum goes to the FDL front
      this.head = (this.head + this.P - 1) % this.P;
      const X = this.fdl[this.head];
      rfft(this.inBuf, N, X[0], X[1], this.wr, this.wi);
      // partition 0 of this stage pairs with the newest spectrum
      const aR = this.accR, aI = this.accI, hr = this.H[0][0], hi = this.H[0][1], xr = X[0], xi = X[1];
      for (let k = 0; k < bins; k++) { aR[k] += xr[k] * hr[k] - xi[k] * hi[k]; aI[k] += xr[k] * hi[k] + xi[k] * hr[k]; }
      if (this.full) {                                      // short stage: all older partitions now (head moved, so offset p)
        for (let p = 1; p < this.P; p++) {
          const Y = this.fdl[(this.head + p) % this.P], H = this.H[p], yr = Y[0], yi = Y[1], gr = H[0], gi = H[1];
          for (let k = 0; k < bins; k++) { aR[k] += yr[k] * gr[k] - yi[k] * gi[k]; aI[k] += yr[k] * gi[k] + yi[k] * gr[k]; }
        }
      }
      irfft(aR, aI, N, this.t, this.wr, this.wi);
      for (let i = 0; i < B; i++) this.out[i] = this.t[B + i];
      aR.fill(0); aI.fill(0);
      this.inBuf.copyWithin(0, B, N);
    }
  }
  class Convolver {
    constructor(h) {
      const n = h.length;
      this.head = new Float64Array(HEAD); for (let i = 0; i < HEAD && i < n; i++) this.head[i] = h[i];
      this.hist = new Float64Array(HEAD * 2); this.hp = 0;
      // stage 1 covers h[128 .. 2048): its partition 0 starts at 128 (one block later than the input block → zero latency)
      this.s1 = new Stage(h, HEAD, B2, B1, true);
      this.s2 = new Stage(h, B2, n, B2, false);
      this.pos1 = 0; this.pos2 = 0; this.sub = 0;
      this.len = n;
    }
    process(x) {
      // direct head (time domain), history kept twice so the inner loop never wraps
      const hp = this.hp; this.hist[hp] = x; this.hist[hp + HEAD] = x;
      let y = 0; const h = this.head, hs = this.hist;
      for (let k = 0; k < HEAD; k++) y += h[k] * hs[hp + HEAD - k];
      this.hp = (hp + 1) % HEAD;
      // stage outputs for this sample (computed one block earlier)
      y += this.s1.out[this.pos1] + this.s2.out[this.pos2];
      // feed input into both stages
      this.s1.inBuf[B1 + this.pos1] = x; this.s2.inBuf[B2 + this.pos2] = x;
      if (++this.pos1 === B1) {
        this.pos1 = 0; this.s1.finish();
        // spread the tail's older partitions over the 16 sub-blocks of each 2048 block
        const P = this.s2.P, per = Math.ceil(Math.max(0, P - 1) / SUB);
        this.s2.accumulate(1 + this.sub * per, 1 + (this.sub + 1) * per);
        this.sub++;
      }
      if (++this.pos2 === B2) { this.pos2 = 0; this.sub = 0; this.s2.finish(); }
      return y;
    }
  }

  // ---------------------------------------------------------------- built-in impulse responses (synthesised, deterministic)
  const IRSPEC = [
    { len: 3.4, rt: 2.3, lo: 1.25, hi: 0.5, ramp: 60, er: 10, erSpan: 70, erGain: 0.35, pre: 18, w: 0.9 },     // Concert Hall
    { len: 1.2, rt: 0.55, lo: 1.1, hi: 0.6, ramp: 8, er: 14, erSpan: 28, erGain: 0.55, pre: 4, w: 0.7 },      // Wood Room
    { len: 2.6, rt: 1.9, lo: 0.9, hi: 0.85, ramp: 3, er: 0, erSpan: 0, erGain: 0, pre: 1, w: 1.0 },           // Bright Plate
    { len: 2.2, rt: 1.4, lo: 1.15, hi: 0.45, ramp: 20, er: 12, erSpan: 45, erGain: 0.45, pre: 8, w: 0.8 },     // Stone Chamber
    { len: 0.5, rt: 0.22, lo: 1.0, hi: 0.7, ramp: 2, er: 10, erSpan: 12, erGain: 0.6, pre: 1, w: 0.5 },       // Vocal Booth
    { len: 7.0, rt: 5.2, lo: 1.35, hi: 0.4, ramp: 120, er: 8, erSpan: 140, erGain: 0.3, pre: 35, w: 1.0 },     // Large Church
    { len: 3.0, rt: 2.0, lo: 0.8, hi: 0.6, ramp: 1, er: 0, erSpan: 0, erGain: 0, pre: 1, w: 0.6, spring: true },// Spring Tank
  ];
  function synthIR(index, fs) {
    const S = IRSPEC[clamp(index | 0, 0, IRSPEC.length - 1)], n = Math.round(S.len * fs);
    const out = [new Float32Array(n), new Float32Array(n)];
    if (S.spring) return springIR(S, fs, n, out);
    const bands = [[0, 250, S.lo], [250, 2000, 1], [2000, 6000, (1 + S.hi) / 2], [6000, 0, S.hi]];
    for (let ch = 0; ch < 2; ch++) {
      const rng = new Rng(9001 + 77 * index + 13 * ch), acc = new Float64Array(n);
      for (const [f0, f1, mult] of bands) {
        const secs = [];
        if (f0 > 0) { secs.push(bqHP(fs, f0, 0.707)); secs.push(bqHP(fs, f0, 0.707)); }
        if (f1 > 0) { secs.push(bqLP(fs, Math.min(f1, fs * 0.45), 0.707)); secs.push(bqLP(fs, Math.min(f1, fs * 0.45), 0.707)); }
        const bq = secs.map((c) => new Biquad(c)), rt = S.rt * mult, k = -6.9078 / (rt * fs);
        for (let i = 0; i < n; i++) {
          const t = i / fs, env = Math.exp(k * i) * (1 - Math.exp(-t * 1000 / S.ramp));
          acc[i] += runSecs(bq, rng.next()) * env;
        }
      }
      // early reflections
      const r2 = new Rng(4242 + 31 * index + ch);
      for (let e = 0; e < S.er; e++) {
        const t = S.pre + S.erSpan * Math.pow((e + 0.5 + 0.3 * r2.next()) / S.er, 1.4), i = Math.round(t * fs / 1000);
        if (i < n) acc[i] += S.erGain * (0.6 + 0.4 * r2.next()) * (r2.next() > 0 ? 1 : -1) * Math.pow(1 - e / (S.er + 2), 1.5);
      }
      // pre-delay: shift
      const sh = Math.round(S.pre * fs / 1000);
      for (let i = n - 1; i >= 0; i--) out[ch][i] = i >= sh ? acc[i - sh] : 0;
      // gentle fade-out over the last 10 %
      const f0 = Math.floor(n * 0.9); for (let i = f0; i < n; i++) out[ch][i] *= 0.5 + 0.5 * Math.cos(PI * (i - f0) / (n - f0));
    }
    // width: mix the channels toward mono for narrower spaces
    const w = S.w;
    for (let i = 0; i < n; i++) { const m = 0.5 * (out[0][i] + out[1][i]), s = 0.5 * (out[0][i] - out[1][i]) * w; out[0][i] = m + s; out[1][i] = m - s; }
    normalizeIR(out);
    return out;
  }
  function springIR(S, fs, n, out) {
    // two springs (L, R): decaying impulse trains through a dispersive all-pass chain → the "boing" chirp
    const periods = [0.0283, 0.0361];
    for (let ch = 0; ch < 2; ch++) {
      const x = new Float64Array(n), T = periods[ch], k = -6.9078 / (S.rt * fs);
      for (let r = 0; r * T * fs < n; r++) { const i = Math.round(r * T * fs) + 20; if (i < n) x[i] += (r % 2 ? -1 : 1) * Math.exp(k * i); }
      const ap = new Float64Array(2 * 80), a = -0.62;
      for (let i = 0; i < n; i++) {
        let v = x[i];
        for (let s = 0; s < 80; s++) { const y = a * v + ap[2 * s]; ap[2 * s] = v - a * y; v = y; }
        x[i] = v;
      }
      const bq = [new Biquad(bqHP(fs, 120, 0.707)), new Biquad(bqLP(fs, Math.min(4500, fs * 0.45), 0.707)), new Biquad(bqLP(fs, Math.min(4500, fs * 0.45), 0.707)), new Biquad(bqPeak(fs, 1800, 1.2, 4))];
      const rng = new Rng(777 + ch);
      for (let i = 0; i < n; i++) out[ch][i] = runSecs(bq, x[i] + 0.02 * rng.next() * Math.exp(k * i));
      const f0 = Math.floor(n * 0.9); for (let i = f0; i < n; i++) out[ch][i] *= 0.5 + 0.5 * Math.cos(PI * (i - f0) / (n - f0));
    }
    normalizeIR(out);
    return out;
  }
  // scale so the IR's energy (per channel average) is fixed → similar loudness for every IR
  function normalizeIR(ir) {
    let e = 0; for (const c of ir) for (let i = 0; i < c.length; i++) e += c[i] * c[i];
    e /= ir.length; const g = e > 0 ? 0.6 / Math.sqrt(e) : 1;
    for (const c of ir) for (let i = 0; i < c.length; i++) c[i] *= g;
    return ir;
  }
  // final IR for a reverb module: trim (irLen %), reverse (revLen ms with fade), normalise. src = [L, R] Float32Arrays
  function prepareIR(src, p, fs) {
    let L = src[0], R = src[1] || src[0];
    let n = Math.max(64, Math.round(L.length * clamp(num(p.irLen, 100), 5, 100) / 100));
    if (p.reverse) n = Math.min(L.length, Math.max(64, Math.round(clamp(num(p.revLen, 1500), 100, 6000) * fs / 1000)));
    const out = [new Float32Array(n), new Float32Array(n)], fade = Math.min(n >> 2, Math.round(0.05 * fs));
    for (let ch = 0; ch < 2; ch++) {
      const s = ch ? R : L;
      for (let i = 0; i < n; i++) { let v = i < s.length ? s[i] : 0; if (i >= n - fade) v *= 0.5 + 0.5 * Math.cos(PI * (i - (n - fade)) / fade); out[ch][i] = v; }
      if (p.reverse) out[ch].reverse();
    }
    return normalizeIR(out);
  }
  // render the algorithmic reverb's impulse response (used for reverse reverb in ALGO mode)
  function renderAlgoIR(p, fs, lenMs) {
    const m = new Reverb(fs, true); m.setParams(Object.assign({}, p, { source: 0, reverse: 0, predelay: 0, mix: 100, width: 100, lowcut: 0, highcut: 0, output: 0, modulation: 0, eqLo: 0, eqMid: 0, eqHi: 0 }));
    const n = Math.round(lenMs * fs / 1000), L = new Float32Array(n), R = new Float32Array(n);
    L[0] = R[0] = 1;
    for (let i = 0; i < n; i += 512) { const k = Math.min(512, n - i); m.process(L.subarray(i, i + k), R.subarray(i, i + k), k); }
    return [L, R];
  }

  // ================================================================ TAPE
  function tapeTau(eq, speed) {            // record/playback EQ time constant (µs)
    if (speed >= 30) return 17.5;
    if (speed >= 15) return eq ? 35 : 50;
    if (speed >= 7.5) return eq ? 70 : 50;
    return eq ? 70 : 90;
  }
  function tapeSpeed(p) { return p.variant === 2 ? 1.875 : clamp(num(p.speed, 15), 1.875, 30); }
  // playback (small-signal) response: head bump, LF roll-off, gap loss, bias, heads, age
  function designTapeTone(p, fs) {
    const s = [], sp = tapeSpeed(p), C = CIRCUIT.tape, heads = C.c2.list[clamp(p.c2 | 0, 0, 2)];
    if (p.variant === 2) {                       // cassette
      s.push(bqHP(fs, 22, 0.6)); s.push(bqPeak(fs, 45, 1.1, 1.0 + heads.bump));
      s.push(bqHighShelf(fs, Math.min(9000, fs * 0.45), 0.6, -1.8 + heads.hf)); s.push(bqLP(fs, Math.min(14500, fs * 0.45), 0.6));
    } else {
      const fb = 3.5 * sp * (p.variant === 1 ? 0.9 : 1), bump = [2.4, 2.1, 1.6, 1.0][sp >= 30 ? 3 : sp >= 15 ? 2 : sp >= 7.5 ? 1 : 0] + (p.variant === 1 ? 0.5 : 0) + heads.bump;
      s.push(bqHP(fs, fb * 0.4, 0.6)); s.push(bqPeak(fs, fb, 1.2, bump)); s.push(bqPeak(fs, fb * 1.9, 1.5, -0.6 * bump));
      const fc = Math.min(4200 * Math.pow(sp, 0.55) * (p.variant === 1 ? 0.93 : 1), fs * 0.45);
      s.push(bqLP(fs, fc, 0.62));
      if (heads.hf) s.push(bqHighShelf(fs, Math.min(8000, fs * 0.45), 0.6, heads.hf));
    }
    const bias = num(p.bias, 0) / 100;       // over-bias: duller; under-bias: brighter
    if (bias) s.push(bqHighShelf(fs, Math.min(9000, fs * 0.45), 0.6, bias > 0 ? -3 * bias : -1.6 * bias));
    const age = num(p.age, 0) / 100; if (age > 0) s.push(bqHighShelf(fs, Math.min(7000, fs * 0.45), 0.6, -2 * age));
    return s;
  }
  class Tape {
    constructor(fs) {
      this.fs = fs; this.type = 'tape'; this.p = Object.assign({}, DEFAULTS.tape);
      this.os = [[new Halfband(), new Halfband()], [new Halfband(), new Halfband()]];
      this.pre = [new Biquad(), new Biquad()]; this.de = [new Biquad(), new Biquad()];
      this.tone = [[], []]; this.dc = [new DCBlock(fs), new DCBlock(fs)];
      this.hiss = [new Hiss(fs, 1234), new Hiss(fs, 5678)];
      this.N = pow2(Math.ceil(fs * 0.012) + 8); this.mask = this.N - 1;
      this.buf = [new Float64Array(this.N), new Float64Array(this.N)]; this.w = 0;
      this.ph = [0, 0, 0]; this.drift = 0; this.driftT = 0; this.driftTarget = 0; this.driftK = 1 / (fs * 0.4); this.rng = new Rng(99);
      this.inMeter = 0; this.outMeter = 0; this.satMeter = 0;
      this.update();
    }
    setParams(o) { Object.assign(this.p, o); this.update(); }
    update() {
      const p = this.p, fs = this.fs, fs2 = 2 * fs, C = CIRCUIT.tape;
      const el = C.c1.list[clamp(p.c1 | 0, 0, 2)], align = C.c3.list[clamp(p.c3 | 0, 0, 2)].lvl, F = FORMULA[clamp(p.formula | 0, 0, 2)];
      const age = num(p.age, 0) / 100, V = Math.pow(num(p.headroom, 24) / 24, 1.5);
      this.analog = !!p.analog;
      this.inG = dbToLin(num(p.drive, 0) + align);
      this.outG = dbToLin(num(p.output, 0) - align);
      // record pre-emphasis / playback de-emphasis (exact inverses) around the tape non-linearity
      const sp = tapeSpeed(p), ft = 1e6 / (2 * PI * (p.variant === 2 ? 70 : tapeTau(p.eqStd | 0, sp)));
      const pre = fo_ZP(fs2, ft, Math.min(ft * 4, fs2 * 0.45)), k = (1 + pre.a1) / (pre.b0 + pre.b1);   // unity at DC
      const preK = { b0: pre.b0 * k, b1: pre.b1 * k, b2: 0, a1: pre.a1, a2: 0 };
      const de = { b0: 1 / preK.b0, b1: preK.a1 / preK.b0, b2: 0, a1: preK.b1 / preK.b0, a2: 0 };   // 1 / pre(z)
      for (let c = 0; c < 2; c++) { this.pre[c].set(preK); this.de[c].set(de); }
      // tape saturation (MOL above 0 VU), bias changes the drive and the asymmetry
      const bias = num(p.bias, 0) / 100;
      this.satCeil = dbToLin(REF + F.mol - (p.variant === 2 ? 3 : 0)) * (1 + 0.25 * bias) * (1 - 0.2 * age);
      this.satB = F.b * (1 - 0.5 * bias) + 0.03 * age;
      this.ampCeil = 1.4 * el.ceil * V * (1 - 0.2 * age); this.ampB = el.b;
      setSecs(this.tone, designTapeTone(p, fs));
      // wow & flutter (speed deviation as a modulated delay)
      const sr = p.variant === 2 ? 1.6 : 1;
      this.wowD = this.analog ? num(p.wow, 0) / 100 * 0.004 * sr * fs / (2 * PI * 0.55) : 0;
      this.flD = this.analog ? num(p.flutter, 0) / 100 * 0.0015 * sr * fs / (2 * PI * 7.3) : 0;
      this.base = this.wowD + this.flD > 0 ? 1.25 * (this.wowD * 1.4 + this.flD * 1.6) + 3 : 0;
      this.dph = [2 * PI * 0.55 / fs, 2 * PI * 7.3 / fs, 2 * PI * 11.7 / fs];
      for (let c = 0; c < 2; c++) this.hiss[c].set(F.hiss + (p.variant === 2 ? 10 : 0) - 6 * Math.log2(Math.max(sp, 1.875) / 15) * 0.5 + (num(p.hiss, 0) - 50) * 0.3 + 6 * age, this.analog && num(p.hiss, 0) > 0);
      const m = clamp(num(p.mix, 100), 0, 100) / 100; this.wetG = m; this.dryG = 1 - m;
    }
    // record → tape → playback at 2x for one channel sample
    tapeSample(c, x) {
      const os = this.os[c]; os[0].up(x * this.inG);
      let a = os[0].o0, b = os[0].o1;
      if (this.analog) {
        a = sat(a, this.ampCeil, this.ampB); b = sat(b, this.ampCeil, this.ampB);
        a = this.pre[c].process(a); a = sat(a, this.satCeil, this.satB); a = this.de[c].process(a);
        b = this.pre[c].process(b); b = sat(b, this.satCeil, this.satB); b = this.de[c].process(b);
      }
      return os[1].down(a, b);
    }
    process(L, R, n) {
      let pi = 0, po = 0; const mask = this.mask;
      for (let i = 0; i < n; i++) {
        const dl = L[i], dr = R[i];
        pi = Math.max(pi, Math.abs(dl), Math.abs(dr));
        let yl = runSecs(this.tone[0], this.tapeSample(0, dl)), yr = runSecs(this.tone[1], this.tapeSample(1, dr));
        if (this.base > 0) {
          // wow: slow sine + random drift; flutter: two faster components
          this.ph[0] += this.dph[0]; this.ph[1] += this.dph[1]; this.ph[2] += this.dph[2];
          for (let k = 0; k < 3; k++) if (this.ph[k] > 2 * PI) this.ph[k] -= 2 * PI;
          if (--this.driftT <= 0) { this.driftT = Math.round(this.fs * 0.25); this.driftTarget = this.rng.next(); }
          this.drift += (this.driftTarget - this.drift) * this.driftK;
          const d = this.base + this.wowD * (Math.sin(this.ph[0]) + 0.4 * this.drift) + this.flD * (Math.sin(this.ph[1]) + 0.6 * Math.sin(this.ph[2]));
          this.buf[0][this.w] = yl; this.buf[1][this.w] = yr;
          yl = hermite(this.buf[0], mask, this.w, d); yr = hermite(this.buf[1], mask, this.w, d);
          this.w = (this.w + 1) & mask;
        }
        yl = this.dc[0].process(yl + this.hiss[0].next()) * this.outG; yr = this.dc[1].process(yr + this.hiss[1].next()) * this.outG;
        yl = dl * this.dryG + yl * this.wetG; yr = dr * this.dryG + yr * this.wetG;
        L[i] = yl; R[i] = yr; po = Math.max(po, Math.abs(yl), Math.abs(yr));
      }
      this.inMeter = pi; this.outMeter = po;
    }
    latency() { return Math.round(this.base); }
  }

  // ================================================================ DELAY
  function delayMs(p, bpm) {
    const mode = clamp(p.variant | 0, 0, 3), max = SPEC.delay.maxTime[mode];
    const t = (p.sync | 0) > 0 ? SYNC.beats[clamp((p.sync | 0) - 1, 0, SYNC.beats.length - 1)] * 60000 / clamp(bpm || 120, 20, 300) : num(p.time, 375);
    return clamp(t, 1, p.reverse ? Math.min(max, REV_MAX) : max);
  }
  function bbdCutoff(ms) { return Math.min(9000, 0.45 * 4096 / (2 * ms / 1000)); }
  // loop filter shown on the frequency map (what each repeat goes through)
  function designDelayLoop(p, fs, bpm) {
    const s = [], mode = p.variant | 0;
    if (num(p.lowcut, 0) >= 20) s.push(bqHP(fs, Math.min(p.lowcut, fs * 0.45), 0.707));
    if (num(p.highcut, 0) >= 200 && p.highcut < 20000) s.push(bqLP(fs, Math.min(p.highcut, fs * 0.45), 0.707));
    if (mode === 1 && p.analog) { s.push(bqPeak(fs, 90, 1.0, 1.5)); s.push(bqHighShelf(fs, Math.min(6000, fs * 0.45), 0.6, -2.5)); }
    if (mode === 2) { const fc = Math.min(bbdCutoff(delayMs(p, bpm)), fs * 0.45); s.push(bqLP(fs, fc, 0.6)); s.push(bqLP(fs, fc, 0.9)); }
    if (mode === 3) { const fc = Math.min(9000, fs * 0.45); s.push(bqLP(fs, fc, 0.54)); s.push(bqLP(fs, fc, 1.31)); }
    return s;
  }
  // 3-band wet EQ (delay: after the loop; reverb: with its low/high cut)
  function design3Band(p, fs) {
    const s = [];
    if (num(p.eqLo, 0)) s.push(bqLowShelf(fs, 180, 0.7, num(p.eqLo, 0)));
    if (num(p.eqMid, 0)) s.push(bqPeak(fs, Math.min(clamp(num(p.eqMidF, 1500), 200, 8000), fs * 0.45), 0.9, num(p.eqMid, 0)));
    if (num(p.eqHi, 0)) s.push(bqHighShelf(fs, Math.min(6000, fs * 0.45), 0.7, num(p.eqHi, 0)));
    return s;
  }
  const REV_MAX = 2000;      // reverse delay: each chunk needs 2× its length in the buffer
  class Delay {
    constructor(fs) {
      this.fs = fs; this.type = 'delay'; this.p = Object.assign({}, DEFAULTS.delay); this.bpm = 120;
      this.N = pow2(Math.ceil(fs * 4.6) + 16); this.mask = this.N - 1;
      this.buf = [new Float64Array(this.N), new Float64Array(this.N)]; this.w = 0;
      this.loop = [[], []]; this.dc = [new DCBlock(fs), new DCBlock(fs)];
      this.weq = [[], []]; this.rloop = [[], []]; this.rn = 0;
      this.hiss = [new Hiss(fs, 4321), new Hiss(fs, 8765)];
      this.cur = [0, 0]; this.nxt = [0, 0]; this.xf = 1; this.smooth = [0, 0]; this.first = true;
      this.fb = [0, 0]; this.ph = 0; this.wph = 0; this.env = 0; this.cnt = 0;
      this.inMeter = 0; this.outMeter = 0; this.wetMeter = 0;
      this.update();
    }
    setBpm(b) { if (b !== this.bpm) { this.bpm = b; this.update(); } }
    setParams(o) { Object.assign(this.p, o); this.update(); }
    snap() { const t = this.targets(); this.cur = t.slice(); this.nxt = t.slice(); this.smooth = t.slice(); this.tgt = t.slice(); this.xf = 1; }
    targets() {
      const p = this.p, ms = delayMs(p, this.bpm), d = ms * this.fs / 1000, st = p.stereo | 0;
      return [d, st === 3 ? Math.max(1, d * clamp(num(p.ratioLR, 75), 10, 200) / 100) : d];
    }
    update() {
      const p = this.p, fs = this.fs;
      this.mode = clamp(p.variant | 0, 0, 3); this.analog = !!p.analog;
      this.g = clamp(num(p.feedback, 35), 0, 110) / 100;
      this.st = clamp(p.stereo | 0, 0, 3);
      const t = this.targets();
      if (this.first) { this.cur = t.slice(); this.nxt = t.slice(); this.smooth = t.slice(); this.first = false; }
      if (this.mode === 0) {          // digital: a time change cross-fades between two read heads (no pitch glide)
        if (Math.abs(t[0] - this.nxt[0]) > 0.5 || Math.abs(t[1] - this.nxt[1]) > 0.5) {
          if (this.xf < 1) { this.cur = this.nxt.slice(); }
          this.nxt = t; this.xf = 0;
        }
      }
      this.tgt = t;
      this.xfStep = 1 / (0.05 * fs);
      this.glide = 1 - Math.exp(-1 / (0.18 * fs));
      setSecs(this.loop, designDelayLoop(p, fs, this.bpm));
      setSecs(this.rloop, designDelayLoop(p, fs, this.bpm));
      setSecs(this.weq, design3Band(p, fs));
      this.rev = !!p.reverse;
      this.lastBbd = delayMs(p, this.bpm);
      this.drive = dbToLin(num(p.drive, 0));
      this.satCeil = [1.0, 0.7, 0.55, 1.0][this.mode];
      this.heads = HEADMASK[clamp(p.heads | 0, 0, 6)];
      const nh = (this.heads & 1) + ((this.heads >> 1) & 1) + ((this.heads >> 2) & 1); this.headNorm = 1 / Math.sqrt(nh);
      this.modD = num(p.modDepth, 0) / 100 * 0.003 * fs; this.dph = 2 * PI * clamp(num(p.modRate, 0.5), 0.05, 10) / fs;
      this.wowD = this.analog && this.mode === 1 ? (0.0006 + 0.002 * num(p.age, 0) / 100) * fs / (2 * PI * 0.7) : this.analog && this.mode === 2 ? 0.0002 * fs / (2 * PI * 0.4) : 0;
      this.wdph = 2 * PI * (this.mode === 1 ? 0.7 : 0.4) / fs;
      const hissDb = this.mode === 1 ? -78 : this.mode === 2 ? -84 : -200;
      for (let c = 0; c < 2; c++) this.hiss[c].set(hissDb + (num(p.hiss, 0) - 50) * 0.3, this.analog && num(p.hiss, 0) > 0 && hissDb > -150);
      this.duck = clamp(num(p.duck, 0), 0, 100) / 100; this.envA = 1 - Math.exp(-1 / (0.005 * fs)); this.envR = 1 - Math.exp(-1 / (0.25 * fs));
      const m = clamp(num(p.mix, 30), 0, 100) / 100; this.wetG = Math.min(1, 2 * m); this.dryG = Math.min(1, 2 * (1 - m));
      this.width = clamp(num(p.width, 100), 0, 200) / 100;
      this.outG = dbToLin(num(p.output, 0));
    }
    // read one channel at delay d (multi-head for tape echo)
    tap(c, d) {
      const buf = this.buf[c], mask = this.mask, w = this.w;
      if (this.mode !== 1 || this.heads === 1) return hermite(buf, mask, w, d);
      let y = 0; const r = SPEC.delay.headRatio;
      for (let h = 0; h < 3; h++) if (this.heads & (1 << h)) y += hermite(buf, mask, w, Math.min(d * r[h], this.N - 4));
      return y * this.headNorm;
    }
    // reverse read of channel c with chunk length T (called after this sample was written)
    revRead(c, T) {
      const buf = this.buf[c], mask = this.mask, w0 = (this.w - 1) & mask;   // w0 = newest sample
      const k1 = this.rn % T, k2 = (this.rn + (T >> 1)) % T;
      const g1 = Math.sin(PI * k1 / T), g2 = Math.sin(PI * k2 / T);
      return buf[(w0 - 2 * k1) & mask] * g1 * g1 + buf[(w0 - 2 * k2) & mask] * g2 * g2;
    }
    record(x) {
      const m = this.mode;
      x *= this.drive;
      if (m === 3) { x = Math.max(-1, Math.min(1, x)); return Math.round(x * 2048) / 2048; }               // 12-bit converter, hard clip
      if (!this.analog) return sat(x, 2.0, 0);                                                // clean: transparent safety limiter only
      if (m === 1) return sat(x, this.satCeil, 0.04);
      if (m === 2) return sat(x, this.satCeil, 0.08);
      return sat(x, 2.0, 0);                                                                                // digital: transparent safety limiter
    }
    process(L, R, n) {
      let pi = 0, po = 0, pw = 0;
      const fs = this.fs, mask = this.mask;
      for (let i = 0; i < n; i++) {
        const dl = L[i], dr = R[i];
        pi = Math.max(pi, Math.abs(dl), Math.abs(dr));
        // delay time (digital: cross-fade; analog: glide)
        let rl, rr;
        this.ph += this.dph; if (this.ph > 2 * PI) this.ph -= 2 * PI;
        this.wph += this.wdph; if (this.wph > 2 * PI) this.wph -= 2 * PI;
        const ml = this.modD * (1 + Math.sin(this.ph)) + this.wowD * (1 + Math.sin(this.wph)), mr = this.modD * (1 + Math.cos(this.ph)) + this.wowD * (1 + Math.sin(this.wph + 0.3));
        if (this.mode === 0) {
          if (this.xf < 1) {
            this.xf = Math.min(1, this.xf + this.xfStep);
            const a = 0.5 - 0.5 * Math.cos(PI * this.xf);
            rl = (1 - a) * this.tap(0, this.cur[0] + ml) + a * this.tap(0, this.nxt[0] + ml);
            rr = (1 - a) * this.tap(1, this.cur[1] + mr) + a * this.tap(1, this.nxt[1] + mr);
            if (this.xf >= 1) this.cur = this.nxt.slice();
          } else { rl = this.tap(0, this.cur[0] + ml); rr = this.tap(1, this.cur[1] + mr); }
        } else {
          this.smooth[0] += (this.tgt[0] - this.smooth[0]) * this.glide; this.smooth[1] += (this.tgt[1] - this.smooth[1]) * this.glide;
          rl = this.tap(0, this.smooth[0] + ml); rr = this.tap(1, this.smooth[1] + mr);
          if (this.mode === 2 && ++this.cnt >= 64) {           // BBD: the anti-alias filters follow the clock
            this.cnt = 0; const ms = this.smooth[0] * 1000 / fs;
            if (Math.abs(ms - this.lastBbd) > this.lastBbd * 0.01) { this.lastBbd = ms; const s = designDelayLoop(Object.assign({}, this.p, { sync: 0, time: ms }), fs, this.bpm); for (let c = 0; c < 2; c++) s.forEach((cf, k) => { this.loop[c][k].set(cf); this.rloop[c][k].set(cf); }); }
          }
        }
        // loop filter (every repeat, including the first)
        const zl = runSecs(this.loop[0], rl) + this.hiss[0].next(), zr = runSecs(this.loop[1], rr) + this.hiss[1].next();
        // write: input + feedback (ping-pong crosses the channels)
        let wl, wr;
        const g = this.g;
        if (this.st === 0) { const m = 0.5 * (dl + dr); wl = m + zl * g; wr = m + zr * g; }
        else if (this.st === 2) { const m = 0.5 * (dl + dr); wl = m + zr * g; wr = zl * g; }
        else { wl = dl + zl * g; wr = dr + zr * g; }
        this.buf[0][this.w] = this.dc[0].process(this.record(wl)); this.buf[1][this.w] = this.dc[1].process(this.record(wr));
        this.w = (this.w + 1) & mask;
        // reverse: two read heads sweep each chunk backwards (delay 1 → 2T), offset by T/2, sin² windows sum to 1
        let ol = zl, or = zr;
        if (this.rev) {
          const T = Math.max(64, Math.round(this.mode === 0 ? this.cur[0] : this.smooth[0])), TR = Math.max(64, Math.round(this.mode === 0 ? this.cur[1] : this.smooth[1]));
          ol = this.revRead(0, T); or = this.revRead(1, TR);
          ol = runSecs(this.rloop[0], ol); or = runSecs(this.rloop[1], or);
          this.rn++;
        }
        // ducking: the echoes step back while the input plays
        const a = Math.max(Math.abs(dl), Math.abs(dr));
        this.env += (a - this.env) * (a > this.env ? this.envA : this.envR);
        const dg = 1 - this.duck * Math.min(1, this.env / 0.25);
        let el = runSecs(this.weq[0], ol) * dg, er = runSecs(this.weq[1], or) * dg;
        const mid = 0.5 * (el + er), side = 0.5 * (el - er) * this.width; el = mid + side; er = mid - side;
        pw = Math.max(pw, Math.abs(el), Math.abs(er));
        const yl = (dl * this.dryG + el * this.wetG) * this.outG, yr = (dr * this.dryG + er * this.wetG) * this.outG;
        L[i] = yl; R[i] = yr; po = Math.max(po, Math.abs(yl), Math.abs(yr));
      }
      this.inMeter = pi; this.outMeter = po; this.wetMeter = pw;
    }
  }

  // ================================================================ REVERB
  const FDN_N = 16;
  const FDN_MS = [31.3, 37.1, 41.9, 47.3, 53.9, 59.1, 64.7, 70.3, 76.9, 83.1, 89.7, 95.3, 101.9, 109.1, 116.3, 123.7];
  const VARIANT = [   // size scale, ER on, diffusion scale, base decay character
    { scale: 1.0, er: 1.0, erSpan: 60, erPre: 8, ld: 1.0 },      // Hall
    { scale: 0.36, er: 1.4, erSpan: 22, erPre: 2, ld: 0.8 },     // Room
    { scale: 0.55, er: 0.0, erSpan: 0, erPre: 0, ld: 1.0 },      // Plate
    { scale: 0.62, er: 1.1, erSpan: 35, erPre: 4, ld: 0.9 },     // Chamber
    { scale: 1.65, er: 0.8, erSpan: 110, erPre: 15, ld: 1.0 },   // Cathedral
  ];
  const AP_MS = [4.77, 3.59, 12.73, 9.31];
  function isPrime(n) { if (n < 2) return false; for (let k = 2; k * k <= n; k++) if (n % k === 0) return false; return true; }
  const nextPrime = (n) => { n = Math.max(2, Math.round(n)); while (!isPrime(n)) n++; return n; };
  function fdnLengths(p, fs) {
    const V = VARIANT[clamp(p.variant | 0, 0, 4)], s = V.scale * clamp(num(p.size, 100), 20, 200) / 100;
    return FDN_MS.map((ms) => nextPrime(ms * s * fs / 1000));
  }
  // per-line loop gains: dB at low / mid / high
  function lineGains(len, p, fs) {
    const rt = clamp(num(p.decay, 2.2), 0.1, 30), lo = clamp(num(p.loMult, 1.2), 0.25, 4), hi = clamp(num(p.hiMult, 0.55), 0.05, 2);
    const g = (r) => -60 * len / (r * fs);
    return { mid: g(rt), low: g(rt * lo), high: g(rt * hi) };
  }
  const LS_F = 250, HS_F = 3500;
  // decay time vs frequency, measured on the designed loop filters (average of the lines)
  function rtCurve(p, fs, freqs) {
    const lens = fdnLengths(p, fs);
    return freqs.map((f) => {
      let acc = 0;
      for (const L of lens) {
        const G = lineGains(L, p, fs);
        const db = G.mid + bqMagDb(foLowShelf(fs, LS_F, dbToLin(G.low - G.mid)), fs, f) + bqMagDb(foHighShelf(fs, HS_F, dbToLin(G.high - G.mid)), fs, f);
        acc += -60 * L / (fs * Math.min(db, -1e-6));
      }
      return acc / lens.length;
    });
  }
  function designWetEQ(p, fs) {
    const s = [];
    if (num(p.lowcut, 0) >= 20) s.push(bqHP(fs, Math.min(p.lowcut, fs * 0.45), 0.707));
    if (num(p.highcut, 0) >= 200 && p.highcut < 20000) s.push(bqLP(fs, Math.min(p.highcut, fs * 0.45), 0.707));
    return s.concat(design3Band(p, fs));
  }
  class Allpass {
    constructor(n) { this.b = new Float64Array(n); this.n = n; this.i = 0; this.g = 0.5; }
    process(x) { const d = this.b[this.i], y = -this.g * x + d; this.b[this.i] = x + this.g * y; this.i = (this.i + 1) % this.n; return y; }
  }
  class Reverb {
    constructor(fs, offline) {
      this.fs = fs; this.type = 'reverb'; this.p = Object.assign({}, DEFAULTS.reverb); this.offline = !!offline;
      const maxLine = pow2(Math.ceil(123.7 * 1.65 * 2 * fs / 1000) + 64);
      this.lines = []; for (let k = 0; k < FDN_N; k++) this.lines.push(new Float64Array(maxLine));
      this.lmask = maxLine - 1; this.lw = 0;
      this.lp = []; this.hp = []; for (let k = 0; k < FDN_N; k++) { this.lp.push(new Biquad()); this.hp.push(new Biquad()); }
      this.v = new Float64Array(FDN_N); this.lphase = new Float64Array(FDN_N); this.ldph = new Float64Array(FDN_N);
      for (let k = 0; k < FDN_N; k++) { this.lphase[k] = k * 0.71; this.ldph[k] = 2 * PI * (0.13 + 0.061 * k) / fs; }
      this.PN = pow2(Math.ceil(0.5 * fs) + 8); this.pmask = this.PN - 1; this.pre = [new Float64Array(this.PN), new Float64Array(this.PN)]; this.pw = 0;
      this.ap = [[], []]; this.erN = pow2(Math.ceil(0.2 * fs * 1.7) + 8); this.erMask = this.erN - 1; this.erBuf = new Float64Array(this.erN); this.erW = 0;
      this.erLP = [new Biquad(), new Biquad()];
      this.eq = [[], []]; this.dc = [new DCBlock(fs), new DCBlock(fs)];
      this.conv = null; this.convOld = null; this.xf = 1; this.irKey = '';
      this.inMeter = 0; this.outMeter = 0; this.wetMeter = 0;
      this.update();
    }
    setParams(o) { Object.assign(this.p, o); this.update(); }
    usesIR() { const p = this.p; return (p.source | 0) > 0 || !!p.reverse; }
    // key of everything the IR depends on (the host builds the IR off the audio thread and calls setIR)
    irKey() {
      const p = this.p;
      if (!this.usesIR()) return '';
      const base = (p.source | 0) === 0 ? ['algo', p.variant, p.size, p.decay, p.hiMult, p.loMult, p.diffusion, p.er] : (p.source | 0) === 1 ? ['ir', p.irIndex] : ['user'];
      return base.concat([p.irLen, p.reverse, p.reverse ? p.revLen : 0]).join('/');
    }
    setIR(L, R) {
      const c = [new Convolver(L), new Convolver(R || L)];
      if (this.conv) { this.convOld = this.conv; this.xf = 0; } else this.xf = 1;
      this.conv = c;
    }
    update() {
      const p = this.p, fs = this.fs, V = VARIANT[clamp(p.variant | 0, 0, 4)];
      this.preD = clamp(num(p.predelay, 20), 0, 490) * fs / 1000;
      this.lens = fdnLengths(p, fs);
      for (let k = 0; k < FDN_N; k++) {
        const G = lineGains(this.lens[k], p, fs);
        this.v[k] = dbToLin(G.mid);
        this.lp[k].set(foLowShelf(fs, LS_F, dbToLin(G.low - G.mid)));
        this.hp[k].set(foHighShelf(fs, HS_F, dbToLin(G.high - G.mid)));
      }
      this.modD = this.offline ? 0 : clamp(num(p.modulation, 30), 0, 100) / 100 * 10 * fs / 48000;
      // input diffusion
      const s = V.scale * clamp(num(p.size, 100), 20, 200) / 100, dg = clamp(num(p.diffusion, 80), 0, 100) / 100 * 0.72;
      for (let c = 0; c < 2; c++) {
        const want = AP_MS.map((ms, k) => nextPrime(ms * Math.sqrt(s) * fs / 1000 * (c ? 1.07 : 1) + k));
        if (this.ap[c].length !== 4 || this.ap[c].some((a, k) => a.n !== want[k])) this.ap[c] = want.map((n) => new Allpass(n));
        this.ap[c].forEach((a) => { a.g = dg; });
      }
      // early reflections: 12 taps per side from a fixed pattern
      this.erTaps = [];
      const erAmt = clamp(num(p.er, 50), 0, 100) / 100 * V.er;
      if (erAmt > 0) {
        const r = new Rng(31337 + (p.variant | 0));
        for (let k = 0; k < 24; k++) {
          const t = (V.erPre + V.erSpan * Math.sqrt(clamp(num(p.size, 100), 20, 200) / 100) * Math.pow((k + 0.5 + 0.4 * r.next()) / 24, 1.3)) * fs / 1000;
          this.erTaps.push({ d: Math.min(this.erN - 4, Math.max(1, t)), g: erAmt * 0.7 * (0.55 + 0.45 * r.next()) * Math.pow(1 - k / 30, 1.2) * (r.next() > 0 ? 1 : -1), ch: k & 1 });
        }
      }
      for (let c = 0; c < 2; c++) this.erLP[c].set(bqLP(fs, Math.min(7000, fs * 0.45), 0.6));
      this.lateG = 1.0 / Math.sqrt(FDN_N) * 0.9;
      setSecs(this.eq, designWetEQ(p, fs));
      this.width = clamp(num(p.width, 100), 0, 200) / 100;
      const m = clamp(num(p.mix, 25), 0, 100) / 100; this.wetG = Math.min(1, 2 * m); this.dryG = Math.min(1, 2 * (1 - m));
      this.outG = dbToLin(num(p.output, 0));
      this.xfStep = 1 / (0.08 * fs);
    }
    // one sample of the algorithmic reverb (input already pre-delayed)
    algo(xl, xr, out) {
      for (let k = 0; k < 4; k++) { xl = this.ap[0][k].process(xl); xr = this.ap[1][k].process(xr); }
      // early reflections from the mono sum
      this.erBuf[this.erW] = 0.5 * (xl + xr);
      let el = 0, er = 0;
      for (const t of this.erTaps) { const v = this.erBuf[(this.erW - Math.floor(t.d)) & this.erMask] * t.g; if (t.ch) er += v; else el += v; }
      this.erW = (this.erW + 1) & this.erMask;
      el = this.erLP[0].process(el); er = this.erLP[1].process(er);
      // read lines (modulated), decay filters
      const s = this.sv || (this.sv = new Float64Array(FDN_N)), lines = this.lines, mask = this.lmask, w = this.lw;
      for (let k = 0; k < FDN_N; k++) {
        let d = this.lens[k];
        if (this.modD > 0) { this.lphase[k] += this.ldph[k]; if (this.lphase[k] > 2 * PI) this.lphase[k] -= 2 * PI; d += this.modD * (1 + Math.sin(this.lphase[k])); }
        const y = this.modD > 0 ? hermite(lines[k], mask, w, d) : lines[k][(w - d) & mask];
        s[k] = this.hp[k].process(this.lp[k].process(y * this.v[k]));
      }
      // outputs: orthogonal sign patterns
      let ol = 0, or = 0;
      for (let k = 0; k < FDN_N; k++) { ol += (k & 1 ? -s[k] : s[k]); or += ((k >> 1) & 1 ? -s[k] : s[k]) * (k & 1 ? 1 : 1); }
      // Hadamard mixing (fast Walsh–Hadamard, normalised)
      for (let h = 1; h < FDN_N; h <<= 1) for (let i = 0; i < FDN_N; i += h << 1) for (let j = i; j < i + h; j++) { const a = s[j], b = s[j + h]; s[j] = a + b; s[j + h] = a - b; }
      for (let k = 0; k < FDN_N; k++) lines[k][w] = s[k] * 0.25 + (k & 1 ? xr : xl) * (k & 2 ? -0.5 : 0.5);
      this.lw = (w + 1) & mask;
      out[0] = ol * this.lateG + el; out[1] = or * this.lateG + er;
    }
    process(L, R, n) {
      let pi = 0, po = 0, pw = 0; const o = this.o2 || (this.o2 = [0, 0]), ir = this.usesIR();
      for (let i = 0; i < n; i++) {
        const dl = L[i], dr = R[i];
        pi = Math.max(pi, Math.abs(dl), Math.abs(dr));
        this.pre[0][this.pw] = dl; this.pre[1][this.pw] = dr;
        const xl = this.preD > 0 ? hermite(this.pre[0], this.pmask, this.pw, Math.max(1, this.preD)) : dl;
        const xr = this.preD > 0 ? hermite(this.pre[1], this.pmask, this.pw, Math.max(1, this.preD)) : dr;
        this.pw = (this.pw + 1) & this.pmask;
        let wl = 0, wr = 0;
        if (ir) {
          if (this.conv) {
            wl = this.conv[0].process(xl); wr = this.conv[1].process(xr);
            if (this.xf < 1 && this.convOld) {
              const a = this.xf; wl = a * wl + (1 - a) * this.convOld[0].process(xl); wr = a * wr + (1 - a) * this.convOld[1].process(xr);
              this.xf = Math.min(1, this.xf + this.xfStep); if (this.xf >= 1) this.convOld = null;
            }
          }
        } else { this.algo(xl, xr, o); wl = o[0]; wr = o[1]; }
        wl = this.dc[0].process(runSecs(this.eq[0], wl)); wr = this.dc[1].process(runSecs(this.eq[1], wr));
        const mid = 0.5 * (wl + wr), side = 0.5 * (wl - wr) * this.width; wl = mid + side; wr = mid - side;
        pw = Math.max(pw, Math.abs(wl), Math.abs(wr));
        const yl = (dl * this.dryG + wl * this.wetG) * this.outG, yr = (dr * this.dryG + wr * this.wetG) * this.outG;
        L[i] = yl; R[i] = yr; po = Math.max(po, Math.abs(yl), Math.abs(yr));
      }
      this.inMeter = pi; this.outMeter = po; this.wetMeter = pw;
    }
  }

  const MAKERS = { tape: Tape, delay: Delay, reverb: Reverb };
  function create(type, fs) { return new MAKERS[type](fs); }

  // ================================================================ Rack
  class Rack {
    constructor(fs) { this.fs = fs; this.slots = []; this.byUid = new Map(); this.inPk = 0; this.outPk = 0; this.bpm = 120; }
    setSlots(list) {
      const next = new Map();
      this.slots = list.map((s) => {
        let m = this.byUid.get(s.uid);
        const fresh = !m || m.type !== s.type;
        if (fresh) m = { type: s.type, mod: create(s.type, this.fs) };
        if (s.p) m.mod.setParams(s.p);
        if (m.mod.setBpm) m.mod.setBpm(this.bpm);
        if (fresh && m.mod.snap) m.mod.snap();
        next.set(s.uid, m);
        return { uid: s.uid, type: s.type, on: s.on !== false, m };
      });
      this.byUid = next;
    }
    setParams(uid, p) { const m = this.byUid.get(uid); if (m) m.mod.setParams(p); }
    setOn(uid, v) { const s = this.slots.find((x) => x.uid === uid); if (s) s.on = !!v; }
    setIR(uid, L, R) { const m = this.byUid.get(uid); if (m && m.mod.setIR) m.mod.setIR(L, R); }
    setBpm(b) { this.bpm = b; for (const s of this.slots) if (s.m.mod.setBpm) s.m.mod.setBpm(b); }
    process(L, R, n) {
      let a = 0; for (let i = 0; i < n; i++) a = Math.max(a, Math.abs(L[i]), Math.abs(R[i])); this.inPk = a;
      for (const s of this.slots) if (s.on) s.m.mod.process(L, R, n);
      a = 0; for (let i = 0; i < n; i++) a = Math.max(a, Math.abs(L[i]), Math.abs(R[i])); this.outPk = a;
    }
    meters() {
      const lv = {};
      for (const s of this.slots) { const m = s.m.mod; lv[s.uid] = [s.on ? m.inMeter : 0, s.on ? m.outMeter : 0, s.on ? (m.wetMeter || 0) : 0]; }
      return { in: this.inPk, out: this.outPk, lv };
    }
  }

  // ================================================================ previews
  // delay: echo pattern of a click (analog noise / modulation off). Returns peak per `binMs` for L and R.
  function echoPattern(p, bpm, durMs, binMs, fs) {
    fs = fs || 24000; const m = new Delay(fs); m.setBpm(bpm || 120);
    m.setParams(Object.assign({}, p, { hiss: 0, modDepth: 0, duck: 0, mix: 100, output: 0, width: 100 }));
    m.wowD = 0; m.snap();
    const n = Math.round(fs * durMs / 1000), step = Math.max(1, Math.round(fs * binMs / 1000));
    const L = new Float32Array(step), R = new Float32Array(step), outL = [], outR = [];
    for (let i0 = 0; i0 + step <= n; i0 += step) {
      L.fill(0); R.fill(0); if (i0 === 0) { L[0] = 1; R[0] = (p.stereo | 0) === 2 ? 0 : 1; }
      m.process(L, R, step);
      let a = 0, b = 0; for (let j = 0; j < step; j++) { a = Math.max(a, Math.abs(L[j])); b = Math.max(b, Math.abs(R[j])); }
      outL.push(a); outR.push(b);
    }
    return [outL, outR];
  }
  // reverb: energy envelope (dB) per bin of the wet response to a click
  function reverbEnvelope(p, irLR, durMs, binMs, fs) {
    fs = fs || 24000;
    const m = new Reverb(fs, false); m.setParams(Object.assign({}, p, { mix: 100, output: 0 }));
    if (m.usesIR() && irLR) m.setIR(irLR[0], irLR[1]);
    const n = Math.round(fs * durMs / 1000), step = Math.max(1, Math.round(fs * binMs / 1000));
    const L = new Float32Array(step), R = new Float32Array(step), env = [];
    for (let i0 = 0; i0 + step <= n; i0 += step) {
      L.fill(0); R.fill(0); if (i0 === 0) { L[0] = R[0] = 1; }
      m.process(L, R, step);
      let e = 0; for (let j = 0; j < step; j++) e += L[j] * L[j] + R[j] * R[j];
      env.push(10 * Math.log10(e / (2 * step) + 1e-14));
    }
    return env;
  }
  // tape: steady-state level at 1 kHz for a range of input levels → transfer curve + THD
  function tapeCurve(p, fs, levels) {
    return levels.map((lv) => {
      const m = new Tape(fs); m.setParams(Object.assign({}, p, { wow: 0, flutter: 0, hiss: 0, mix: 100 }));
      const n = 4096, A = dbToLin(lv), L = new Float32Array(n), R = new Float32Array(n);
      for (let i = 0; i < n; i++) L[i] = R[i] = A * Math.sin(2 * PI * 1000 * i / fs);
      m.process(L, R, n);
      let pk = 0; for (let i = n / 2; i < n; i++) pk = Math.max(pk, Math.abs(L[i]));
      return linToDb(pk);
    });
  }
  // tape: steady-state response at a level (small signal vs hot) for a few frequencies
  function tapeLevelResponse(p, fs, freqs, lvDb) {
    return freqs.map((f) => {
      const m = new Tape(fs); m.setParams(Object.assign({}, p, { wow: 0, flutter: 0, hiss: 0, mix: 100, output: 0 }));
      const n = Math.max(4096, Math.round(fs * 8 / f)), A = dbToLin(lvDb), L = new Float32Array(n), R = new Float32Array(n);
      for (let i = 0; i < n; i++) L[i] = R[i] = A * Math.sin(2 * PI * f * i / fs);
      m.process(L, R, n);
      // amplitude of the fundamental over the second half
      let re = 0, im = 0; const h = Math.floor(n / 2);
      for (let i = h; i < n; i++) { const ph = 2 * PI * f * i / fs; re += L[i] * Math.cos(ph); im += L[i] * Math.sin(ph); }
      return linToDb(2 * Math.hypot(re, im) / (n - h)) - lvDb - num(p.output, 0);
    });
  }
  function harmonics(type, p, fs, f0, levelDb) {
    const m = create(type, fs); m.setParams(Object.assign({}, p, { hiss: 0, wow: 0, flutter: 0, mix: 100 }));
    const n = Math.round(fs * 0.3), N = Math.round(fs * 0.1), A = dbToLin(levelDb);
    const L = new Float32Array(n), R = new Float32Array(n);
    for (let i = 0; i < n; i++) L[i] = R[i] = A * Math.sin(2 * PI * f0 * i / fs);
    for (let i = 0; i < n; i += 256) { const k = Math.min(256, n - i); m.process(L.subarray(i, i + k), R.subarray(i, i + k), k); }
    const mag = (f) => { let re = 0, im = 0; for (let i = 0; i < N; i++) { const w = 0.5 - 0.5 * Math.cos(2 * PI * i / (N - 1)), x = L[n - N + i] * w, ph = 2 * PI * f * i / fs; re += x * Math.cos(ph); im += x * Math.sin(ph); } return Math.hypot(re, im); };
    const h1 = mag(f0) || 1e-12, h = []; let sum = 0;
    for (let k = 2; k <= 7; k++) { const v = f0 * k < fs * 0.45 ? mag(f0 * k) : 0; sum += v * v; h.push(20 * Math.log10(Math.max(v / h1, 1e-7))); }
    return { h, thd: Math.sqrt(sum) / h1 * 100 };
  }
  // frequency-map curves per module type
  function responseTone(type, p, fs, freqs, bpm) {
    const s = type === 'tape' ? designTapeTone(p, fs) : type === 'delay' ? designDelayLoop(p, fs, bpm).concat(design3Band(p, fs)) : designWetEQ(p, fs);
    return freqs.map((f) => secsMagDb(s, fs, f));
  }
  // the IR a reverb module needs (or null): built-in / user (passed in) / rendered algorithm, then trim + reverse
  function buildIR(p, fs, userIR) {
    const src = (p.source | 0);
    if (src === 0 && !p.reverse) return null;
    let raw;
    if (src === 1) raw = synthIR(p.irIndex | 0, fs);
    else if (src === 2) raw = userIR || null;
    else raw = renderAlgoIR(p, fs, clamp(num(p.revLen, 1500), 100, 6000) + 50);
    if (!raw) return null;
    return prepareIR(raw, p, fs);
  }

  return { TYPES, SPEC, SYNC, CIRCUIT, DEFAULTS, REF, Rack, create, Convolver, synthIR, prepareIR, buildIR, renderAlgoIR, normalizeIR,
           echoPattern, reverbEnvelope, design3Band, designDelayLoop, designWetEQ, tapeCurve, tapeLevelResponse, harmonics, responseTone, rtCurve, delayMs, fdnLengths,
           halfbandCoefs, Halfband, fft, rfft, irfft, dbToLin, linToDb, bqMagDb, secsMagDb, designTapeTone, IRSPEC };
})();
if (typeof module !== 'undefined') module.exports = TS;
