// Port A unit tests for model/acoustic-medium + acoustic-phy, driven by the ns-3.41
// core scheduler (the same Simulator the scenario uses). Exit code != 0 on failure.
#include "ns3/core-module.h"
#include "ns3/acoustic-medium.h"
#include "ns3/acoustic-phy.h"

#include <cstdio>
#include <memory>
#include <vector>

using namespace ns3;
using namespace dahello;

static int g_fail = 0, g_pass = 0;
#define CHECK(cond, ...)                                                                           \
  do {                                                                                             \
    if (cond) { ++g_pass; std::printf ("PASS "); }                                                 \
    else { ++g_fail; std::printf ("FAIL "); }                                                      \
    std::printf (__VA_ARGS__);                                                                     \
    std::printf ("\n");                                                                            \
  } while (0)

struct Rig
{
  AcousticPhy phy;
  std::vector<std::array<double, 3>> p;
  std::unique_ptr<AcousticMedium> m;
  std::vector<std::vector<std::pair<double, MediumFrame>>> got; // per receiver (time, frame)
  Rig (AcousticPhyConfig pc, MediumConfig mc, std::vector<std::array<double, 3>> pos)
      : phy (pc), p (pos)
  {
    Simulator::Destroy ();
    got.resize (p.size ());
    m = std::make_unique<AcousticMedium> (
        phy, mc, p.size (),
        [] (double d, std::function<void ()> f) { Simulator::Schedule (Seconds (d), f); },
        [] { return Simulator::Now ().GetSeconds (); });
    m->pos = [this] (uint32_t i, double &x, double &y, double &z) { x = p[i][0]; y = p[i][1]; z = p[i][2]; };
    m->linkSnrDb = [this] (uint32_t s, uint32_t r) {
      const double d = std::sqrt (std::pow (p[s][0] - p[r][0], 2) + std::pow (p[s][1] - p[r][1], 2)
                                  + std::pow (p[s][2] - p[r][2], 2));
      return phy.formulaSnrDb (d);
    };
    m->onDecode = [this] (uint32_t r, const MediumFrame &f, double) {
      got[r].push_back ({Simulator::Now ().GetSeconds (), f});
    };
  }
  void sendAt (double t, uint32_t n, uint8_t kind = kFrameData, int bits = 400)
  {
    Simulator::Schedule (Seconds (t), [this, n, kind, bits] {
      MediumFrame f;
      f.kind = kind;
      f.bits = bits;
      m->enqueue (n, f);
    });
  }
  void run (double T) { Simulator::Stop (Seconds (T)); Simulator::Run (); }
};

static AcousticPhyConfig pc0 () { return AcousticPhyConfig (); } // frozen primary P-5k
static MediumConfig mc (MacMode m) { MediumConfig c; c.mac = m; c.seed = 7; return c; }

int
main (int argc, char **argv)
{
  // ---- PHY constants (prereg section 1) ----
  {
    AcousticPhy ph (pc0 ());
    CHECK (std::fabs (ph.nlDb () - 69.79) < 0.01, "NL(5 kHz) = %.3f dB (prereg 69.79)", ph.nlDb ());
    CHECK (std::fabs (ph.snrReqDb () - 11.79) < 0.01, "snr_req = %.3f dB (prereg 11.79)", ph.snrReqDb ());
    CHECK (std::fabs (ph.slDb () - 134.60) < 0.01, "SL = %.3f dB (prereg 134.60)", ph.slDb ());
    CHECK (std::fabs (thorpDbPerKm (24.0) - 5.69) < 0.01, "Thorp(24 kHz) = %.3f dB/km", thorpDbPerKm (24.0));
    CHECK (std::fabs (ph.formulaSnrDb (1200.0) - ph.snrReqDb ()) < 1e-9, "SNR(R_op) = snr_req");
    CHECK (std::fabs (ph.psucc (std::pow (10, ph.snrReqDb () / 10), 400) - 0.9) < 1e-9, "PER(snr_req, 400 b) = 0.1");
    AcousticPhyConfig s = pc0 ();
    s.bKhz = 2.0; s.rbBps = 1000.0;
    AcousticPhy p1 (s);
    CHECK (std::fabs (p1.nlDb () - 65.77) < 0.01 && std::fabs (p1.snrReqDb () - 8.78) < 0.01 && std::fabs (p1.slDb () - 127.57) < 0.01,
           "P-1k: NL %.2f snr_req %.2f SL %.2f (prereg 65.77 / 8.78 / 127.57)", p1.nlDb (), p1.snrReqDb (), p1.slDb ());
    CHECK (std::fabs (ph.txTime (400) - 0.13) < 1e-12, "T(400 b) = 50 ms + 80 ms");
  }
  // ---- 1. isolated link: delivery = psucc, delivery time = t0 + d/c + T ----
  {
    const double d = 1100.0;
    Rig g (pc0 (), mc (MacMode::Aloha), {{0, 0, 100}, {d, 0, 100}});
    const int n = 20000;
    for (int k = 0; k < n; ++k) g.sendAt (k * 2.0, 0);
    g.run (n * 2.0 + 5);
    const double ps = g.phy.psucc (std::pow (10, g.phy.formulaSnrDb (d) / 10), 400);
    const double rate = double (g.got[1].size ()) / n;
    const double se = std::sqrt (ps * (1 - ps) / n);
    CHECK (std::fabs (rate - ps) < 4 * se, "isolated link d=%.0f: delivered %.4f vs psucc %.4f (4 se %.4f)", d, rate, ps, 4 * se);
    const double t = g.got[1].front ().first;
    CHECK (std::fabs (t - (d / 1500.0 + 0.13)) < 1e-9 || g.got[1].front ().second.txId != 1,
           "first decode at %.9f s = d/c + T = %.9f s", t, d / 1500.0 + 0.13);
    const double ttx = g.m->tTx (0), trx = g.m->tRx (1);
    CHECK (std::fabs (ttx - n * 0.13) < 1e-6, "energy: tx time %.6f = %d x 0.13 s", ttx, n);
    CHECK (std::fabs (trx - n * 0.13) < 1e-6, "energy: rx (locked) time %.6f = %d x 0.13 s", trx, n);
    const double T = Simulator::Now ().GetSeconds ();
    PowerProfile pw = powerProfile ("s2c");
    const double e = g.m->energyUsed (0, pw);
    CHECK (std::fabs (e - (8.0 * ttx + 0.285 * (T - ttx))) < 1e-6, "energy: E_tx-node = 8*Ttx + 0.285*(T-Ttx) = %.6f J", e);
    CHECK (std::fabs (g.m->tIdle (1) + g.m->tRx (1) + g.m->tTx (1) - T) < 1e-9, "energy: idle+rx+tx = alive time (rx node)");
  }
  // ---- 2. collision of two equal-power overlapping frames; no loss when separated ----
  {
    Rig g (pc0 (), mc (MacMode::Aloha), {{0, 0, 100}, {800, 0, 100}, {-800, 0, 100}});
    g.sendAt (0.0, 1);
    g.sendAt (0.05, 2); // overlap 80 ms at node 0
    g.sendAt (10.0, 1);
    g.sendAt (20.0, 2);
    g.run (30);
    int first = 0, later = 0;
    for (auto &e : g.got[0]) (e.first < 5 ? first : later)++;
    CHECK (first == 0, "equal-power overlap at receiver: %d of 2 decoded (expect 0: SINR ~0 dB)", first);
    CHECK (later == 2 || later == 1, "separated frames: %d of 2 decoded (PER at 800 m small)", later);
    CHECK (g.m->stats ().collByKind[kFrameData][0] >= 1 && g.m->stats ().busyMissByKind[kFrameData] >= 1,
           "stats: collision %llu, busy-miss %llu", (unsigned long long) g.m->stats ().collByKind[kFrameData][0],
           (unsigned long long) g.m->stats ().busyMissByKind[kFrameData]);
  }
  // ---- 3. capture only through SINR; locks on first arrival ----
  {
    // A at 200 m (strong), B at 1150 m (weak). SNR_A - SNR_B ~ 16 dB.
    Rig g (pc0 (), mc (MacMode::Aloha), {{0, 0, 100}, {200, 0, 100}, {1150, 0, 100}});
    const int n = 2000;
    int strongFirst = 0, weakFirst = 0;
    for (int k = 0; k < n; ++k)
      {
        const double t = k * 10.0;
        // strong first: A's frame reaches 0 at t+0.133; B's at t+0.767+0.05 overlaps
        g.sendAt (t, 1);
        g.sendAt (t - 0.6, 2);
      }
    g.run (n * 10.0 + 5);
    for (auto &e : g.got[0]) (e.second.src == 1 ? strongFirst : weakFirst)++;
    const double sA = g.phy.formulaSnrDb (200), sB = g.phy.formulaSnrDb (1150);
    const double ps = g.phy.psucc (std::pow (10, sA / 10) / (1 + std::pow (10, sB / 10)), 400);
    CHECK (std::fabs (double (strongFirst) / n - ps) < 0.03 && weakFirst == 0,
           "strong-first overlap: strong decoded %.3f (SINR model %.3f), weak 0 (got %d)", double (strongFirst) / n, ps, weakFirst);
    Rig h (pc0 (), mc (MacMode::Aloha), {{0, 0, 100}, {200, 0, 100}, {1150, 0, 100}});
    for (int k = 0; k < n; ++k)
      {
        const double t = k * 10.0;
        h.sendAt (t, 2);        // weak arrives at t + 0.767
        h.sendAt (t + 0.7, 1);  // strong arrives at t + 0.833, while locked on weak
      }
    h.run (n * 10.0 + 5);
    int s2 = 0, w2 = 0;
    for (auto &e : h.got[0]) (e.second.src == 1 ? s2 : w2)++;
    CHECK (s2 == 0 && w2 == 0, "weak-first overlap: receiver locked on weak, strong lost (no capture beyond SINR): strong %d weak %d", s2, w2);
  }
  // ---- 4. half-duplex ----
  {
    Rig g (pc0 (), mc (MacMode::Aloha), {{0, 0, 100}, {600, 0, 100}});
    g.sendAt (0.0, 1);       // arrives at 0: [0.4, 0.53]
    g.sendAt (0.35, 0);      // node 0 transmitting [0.35, 0.48]: arrival during tx -> hd loss
    g.sendAt (10.0, 1);      // arrives [10.4, 10.53]
    g.sendAt (10.45, 0);     // node 0 starts tx during reception -> abort
    g.run (20);
    const auto &s = g.m->stats ();
    CHECK (g.got[0].empty () && s.hdLossByKind[kFrameData] == 1 && s.hdAbortByKind[kFrameData] == 1,
           "half-duplex: decoded %zu, hd_loss %llu, hd_abort %llu", g.got[0].size (),
           (unsigned long long) s.hdLossByKind[kFrameData], (unsigned long long) s.hdAbortByKind[kFrameData]);
    CHECK (std::fabs (g.m->tRx (0) - 0.05) < 1e-9, "aborted reception billed 50 ms rx time (got %.6f)", g.m->tRx (0));
  }
  // ---- 5. delay-aware carrier sense ----
  {
    MediumConfig c = mc (MacMode::Cs);
    Rig g (pc0 (), c, {{0, 0, 100}, {1050, 0, 100}});
    bool busyEarly = true, busyLate = false;
    g.sendAt (0.0, 0); // reaches node 1 at 0.7
    Simulator::Schedule (Seconds (0.5), [&] { busyEarly = g.m->sensedBusy (1); });
    Simulator::Schedule (Seconds (0.75), [&] { busyLate = g.m->sensedBusy (1); });
    g.run (5);
    CHECK (!busyEarly && busyLate, "delay-aware CS: busy at 0.5 s = %d (signal not arrived), at 0.75 s = %d", busyEarly, busyLate);
    c.csOracle = true;
    Rig h (pc0 (), c, {{0, 0, 100}, {1050, 0, 100}});
    bool oracleEarly = false;
    h.sendAt (0.0, 0);
    Simulator::Schedule (Seconds (0.05), [&] { oracleEarly = h.m->sensedBusy (1); });
    h.run (5);
    CHECK (oracleEarly, "oracle CS (test-only contrast) reports busy at 0.05 s");
    // CS defers: node 1 enqueues at 0.75 while sensing node 0's frame -> backs off, transmits later
    MediumConfig c2 = mc (MacMode::Cs);
    Rig k (pc0 (), c2, {{0, 0, 100}, {1050, 0, 100}, {2100, 0, 100}});
    k.sendAt (0.0, 0);
    k.sendAt (0.75, 1);
    k.run (10);
    CHECK (k.m->stats ().csBusy >= 1 && k.m->lastTxStart (1) > 0.83 - 1e-9, "CS back-off: busy %llu, node 1 tx at %.3f s (after arrival end 0.83 s)",
           (unsigned long long) k.m->stats ().csBusy, k.m->lastTxStart (1));
  }
  // ---- 6. interference floor and lock threshold ----
  {
    // node 2 at 5 km: RL below NL-10 dB -> ignored, no effect on 1->0
    Rig g (pc0 (), mc (MacMode::Aloha), {{0, 0, 100}, {300, 0, 100}, {9000, 0, 100}});
    g.sendAt (0.0, 1);
    g.sendAt (0.0, 2);
    g.run (10);
    const double snr2 = g.phy.formulaSnrDb (9000);
    CHECK (snr2 < g.phy.floorDb () && g.got[0].size () == 1, "below-floor interferer (SNR %.1f dB) ignored: decoded %zu", snr2, g.got[0].size ());
  }
  // ---- 7. queue cap and back-off drop ----
  {
    MediumConfig c = mc (MacMode::Cs);
    c.queueCap = 3;
    Rig g (pc0 (), c, {{0, 0, 100}, {500, 0, 100}});
    int drops = 0;
    g.m->onMacDrop = [&] (uint32_t, const MediumFrame &, int why) { drops += (why == 0); };
    Simulator::Schedule (Seconds (0.0), [&] {
      for (int k = 0; k < 6; ++k) { MediumFrame f; f.kind = kFrameData; g.m->enqueue (0, f); }
    });
    g.run (20);
    CHECK (drops == 3 && g.m->stats ().txByKind[kFrameData] == 3, "queue cap 3: dropped %d, sent %llu", drops,
           (unsigned long long) g.m->stats ().txByKind[kFrameData]);
  }
  // ---- 8. kill mid-transmission freezes energy ----
  {
    Rig g (pc0 (), mc (MacMode::Aloha), {{0, 0, 100}, {500, 0, 100}});
    g.sendAt (1.0, 0);
    Simulator::Schedule (Seconds (1.05), [&] { g.m->kill (0); });
    g.run (10);
    CHECK (std::fabs (g.m->tTx (0) - 0.05) < 1e-9 && std::fabs (g.m->tAlive (0) - 1.05) < 1e-9,
           "kill at 1.05 s mid-tx: tx %.6f s, alive %.6f s", g.m->tTx (0), g.m->tAlive (0));
  }
  std::printf ("MEDIUM_UNIT pass=%d fail=%d\n", g_pass, g_fail);
  Simulator::Destroy ();
  return g_fail ? 1 : 0;
}
