// 400Scalper — Spectral mode prototype (detail-preserving clipper).
//
// 1. A short STFT (N ≈ 10.7 ms, hop N/4, sqrt-Hann WOLA) is grouped into 26 Mel-spaced
//    "ear" bands with triangular weights.
// 2. Per frame, bands within RANGE dB of the loudest band are LOUD (mask -> 1), quieter
//    bands are DETAIL (mask -> 0). Masks attack instantly and release over ~60 ms.
// 3. The DETAIL signal D is resynthesised from the quiet bins; the loud part is x - D
//    (complementary, so x = L + D exactly).
// 4. Clip only the loud part, against bounds that leave room for the detail:
//       y = d·D + softclip(x - d·D, hi = T - d·D, lo = -T - d·D)
//    -> |y| <= T always; below threshold y = x; while clipping the detail rides through.
// Latency = N samples (frame length).

(function (root) {
  function makeFFT(n) {
    const rev = new Uint32Array(n);
    for (let i = 0, j = 0; i < n; i++) {
      rev[i] = j;
      let bit = n >> 1;
      for (; j & bit; bit >>= 1) j ^= bit;
      j ^= bit;
    }
    const cos = new Float64Array(n / 2), sin = new Float64Array(n / 2);
    for (let i = 0; i < n / 2; i++) { cos[i] = Math.cos(2 * Math.PI * i / n); sin[i] = Math.sin(2 * Math.PI * i / n); }
    return function fft(re, im, inverse) {
      for (let i = 0; i < n; i++) { const j = rev[i]; if (i < j) { let t = re[i]; re[i] = re[j]; re[j] = t; t = im[i]; im[i] = im[j]; im[j] = t; } }
      for (let len = 2; len <= n; len <<= 1) {
        const step = n / len, half = len / 2;
        for (let i = 0; i < n; i += len) {
          for (let j = 0; j < half; j++) {
            const wr = cos[j * step], wi = inverse ? sin[j * step] : -sin[j * step];
            const a = i + j, b = a + half;
            const tr = re[b] * wr - im[b] * wi, ti = re[b] * wi + im[b] * wr;
            re[b] = re[a] - tr; im[b] = im[a] - ti; re[a] += tr; im[a] += ti;
          }
        }
      }
      if (inverse) for (let i = 0; i < n; i++) { re[i] /= n; im[i] /= n; }
    };
  }

  const NB = 26;
  const mel = (f) => 2595 * Math.log10(1 + f / 700);
  const imel = (m) => 700 * (Math.pow(10, m / 2595) - 1);

  function createSpectral(sr) {
    const N = Math.pow(2, Math.round(Math.log2(0.0107 * sr))); // 512 @ 44.1/48k, 1024 @ 88.2/96k
    const H = N / 4, nb = N / 2 + 1;
    const fft = makeFFT(N);
    const win = new Float64Array(N);
    for (let i = 0; i < N; i++) win[i] = Math.sqrt(0.5 - 0.5 * Math.cos(2 * Math.PI * i / N)); // periodic sqrt-Hann
    // Mel band edges 20 Hz .. 20 kHz (clamped to Nyquist), triangular weights per bin
    const fmax = Math.min(20000, sr / 2 * 0.98);
    const edges = []; for (let i = 0; i <= NB + 1; i++) edges.push(imel(mel(20) + (mel(fmax) - mel(20)) * i / (NB + 1)));
    const centers = edges.slice(1, NB + 1);
    const W = []; // W[band] = Float64Array(nb)
    for (let b = 0; b < NB; b++) {
      const w = new Float64Array(nb), lo = edges[b], c = edges[b + 1], hi = edges[b + 2];
      for (let k = 0; k < nb; k++) {
        const f = k * sr / N;
        if (f > lo && f < hi) w[k] = f <= c ? (f - lo) / (c - lo) : (hi - f) / (hi - c);
      }
      // bands narrower than a bin: give them the nearest bin so every band is measurable
      let s = 0; for (let k = 0; k < nb; k++) s += w[k];
      if (s === 0) w[Math.min(nb - 1, Math.round(c * N / sr))] = 1;
      W.push(w);
    }
    const wsum = new Float64Array(nb);
    for (let k = 0; k < nb; k++) { let s = 0; for (let b = 0; b < NB; b++) s += W[b][k]; wsum[k] = s || 1; }

    const inBuf = new Float64Array(N), outAcc = new Float64Array(N + H), xDelay = new Float64Array(N - 1);
    const re = new Float64Array(N), im = new Float64Array(N);
    const bandDb = new Float64Array(NB).fill(-120), mask = new Float64Array(NB), binMask = new Float64Array(nb), dil = new Float64Array(nb);
    let inPos = 0, hopCount = 0, outPos = 0, xdPos = 0;
    const relPerHop = Math.exp(-H / (0.060 * sr));
    const envRel = Math.exp(-1 / (0.015 * sr));
    let env = 0;

    const st = {
      N, H, latency: N - 1, centers, bandDb, mask,
      range: 15,      // dB below the loudest band that still counts as LOUD
      detail: 1.0,    // 0..1
      process(x, T, knee) {
        // ---- analysis/synthesis of the DETAIL signal ----
        inBuf[inPos] = x; inPos = (inPos + 1) % N;
        if (++hopCount >= H) {
          hopCount = 0;
          for (let i = 0; i < N; i++) { re[i] = inBuf[(inPos + i) % N] * win[i]; im[i] = 0; }
          fft(re, im, false);
          let mx = -120;
          for (let b = 0; b < NB; b++) {
            let e = 0, s = 0; const w = W[b];
            for (let k = 0; k < nb; k++) if (w[k]) { e += w[k] * (re[k] * re[k] + im[k] * im[k]); s += w[k]; }
            const db = 10 * Math.log10(e / (s || 1) + 1e-20);
            bandDb[b] = db; if (db > mx) mx = db;
          }
          for (let b = 0; b < NB; b++) {
            const t = Math.max(0, Math.min(1, (bandDb[b] - (mx - st.range)) / 6 + 0.5)); // 6 dB soft edge
            mask[b] = t > mask[b] ? t : mask[b] * relPerHop + t * (1 - relPerHop);
          }
          for (let k = 0; k < nb; k++) { let m = 0; for (let b = 0; b < NB; b++) if (W[b][k]) m += W[b][k] * mask[b]; binMask[k] = m / wsum[k]; }
          // widen loud regions by ±3 bins so window leakage of a loud tone is not mistaken for detail
          for (let k = 0; k < nb; k++) { let m = 0; for (let j = Math.max(0, k - 3); j <= Math.min(nb - 1, k + 3); j++) if (binMask[j] > m) m = binMask[j]; dil[k] = m; }
          for (let k = 0; k < nb; k++) binMask[k] = dil[k];
          // keep only DETAIL (1 - loud mask)
          for (let k = 0; k < nb; k++) { const g = 1 - binMask[k]; re[k] *= g; im[k] *= g; if (k > 0 && k < N / 2) { re[N - k] = re[k]; im[N - k] = -im[k]; } }
          fft(re, im, true);
          for (let i = 0; i < N; i++) outAcc[i] += re[i] * win[i] * 0.5; // sqrt-Hann^2, hop N/4 sums to 2
          outPos = 0;
        }
        const D = outAcc[outPos];
        outPos++;
        if (outPos === H) { outAcc.copyWithin(0, H); outAcc.fill(0, N); }
        // ---- delay the dry input by N to line up with D ----
        const xd = xDelay[xdPos]; xDelay[xdPos] = x; xdPos = (xdPos + 1) % (N - 1);
        // ---- detail-preserving clip ----
        // Clip the loud part at T minus the detail's peak envelope, then add the detail back on
        // top: the detail survives the clipped region, and |y| <= (T - env) + |dD| <= T.
        let dD = st.detail * D;
        // detail can never use more than 80% of the ceiling (e.g. a broadband snare tagged as detail)
        if (Math.abs(dD) > 0.8 * T) dD *= 0.8 * T / Math.abs(dD);
        const a = Math.abs(dD);
        env = Math.max(a, env * envRel);
        const h = T - Math.min(env, 0.8 * T);
        const r = xd - dD;
        const f = (v) => { const ks = h * (1 - knee); if (v <= ks) return v; if (knee <= 1e-4) return h; const w = h - ks; return ks + w * Math.tanh((v - ks) / w); };
        const yr = r >= 0 ? f(r) : -f(-r);
        return { y: dD + yr, xd, D };
      },
    };
    return st;
  }

  root.createSpectral = createSpectral;
  root.SPECTRAL_NB = NB;
  if (typeof module !== 'undefined') module.exports = { createSpectral, NB };
})(typeof window !== 'undefined' ? window : globalThis);
