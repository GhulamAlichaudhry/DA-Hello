// Port A (2026-10-12): shared acoustic medium + MAC for the ns-3.41 scenario.
// Finite transmission time, per-receiver propagation delay, additive
// interference (worst-case SINR over the packet), BFSK PER draw, half-duplex
// (tx blocks rx; starting a tx aborts an ongoing rx), lock-on-first-arrival,
// delay-aware carrier sense (CS sees only signal that has physically arrived),
// pure ALOHA / CS-ALOHA / slotted ALOHA, FIFO queue, exact per-state energy time.
// Pure C++: the scenario injects the scheduler (ns-3 Simulator::Schedule) and the
// true positions; nothing here is visible to routing hosts or HELLO policies.
// Reference only: the void v2 kernel (src/v2/uwsim.cc, commit 42e8692).
#ifndef DAHELLO_ACOUSTIC_MEDIUM_H
#define DAHELLO_ACOUSTIC_MEDIUM_H

#include "acoustic-phy.h"

#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <random>
#include <vector>

namespace dahello {

enum FrameKind : uint8_t
{
  kFrameData = 0,
  kFrameAck = 1,
  kFrameHello = 2,
  kFrameBeacon = 3,
  kFrameKinds = 4
};
const char *frameKindName (int k);

// Event times pass through ns-3 Time (1 ns ticks), so a callback scheduled for
// t can fire a fraction of a ns early. Comparisons against "busy until t" use
// this tolerance (1 us, far below any frame or propagation time). Bug fix
// 2026-10-12: without it a node could reschedule attempt() at the same tick forever.
constexpr double kTimeEps = 1e-6;

enum class MacMode
{
  Ideal,  // no interference, no half-duplex, no locking: each arrival decided alone (attribution only)
  Aloha,  // pure ALOHA
  Cs,     // ALOHA with delay-aware carrier sense + exponential back-off
  Slotted // slotted ALOHA, slot = T(slotBits) + rangeM / c
};
MacMode parseMacMode (const std::string &s);

struct MediumFrame
{
  uint64_t txId = 0;
  uint32_t src = 0;
  int32_t dst = -1; // -1 broadcast (link-layer address; every decoder still gets onDecode)
  uint8_t kind = kFrameHello;
  uint8_t cause = 0;
  int bits = 400;
  std::shared_ptr<void> payload; // opaque, owned by the scenario
};

struct MediumConfig
{
  MacMode mac = MacMode::Cs;
  int queueCap = 20;
  int boMaxAttempts = 5;
  double boBase = -1.0; // <0: T(dataBits) + rangeM / c
  int dataBits = 400;
  int slotBits = 400;
  double rangeM = 1200.0;
  bool csOracle = false; // TEST ONLY: CS sees any in-range tx instantly (no propagation)
  uint64_t seed = 1;
};

struct MediumStats
{
  uint64_t txByKind[kFrameKinds] = {0, 0, 0, 0};
  uint64_t bitsByKind[kFrameKinds] = {0, 0, 0, 0};
  uint64_t okByKind[kFrameKinds] = {0, 0, 0, 0};
  // locked receptions that failed with interference present and the interference-free
  // success probability higher: collisions. [rx kind][1 if any interferer was control]
  uint64_t collByKind[kFrameKinds][2] = {{0, 0}, {0, 0}, {0, 0}, {0, 0}};
  uint64_t noiseByKind[kFrameKinds] = {0, 0, 0, 0};
  uint64_t busyMissByKind[kFrameKinds] = {0, 0, 0, 0}; // arrived while locked on another frame
  uint64_t hdLossByKind[kFrameKinds] = {0, 0, 0, 0};   // arrived while transmitting
  uint64_t hdAbortByKind[kFrameKinds] = {0, 0, 0, 0};  // own tx aborted this reception
  uint64_t rxStarts = 0, csAttempts = 0, csBusy = 0, macDrop = 0, qDrop = 0, unwanted = 0;
  double busyTime = 0.0; // sum over tx of airtime (s)
};

class AcousticMedium
{
public:
  using Sched = std::function<void (double delay, std::function<void ()>)>;
  AcousticMedium (const AcousticPhy &phy, MediumConfig cfg, uint32_t n, Sched sched,
                  std::function<double ()> now);

  // scenario-provided physics (true state; never passed to hosts)
  std::function<void (uint32_t, double &, double &, double &)> pos;
  std::function<double (uint32_t s, uint32_t r)> linkSnrDb; // noise-only SNR of s->r (dB)
  // scenario hooks
  std::function<void (uint32_t tx, MediumFrame &)> onTxStart; // fill header at tx time
  std::function<void (uint32_t tx, const MediumFrame &)> onTxEnd;
  std::function<void (uint32_t rx, const MediumFrame &, double snrDb)> onDecode;
  std::function<bool (uint32_t n, const MediumFrame &)> wanted;              // false: discard at head
  std::function<void (uint32_t n, const MediumFrame &, int why)> onMacDrop; // 0 queue full, 1 back-off limit

  void enqueue (uint32_t n, MediumFrame f, bool front = false);
  void kill (uint32_t n);           // node removed: stops tx/rx, freezes energy time
  bool alive (uint32_t n) const { return nd_[n].alive; }
  bool sensedBusy (uint32_t n) const;
  size_t queueLen (uint32_t n) const { return nd_[n].q.size (); }
  bool transmitting (uint32_t n) const { return nd_[n].txUntil > now_ () + kTimeEps; }
  double lastTxStart (uint32_t n) const { return nd_[n].lastTx; }

  // exact state time up to now (s)
  double tTx (uint32_t n) const;
  double tRx (uint32_t n) const;
  double tAlive (uint32_t n) const;
  double tIdle (uint32_t n) const { return tAlive (n) - tTx (n) - tRx (n); }
  double energyUsed (uint32_t n, const PowerProfile &p) const
  {
    return p.tx * tTx (n) + p.rx * tRx (n) + p.idle * tIdle (n);
  }
  // per-kind tx time split (for control vs data energy)
  double tTxKind (uint32_t n, int k) const { return nd_[n].ttxKind[k]; }
  double tRxKind (uint32_t n, int k) const { return nd_[n].trxKind[k]; }

  const MediumStats &stats () const { return st_; }
  const AcousticPhy &phy () const { return phy_; }
  const MediumConfig &config () const { return cfg_; }

private:
  struct Arr
  {
    uint64_t tx;
    double t0, t1, pw;
    uint8_t kind;
  };
  struct Node
  {
    bool alive = true;
    double tDead = 0.0;
    double txUntil = -1.0, lastTx = -1e18, ttx = 0.0, trx = 0.0;
    uint8_t curTxKind = 0;
    double ttxKind[kFrameKinds] = {0, 0, 0, 0}, trxKind[kFrameKinds] = {0, 0, 0, 0};
    bool locked = false;
    uint64_t lockTx = 0;
    double lockT0 = 0, lockT1 = 0, lockPw = 0;
    MediumFrame lockFrame;
    std::vector<Arr> arr;
    std::deque<MediumFrame> q;
    bool macBusy = false;
    int bo = 0;
  };
  double dist (uint32_t a, uint32_t b) const;
  void at (double t, std::function<void ()> f);
  void attempt (uint32_t n);
  void transmit (uint32_t n, MediumFrame f);
  void txEnd (uint32_t n, MediumFrame f);
  void rxStart (uint32_t r, uint64_t id, const MediumFrame &f, double a0, double a1, double pw);
  void rxEnd (uint32_t r, uint64_t id);
  void endLock (Node &R, double tEnd);
  void maxInterf (const Node &R, uint64_t id, double a0, double a1, double &imax, bool &ctrl) const;
  void prune (Node &R);

  const AcousticPhy &phy_;
  MediumConfig cfg_;
  std::vector<Node> nd_;
  Sched sched_;
  std::function<double ()> now_;
  double lockLin_, csLin_, floorLin_;
  uint64_t txSeq_ = 0;
  std::mt19937_64 rPer_, rMac_;
  MediumStats st_;
};

} // namespace dahello

#endif
