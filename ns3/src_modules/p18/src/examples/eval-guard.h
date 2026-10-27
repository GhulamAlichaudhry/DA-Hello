// eval-guard.h — PORT A amend1 §A6(d) evaluation-seed guard (p3, 2026-10-16).
//
// Rule: a seed >= kEvalSeedMin (20261201) is an EVALUATION seed. The binary
// refuses it (exit kEvalGuardExit, before placement / protocol build /
// simulation) unless ALL of:
//   (1) --eval=1
//   (2) --evalAddendum=<path> names a readable file (the hashed tuning addendum)
//   (3) the registry file (--evalAddendumRegistry, default
//       ${OUT}/portA_eval_addendum.sha256) holds a 64-hex SHA-256 as its
//       first token, written once when the addendum is frozen, and
//   (4) sha256(<addendum file bytes>) equals that recorded value.
// Also refused: --eval=1 with a seed < kEvalSeedMin (no mislabelled eval runs).
// NOTE: the bound is the literal frozen text (numeric >= 20261201), so the burned
// diagnostic blocks 20269101-20269305 also fall under the guard (refused w/o --eval=1).
// Default path (eval=0, seed < kEvalSeedMin): accepted silently, no output.
// Header-only and free of ns-3 so the unit check can test it in isolation.
#pragma once
#include <array>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace portaguard {

constexpr uint32_t kEvalSeedMin = 20261201u;
constexpr int kEvalGuardExit = 4; // 3 is reserved for the oracle-leak guard
inline const char *kDefaultRegistry = "${OUT}/portA_eval_addendum.sha256";

// Minimal FIPS 180-4 SHA-256 (checked against NIST vectors in eval-guard-unit-check).
inline std::string Sha256Hex (const std::string &data)
{
  static const uint32_t K[64] = {
      0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
      0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
      0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
      0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
      0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
      0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
      0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
      0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
  uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                   0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
  std::vector<uint8_t> m (data.begin (), data.end ());
  const uint64_t bitLen = static_cast<uint64_t> (m.size ()) * 8u;
  m.push_back (0x80);
  while (m.size () % 64 != 56)
    {
      m.push_back (0);
    }
  for (int i = 7; i >= 0; --i)
    {
      m.push_back (static_cast<uint8_t> (bitLen >> (8 * i)));
    }
  auto rotr = [] (uint32_t x, int n) { return (x >> n) | (x << (32 - n)); };
  for (size_t off = 0; off < m.size (); off += 64)
    {
      uint32_t w[64];
      for (int t = 0; t < 16; ++t)
        {
          w[t] = (uint32_t (m[off + 4 * t]) << 24) | (uint32_t (m[off + 4 * t + 1]) << 16)
                 | (uint32_t (m[off + 4 * t + 2]) << 8) | uint32_t (m[off + 4 * t + 3]);
        }
      for (int t = 16; t < 64; ++t)
        {
          const uint32_t s0 = rotr (w[t - 15], 7) ^ rotr (w[t - 15], 18) ^ (w[t - 15] >> 3);
          const uint32_t s1 = rotr (w[t - 2], 17) ^ rotr (w[t - 2], 19) ^ (w[t - 2] >> 10);
          w[t] = w[t - 16] + s0 + w[t - 7] + s1;
        }
      uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
      for (int t = 0; t < 64; ++t)
        {
          const uint32_t S1 = rotr (e, 6) ^ rotr (e, 11) ^ rotr (e, 25);
          const uint32_t ch = (e & f) ^ (~e & g);
          const uint32_t t1 = hh + S1 + ch + K[t] + w[t];
          const uint32_t S0 = rotr (a, 2) ^ rotr (a, 13) ^ rotr (a, 22);
          const uint32_t mj = (a & b) ^ (a & c) ^ (b & c);
          const uint32_t t2 = S0 + mj;
          hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
        }
      h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }
  static const char *hex = "0123456789abcdef";
  std::string out;
  for (uint32_t v : h)
    {
      for (int i = 7; i >= 0; --i)
        {
          out.push_back (hex[(v >> (4 * i)) & 0xf]);
        }
    }
  return out;
}

inline bool ReadFileBytes (const std::string &path, std::string &out)
{
  std::ifstream f (path, std::ios::binary);
  if (!f)
    {
      return false;
    }
  std::ostringstream ss;
  ss << f.rdbuf ();
  out = ss.str ();
  return true;
}

struct EvalGuardResult
{
  bool accept = true;
  bool evalSeed = false;
  std::string reason;      // empty on the silent default path
  std::string addendumSha; // computed, when an addendum was read
};

inline EvalGuardResult CheckEvalGuard (uint32_t seed, bool eval, const std::string &addendumPath,
                                       const std::string &registryPath)
{
  EvalGuardResult r;
  r.evalSeed = seed >= kEvalSeedMin;
  if (!r.evalSeed)
    {
      if (eval)
        {
          r.accept = false;
          r.reason = "--eval=1 with a non-evaluation seed (< 20261201)";
        }
      return r;
    }
  r.accept = false;
  if (!eval)
    {
      r.reason = "evaluation seed (>= 20261201) without --eval=1";
      return r;
    }
  if (addendumPath.empty ())
    {
      r.reason = "--eval=1 but no --evalAddendum=<path>";
      return r;
    }
  std::string bytes;
  if (!ReadFileBytes (addendumPath, bytes))
    {
      r.reason = "--evalAddendum not readable: " + addendumPath;
      return r;
    }
  r.addendumSha = Sha256Hex (bytes);
  std::string reg;
  if (registryPath.empty () || !ReadFileBytes (registryPath, reg))
    {
      r.reason = "addendum registry not readable: " + registryPath;
      return r;
    }
  std::istringstream rs (reg);
  std::string want;
  rs >> want;
  bool hexOk = want.size () == 64;
  for (char &ch : want)
    {
      ch = static_cast<char> (std::tolower (static_cast<unsigned char> (ch)));
      hexOk = hexOk && std::isxdigit (static_cast<unsigned char> (ch));
    }
  if (!hexOk)
    {
      r.reason = "addendum registry does not start with a 64-hex sha256: " + registryPath;
      return r;
    }
  if (want != r.addendumSha)
    {
      r.reason = "addendum sha256 " + r.addendumSha + " != registry " + want;
      return r;
    }
  r.accept = true;
  r.reason = "evaluation seed accepted: --eval=1 and addendum sha256 matches registry";
  return r;
}

} // namespace portaguard
