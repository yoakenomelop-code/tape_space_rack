// C++ vs JS parity: node tests/parity_gen.js && g++ -O2 -std=c++17 tests/parity.cpp -o /tmp/parity && /tmp/parity
#include "../Source/DSP.h"
#include "parity_cases.h"
#include <cstdio>
int main() {
  auto cs = makeCases(); const int N = 48000, SW = 24064; const double FS = 48000;
  FILE* f = fopen("tests/parity_js_out.bin", "rb"); if (!f) return 1; std::vector<float> js((size_t) cs.size() * 2 * N); if (fread(js.data(), 4, js.size(), f) != js.size()) return 1; fclose(f);
  double worst = -400; int k = 0;
  for (auto& c : cs) {
    std::vector<float> L(N), R(N); uint32_t s = 12345;
    for (int i = 0; i < N; i++) { s = s * 1103515245u + 12345u; double nz = ((double) s / 4294967296.0 - 0.5) * 0.2;
      double env = (i / 4000) % 3 == 0 ? 1 : 0; L[i] = (float) (env * (0.4 * std::sin(2 * ts::PI * 220 * i / FS) + nz)); R[i] = (float) (env * (0.3 * std::sin(2 * ts::PI * 330 * i / FS) + nz * 0.7)); }
    auto m = ts::create(c.type, FS); m->setParams(c.p);
    std::unique_ptr<ts::ConvPair> cp;
    if (c.type == ts::kReverb) { auto ir = ts::buildIR(c.p, FS, nullptr); if (! ir[0].empty()) { cp = std::make_unique<ts::ConvPair>(ir[0], ir[1]); static_cast<ts::Reverb*>(m.get())->setIR(cp.get()); } }
    for (int i = 0; i < N; i += 256) { if (c.change && i == SW) m->setParams(c.p2); m->process(L.data() + i, R.data() + i, std::min(256, N - i)); }
    if (c.type == ts::kReverb) static_cast<ts::Reverb*>(m.get())->releaseAll();
    double md = 0, mx = 0;
    for (int i = 0; i < N; i++) { md = std::max(md, (double) std::abs(L[i] - js[(size_t)(2*k)*N + i])); md = std::max(md, (double) std::abs(R[i] - js[(size_t)(2*k+1)*N + i])); mx = std::max(mx, (double) std::abs(js[(size_t)(2*k)*N + i])); }
    double d = 20 * std::log10(md / std::max(mx, 1e-9) + 1e-20); worst = std::max(worst, d);
    if (d > -100) printf("case %d type %d diff %.1f dB (peak %.3f)\n", k, c.type, d, mx);
    k++;
  }
  printf("cases %d worst diff %.1f dB re peak\n", k, worst);
  return worst > -100 ? 2 : 0;
}
