// generates parity_cases.h (C++) + JS reference outputs for C++/JS parity testing
const TS = require('../web/dsp.js'), fs = require('fs'), path = require('path');
const P = JSON.parse(fs.readFileSync(path.join(__dirname, '..', 'Resources', 'presets.json'), 'utf8'));
const cases = [];
for (const t of TS.TYPES) {
  cases.push([t, {}]);
  for (const m of P.module[t]) cases.push([t, m.p]);
  cases.push([t, { analog: 0, mix: 60, output: -3, eqLo: 4, eqMid: -5, eqMidF: 900, eqHi: 3 }]);
}
cases.push(['tape', { c1: 1, c2: 2, c3: 2, bias: 40, age: 50, headroom: 18, wow: 40, flutter: 30, hiss: 70, speed: 7.5, eqStd: 1 }]);
cases.push(['delay', { variant: 2, reverse: 1, time: 500, feedback: 60, stereo: 3, ratioLR: 60 }, { time: 300 }]);
cases.push(['delay', { variant: 0, time: 300, feedback: 50 }, { time: 420 }]);
cases.push(['delay', { variant: 1, heads: 5, time: 200, feedback: 70, age: 60, hiss: 40 }, { time: 260 }]);
cases.push(['reverb', { source: 1, irIndex: 6, irLen: 60, reverse: 1, revLen: 900 }]);
cases.push(['reverb', { source: 0, size: 180, decay: 12, modulation: 100, diffusion: 20, er: 100, variant: 4 }]);
const NI = (n) => TS.SYNC.names.indexOf(n);
cases.push(['delay', { variant: 0, sync: NI('14/64 (7/32)'), feedback: 55, stereo: 3, ratioLR: 66 }]);
cases.push(['delay', { variant: 0, sync: NI('4'), feedback: 30 }]);
cases.push(['delay', { variant: 1, sync: NI('1/16 Trip'), feedback: 60, heads: 6 }, { sync: NI('5/64') }]);
cases.push(['reverb', { variant: 2, decaySync: NI('1'), preSync: NI('1/32 Dot') }]);
cases.push(['reverb', { variant: 0, reverse: 1, revSync: NI('1/4'), preSync: NI('1/64') }]);
cases.push(['reverb', { source: 1, irIndex: 3, preSync: NI('1/16') }]);
const BPM = 97.3;
const KEYS = Object.keys(TS.DEFAULTS.tape);
const N = 48000, FS = 48000, SW = 24064;
const sig = () => { const L = new Float32Array(N), R = new Float32Array(N); let s = 12345;
  for (let i = 0; i < N; i++) { s = (Math.imul(s, 1103515245) + 12345) >>> 0; const nz = (s / 4294967296 - 0.5) * 0.2;
    const env = Math.floor(i / 4000) % 3 === 0 ? 1 : 0; L[i] = env * (0.4 * Math.sin(2 * Math.PI * 220 * i / FS) + nz); R[i] = env * (0.3 * Math.sin(2 * Math.PI * 330 * i / FS) + nz * 0.7); }
  return [L, R]; };
let h = '#pragma once\n#include <vector>\nstruct Case { int type; ts::Params p, p2; bool change; };\ninline std::vector<Case> makeCases() { std::vector<Case> v; ts::Params p, q;\n';
const lit = (x) => { const s = String(+x); return /[.e]/.test(s) ? s : s + '.0'; };
const outs = [];
cases.forEach(([t, o, o2]) => {
  const ti = TS.TYPES.indexOf(t), p = Object.assign({}, TS.DEFAULTS[t], o), p2 = Object.assign({}, p, o2 || {});
  h += `  p = ts::defaultsFor (${ti});` + KEYS.map((k) => ` p.${k} = ${lit(p[k])};`).join('') + `\n  q = p;` + KEYS.filter((k) => p2[k] !== p[k]).map((k) => ` q.${k} = ${lit(p2[k])};`).join('') + ` v.push_back ({ ${ti}, p, q, ${o2 ? 'true' : 'false'} });\n`;
  const m = TS.create(t, FS); if (m.setBpm) m.setBpm(BPM); m.setParams(p);
  if (t === 'reverb') { const ir = TS.buildIR(p, FS, null, BPM); if (ir) m.setIR(ir[0], ir[1]); }
  const [L, R] = sig();
  for (let i = 0; i < N; i += 256) { if (o2 && i === SW) m.setParams(p2); m.process(L.subarray(i, i + 256), R.subarray(i, i + 256), Math.min(256, N - i)); }
  outs.push(L, R);
});
h += '  return v; }\n';
fs.writeFileSync(path.join(__dirname, 'parity_cases.h'), h);
const buf = Buffer.alloc(outs.length * N * 4); outs.forEach((a, k) => Buffer.from(a.buffer).copy(buf, k * N * 4)); fs.writeFileSync(path.join(__dirname, 'parity_js_out.bin'), buf);
console.log('cases', cases.length);
