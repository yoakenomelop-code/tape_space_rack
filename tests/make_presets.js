// Generates Resources/presets.json (module + rack presets). Values are partial; the rest comes from DEFAULTS.
const fs = require('fs'), path = require('path');
const TS = require('../web/dsp.js');
const sync = (name) => { const i = TS.SYNC.names.indexOf(name); if (i < 1) throw new Error('note ' + name); return i; };
const M = {
  tape: [
    ['Clean 30 ips Master', { speed: 30, formula: 0, c1: 2 }],
    ['Warm 15 ips NAB', { speed: 15, formula: 1, drive: 3 }],
    ['Silky 15 ips CCIR', { speed: 15, eqStd: 1, formula: 0, drive: 2 }],
    ['Hot 15 ips Glue', { speed: 15, formula: 1, drive: 6, c3: 1 }],
    ['Multitrack 15 ips Drums', { variant: 1, speed: 15, formula: 1, drive: 6 }],
    ['Multitrack 7.5 ips Fat', { variant: 1, speed: 7.5, formula: 2, drive: 5 }],
    ['Vintage 7.5 ips Saturated', { speed: 7.5, formula: 2, drive: 9, age: 30, wow: 10, flutter: 15, hiss: 30 }],
    ['Lo-fi 3.75 ips', { speed: 3.75, formula: 2, drive: 6, wow: 35, flutter: 30, hiss: 50, age: 50 }],
    ['Cassette Demo', { variant: 2, formula: 1, drive: 3, wow: 25, flutter: 25, hiss: 40 }],
    ['Cassette Worn Out', { variant: 2, formula: 2, drive: 6, wow: 60, flutter: 50, hiss: 60, age: 70, c2: 1 }],
    ['Subtle Polish', { speed: 30, formula: 0, drive: 1, c1: 1 }],
    ['Bias Under (Bright Grit)', { speed: 15, formula: 1, drive: 5, bias: -60 }],
    ['Bias Over (Dark Smooth)', { speed: 15, formula: 1, drive: 5, bias: 70 }],
  ],
  delay: [
    ['HQ Quarter (Sync)', { variant: 0, sync: sync('1/4'), feedback: 30 }],
    ['HQ Dotted Eighth', { variant: 0, sync: sync('1/8 Dot'), feedback: 40, mix: 25 }],
    ['HQ Slapback 90 ms', { variant: 0, time: 90, feedback: 0, stereo: 0, mix: 25 }],
    ['HQ Wide Dual 1/4 : 1/8.', { variant: 0, stereo: 3, sync: sync('1/4'), ratioLR: 75, feedback: 35 }],
    ['Ping-Pong Eighths', { variant: 0, stereo: 2, sync: sync('1/8'), feedback: 45 }],
    ['Ping-Pong Triplets', { variant: 0, stereo: 2, sync: sync('1/8 Trip'), feedback: 40, highcut: 8000 }],
    ['Reverse Swell', { variant: 0, reverse: 1, time: 600, feedback: 35, mix: 40 }],
    ['Reverse Ping-Pong', { variant: 0, reverse: 1, stereo: 2, sync: sync('1/4'), feedback: 40, mix: 35 }],
    ['Telephone Echo (Wet EQ)', { variant: 0, time: 300, feedback: 35, lowcut: 300, highcut: 3500, eqLo: -12, eqMid: 8, eqMidF: 1500, eqHi: -12 }],
    ['Dark Ambient Wash', { variant: 0, sync: sync('1/4 Dot'), feedback: 75, highcut: 3000, modDepth: 20, mix: 35, eqHi: -4 }],
    ['Tape Echo Single Head', { variant: 1, heads: 0, time: 320, feedback: 40, hiss: 30, drive: 3 }],
    ['Tape Echo Multi-Head', { variant: 1, heads: 6, time: 180, feedback: 45, hiss: 30 }],
    ['Tape Echo Runaway', { variant: 1, heads: 3, time: 280, feedback: 105, drive: 6, hiss: 40, age: 30 }],
    ['Rockabilly Slap', { variant: 1, heads: 0, time: 110, feedback: 10, stereo: 0, mix: 30 }],
    ['Analog BBD Warm', { variant: 2, time: 350, feedback: 45, modDepth: 10, hiss: 20 }],
    ['BBD Chorus-Echo', { variant: 2, time: 250, modRate: 0.8, modDepth: 40, feedback: 30, hiss: 20 }],
    ['BBD Long Murky', { variant: 2, time: 900, feedback: 55, hiss: 30 }],
    ['Vintage 12-bit Rack', { variant: 3, time: 400, feedback: 40 }],
    ['Vintage Digital Dub', { variant: 3, sync: sync('1/4 Dot'), feedback: 70, lowcut: 200, highcut: 4000, stereo: 2, eqMid: 4, eqMidF: 900 }],
    ['Fine 5/16 Polyrhythm', { variant: 0, sync: sync('20/64 (5/16)'), feedback: 45, stereo: 2, mix: 25 }],
    ['Fine 7/32 Push', { variant: 0, sync: sync('14/64 (7/32)'), feedback: 35, highcut: 9000, mix: 25 }],
    ['Bar-long Swell (1 bar)', { variant: 0, sync: sync('1'), feedback: 40, reverse: 0, highcut: 6000, modDepth: 15, mix: 30 }],
    ['Ducked Vocal Echo', { variant: 0, sync: sync('1/4'), feedback: 35, duck: 70, mix: 35, lowcut: 200, eqHi: -3 }],
  ],
  reverb: [
    ['Concert Hall', { variant: 0, decay: 2.4, predelay: 25 }],
    ['Large Hall Long', { variant: 0, size: 150, decay: 4.5, predelay: 40 }],
    ['Small Room', { variant: 1, decay: 0.6, size: 80, predelay: 5, er: 70 }],
    ['Drum Room', { variant: 1, decay: 0.9, er: 80, lowcut: 150, mix: 30 }],
    ['Vocal Plate', { variant: 2, decay: 1.8, predelay: 30, hiMult: 0.8, lowcut: 200, er: 0 }],
    ['Bright Plate Short', { variant: 2, decay: 1.1, hiMult: 0.9, er: 0 }],
    ['Chamber', { variant: 3, decay: 1.5, predelay: 12 }],
    ['Cathedral', { variant: 4, decay: 6, predelay: 60, size: 120 }],
    ['Infinite Pad', { variant: 4, decay: 20, modulation: 60, mix: 40, hiMult: 0.4 }],
    ['IR Concert Hall', { source: 1, irIndex: 0 }],
    ['IR Wood Room', { source: 1, irIndex: 1, mix: 30 }],
    ['IR Bright Plate', { source: 1, irIndex: 2 }],
    ['IR Stone Chamber', { source: 1, irIndex: 3 }],
    ['IR Vocal Booth', { source: 1, irIndex: 4, mix: 30 }],
    ['IR Large Church', { source: 1, irIndex: 5, predelay: 10 }],
    ['IR Spring Tank', { source: 1, irIndex: 6, lowcut: 150, predelay: 0 }],
    ['Reverse Hall Swell', { source: 0, reverse: 1, variant: 0, decay: 3, revLen: 1500, revSync: sync('1/2'), mix: 35 }],
    ['Tempo Plate (decay 1 bar)', { variant: 2, decaySync: sync('1'), preSync: sync('1/64'), er: 0, lowcut: 200, mix: 25 }],
    ['Tempo Hall (pre 1/32, decay 2 bars)', { variant: 0, decaySync: sync('2'), preSync: sync('1/32'), mix: 25 }],
    ['Reverse Plate Lead-in', { source: 0, reverse: 1, variant: 2, decay: 2, revLen: 800, er: 0, mix: 35 }],
    ['Reverse Church IR', { source: 1, irIndex: 5, reverse: 1, revLen: 2500, revSync: sync('1'), mix: 35 }],
  ],
};
const mods = {};
for (const t of Object.keys(M)) mods[t] = M[t].map(([name, p]) => ({ name, p }));
const rack = [
  { name: 'Tape + Slap + Room', slots: [{ type: 'tape', preset: 'Warm 15 ips NAB' }, { type: 'delay', preset: 'Rockabilly Slap' }, { type: 'reverb', preset: 'Small Room' }] },
  { name: 'Modern Vocal', slots: [{ type: 'tape', preset: 'Subtle Polish' }, { type: 'delay', preset: 'Ducked Vocal Echo' }, { type: 'reverb', preset: 'Vocal Plate' }] },
  { name: 'Dub Station', slots: [{ type: 'tape', preset: 'Vintage 7.5 ips Saturated' }, { type: 'delay', preset: 'Tape Echo Runaway', p: { feedback: 85 } }, { type: 'reverb', preset: 'IR Spring Tank' }] },
  { name: 'Ambient Wash', slots: [{ type: 'delay', preset: 'Dark Ambient Wash' }, { type: 'reverb', preset: 'Infinite Pad' }] },
  { name: 'Lo-fi Cassette Dream', slots: [{ type: 'tape', preset: 'Cassette Worn Out' }, { type: 'delay', preset: 'BBD Chorus-Echo' }, { type: 'reverb', preset: 'Chamber' }] },
  { name: 'Reverse Dream', slots: [{ type: 'delay', preset: 'Reverse Swell' }, { type: 'reverb', preset: 'Reverse Hall Swell' }] },
  { name: 'Big Drums', slots: [{ type: 'tape', preset: 'Multitrack 15 ips Drums' }, { type: 'reverb', preset: 'Drum Room' }] },
  { name: 'Church Ping-Pong', slots: [{ type: 'delay', preset: 'Ping-Pong Eighths', p: { mix: 25 } }, { type: 'reverb', preset: 'IR Large Church' }] },
  { name: 'Tape Echo into Plate', slots: [{ type: 'tape', preset: 'Hot 15 ips Glue' }, { type: 'delay', preset: 'Tape Echo Multi-Head' }, { type: 'reverb', preset: 'Vocal Plate' }] },
];
for (const r of rack) for (const s of r.slots) if (!mods[s.type].some((x) => x.name === s.preset)) throw new Error('missing ' + s.preset);
const out = path.join(__dirname, '../Resources/presets.json');
fs.mkdirSync(path.dirname(out), { recursive: true });
fs.writeFileSync(out, JSON.stringify({ module: mods, rack }, null, 1));
console.log('wrote', out, Object.values(mods).map((x) => x.length), rack.length);
