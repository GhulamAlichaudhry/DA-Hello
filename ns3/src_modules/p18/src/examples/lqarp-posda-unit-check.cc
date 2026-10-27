// lqarp-posda-unit-check.cc — standalone unit test of the LQARP position-aware DA test (p6).
// Pure function tests; no simulation, no scenario, no seeds.
#include "ns3/lqarp-posda.h"
#include <algorithm>
#include <cstdio>
#include <random>
#include <string>
using namespace dahello;
static int total = 0, fails = 0;
static void Check (bool ok, const std::string &name)
{
  ++total;
  if (!ok) ++fails;
  std::printf ("%s %s\n", ok ? "PASS" : "FAIL", name.c_str ());
}
static PosDaRec Rec (uint32_t id, double x, double y, double z, double e = 5000, double V = 0, double n = 3)
{
  PosDaRec r; r.id = id; r.x = x; r.y = y; r.z = z; r.energy = e; r.V = V; r.number = n; r.fresh = true; return r;
}
static PosDaInput In (PosDaPose adv, PosDaPose now, std::vector<PosDaRec> recs, uint32_t self = 10)
{
  PosDaInput in; in.self = self; in.adv = adv; in.now = now; in.eSelf = 5000; in.vSelf = 0; in.numSelf = 3; in.recs = recs; return in;
}
// Brute-force reference: build i's estimated candidate set and full ordering under adv and now.
static bool BruteFire (const PosDaInput &in, const PosDaParams &q)
{
  for (const auto &i : in.recs)
    {
      if (!i.fresh || i.id == in.self) continue;
      auto setOf = [&] (const PosDaPose &pj) {
        std::vector<std::pair<uint32_t, PosDaPose>> c;
        if (PosDaMember (i, pj, q.rRangeM, {})) c.push_back ({in.self, pj});
        for (const auto &k : in.recs)
          if (k.fresh && k.id != i.id && k.id != in.self && PosDaMember (i, {k.x, k.y, k.z}, q.rRangeM, {}))
            c.push_back ({k.id, {k.x, k.y, k.z}});
        return c;
      };
      auto A = setOf (in.adv), B = setOf (in.now);
      const bool inA = std::any_of (A.begin (), A.end (), [&] (auto &p) { return p.first == in.self; });
      const bool inB = std::any_of (B.begin (), B.end (), [&] (auto &p) { return p.first == in.self; });
      if (inA != inB) return true;
      if (!inA) continue;
      double eSum = in.eSelf; int cnt = 1;
      for (auto &p : A) if (p.first != in.self) for (auto &k : in.recs) if (k.id == p.first) { eSum += k.energy; ++cnt; }
      const double eAvg = eSum / cnt;
      auto score = [&] (uint32_t id, double z) {
        if (id == in.self) return PosDaScore (in.eSelf, eAvg, i.z, z, in.numSelf, in.vSelf, q);
        for (auto &k : in.recs) if (k.id == id) return PosDaScore (k.energy, eAvg, i.z, k.z, k.number, k.V, q);
        return 0.0;
      };
      // order of i's candidates (descending score, ties smaller id) — compare j's relative position to each k
      for (auto &p : A)
        {
          if (p.first == in.self) continue;
          const double sk = score (p.first, 0);
          const bool pa = PosDaPref (score (in.self, in.adv.z), sk, in.self, p.first);
          const bool pb = PosDaPref (score (in.self, in.now.z), sk, in.self, p.first);
          if (pa != pb) return true;
        }
    }
  return false;
}
int main ()
{
  PosDaParams q; q.rRangeM = 1000.0; q.eIni = 10000.0; q.gamma = 0.8; q.margin = 0.0;
  // formula identity with LqarpProtocol statics
  {
    const double s = PosDaScore (6000, 5000, 900, 300, 4, 2.5, q);
    const double ref = LqarpProtocol::cEne (6000, 5000, 1e4) * LqarpProtocol::cDel (6000, 1e4, 900, 300) * LqarpProtocol::cNei (4) + 0.8 * 2.5;
    Check (s == ref, "score = cEne*cDel*cNei + gamma*V (LqarpProtocol statics)");
  }
  PosDaRec I = Rec (1, 0, 0, 1000);
  // membership
  auto r = EvaluateLqarpPosDa (In ({0, 0, 400}, {1200, 0, 400}, {I}), q);
  Check (r.membership && !r.rank && r.memI == 1, "membership LEAVE by range (dist 600 -> 1281 m) fires");
  r = EvaluateLqarpPosDa (In ({1200, 0, 400}, {0, 0, 400}, {I}), q);
  Check (r.membership && r.memI == 1, "membership ENTER by range fires");
  r = EvaluateLqarpPosDa (In ({0, 0, 400}, {0, 0, 1100}, {I}), q);
  Check (r.membership, "membership LEAVE by depth (j becomes deeper than i) fires");
  r = EvaluateLqarpPosDa (In ({0, 0, 400}, {100, 0, 420}, {I}), q);
  Check (!r.fire () && r.nUpstream == 1, "small move, single upstream, no co-candidate: no fire");
  r = EvaluateLqarpPosDa (In ({2000, 0, 400}, {2100, 0, 400}, {I}), q);
  Check (!r.fire () && r.nUpstream == 0, "out of range both poses (not upstream): no fire");
  {
    PosDaSnrFn snr = [] (const PosDaRec &, const PosDaPose &p) { return p.x < 500; };
    r = EvaluateLqarpPosDa (In ({0, 0, 400}, {600, 0, 400}, {I}), q, snr);
    Check (r.membership, "membership change via SNR-definedness callback fires");
  }
  // rank: k at z=500 held; j adv z=300 (shallower => larger c_del => better), now z=700 (worse)
  // Rank fixture: own energy 6000 = k's energy; a low-energy co-candidate L pulls Eavg below
  // 6000 so c_ene(j) = c_ene(k) > 0 and the order j vs k is decided by depth (c_del) alone.
  // j's c_nei uses numSelf = 3 = k's number. With equal energy/number/V only position matters.
  PosDaRec K = Rec (2, 550, 0, 500, 6000, 0, 3);   // >1 km from j: j never in K's own set
  PosDaRec L = Rec (3, 500, 0, 900, 1000, 0, 3);   // >1 km from j at both poses
  auto InR = [&] (PosDaPose a, PosDaPose b, std::vector<PosDaRec> recs, uint32_t self = 10) {
    auto in = In (a, b, recs, self); in.eSelf = 6000; return in; };
  r = EvaluateLqarpPosDa (InR ({-500, 0, 300}, {-500, 0, 700}, {I, K, L}), q);
  Check (r.rank && !r.membership && r.i == 1 && r.k == 2, "rank flip (j moves from ahead of k to behind k) fires");
  r = EvaluateLqarpPosDa (InR ({-500, 0, 700}, {-500, 0, 300}, {I, K, L}), q);
  Check (r.rank, "rank flip (behind -> ahead) fires");
  r = EvaluateLqarpPosDa (InR ({-500, 0, 300}, {-450, 0, 350}, {I, K, L}), q);
  Check (!r.fire () && r.nPairs == 2, "move that keeps j ahead of k: no fire");
  {
    PosDaParams qm = q; qm.margin = 1e9;
    r = EvaluateLqarpPosDa (InR ({-500, 0, 300}, {-500, 0, 700}, {I, K, L}), qm);
    Check (!r.fire (), "rank flip smaller than margin m: no fire");
  }
  // ties: k identical attributes at same depth as j's adv
  {
    PosDaRec Kt = Rec (12, 550, 0, 400, 6000, 0, 3);
    r = EvaluateLqarpPosDa (InR ({-500, 0, 400}, {-500, 0, 401}, {I, Kt, L}, /*self*/ 10), q);
    Check (r.rank, "tie at adv (j id 10 < k id 12 => j preferred), j slightly worse now: fires");
    PosDaRec Kt2 = Rec (5, 550, 0, 400, 6000, 0, 3);
    r = EvaluateLqarpPosDa (InR ({-500, 0, 400}, {-500, 0, 401}, {I, Kt2, L}, 10), q);
    Check (!r.fire (), "tie at adv (k id 5 < j id 10 => k preferred), j worse now: no fire");
    r = EvaluateLqarpPosDa (InR ({-500, 0, 401}, {-500, 0, 400}, {I, Kt2, L}, 10), q);
    Check (!r.fire (), "j improves only to a tie that k (smaller id) still wins: no fire");
  }
  // empty / stale / NaN
  r = EvaluateLqarpPosDa (In ({0, 0, 300}, {1500, 0, 300}, {}), q);
  Check (!r.fire () && r.nUpstream == 0, "empty held set: no fire");
  {
    PosDaRec St = I; St.fresh = false;
    r = EvaluateLqarpPosDa (In ({0, 0, 300}, {1500, 0, 300}, {St}), q);
    Check (!r.fire (), "stale (beyond T_nb) upstream ignored: no fire");
    PosDaRec Nn = I; Nn.x = std::nan ("");
    r = EvaluateLqarpPosDa (In ({0, 0, 300}, {1500, 0, 300}, {Nn}), q);
    Check (!r.fire () && r.nSkipPose == 1, "NaN held pose of i: record ignored and counted, no fire");
    r = EvaluateLqarpPosDa (In ({0, 0, 300}, {std::nan (""), 0, 300}, {I}), q);
    Check (!r.fire () && r.selfNonfinite, "NaN own pose: no fire, selfNonfinite");
    PosDaRec Ke = K; Ke.energy = std::nan ("");
    r = EvaluateLqarpPosDa (InR ({-500, 0, 300}, {-500, 0, 700}, {I, Ke, L}), q);
    Check (!r.fire () && r.nSkipEnergy == 1, "NaN held energy of k: excluded from rank (counted), no fire");
    PosDaRec Kv = K; Kv.V = std::nan (""); Kv.number = std::nan ("");
    r = EvaluateLqarpPosDa (InR ({-500, 0, 300}, {-500, 0, 700}, {I, Kv, L}), q);
    Check (r.nPairs >= 1 && r.nSkipScore == 0, "NaN held V/number of k -> 0 (LQARP heard convention), pair still evaluated (finite score)");
  }
  // own id among records is ignored
  r = EvaluateLqarpPosDa (In ({0, 0, 300}, {0, 0, 300}, {I, K, Rec (10, 0, 0, 300)}), q);
  Check (!r.fire (), "own id in held records ignored; adv == now: no fire");
  // property tests vs brute force (no randomness from any simulation seed: fixed std::mt19937(12345))
  {
    std::mt19937 g (12345);
    std::uniform_real_distribution<double> U (0, 2000), Z (0, 1500), E (1000, 10000), V (-5, 5);
    int agree = 0, fired = 0, sameNo = 0, same = 0; const int N = 20000;
    for (int t = 0; t < N; ++t)
      {
        std::vector<PosDaRec> recs;
        const int m = 1 + (int) (g () % 6);
        for (int a = 0; a < m; ++a) recs.push_back (Rec (20 + a, U (g), U (g), Z (g), E (g), V (g), (double) (g () % 6)));
        PosDaPose adv{U (g), U (g), Z (g)};
        PosDaPose now = (g () % 4 == 0) ? adv : PosDaPose{adv.x + U (g) / 4 - 250, adv.y + U (g) / 4 - 250, adv.z + Z (g) / 4 - 190};
        auto in = In (adv, now, recs, 10);
        const bool f = EvaluateLqarpPosDa (in, q).fire ();
        fired += f;
        if (f == BruteFire (in, q)) ++agree;
        if (adv.x == now.x && adv.y == now.y && adv.z == now.z) { ++same; if (!f) ++sameNo; }
      }
    Check (agree == N, "property: fire == brute-force (set or j-vs-k order changes), 20000 random cases (agree " + std::to_string (agree) + ", fired " + std::to_string (fired) + ")");
    Check (same > 0 && sameNo == same, "property: adv == now never fires (" + std::to_string (sameNo) + "/" + std::to_string (same) + " identical-pose cases silent)");
  }
  std::printf ("LQARP_POSDA_UNIT %d/%d %s\n", total - fails, total, fails ? "FAIL" : "PASS");
  return fails ? 1 : 0;
}
