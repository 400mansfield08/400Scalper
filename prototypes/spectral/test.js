// Run: node prototypes/spectral/test.js
// Checks: perfect reconstruction (latency N-1), detail kept vs plain clipper, ceiling holds.
const { createSpectral } = require('./spectral.js');
const sr = 48000;
let ok = true;
const check = (c, msg) => { console.log((c ? 'ok:   ' : 'FAIL: ') + msg); ok = ok && c; };

{ // reconstruction: nothing loud -> D equals the input delayed by the latency
  const s = createSpectral(sr); s.range = -1000;
  const xs = [], Ds = [];
  for (let n = 0; n < 20000; n++) { const x = Math.sin(n * 0.031) * 0.5 + Math.sin(n * 0.7) * 0.2; xs.push(x); Ds.push(s.process(x, 10, 0).D); }
  let e = 0; for (let n = 5000; n < 20000; n++) e = Math.max(e, Math.abs(Ds[n] - xs[n - s.latency]));
  check(e < 1e-9, `perfect reconstruction at latency ${s.latency} samples (${(1000 * s.latency / sr).toFixed(1)} ms), err ${e.toExponential(1)}`);
}
function tone(detail) {
  const s = createSpectral(sr); s.detail = detail;
  const out = [];
  for (let n = 0; n < sr; n++) out.push(s.process(Math.sin(2 * Math.PI * 60 * n / sr) + 0.05 * Math.sin(2 * Math.PI * 4000 * n / sr), 0.4, 0).y);
  const t = out.slice(sr / 2);
  let a = 0, b = 0; t.forEach((v, i) => { const ph = 2 * Math.PI * 4000 * (i + sr / 2) / sr; a += v * Math.cos(ph); b += v * Math.sin(ph); });
  return { amp: 2 * Math.hypot(a, b) / t.length, peak: t.reduce((m, v) => Math.max(m, Math.abs(v)), 0) };
}
const plain = tone(0), spec = tone(1);
check(spec.amp > 0.048 && plain.amp < 0.02, `4 kHz detail under a clipped 60 Hz tone: plain ${(plain.amp / 0.05 * 100).toFixed(0)}%, spectral ${(spec.amp / 0.05 * 100).toFixed(0)}%`);
check(spec.peak <= 0.4 + 1e-12, `ceiling holds: peak ${spec.peak.toFixed(6)} <= 0.4`);
process.exit(ok ? 0 : 1);
