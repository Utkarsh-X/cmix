// Implementation of the calibrated online log-bilinear word model for CMIX (EXP-176).
// Origin: zmix 1.0 (James Byrne), GPL-3.0-or-later.
#include "word_label.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

// Compile-time geometry and hyper-parameters
constexpr int kD = 96;
constexpr int kNodeBits = 17;
constexpr uint32_t kNodeMask = (1u << kNodeBits) - 1u;
constexpr uint32_t kNodeCount = kNodeMask + 1u;      // 131072
constexpr uint32_t kNodeBase2 = 16640u;
constexpr int kVMax = 54857;                         // CMIX dictionary vocabulary size
constexpr int kCtx = 16;                             // Context window depth

constexpr float kEtaN = 0.060f;                      // Base node learning rate
constexpr float kEtaE = 0.030f;                      // Base embedding learning rate
constexpr float kLambda = 0.965f;                    // Calibrated context decay per word (EXP-176)
constexpr float kTopicRate = 0.004f;                 // Topic EMA rate
constexpr float kTopicW = 0.25f;                     // Topic weight in context vector
constexpr float kGradClamp = 0.5f;                   // Gradient clamp for embeddings
constexpr float kLogitClamp = 20.0f;                 // Logit clamp before squash
constexpr double kInitScale = 0.05;                  // Initial embedding half-width
constexpr uint64_t kSeed = 0x243F6A8885A308D3ull;   // Fixed xorshift64 seed

inline float SquashF(float x) {
  if (x < -30.0f) x = -30.0f;
  else if (x > 30.0f) x = 30.0f;
  const float e = (float)std::exp((double)(-x));
  return 1.0f / (1.0f + e);
}

// Splitmix64-style avalanche hash for byte-2 nodes
inline uint32_t Hsh(uint64_t x, uint32_t mask) {
  x *= 0x9E3779B97F4A7C15ull;
  x ^= x >> 29;
  x *= 0xBF58476D1CE4E5B9ull;
  x ^= x >> 32;
  return (uint32_t)x & mask;
}

// Map codeword byte position and digit trie index to node id
inline uint32_t NodeId(int bytepos, uint32_t b0, uint32_t b1, uint32_t nodeix) {
  if (bytepos == 0) return nodeix;
  if (bytepos == 1) return 128u + (b0 - 128u) * 128u + nodeix;
  return kNodeBase2 +
      Hsh(((uint64_t)b0 << 20) ^ ((uint64_t)b1 << 8) ^ (uint64_t)nodeix,
          kNodeMask - kNodeBase2);
}

// Fixed-seed xorshift64 mapped to [0, 1)
inline double Rnd(uint64_t* st) {
  *st ^= *st << 13;
  *st ^= *st >> 7;
  *st ^= *st << 17;
  return (double)(*st >> 11) / 9007199254740992.0;
}

void* Xalloc(size_t n, bool zero, const char* what) {
  void* p = zero ? calloc(n, 1) : malloc(n);
  if (p == nullptr) {
    fprintf(stderr, "\nword_label: %zu B for %s failed (D=%d, nodes=%d, vmax=%d)\n",
            n, what, kD, kNodeBits, kVMax);
    fflush(stderr);
    abort();
  }
  return p;
}

}  // namespace

struct WordLabel::State {
  float* nd = nullptr;       // [kNodeCount * kD]  trie-node weights
  float* nb = nullptr;       // [kNodeCount]       trie-node biases
  float* e1 = nullptr;       // [kVMax * kD]       shared embeddings
  float topic[kD];

  float cvec[kD];
  float gacc[kD];
  bool cvec_valid;

  uint32_t ring[kCtx];
  float lampow[kCtx];

  uint32_t breg;
  int nbits_in;
  int first_bit;
  int expecting;
  uint32_t pfx0, pfx1;
  bool escape_next;

  uint32_t nodeix;
  bool live;
  uint32_t nid;
  float plbl;

  uint64_t total_bytes;

  float GetDecay() const {
    // Hyperbolic learning rate decay: rapid early acquisition, stable asymptotic fine-tuning
    return 0.20f + 0.80f / (1.0f + (float)total_bytes / 32768.0f);
  }

  void Init() {
    nd = (float*)Xalloc((size_t)kNodeCount * kD * sizeof(float), true, "nd");
    nb = (float*)Xalloc((size_t)kNodeCount * sizeof(float), true, "nb");
    const size_t etab = (size_t)kVMax * kD * sizeof(float);
    e1 = (float*)Xalloc(etab, false, "e1");
    ResetParams();
  }

  void Free() {
    free(nd); nd = nullptr;
    free(nb); nb = nullptr;
    free(e1); e1 = nullptr;
  }

  void ResetParams() {
    memset(nd, 0, (size_t)kNodeCount * kD * sizeof(float));
    memset(nb, 0, (size_t)kNodeCount * sizeof(float));
    memset(topic, 0, sizeof(topic));
    memset(cvec, 0, sizeof(cvec));
    memset(gacc, 0, sizeof(gacc));
    memset(ring, 0, sizeof(ring));

    uint64_t st = kSeed;
    const size_t n = (size_t)kVMax * kD;
    for (size_t i = 0; i < n; ++i) {
      e1[i] = (float)((Rnd(&st) - 0.5) * 2.0 * kInitScale);
    }

    float w = 1.0f;
    for (int i = 0; i < kCtx; ++i) {
      lampow[i] = w;
      w *= kLambda;
    }

    breg = 1;
    nbits_in = 0;
    first_bit = 0;
    expecting = 0;
    pfx0 = 0;
    pfx1 = 0;
    escape_next = false;
    nodeix = 1;
    live = false;
    nid = 0;
    plbl = 0.5f;
    cvec_valid = false;
    total_bytes = 0;
  }

  void BuildCvec() {
    for (int k = 0; k < kD; ++k) cvec[k] = kTopicW * topic[k];
    for (int sx = 0; sx < kCtx; ++sx) {
      const float w = lampow[sx];
      const float* e = e1 + (size_t)ring[sx] * kD;
      for (int k = 0; k < kD; ++k) cvec[k] += w * e[k];
    }
    cvec_valid = true;
  }

  void ParseAbort() {
    expecting = 0;
    cvec_valid = false;
    memset(gacc, 0, sizeof(gacc));
  }

  void TokenDone(uint32_t r) {
    expecting = 0;
    const float decay = GetDecay();
    const float eta_e = kEtaE * decay;
    const size_t ri = (r < (uint32_t)kVMax) ? (size_t)r : (size_t)(kVMax - 1);
    for (int sx = 0; sx < kCtx; ++sx) {
      const float w = lampow[sx];
      float* e = e1 + (size_t)ring[sx] * kD;
      for (int k = 0; k < kD; ++k) {
        float gk = eta_e * w * gacc[k];
        if (gk > kGradClamp) gk = kGradClamp;
        else if (gk < -kGradClamp) gk = -kGradClamp;
        e[k] -= gk;
      }
    }
    {
      const float* er = e1 + ri * kD;
      for (int k = 0; k < kD; ++k) topic[k] += kTopicRate * (er[k] - topic[k]);
    }
    for (int sx = kCtx - 1; sx > 0; --sx) ring[sx] = ring[sx - 1];
    ring[0] = (uint32_t)ri;
    memset(gacc, 0, sizeof(gacc));
    cvec_valid = false;
  }

  void ByteDone(uint32_t byte) {
    ++total_bytes;
    if (escape_next) {
      escape_next = false;
      return;
    }
    if (expecting == 0) {
      if (byte == 0x0Cu) {
        escape_next = true;
      } else if (byte < 0x80u) {
        // Literal byte: ignored
      } else if (byte <= 0xCFu) {
        TokenDone(byte - 0x80u);  // Band 1
      } else {
        pfx0 = byte;
        expecting = 1;
      }
      return;
    }
    if (expecting == 1) {
      if (byte >= 0x80u && byte <= 0xCFu) {
        TokenDone(80u + (pfx0 - 0xD0u) * 80u + (byte - 0x80u));  // Band 2
      } else if (byte >= 0xD0u) {
        pfx1 = byte;
        expecting = 2;
      } else {
        ParseAbort();
      }
      return;
    }
    // expecting == 2
    if (byte >= 0x80u && byte <= 0xCFu) {
      if (pfx0 >= 0xF0u && pfx1 <= 0xEFu) {
        TokenDone(3920u + 80u * (32u * (pfx0 - 0xF0u) + (pfx1 - 0xD0u)) + (byte - 0x80u));  // Band 3
      } else if (pfx0 >= 0xF0u) {
        TokenDone(44880u + (pfx0 - 0xF0u) * 1280u + (pfx1 - 0xF0u) * 80u + (byte - 0x80u));  // Band 4
      } else {
        ParseAbort();
      }
    } else {
      ParseAbort();
    }
    expecting = 0;
  }
};

WordLabel::WordLabel() : state_(std::make_unique<State>()) {
  state_->Init();
}

WordLabel::~WordLabel() {
  if (state_) state_->Free();
}

void WordLabel::Reset() {
  if (state_) state_->ResetParams();
}

float WordLabel::Forward() {
  State* s = state_.get();
  s->live = false;
  if (s->nbits_in == 0) return 0.0f;
  if (s->escape_next) return 0.0f;
  const int bp = s->expecting;
  if (bp == 0 && s->first_bit == 0) return 0.0f;  // Literal byte
  if (!s->cvec_valid) s->BuildCvec();
  const uint32_t nid = NodeId(bp, s->pfx0, s->pfx1, s->nodeix);
  const float* nv = s->nd + (size_t)nid * kD;
  float zl = s->nb[nid];
  for (int k = 0; k < kD; ++k) zl += s->cvec[k] * nv[k];
  if (zl > kLogitClamp) zl = kLogitClamp;
  else if (zl < -kLogitClamp) zl = -kLogitClamp;
  s->nid = nid;
  s->plbl = SquashF(zl);
  s->live = true;
  return zl;
}

void WordLabel::Perceive(int bit) {
  State* s = state_.get();
  const int y = bit & 1;
  if (s->live) {
    const float g = s->plbl - (float)y;
    float* nv = s->nd + (size_t)s->nid * kD;
    const float decay = s->GetDecay();
    const float ge = (kEtaN * decay) * g;
    for (int k = 0; k < kD; ++k) {
      s->gacc[k] += g * nv[k];
      nv[k] -= ge * s->cvec[k];
    }
    s->nb[s->nid] -= ge;
    s->nodeix = s->nodeix * 2u + (uint32_t)y;
    s->live = false;
  }
  if (s->nbits_in == 0) s->first_bit = y;
  s->breg = (s->breg << 1) | (uint32_t)y;
  ++s->nbits_in;
  if (s->nbits_in < 8) return;
  const uint32_t byte = s->breg & 0xFFu;
  s->breg = 1;
  s->nbits_in = 0;
  s->nodeix = 1;
  s->ByteDone(byte);
}
