/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Section 4.2 Scenario I (Stationary Anchor Baseline),
 * Scenario II (Linear Uniform Current Drift), and
 * Scenario III (Rankine eddy + horizontal Gauss-Markov).
 *
 * Four protocols on the same placement / traffic / ChannelGate:
 * LQARP, DA-HELLO, RLOR-inspired (no detour), Fixed-Periodic DA-HELLO
 * ablation. Scenario I: ConstantPositionMobilityModel, DA-HELLO is
 * the identity (beta_1=beta_2=0, epsilon_0=1e12, EKF off).
 * Scenario II: ConstantVelocityMobilityModel, v_cmd=0, uniform
 * horizontal v_drift on AUVs, sink fixed; DA-HELLO uses real
 * beta_1/beta_2 and the event trigger.
 * Scenario III: same DA-HELLO defaults as II; per-AUV velocity is
 * eddy mean + Gauss-Markov (sigma_z=0, SPEC Part 9).
 */

#include "ns3/lqarp-posda.h" // p6: LQARP position-aware DA test
#include "eval-guard.h" // p3: amend1 A6(d) evaluation-seed guard
#include "comparators.h" // p9: B6 / B8 / B9 comparator logic
#include "ns3/acoustic-energy.h"
#include "ns3/channel-gate.h"
#include "ns3/constant-velocity-mobility-model.h"
#include "ns3/core-module.h"
#include "ns3/dahello-protocol.h"
#include "ns3/lqarp-protocol.h"
#include "ns3/mobility-module.h"
#include "ns3/network-module.h"
#include "ns3/gauss-markov.h"
#include "ns3/rlor-inspired-protocol.h"
#include "ns3/qelar-protocol.h"
#include "ns3/saqr-protocol.h"
#include "ns3/marv-protocol.h"
#include "ns3/acoustic-medium.h"
#include "ns3/acoustic-phy.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <random>
#include <sstream>
#include <string>
#include <vector>

using namespace ns3;

namespace {
// p15 A2.2(c): eddy centre / core radius (m). Defaults = p14 hard-coded EddyConfig values
// (gauss-markov.h). Applied to the Pipeline eddy and to both diagnostic routines.
double g_eddyCx = 3000.0;
double g_eddyCy = 3000.0;
double g_eddyR = 2000.0;

// Port A: mutable so --topo=t1 can set the deep column; defaults = legacy slab.
double kVolumeXyM = 6000.0;
double kVolumeZM = 500.0;
constexpr double kMinAuvDepthM = 10.0; // keep AUVs strictly deeper than the sink
constexpr double kSinkDepthM = 5.0;    // kinematic hull depth; unique shallowest
constexpr double kMinSepM = 1.0;
constexpr uint32_t kMaxHops = 32;

// Step 22 seeds. Diagnostic and density sweeps must reuse these.
constexpr uint32_t kStep22Seeds[] = {20260921, 20260922, 20260923, 20260924,
                                     20260925};
constexpr uint32_t kStep22Ns[] = {20, 40, 60};
constexpr double kDiagSinkDepthsM[] = {5.0, 10.0, 50.0, 100.0};

constexpr double kIdentityEpsilon0 = 1.0e12; // unit-test identity: trigger never fires
constexpr double kCtrlPacketBits = 400.0;    // first-pass; SPEC gives no distinct HELLO size

std::vector<Vector> SamplePositions (uint32_t nAuv, uint32_t seed, uint32_t rngRun);

struct EkfView
{
  double x[7] = {};
  double xForecast[7] = {};
  double e_k = 0.0;
  double e_km1 = 0.0;
  double epsilon_k = 0.0;
  double S_k = 0.0;
  double eTrigger = 0.0;
  double STrigger = 0.0;
  double epsilonTrigger = 0.0;
  double maxAbsETrigger = 0.0;
  double maxAbsETriggerUnheard = 0.0;
  double maxEpsilonTriggerUnheard = 0.0;
  double xFcVel[3] = {};
  double xHatVel[3] = {};
  double firstBroadcastTime = std::numeric_limits<double>::quiet_NaN ();
  uint64_t nBroadcastRising = 0;
  int broadcast_k = 0;
  bool heard = false;
};

// Reward factors at one already-evaluated action. Read after learnFrom
// so a second reward() call does not discover a new neighbor.
#include "porta-ticktest.inc"

struct RewardPeek
{
  bool ok = false;
  double q = 0.0;
  double R = 0.0;
  double c_ene = 0.0;
  double c_del = 0.0;
  double c_nei = 0.0;
  double c_lin = 0.0;
};

// Protocol-agnostic forwarding seam. Same ChannelGate / energy / visited
// loop for all four variants.
class Fwd
{
public:
  virtual ~Fwd () = default;
  virtual std::string name () const = 0;
  virtual std::vector<uint32_t> candidateSet (uint32_t i) const = 0;
  virtual void learnFrom (uint32_t i, const std::vector<uint32_t> &a) = 0;
  virtual int nextHop (uint32_t i, const std::vector<uint32_t> &a) const = 0;
  virtual std::vector<uint32_t> detourTargets (uint32_t i, uint32_t sinkId,
                                               const std::vector<uint32_t> &vis) const = 0;
  virtual bool useDetour () const = 0;
  virtual dahello::ChannelSample linkSample (uint32_t i, uint32_t j) const = 0;
  virtual void setEnergy (uint32_t i, double joules) = 0;
  virtual double energy (uint32_t i) const = 0;
  virtual double rRangeM () const = 0;
  virtual const std::vector<dahello::LqarpNode> &nodes () const = 0;
  virtual uint64_t controlTxCount () const = 0;
  virtual uint64_t eventHelloCount () const { return 0; }
  virtual uint64_t bootstrapHelloCount () const { return 0; }
  virtual uint64_t energyHelloCount () const { return 0; }
  virtual int energyBeaconBits () const { return 0; }
  virtual void tick (double dt) = 0;
  // Data hop tx -> rx was demodulated (received-only z_k feed).
  virtual void noteDataReception (uint32_t /*rx*/, uint32_t /*tx*/) {}
  virtual void noteTxAttempt (uint32_t /*tx*/) {}
  virtual uint64_t receptionCount () const { return 0; }
  virtual uint64_t measuredUpdates () const { return 0; }
  virtual uint64_t silentPredicts () const { return 0; }
  virtual bool wantsTick () const = 0;
  virtual bool hopMathFinite (uint32_t i, uint32_t j) const = 0;
  virtual void setKinematics (uint32_t i, double x, double y, double z,
                              double vx, double vy, double vz) = 0;
  virtual uint64_t broadcastKOnes () const { return 0; }
  virtual uint64_t broadcastRising () const { return 0; }
  virtual uint64_t nanMeasCount () const { return 0; }
  virtual double maxAbsInnovation () const { return 0.0; }
  virtual double beta1 () const { return 0.0; }
  virtual double beta2 () const { return 0.0; }
  virtual double epsilon0Used () const { return std::numeric_limits<double>::quiet_NaN (); }
  virtual bool hasVRel () const { return false; }
  virtual double vRel (uint32_t, uint32_t) const { return 0.0; }
  virtual bool zohHeard (uint32_t, uint32_t) const { return false; }
  virtual double lastZohTime (uint32_t, uint32_t) const
  {
    return std::numeric_limits<double>::quiet_NaN ();
  }
  virtual bool ekfVelP (uint32_t, uint32_t, double &p33, double &p44, double &p55,
                        int &bc) const
  {
    p33 = p44 = p55 = 0.0;
    bc = 0;
    return false;
  }
  virtual bool zohVelocity (uint32_t, uint32_t, double &vx, double &vy, double &vz) const
  {
    vx = vy = vz = 0.0;
    return false;
  }
  virtual bool ekfView (uint32_t, uint32_t, EkfView &v) const
  {
    v = EkfView{};
    return false;
  }
  // OPT-4 (2026-10-12): pose/SNR-free variants for all-pair end-of-run scans.
  // They return the same values for every field the callers read (Broadcast_k;
  // velocity components x[3:5] / xForecast[3:5], which a predict step copies
  // unchanged) without forcing the lazy EKF predicts. Defaults: the full calls.
  virtual bool ekfBroadcastK (uint32_t i, uint32_t j, int &bc) const
  {
    double a, b, c;
    return ekfVelP (i, j, a, b, c, bc);
  }
  virtual bool ekfViewVel (uint32_t i, uint32_t j, EkfView &v) const { return ekfView (i, j, v); }
  // Both reads are diagnostic. peekFilter does not discover a neighbor.
  // peekReward repeats reward() and must be called only for an action
  // learnFrom has already updated.
  virtual bool peekFilter (uint32_t, uint32_t, dahello::FilterPeek &) const { return false; }
  virtual bool peekReward (uint32_t, uint32_t, RewardPeek &) const { return false; }
  virtual bool peekQ (uint32_t, uint32_t, double &) const { return false; }
  // NOVELTY: read-only access to the HELLO scheduler (counters).
  virtual const dahello::DahelloProtocol *scheduler () const { return nullptr; }
  // Port A: control-frame airtime hook (tx node, cause). No-op by default.
  virtual void setCtrlTxHook (std::function<void (uint32_t, int)>) {}
  // Port A Gate 2: heard-only discovery. No-op by default (oracle).
  virtual void setHeardOnly (bool) {}
  // Port A: neighbour freshness T_nb (heard) and test-only planted leak
  virtual void setTnb (double) {}
  virtual void setLeakPlant (int) {}
  // Port A heard mode: host-specific held write from one decoded frame (I2/I3).
  virtual void applyHostFrame (uint32_t, uint32_t, double, const dahello::HeardFrame &) {}
  // I6: own unicast hop sent / acknowledged (SAQR PLR from own ACK statistics)
  virtual void noteOwnSend (uint32_t, uint32_t) {}
  virtual void noteOwnAck (uint32_t, uint32_t) {}
  // Sender's own group-average energy (QELAR data header), NaN if the host has none.
  virtual double advertisedGroupAvg (uint32_t) const { return std::numeric_limits<double>::quiet_NaN (); }
  // Poison (amend1 enforcement 2): every protocol copy this adapter owns.
  virtual void forEachNodeCopy (const std::function<void (std::vector<dahello::LqarpNode> &)> &) {}
  // DIAG: neighbour freshness T_nb the host's heard table applies (NaN: no expiry)
  virtual double heardTnbS () const { return std::numeric_limits<double>::quiet_NaN (); }
  virtual void applyHeardHold (uint32_t, uint32_t, double,
                               double, double, double,
                               double, double, double,
                               double, double, double, double) {}
  virtual void setLeakLiveLink (bool) {}
  // max_a Q(i,*) for the V-value header field (LQARP/DA-HELLO).
  virtual double advertisedV (uint32_t) const { return 0.0; }
  virtual uint32_t advertisedNumber (uint32_t) const { return 0; }
  // p6: i's held record of j for the LQARP position-aware DA test (LQARP host only).
  virtual bool peekHeld (uint32_t, uint32_t, dahello::LqarpProtocol::HeldView &) const { return false; }
};

class LqarpFwd : public Fwd
{
public:
  LqarpFwd (dahello::ChannelGate &gate, dahello::LqarpConfig cfg, std::vector<dahello::LqarpNode> nodes,
            std::string label = "lqarp")
      : p_ (gate, cfg, std::move (nodes)), label_ (std::move (label))
  {
  }
  std::string name () const override { return label_; }
  std::vector<uint32_t> candidateSet (uint32_t i) const override { return p_.candidateSet (i); }
  void learnFrom (uint32_t i, const std::vector<uint32_t> &a) override { p_.learnFrom (i, a); }
  int nextHop (uint32_t i, const std::vector<uint32_t> &a) const override { return p_.nextHop (i, a); }
  std::vector<uint32_t> detourTargets (uint32_t i, uint32_t sinkId,
                                       const std::vector<uint32_t> &vis) const override
  {
    return p_.detourTargets (i, sinkId, vis);
  }
  bool useDetour () const override { return true; }
  dahello::ChannelSample linkSample (uint32_t i, uint32_t j) const override
  {
    return p_.linkSample (i, j);
  }
  void setEnergy (uint32_t i, double joules) override { p_.setEnergy (i, joules); }
  double energy (uint32_t i) const override { return p_.energy (i); }
  double rRangeM () const override { return p_.rRangeM (); }
  const std::vector<dahello::LqarpNode> &nodes () const override { return p_.nodes (); }
  uint64_t controlTxCount () const override { return 0; }
  void tick (double dt) override { p_.tick (dt); }
  bool wantsTick () const override { return p_.periodicHoldRefresh (); }
  void syncHoldRefresh () { p_.emitHoldRefresh (); }
  bool hopMathFinite (uint32_t i, uint32_t j) const override
  {
    const auto rw = p_.reward (i, j);
    return std::isfinite (rw.R) && std::isfinite (p_.qValue (i, j));
  }
  void setKinematics (uint32_t i, double x, double y, double z,
                      double vx, double vy, double vz) override
  {
    p_.setKinematics (i, x, y, z, vx, vy, vz);
  }
  void setHeardOnly (bool on) override { p_.setHeardOnly (on); }
  double heardTnbS () const override { return p_.config ().tNbS; }
  void setTnb (double s) override { p_.setTnb (s); }
  void forEachNodeCopy (const std::function<void (std::vector<dahello::LqarpNode> &)> &f) override
  {
    f (p_.nodesMut ());
  }
  void setLeakLiveLink (bool on) override { p_.setLeakLiveLink (on); }
  void applyHeardHold (uint32_t i, uint32_t j, double t,
                       double x, double y, double z,
                       double vx, double vy, double vz,
                       double energy, double snrDb,
                       double number, double V) override
  {
    p_.applyHeardHold (i, j, t, x, y, z, vx, vy, vz, energy, snrDb, number, V);
  }
  double advertisedV (uint32_t i) const override
  {
    double m = 0.0;
    for (uint32_t a : p_.candidateSet (i))
      {
        m = std::max (m, p_.qValue (i, a));
      }
    return m;
  }
  uint32_t advertisedNumber (uint32_t i) const override
  {
    return static_cast<uint32_t> (p_.candidateSet (i).size ());
  }
  bool peekHeld (uint32_t i, uint32_t j, dahello::LqarpProtocol::HeldView &v) const override
  {
    v = p_.heldView (i, j);
    return true;
  }
  bool peekFilter (uint32_t i, uint32_t j, dahello::FilterPeek &out) const override
  {
    return p_.peekFilter (i, j, out);
  }
  bool peekReward (uint32_t i, uint32_t j, RewardPeek &out) const override
  {
    const auto rw = p_.reward (i, j);
    out.ok = true;
    out.q = p_.qValue (i, j);
    out.R = rw.R;
    out.c_ene = rw.c_ene;
    out.c_del = rw.c_del;
    out.c_nei = rw.c_nei;
    out.c_lin = rw.c_lin;
    return true;
  }
  bool peekQ (uint32_t i, uint32_t j, double &q) const override
  {
    q = p_.qValue (i, j);
    return true;
  }

private:
  dahello::LqarpProtocol p_;
  std::string label_;
};

class DahelloFwd : public Fwd
{
public:
  DahelloFwd (dahello::ChannelGate &gate, dahello::DahelloConfig cfg, std::vector<dahello::LqarpNode> nodes,
            std::string label, bool tick)
      : p_ (gate, cfg, std::move (nodes)), label_ (std::move (label)), tick_ (tick)
  {
  }
  std::string name () const override { return label_; }
  std::vector<uint32_t> candidateSet (uint32_t i) const override { return p_.candidateSet (i); }
  void learnFrom (uint32_t i, const std::vector<uint32_t> &a) override { p_.learnFrom (i, a); }
  int nextHop (uint32_t i, const std::vector<uint32_t> &a) const override { return p_.nextHop (i, a); }
  std::vector<uint32_t> detourTargets (uint32_t i, uint32_t sinkId,
                                       const std::vector<uint32_t> &vis) const override
  {
    return p_.detourTargets (i, sinkId, vis);
  }
  bool useDetour () const override { return true; }
  dahello::ChannelSample linkSample (uint32_t i, uint32_t j) const override
  {
    return p_.linkSample (i, j);
  }
  void setEnergy (uint32_t i, double joules) override { p_.setEnergy (i, joules); }
  double energy (uint32_t i) const override { return p_.energy (i); }
  double rRangeM () const override { return p_.rRangeM (); }
  const std::vector<dahello::LqarpNode> &nodes () const override { return p_.nodes (); }
  uint64_t controlTxCount () const override { return p_.controlTxCount (); }
  uint64_t eventHelloCount () const override { return p_.eventHelloCount (); }
  uint64_t bootstrapHelloCount () const override { return p_.bootstrapHelloCount (); }
  uint64_t energyHelloCount () const override { return p_.energyHelloCount (); }
  int energyBeaconBits () const override { return p_.energyBeaconBits (); }
  void tick (double dt) override { p_.tick (dt); }
  bool wantsTick () const override { return tick_; }
  void noteDataReception (uint32_t rx, uint32_t tx) override { p_.noteReception (rx, tx); }
  uint64_t receptionCount () const override { return p_.receptionCount (); }
  uint64_t measuredUpdates () const override { return p_.measuredUpdates (); }
  uint64_t silentPredicts () const override { return p_.silentPredicts (); }
  bool hopMathFinite (uint32_t i, uint32_t j) const override
  {
    const auto rw = p_.reward (i, j);
    return std::isfinite (rw.R) && std::isfinite (p_.qValue (i, j));
  }
  void setKinematics (uint32_t i, double x, double y, double z,
                      double vx, double vy, double vz) override
  {
    p_.setKinematics (i, x, y, z, vx, vy, vz);
  }
  uint64_t broadcastKOnes () const override { return p_.broadcastKOnes (); }
  uint64_t broadcastRising () const override { return p_.broadcastRising (); }
  uint64_t nanMeasCount () const override { return p_.nanMeasCount (); }
  double maxAbsInnovation () const override { return p_.maxAbsInnovation (); }
  double beta1 () const override { return p_.config ().beta_1; }
  double beta2 () const override { return p_.config ().beta_2; }
  double epsilon0Used () const override { return p_.config ().epsilon_0; }
  bool hasVRel () const override { return true; }
  double vRel (uint32_t i, uint32_t j) const override { return p_.vRel (i, j); }
  bool zohHeard (uint32_t i, uint32_t j) const override { return p_.zohHeard (i, j); }
  double lastZohTime (uint32_t i, uint32_t j) const override { return p_.lastZohTime (i, j); }
  bool ekfVelP (uint32_t i, uint32_t j, double &p33, double &p44, double &p55,
                int &bc) const override
  {
    const auto &s = p_.ekf (i, j);
    p33 = s.P[3 * 7 + 3];
    p44 = s.P[4 * 7 + 4];
    p55 = s.P[5 * 7 + 5];
    bc = s.broadcast_k;
    return true;
  }
  bool zohVelocity (uint32_t i, uint32_t j, double &vx, double &vy, double &vz) const override
  {
    const auto &s = p_.ekfMeta (i, j); // OPT-4: zoh fields only
    vx = s.vx_j_zoh;
    vy = s.vy_j_zoh;
    vz = s.vz_j_zoh;
    return true;
  }
  bool ekfBroadcastK (uint32_t i, uint32_t j, int &bc) const override
  {
    bc = p_.ekfMeta (i, j).broadcast_k;
    return true;
  }
  bool ekfViewVel (uint32_t i, uint32_t j, EkfView &v) const override
  {
    return fillEkfView (p_.ekfMeta (i, j), v, true);
  }
  bool ekfView (uint32_t i, uint32_t j, EkfView &v) const override
  {
    return fillEkfView (p_.ekf (i, j), v, false);
  }
  static bool fillEkfView (const dahello::NeighborEkf &s, EkfView &v, bool velOnly)
  {
    for (int k = 0; k < 7; ++k)
      {
        const bool vel = (k >= 3 && k <= 5);
        // velOnly: pose/SNR components may be behind by deferred predicts; poison them
        v.x[k] = (!velOnly || vel) ? s.x[static_cast<size_t> (k)] : std::numeric_limits<double>::quiet_NaN ();
        v.xForecast[k] = (!velOnly || vel) ? s.xForecast[static_cast<size_t> (k)]
                                           : std::numeric_limits<double>::quiet_NaN ();
      }
    v.e_k = s.e_k;
    v.e_km1 = s.e_km1;
    v.epsilon_k = s.epsilon_k;
    v.S_k = s.S_k;
    v.eTrigger = s.eTrigger;
    v.STrigger = s.STrigger;
    v.epsilonTrigger = s.epsilonTrigger;
    v.maxAbsETrigger = s.maxAbsETrigger;
    v.maxAbsETriggerUnheard = s.maxAbsETriggerUnheard;
    v.maxEpsilonTriggerUnheard = s.maxEpsilonTriggerUnheard;
    v.xHatVel[0] = s.x[3];
    v.xHatVel[1] = s.x[4];
    v.xHatVel[2] = s.x[5];
    v.xFcVel[0] = s.xForecast[3];
    v.xFcVel[1] = s.xForecast[4];
    v.xFcVel[2] = s.xForecast[5];
    v.firstBroadcastTime = s.firstBroadcastTime;
    v.nBroadcastRising = s.nBroadcastRising;
    v.broadcast_k = s.broadcast_k;
    v.heard = s.zohHeard;
    return true;
  }
  bool peekFilter (uint32_t i, uint32_t j, dahello::FilterPeek &out) const override
  {
    return p_.peekFilter (i, j, out);
  }
  const dahello::DahelloProtocol *scheduler () const override { return &p_; }
  void setCtrlTxHook (std::function<void (uint32_t, int)> h) override { p_.setTxHook (std::move (h)); }
  void setHeardOnly (bool on) override { p_.setHeardOnly (on); }
  void forEachNodeCopy (const std::function<void (std::vector<dahello::LqarpNode> &)> &f) override
  {
    f (p_.nodesMut ());
  }
  double heardTnbS () const override { return std::numeric_limits<double>::quiet_NaN (); } // DahelloProtocol host table: zohHeard, no expiry
  void applyHeardHold (uint32_t i, uint32_t j, double t,
                       double x, double y, double z,
                       double vx, double vy, double vz,
                       double energy, double snrDb,
                       double number, double V) override
  {
    p_.applyHeardHold (i, j, t, x, y, z, vx, vy, vz, energy, snrDb, number, V);
  }
  bool peekReward (uint32_t i, uint32_t j, RewardPeek &out) const override
  {
    const auto rw = p_.reward (i, j);
    out.ok = true;
    out.q = p_.qValue (i, j);
    out.R = rw.R;
    out.c_ene = rw.c_ene;
    out.c_del = rw.c_del;
    out.c_nei = rw.c_nei;
    out.c_lin = rw.c_lin;
    return true;
  }
  bool peekQ (uint32_t i, uint32_t j, double &q) const override
  {
    q = p_.qValue (i, j);
    return true;
  }

private:
  dahello::DahelloProtocol p_;
  std::string label_;
  bool tick_;
};

class RlorFwd : public Fwd
{
public:
  RlorFwd (dahello::ChannelGate &gate, dahello::LqarpConfig cfg, std::vector<dahello::LqarpNode> nodes)
      : p_ (gate, cfg, std::move (nodes))
  {
  }
  std::string name () const override { return "rlor"; }
  std::vector<uint32_t> candidateSet (uint32_t i) const override { return p_.candidateSet (i); }
  void learnFrom (uint32_t, const std::vector<uint32_t> &) override {}
  int nextHop (uint32_t i, const std::vector<uint32_t> &a) const override { return p_.nextHop (i, a); }
  std::vector<uint32_t> detourTargets (uint32_t, uint32_t, const std::vector<uint32_t> &) const override
  {
    return {};
  }
  bool useDetour () const override { return false; }
  dahello::ChannelSample linkSample (uint32_t i, uint32_t j) const override
  {
    return p_.linkSample (i, j);
  }
  void setEnergy (uint32_t i, double joules) override { p_.setEnergy (i, joules); }
  double energy (uint32_t i) const override { return p_.energy (i); }
  double rRangeM () const override { return p_.rRangeM (); }
  const std::vector<dahello::LqarpNode> &nodes () const override { return p_.nodes (); }
  uint64_t controlTxCount () const override { return 0; }
  void tick (double) override {}
  bool wantsTick () const override { return false; }
  bool hopMathFinite (uint32_t i, uint32_t j) const override
  {
    return std::isfinite (p_.pSucc (i, j));
  }
  void setKinematics (uint32_t i, double x, double y, double z,
                      double vx, double vy, double vz) override
  {
    p_.setKinematics (i, x, y, z, vx, vy, vz);
  }

private:
  dahello::RlorInspiredProtocol p_;
};

class QelarFwd : public Fwd
{
public:
  enum class Hello { Off, Pose, Energy };

  QelarFwd (dahello::ChannelGate &gate, dahello::QelarConfig cfg, std::vector<dahello::LqarpNode> nodes,
            std::string label, Hello hello, dahello::DahelloConfig schedCfg)
      : p_ (gate, cfg, nodes), label_ (std::move (label)), hello_ (hello)
  {
    if (hello_ != Hello::Off)
      {
        sched_ = std::make_unique<dahello::DahelloProtocol> (gate, schedCfg, std::move (nodes));
      }
  }
  void setSink (uint32_t sinkId) { p_.setSink (sinkId); }
  std::string name () const override { return label_; }
  std::vector<uint32_t> candidateSet (uint32_t i) const override
  {
    auto raw = p_.candidateSet (i);
    if (!sched_)
      {
        return raw;
      }
    // Same one-time discovery the DA-HELLO filter does inside inNRange.
    // Without it the scheduler never opens a hold and the trigger stays dark.
    const uint32_t n = static_cast<uint32_t> (p_.nodes ().size ());
    for (uint32_t j = 0; j < n; ++j)
      {
        if (j != i)
          {
            sched_->inNRange (i, j);
          }
      }
    std::vector<uint32_t> out;
    for (uint32_t j : raw)
      {
        if (sched_->inNStab (i, j))
          {
            out.push_back (j);
          }
      }
    return out;
  }
  void learnFrom (uint32_t i, const std::vector<uint32_t> &a) override
  {
    if (sched_ && hello_ == Hello::Energy)
      {
        for (uint32_t j : a)
          {
            if (sched_->zohHeard (i, j))
              {
                p_.setHeardEnergy (i, j, sched_->ekfMeta (i, j).energy_j_zoh);
              }
          }
      }
    p_.learnFrom (i, a);
  }
  int nextHop (uint32_t i, const std::vector<uint32_t> &a) const override { return p_.nextHop (i, a); }
  std::vector<uint32_t> detourTargets (uint32_t, uint32_t, const std::vector<uint32_t> &) const override
  {
    return {};
  }
  bool useDetour () const override { return false; }
  dahello::ChannelSample linkSample (uint32_t i, uint32_t j) const override { return p_.linkSample (i, j); }
  void setEnergy (uint32_t i, double joules) override
  {
    p_.setEnergy (i, joules);
    if (sched_)
      {
        sched_->setEnergy (i, joules);
      }
  }
  double energy (uint32_t i) const override { return p_.energy (i); }
  double rRangeM () const override { return p_.rRangeM (); }
  const std::vector<dahello::LqarpNode> &nodes () const override { return p_.nodes (); }
  uint64_t controlTxCount () const override { return sched_ ? sched_->controlTxCount () : 0; }
  uint64_t eventHelloCount () const override { return sched_ ? sched_->eventHelloCount () : 0; }
  uint64_t bootstrapHelloCount () const override
  {
    return sched_ ? sched_->bootstrapHelloCount () : 0;
  }
  uint64_t energyHelloCount () const override { return sched_ ? sched_->energyHelloCount () : 0; }
  int energyBeaconBits () const override { return sched_ ? sched_->energyBeaconBits () : 0; }
  void tick (double dt) override
  {
    if (sched_)
      {
        sched_->tick (dt);
      }
  }
  bool wantsTick () const override { return hello_ != Hello::Off; }
  void noteDataReception (uint32_t rx, uint32_t tx) override
  {
    if (!heard_)
      {
        p_.noteHeader (rx, tx); // heard: the decode path writes the hold (applyHostFrame)
      }
    if (sched_)
      {
        sched_->noteReception (rx, tx);
      }
  }
  bool hopMathFinite (uint32_t i, uint32_t j) const override
  {
    return std::isfinite (p_.reward (i, j).R) && std::isfinite (p_.qValue (i, j));
  }
  void setKinematics (uint32_t i, double x, double y, double z, double vx, double vy, double vz) override
  {
    p_.setKinematics (i, x, y, z, vx, vy, vz);
    if (sched_)
      {
        sched_->setKinematics (i, x, y, z, vx, vy, vz);
      }
  }
  bool peekReward (uint32_t i, uint32_t j, RewardPeek &out) const override
  {
    const auto rw = p_.reward (i, j);
    out.ok = true;
    out.q = p_.qValue (i, j);
    out.R = rw.R;
    out.c_ene = rw.cSender;
    out.c_del = rw.dSender;
    out.c_nei = rw.cNext;
    out.c_lin = rw.dNext;
    return true;
  }
  bool peekQ (uint32_t i, uint32_t j, double &q) const override
  {
    q = p_.qValue (i, j);
    return true;
  }

private:
  dahello::QelarProtocol p_;
  std::unique_ptr<dahello::DahelloProtocol> sched_;

public:
  const dahello::DahelloProtocol *scheduler () const override { return sched_.get (); }
  void setCtrlTxHook (std::function<void (uint32_t, int)> h) override
  {
    if (sched_)
      {
        sched_->setTxHook (std::move (h));
      }
  }
  void setHeardOnly (bool on) override
  {
    if (sched_)
      {
        sched_->setHeardOnly (on);
      }
    heard_ = on;
    p_.heard ().on = on;
    p_.heard ().clock = [] { return Simulator::Now ().GetSeconds (); };
  }
  void setTnb (double s) override { p_.heard ().tNbS = s; }
  void setLeakPlant (int k) override { p_.heard ().leakPlant = k; }
  double heardTnbS () const override { return heard_ ? p_.heard ().tNbS : std::numeric_limits<double>::quiet_NaN (); }
  void applyHostFrame (uint32_t r, uint32_t s, double t, const dahello::HeardFrame &f) override
  {
    p_.hearFrame (r, s, t, f);
  }
  double advertisedV (uint32_t s) const override { return heard_ ? p_.ownV (s) : 0.0; }
  double advertisedGroupAvg (uint32_t s) const override
  {
    return heard_ ? p_.ownGroupAverage (s) : std::numeric_limits<double>::quiet_NaN ();
  }
  void forEachNodeCopy (const std::function<void (std::vector<dahello::LqarpNode> &)> &f) override
  {
    f (p_.nodesMut ());
    if (sched_)
      {
        f (sched_->nodesMut ());
      }
  }
  bool heard_ = false;
  void applyHeardHold (uint32_t i, uint32_t j, double t,
                       double x, double y, double z,
                       double vx, double vy, double vz,
                       double energy, double snrDb,
                       double number, double V) override
  {
    if (sched_)
      {
        sched_->applyHeardHold (i, j, t, x, y, z, vx, vy, vz, energy, snrDb, number, V);
      }
  }

private:
  std::string label_;
  Hello hello_;
};

class SaqrFwd : public Fwd
{
public:
  enum class Hello { Off, Pose, Energy };

  SaqrFwd (dahello::ChannelGate &gate, dahello::SaqrConfig cfg, std::vector<dahello::LqarpNode> nodes,
           std::string label, Hello hello, dahello::DahelloConfig schedCfg)
      : p_ (gate, cfg, nodes), label_ (std::move (label)), hello_ (hello)
  {
    if (hello_ != Hello::Off)
      {
        sched_ = std::make_unique<dahello::DahelloProtocol> (gate, schedCfg, std::move (nodes));
      }
  }
  void setSink (uint32_t sinkId) { p_.setSink (sinkId); }
  std::string name () const override { return label_; }
  std::vector<uint32_t> candidateSet (uint32_t i) const override
  {
    auto raw = p_.candidateSet (i);
    if (!sched_)
      {
        return raw;
      }
    // Same one-time discovery the DA-HELLO filter does inside inNRange.
    // Without it the scheduler never opens a hold and the trigger stays dark.
    const uint32_t n = static_cast<uint32_t> (p_.nodes ().size ());
    for (uint32_t j = 0; j < n; ++j)
      {
        if (j != i)
          {
            sched_->inNRange (i, j);
          }
      }
    std::vector<uint32_t> out;
    for (uint32_t j : raw)
      {
        if (sched_->inNStab (i, j))
          {
            out.push_back (j);
          }
      }
    return out;
  }
  void learnFrom (uint32_t i, const std::vector<uint32_t> &a) override
  {
    if (sched_ && hello_ == Hello::Energy)
      {
        for (uint32_t j : a)
          {
            if (sched_->zohHeard (i, j))
              {
                p_.setHeardEnergy (i, j, sched_->ekfMeta (i, j).energy_j_zoh);
              }
          }
      }
    p_.learnFrom (i, a);
  }
  int nextHop (uint32_t i, const std::vector<uint32_t> &a) const override { return p_.nextHop (i, a); }
  std::vector<uint32_t> detourTargets (uint32_t, uint32_t, const std::vector<uint32_t> &) const override
  {
    return {};
  }
  bool useDetour () const override { return false; }
  dahello::ChannelSample linkSample (uint32_t i, uint32_t j) const override { return p_.linkSample (i, j); }
  void setEnergy (uint32_t i, double joules) override
  {
    p_.setEnergy (i, joules);
    if (sched_)
      {
        sched_->setEnergy (i, joules);
      }
  }
  double energy (uint32_t i) const override { return p_.energy (i); }
  double rRangeM () const override { return p_.rRangeM (); }
  const std::vector<dahello::LqarpNode> &nodes () const override { return p_.nodes (); }
  uint64_t controlTxCount () const override { return sched_ ? sched_->controlTxCount () : 0; }
  uint64_t eventHelloCount () const override { return sched_ ? sched_->eventHelloCount () : 0; }
  uint64_t bootstrapHelloCount () const override
  {
    return sched_ ? sched_->bootstrapHelloCount () : 0;
  }
  uint64_t energyHelloCount () const override { return sched_ ? sched_->energyHelloCount () : 0; }
  int energyBeaconBits () const override { return sched_ ? sched_->energyBeaconBits () : 0; }
  void tick (double dt) override
  {
    if (sched_)
      {
        sched_->tick (dt);
      }
  }
  bool wantsTick () const override { return hello_ != Hello::Off; }
  void noteDataReception (uint32_t rx, uint32_t tx) override
  {
    if (!heard_)
      {
        p_.noteHeader (rx, tx);   // heard: applyHostFrame writes the hold
        p_.noteTxSuccess (tx);    // heard: PLR from own ACK statistics (noteOwnAck)
      }
    if (sched_)
      {
        sched_->noteReception (rx, tx);
      }
  }
  void noteTxAttempt (uint32_t tx) override { p_.noteTxAttempt (tx); }
  bool hopMathFinite (uint32_t i, uint32_t j) const override
  {
    return std::isfinite (p_.reward (i, j).R) && std::isfinite (p_.qValue (i, j));
  }
  void setKinematics (uint32_t i, double x, double y, double z, double vx, double vy, double vz) override
  {
    p_.setKinematics (i, x, y, z, vx, vy, vz);
    if (sched_)
      {
        sched_->setKinematics (i, x, y, z, vx, vy, vz);
      }
  }
  bool peekReward (uint32_t i, uint32_t j, RewardPeek &out) const override
  {
    const auto rw = p_.reward (i, j);
    out.ok = true;
    out.q = p_.qValue (i, j);
    out.R = rw.R;
    out.c_ene = rw.plr;
    out.c_del = rw.delay;
    out.c_nei = rw.er;
    out.c_lin = rw.alm;
    return true;
  }
  bool peekQ (uint32_t i, uint32_t j, double &q) const override
  {
    q = p_.qValue (i, j);
    return true;
  }
  const dahello::DahelloProtocol *scheduler () const override { return sched_.get (); }
  void setCtrlTxHook (std::function<void (uint32_t, int)> h) override
  {
    if (sched_)
      {
        sched_->setTxHook (std::move (h));
      }
  }
  void setHeardOnly (bool on) override
  {
    if (sched_)
      {
        sched_->setHeardOnly (on);
      }
    heard_ = on;
    p_.heard ().on = on;
    p_.heard ().clock = [] { return Simulator::Now ().GetSeconds (); };
  }
  void setTnb (double s) override { p_.heard ().tNbS = s; }
  void setLeakPlant (int k) override { p_.heard ().leakPlant = k; }
  double heardTnbS () const override { return heard_ ? p_.heard ().tNbS : std::numeric_limits<double>::quiet_NaN (); }
  void applyHostFrame (uint32_t r, uint32_t s, double t, const dahello::HeardFrame &f) override
  {
    p_.hearFrame (r, s, t, f);
  }
  void noteOwnSend (uint32_t i, uint32_t j) override { if (heard_) p_.noteOwnSend (i, j); }
  void noteOwnAck (uint32_t i, uint32_t j) override { if (heard_) p_.noteOwnAck (i, j); }
  double advertisedV (uint32_t s) const override { return heard_ ? p_.ownV (s) : 0.0; }
  uint32_t advertisedNumber (uint32_t s) const override { return heard_ ? p_.ownNumber (s) : 0; }
  void forEachNodeCopy (const std::function<void (std::vector<dahello::LqarpNode> &)> &f) override
  {
    f (p_.nodesMut ());
    if (sched_)
      {
        f (sched_->nodesMut ());
      }
  }
  bool heard_ = false;
  void applyHeardHold (uint32_t i, uint32_t j, double t,
                       double x, double y, double z,
                       double vx, double vy, double vz,
                       double energy, double snrDb,
                       double number, double V) override
  {
    if (sched_)
      {
        sched_->applyHeardHold (i, j, t, x, y, z, vx, vy, vz, energy, snrDb, number, V);
      }
  }

private:
  dahello::SaqrProtocol p_;
  std::unique_ptr<dahello::DahelloProtocol> sched_;
  std::string label_;
  Hello hello_;
};

// MARV host adapter. Neighbour geometry is the binary's (range only,
// as for QELAR/SAQR). V / void flag / residual energy of a neighbour
// come from what this node has heard: data-packet headers (all variants,
// MARV p.7047-7048 method 1), plus the periodic control packet (marv-T*),
// or the DA-HELLO HELLO / bootstrap and optional energy beacon
// (marv-dahello, marv-dahello-energy). The packet engine (priority list,
// holding, suppression) lives in Pipeline::Marv*.
class MarvFwd : public Fwd
{
public:
  enum class Ctrl { Off, Periodic, Hello, HelloEnergy, Trickle, Speed, Let, Naive };

  MarvFwd (dahello::ChannelGate &gate, dahello::MarvConfig cfg, std::vector<dahello::LqarpNode> nodes,
           std::string label, Ctrl ctrl, double tCtrl, dahello::DahelloConfig schedCfg,
           bool hostState = false)
      : gate_ (&gate), p_ (gate, cfg, nodes), label_ (std::move (label)), ctrl_ (ctrl), tCtrl_ (tCtrl),
        hs_ (hostState)
  {
    const size_t n = nodes.size ();
    if (ctrl_ == Ctrl::Hello || ctrl_ == Ctrl::HelloEnergy)
      {
        sched_ = std::make_unique<dahello::DahelloProtocol> (gate, schedCfg, std::move (nodes));
      }
    polledZoh_.assign (n, std::vector<double> (n, std::numeric_limits<double>::quiet_NaN ()));
    polledE_.assign (n, std::vector<double> (n, std::numeric_limits<double>::quiet_NaN ()));
    // Last transmitted host state: the true initial advertisement
    // (V = 0 by D7 + D8, no void flag).
    vLast_.assign (n, 0.0);
    voidLast_.assign (n, 0);
    bootSeen_.assign (n, 0);
    pendCause_.assign (n, 0);
    if (hs_ && sched_)
      {
        thetaV_ = p_.config ().gc; // D17: theta_V = g_c
        sched_->setStateDirtyHook ([this] (uint32_t j) { return stateDirtyFire (j); });
      }
  }

  // H1: advertised state changed since it was last transmitted.
  // Cause 1 = void flag flip (D18), 2 = |V - V_last| > theta_V (D17).
  // Both at once is attributed to the void flip.
  int stateDirtyCause (uint32_t j) const
  {
    if (!hs_ || j == p_.sinkId () || !isAlive (j))
      {
        return 0;
      }
    if ((p_.advertisedVoid (j) ? 1 : 0) != voidLast_[j])
      {
        return 1;
      }
    if (std::fabs (p_.advertisedV (j) - vLast_[j]) > thetaV_)
      {
        return 2;
      }
    return 0;
  }
  bool stateDirty (uint32_t j) const { return stateDirtyCause (j) != 0; }
  // Any packet that carries j's host state (data header, link HELLO,
  // bootstrap HELLO, host-state HELLO) resets the reference.
  void noteStateSent (uint32_t j) const
  {
    vLast_[j] = p_.advertisedV (j);
    voidLast_[j] = p_.advertisedVoid (j) ? 1 : 0;
    if (eLast_.size () == vLast_.size ())
      {
        eLast_[j] = p_.energy (j);
      }
  }
  // DECISION-AWARE trigger (design choice, default off; see
  // ${OUT}/decision_aware_design.md). Mode 1 = DA, 2 = DA + back-off.
  void setDa (int mode, double margin, double imin, int doublings, double tNb)
  {
    daMode_ = mode;
    daMargin_ = margin;
    daImin_ = imin;
    daImax_ = imin * std::ldexp (1.0, doublings);
    daTnb_ = tNb;
    const size_t n = p_.nodes ().size ();
    eLast_.assign (n, p_.config ().eIni);
    daI_.assign (n, daImin_);
    daHold_.assign (n, -1.0);
  }
  void setTrickleStrict (bool s) { trStrict_ = s; }
  int daMode () const { return daMode_; }
  uint64_t daStrong () const { return daStrong_; }
  uint64_t daWeak () const { return daWeak_; }
  uint64_t daDeferred () const { return daDeferred_; }
  uint64_t daNoUpstream () const { return daNoUp_; }
  // Score of candidate k in an upstream node's ranking, from j's own view:
  // S(k) = -w_E E(k) - w_D z_k / R + gamma (lambda1 - lambda2) V_k
  // (the i-dependent terms of Eq. 8/9 cancel in every pairwise comparison;
  // psat assumed equal, C = 0 by D3).
  double daScore (double eCost, double z, double V) const
  {
    const auto &c = p_.config ();
    const double wE = 1.0 - c.omega - c.voi;
    const double wD = c.omega + c.voi;
    return -wE * eCost - wD * z / p_.rRangeM () + c.gamma * (c.lambda1 - c.lambda2) * V;
  }
  // Returns 0 (no HELLO), 1 (membership event: void flip with an upstream
  // node), 2 (rank event: some co-candidate k changes order vs. j by more
  // than the margin, relative to what upstream nodes last heard from j).
  // p8: depth of peer i as seen by j for the DA test. Legacy (oracle) mode: true z.
  // Heard mode: j's held depth record (sink: fixed deployment depth, I5); no held
  // depth -> peer skipped. A used depth that is non-finite or |z| > 1e7 (poisoned
  // node copy) is a true-state read -> oracleLeak (exit 3). --leakTest=8 re-enables
  // the legacy true-z read in heard mode so the guard can be shown to catch it.
  bool daPeerZ (uint32_t j, uint32_t i, double &z) const
  {
    if (!heard_)
      {
        z = p_.nodes ()[i].z;
        return true;
      }
    double held;
    if (i == p_.sinkId ())
      {
        held = p_.nodes ()[i].z; // I5: fixed, known sink deployment depth
      }
    else
      {
        const auto &r = p_.record (j, i);
        if (!(r.tZ >= 0.0) || !std::isfinite (r.z))
          {
            return false;
          }
        held = r.z;
      }
    z = (p_.heard ().leakPlant == 8) ? p_.nodes ()[i].z : held;
    if (!std::isfinite (z) || std::fabs (z) > 1e7)
      {
        dahello::oracleLeak ("MarvFwd::daFire:peerZ", j, i);
      }
    return true;
  }
  int daFire (uint32_t j)
  {
    if (j == p_.sinkId () || !isAlive (j))
      {
        return 0;
      }
    const double now = Simulator::Now ().GetSeconds ();
    const uint32_t n = static_cast<uint32_t> (p_.nodes ().size ());
    const auto &nj = p_.nodes ()[j];
    // Upstream set from j's own records: nodes j heard within T_nb that are
    // deeper than j (they may rank j as a candidate).
    double zUpMax = -1e300;
    bool up = false;
    for (uint32_t i = 0; i < n; ++i)
      {
        if (i == j)
          {
            continue;
          }
        const auto &r = p_.record (j, i);
        if (!(r.heard && now - r.t <= daTnb_))
          {
            continue;
          }
        double zi;
        if (!daPeerZ (j, i, zi))
          {
            continue;
          }
        if (zi > nj.z)
          {
            up = true;
            zUpMax = std::max (zUpMax, zi);
          }
      }
    const bool voidNow = p_.advertisedVoid (j);
    const bool voidFlip = (voidNow ? 1 : 0) != voidLast_[j];
    if (!up)
      {
        if (voidFlip || std::fabs (p_.advertisedV (j) - vLast_[j]) > 0.0)
          {
            ++daNoUp_;
          }
        return 0;
      }
    if (voidFlip)
      {
        ++daStrong_;
        daI_[j] = daImin_;
        daHold_[j] = now + daI_[j];
        return 1;
      }
    if (voidNow)
      {
        return 0; // a void j is not a candidate: rank changes are irrelevant
      }
    const double eIni = p_.config ().eIni;
    const double sAdv = daScore (1.0 - eLast_[j] / eIni, nj.z, vLast_[j]);
    const double sNow = daScore (1.0 - p_.energy (j) / eIni, nj.z, p_.advertisedV (j));
    if (sAdv == sNow)
      {
        return 0;
      }
    bool flip = false;
    for (uint32_t k = 0; k < n && !flip; ++k)
      {
        if (k == j)
          {
            continue;
          }
        const auto &r = p_.record (j, k);
        if (!r.heard || now - r.t > daTnb_)
          {
            continue;
          }
        double zk;
        if (!daPeerZ (j, k, zk) || !(zk < zUpMax))
          {
            continue;
          }
        if (k != p_.sinkId () && r.voidFlag)
          {
            continue;
          }
        const double Vk = (k == p_.sinkId ()) ? 0.0 : r.V;
        const double sk = daScore (1.0 - r.energy / eIni, zk, Vk);
        const double gA = sAdv - sk, gN = sNow - sk;
        if ((gA > 0.0) != (gN > 0.0) && std::fabs (gN) >= daMargin_)
          {
            flip = true;
          }
      }
    if (!flip)
      {
        return 0;
      }
    if (daMode_ == 2)
      {
        if (now + 1e-9 < daHold_[j])
          {
            ++daDeferred_;
            return 0; // stays pending: re-tested every tick
          }
        daI_[j] = std::min (2.0 * daI_[j], daImax_);
        daHold_[j] = now + daI_[j];
      }
    ++daWeak_;
    return 2;
  }
  bool hostState () const { return hs_; }
  double thetaV () const { return thetaV_; }
  uint64_t hsVoidFires () const { return hsVoid_; }
  uint64_t hsVFires () const { return hsV_; }
  uint64_t hsPreempted () const { return hsPreempt_; }
  uint64_t hsHellos () const { return sched_ ? sched_->hostStateHelloCount () : 0; }
  uint64_t linkHellos () const { return sched_ ? sched_->eventHelloCount () : 0; }
  void setSink (uint32_t s) { p_.setSink (s); }
  void setAlive (std::function<bool (uint32_t)> f) { alive_ = std::move (f); }
  dahello::MarvProtocol &marv () { return p_; }
  const dahello::MarvProtocol &marv () const { return p_; }
  Ctrl ctrl () const { return ctrl_; }
  double tCtrl () const { return tCtrl_; }

  // First periodic round at t=0 (same phase as the binary's Fixed-Periodic).
  void startPeriodic ()
  {
    if (ctrl_ == Ctrl::Periodic)
      {
        periodicRound (); // p14 build-time t=0 round (kept unchanged under --periodicJitter=1)
        buildRoundRx_ = periodicRx_;
        nextPeriodic_ = tCtrl_;
        if (pjOn_ && tCtrl_ > 0.0)
          {
            // p15 A2.2(a): one phase offset U[0,T) per node at start-up, seeded from the
            // run seed; node j sends at offset_j + kT (k >= 0) instead of the shared kT grid.
            const uint32_t n = static_cast<uint32_t> (p_.nodes ().size ());
            std::mt19937_64 rng (pjSeed_);
            std::uniform_real_distribution<double> u (0.0, tCtrl_);
            pjOff_.assign (n, 0.0);
            pjFirst_.assign (n, std::numeric_limits<double>::quiet_NaN ());
            for (uint32_t j = 0; j < n; ++j)
              {
                pjOff_[j] = u (rng);
                Simulator::Schedule (Seconds (pjOff_[j]), &MarvFwd::pjFire, this, j, 0u);
              }
          }
      }
    if (ctrl_ == Ctrl::Trickle || ctrl_ == Ctrl::Speed || ctrl_ == Ctrl::Let || ctrl_ == Ctrl::Naive)
      {
        // NOVELTY step 2: same t=0 round as the periodic arms.
        periodicRound ();
        adaptiveInit ();
      }
  }
  void setAdaptiveSeed (uint32_t s) { adSeed_ = s; }
  // p15 A2.2(a) / (e)
  void setPeriodicJitter (bool on, uint64_t seed) { pjOn_ = on; pjSeed_ = seed; }
  void setPollStored (bool on) { pollStored_ = on; }
  bool periodicJitter () const { return pjOn_; }
  const std::vector<double> &pjOffsets () const { return pjOff_; }
  const std::vector<double> &pjFirstSend () const { return pjFirst_; }
  uint64_t buildRoundRx () const { return buildRoundRx_; }
  uint64_t pollLiveW () const { return pollLiveW_; }
  uint64_t pollStoredW () const { return pollStoredW_; }
  uint64_t pollEnergyLiveW () const { return pollEnergyLiveW_; }
  uint64_t pollEnergyStoredW () const { return pollEnergyStoredW_; }
  void pjFire (uint32_t j, uint32_t k)
  {
    const double now = Simulator::Now ().GetSeconds ();
    if (isAlive (j))
      {
        if (!std::isfinite (pjFirst_[j]))
          {
            pjFirst_[j] = now;
          }
        sendOne (j, now);
      }
    const double next = pjOff_[j] + static_cast<double> (k + 1) * tCtrl_;
    Simulator::Schedule (Seconds (next) - Simulator::Now (), &MarvFwd::pjFire, this, j, k + 1);
  }
  // t25: QLAR tau and naive refresh for marv-let / marv-naive.
  void setLetFrac (double f) { letFrac_ = f; }
  void setLetFloor (double s) { kLetTmin = s; } // p2 opt-in
  void setNaiveRefresh (double s) { naiveRefresh_ = s; }
  uint64_t periodicRx () const { return periodicRx_; }
  uint64_t adResets () const { return adResets_; }
  uint64_t adSuppressed () const { return adSuppressed_; }
  // t31 output-only diagnostics
  bool isLet () const { return ctrl_ == Ctrl::Let; }
  bool isNaive () const { return ctrl_ == Ctrl::Naive; }
  uint64_t nvVoidChanges () const { return nvVoidChanges_; }
  uint64_t nvVoidSends () const { return nvVoidSends_; }
  uint64_t nvRefreshSends () const { return nvRefreshSends_; }
  const dahello::LetIntervalStats &letStats () const { return letStats_; }
  const dahello::DahelloProtocol *scheduler () const override { return sched_.get (); }
  void setCtrlTxHook (std::function<void (uint32_t, int)> h) override
  {
    if (sched_)
      {
        sched_->setTxHook (h);
      }
    ctrlHook_ = std::move (h);
  }
  void setHeardOnly (bool on) override
  {
    if (sched_)
      {
        sched_->setHeardOnly (on);
      }
    // Port A (amend1 A5): MARV host on held records only; legacy oracle paths abort.
    heard_ = on;
    p_.heard ().on = on;
    p_.heard ().clock = [] { return Simulator::Now ().GetSeconds (); };
  }
  void setTnb (double s) override { p_.heard ().tNbS = s; }
  void setLeakPlant (int k) override { p_.heard ().leakPlant = k; }
  double heardTnbS () const override { return heard_ ? p_.heard ().tNbS : std::numeric_limits<double>::quiet_NaN (); }
  void forEachNodeCopy (const std::function<void (std::vector<dahello::LqarpNode> &)> &f) override
  {
    f (p_.nodesMut ());
    if (sched_)
      {
        f (sched_->nodesMut ());
      }
  }
  // MARV-specific held write from a decoded frame (V, void, energy, depth; HELLO adds velocity).
  void applyMarvHeard (uint32_t i, uint32_t j, double t, double V, bool voidFlag, double energy,
                       double z, double vx, double vy, double vz, bool isHello)
  {
    p_.hear (i, j, V, voidFlag, energy, t);
    p_.hearZ (i, j, z, t);
    const uint32_t n = static_cast<uint32_t> (p_.nodes ().size ());
    if (heldVel_.size () != n)
      {
        heldVel_.assign (n, std::vector<std::array<double, 3>> (n, {0.0, 0.0, 0.0}));
        heldVelOk_.assign (n, std::vector<char> (n, 0));
      }
    if (isHello && std::isfinite (vx) && std::isfinite (vy) && std::isfinite (vz))
      {
        heldVel_[i][j] = {vx, vy, vz};
        heldVelOk_[i][j] = 1;
        ++periodicRx_;
        if (ctrl_ == Ctrl::Trickle && !nbPrev_.empty () && nbPrev_[i][j])
          {
            ++trC_[i];
          }
      }
  }
  void applyHeardHold (uint32_t i, uint32_t j, double t,
                       double x, double y, double z,
                       double vx, double vy, double vz,
                       double energy, double snrDb,
                       double number, double V) override
  {
    if (ctrl_ == Ctrl::Let && std::isfinite (x) && std::isfinite (y) && std::isfinite (z))
      {
        // t25: HELLO carries 3-D coordinates (QLAR p.11); data frames have x = NaN.
        notePose (i, j, x, y, z, t);
      }
    if (sched_)
      {
        sched_->applyHeardHold (i, j, t, x, y, z, vx, vy, vz, energy, snrDb, number, V);
      }
  }
  void notePose (uint32_t i, uint32_t j, double x, double y, double z, double t)
  {
    const uint32_t n = static_cast<uint32_t> (p_.nodes ().size ());
    if (heldPos_.size () != n)
      {
        heldPos_.assign (n, std::vector<std::array<double, 4>> (n, {0.0, 0.0, 0.0, 0.0}));
        heldPosOk_.assign (n, std::vector<char> (n, 0));
      }
    heldPos_[i][j] = {x, y, z, t};
    heldPosOk_[i][j] = 1;
  }
  // t25: QLAR Eq. 14 / Eq. 6 on MARV (held pose propagated at held velocity).
  double letPeriod (uint32_t j, double now) const
  {
    const uint32_t n = static_cast<uint32_t> (p_.nodes ().size ());
    const auto &a = p_.nodes ()[j];
    const double R = p_.rRangeM ();
    double minLet = std::numeric_limits<double>::infinity ();
    for (uint32_t k = 0; k < n; ++k)
      {
        if (k == j || heldPos_.empty () || heldVelOk_.empty () || !heldPosOk_[j][k] || !heldVelOk_[j][k])
          {
            continue;
          }
        if (heard_ && !p_.fresh (j, k))
          {
            continue;
          }
        const auto &v = heldVel_[j][k];
        const auto &q = heldPos_[j][k];
        const double age = std::max (0.0, now - q[3]);
        const double bb = a.x - (q[0] + v[0] * age), dd = a.y - (q[1] + v[1] * age),
                     ff = a.z - (q[2] + v[2] * age);
        const double aa = a.vx - v[0], cc = a.vy - v[1], ee = a.vz - v[2];
        const double pp = bb * bb + dd * dd + ff * ff;
        if (!(pp < R * R))
          {
            continue;
          }
        const double vv = aa * aa + cc * cc + ee * ee;
        if (!(vv > 1e-18))
          {
            continue;
          }
        const double pv = aa * bb + cc * dd + ee * ff;
        const double disc = pv * pv - vv * (pp - R * R);
        const double t = -pv / vv + std::sqrt (std::max (0.0, disc)) / vv;
        if (t > 0.0)
          {
            minLet = std::min (minLet, t);
          }
      }
    double T = kLetTmax;
    if (std::isfinite (minLet))
      {
        T = letFrac_ * minLet;
      }
    return std::min (kLetTmax, std::max (kLetTmin, T));
  }
  bool heard_ = false;
  std::function<void (uint32_t, int)> ctrlHook_;

  std::string name () const override { return label_; }

  // Qualified set U(i) before the engine's visited/alive filter.
  std::vector<uint32_t> candidateSet (uint32_t i) const override
  {
    auto raw = p_.rangeSet (i);
    std::vector<uint32_t> out;
    if (sched_)
      {
        const uint32_t n = static_cast<uint32_t> (p_.nodes ().size ());
        for (uint32_t j = 0; j < n; ++j)
          {
            if (j != i)
              {
                sched_->inNRange (i, j);
              }
          }
        pollRow (i);
        if (hs_)
          {
            noteBootstraps ();
          }
      }
    for (uint32_t j : raw)
      {
        if (sched_ && !sched_->inNStab (i, j))
          {
            continue;
          }
        if (p_.qualifies (i, j))
          {
            out.push_back (j);
          }
      }
    return out;
  }
  void learnFrom (uint32_t, const std::vector<uint32_t> &) override {}
  int nextHop (uint32_t, const std::vector<uint32_t> &) const override { return -1; }
  std::vector<uint32_t> detourTargets (uint32_t, uint32_t, const std::vector<uint32_t> &) const override
  {
    return {};
  }
  bool useDetour () const override { return false; }
  dahello::ChannelSample linkSample (uint32_t i, uint32_t j) const override { return p_.linkSample (i, j); }
  void setEnergy (uint32_t i, double joules) override
  {
    p_.setEnergy (i, joules);
    if (sched_)
      {
        sched_->setEnergy (i, joules);
      }
  }
  double energy (uint32_t i) const override { return p_.energy (i); }
  double rRangeM () const override { return p_.rRangeM (); }
  const std::vector<dahello::LqarpNode> &nodes () const override { return p_.nodes (); }
  uint64_t controlTxCount () const override
  {
    if (sched_)
      {
        return sched_->controlTxCount ();
      }
    return periodicTx_;
  }
  uint64_t eventHelloCount () const override
  {
    return sched_ ? sched_->eventHelloCount () : periodicTx_;
  }
  uint64_t bootstrapHelloCount () const override
  {
    return sched_ ? sched_->bootstrapHelloCount () : 0;
  }
  uint64_t energyHelloCount () const override { return sched_ ? sched_->energyHelloCount () : 0; }
  int energyBeaconBits () const override { return sched_ ? sched_->energyBeaconBits () : 0; }
  void tick (double dt) override
  {
    if (sched_)
      {
        sched_->tick (dt);
        // p8: heard mode writes V/void/energy/depth from the decoded frame
        // (applyMarvHeard); pollPair would re-read the sender's live V at poll time.
        if (!heard_)
          {
            pollAll ();
          }
        if (hs_)
          {
            afterHsTick ();
          }
      }
    if (ctrl_ == Ctrl::Periodic && tCtrl_ > 0.0 && !pjOn_) // p15: jittered sends are self-scheduled (pjFire)
      {
        const double now = Simulator::Now ().GetSeconds ();
        while (now + 1e-9 >= nextPeriodic_)
          {
            periodicRound ();
            nextPeriodic_ += tCtrl_;
          }
      }
    if (ctrl_ == Ctrl::Trickle || ctrl_ == Ctrl::Speed || ctrl_ == Ctrl::Let || ctrl_ == Ctrl::Naive)
      {
        adaptiveTick (Simulator::Now ().GetSeconds ());
      }
  }
  bool wantsTick () const override { return ctrl_ != Ctrl::Off; }
  void noteDataReception (uint32_t rx, uint32_t tx) override
  {
    if (sched_)
      {
        sched_->noteReception (rx, tx);
      }
  }
  bool hopMathFinite (uint32_t i, uint32_t j) const override
  {
    return std::isfinite (p_.reward (i, j).r) && std::isfinite (p_.qValue (i, j));
  }
  void setKinematics (uint32_t i, double x, double y, double z, double vx, double vy, double vz) override
  {
    p_.setKinematics (i, x, y, z, vx, vy, vz);
    if (sched_)
      {
        sched_->setKinematics (i, x, y, z, vx, vy, vz);
      }
  }
  bool peekReward (uint32_t i, uint32_t j, RewardPeek &out) const override
  {
    const auto rw = p_.reward (i, j);
    out.ok = true;
    out.q = p_.qValue (i, j);
    out.R = rw.r;
    out.c_ene = rw.Ej;
    out.c_del = rw.D;
    out.c_nei = rw.psat;
    out.c_lin = rw.Rij;
    return true;
  }
  bool peekQ (uint32_t i, uint32_t j, double &q) const override
  {
    q = p_.qValue (i, j);
    return true;
  }

private:
  bool isAlive (uint32_t i) const { return !alive_ || alive_ (i); }

  bool stateDirtyFire (uint32_t j)
  {
    if (daMode_ > 0)
      {
        const int d = daFire (j);
        pendCause_[j] = d;
        return d != 0;
      }
    const int c = stateDirtyCause (j);
    pendCause_[j] = c;
    return c != 0;
  }
  void noteBootstraps () const
  {
    const uint32_t n = static_cast<uint32_t> (p_.nodes ().size ());
    for (uint32_t j = 0; j < n; ++j)
      {
        const uint64_t b = sched_->bootstrapFromCount (j);
        if (b != bootSeen_[j])
          {
            bootSeen_[j] = b;
            noteStateSent (j);
          }
      }
  }
  // After a scheduler tick: a link HELLO from j carried j's host state
  // (counted as link; a pending host-state change is "preempted"); a
  // host-state HELLO delivers V / void / residual / congestion 0 (H4) to
  // every alive node that passes the HELLO reception gate.
  void afterHsTick ()
  {
    const double now = Simulator::Now ().GetSeconds ();
    const uint32_t n = static_cast<uint32_t> (p_.nodes ().size ());
    noteBootstraps ();
    for (uint32_t j = 0; j < n; ++j)
      {
        if (sched_->linkFiredLastTick (j))
          {
            if (stateDirty (j))
              {
                ++hsPreempt_;
              }
            noteStateSent (j);
            continue;
          }
        if (!sched_->hostFiredLastTick (j))
          {
            continue;
          }
        if (pendCause_[j] == 1)
          {
            ++hsVoid_;
          }
        else
          {
            ++hsV_;
          }
        // p8: in heard mode the host-state HELLO reaches receivers only through the
        // medium decode (txHook kind 3 -> tx-time frame snapshot -> applyMarvHeard).
        // The legacy direct delivery below uses the live link gate (linkSample) and the
        // sender's live state; it is skipped in heard mode (--leakTest=7 re-enables it).
        if (!heard_ || p_.heard ().leakPlant == 7)
          {
            for (uint32_t k = 0; k < n; ++k)
              {
                if (k == j || !isAlive (k) || !sched_->helloReachesView (k, j))
                  {
                    continue;
                  }
                p_.hear (k, j, p_.advertisedV (j), p_.advertisedVoid (j), p_.energy (j), now);
              }
          }
        noteStateSent (j);
      }
  }

  // Control packet (D5): V + void flag, residual energy, depth, congestion
  // number 0 (D3). Receiver k keeps it iff k is in range and demodulates it
  // (the binary's HELLO reception gate, --helloRxGate=1).
  void periodicRound ()
  {
    const double now = Simulator::Now ().GetSeconds ();
    const uint32_t n = static_cast<uint32_t> (p_.nodes ().size ());
    for (uint32_t j = 0; j < n; ++j)
      {
        if (!isAlive (j))
          {
            continue;
          }
        sendOne (j, now);
      }
  }
  void sendOne (uint32_t j, double now)
  {
    const uint32_t n = static_cast<uint32_t> (p_.nodes ().size ());
    ++periodicTx_;
    if (ctrlHook_)
      {
        ctrlHook_ (j, 1);
      }
    if (heard_)
      {
        // heard: receivers learn only from the medium decode (applyMarvHeard)
        trV_.resize (n, 0.0);
        trVoid_.resize (n, 0);
        trV_[j] = p_.advertisedV (j); // own advertised state of the sender (I1)
        trVoid_[j] = p_.advertisedVoid (j) ? 1 : 0;
        return;
      }
    if (heldVel_.size () != n)
      {
        heldVel_.assign (n, std::vector<std::array<double, 3>> (n, {0.0, 0.0, 0.0}));
        heldVelOk_.assign (n, std::vector<char> (n, 0));
      }
    const auto &nj = p_.nodes ()[j];
    for (uint32_t k = 0; k < n; ++k)
      {
        if (k == j || !isAlive (k) || !p_.inRange (j, k))
          {
            continue;
          }
        if (!gate_->receptionOk (p_.linkSample (j, k)))
          {
            continue;
          }
        p_.hear (k, j, p_.advertisedV (j), p_.advertisedVoid (j), p_.energy (j), now);
        ++periodicRx_;
        heldVel_[k][j] = {nj.vx, nj.vy, nj.vz};
        heldVelOk_[k][j] = 1;
        if (ctrl_ == Ctrl::Let)
          {
            notePose (k, j, nj.x, nj.y, nj.z, now);
          }
        if (ctrl_ == Ctrl::Trickle && !nbPrev_.empty () && nbPrev_[k][j])
          {
            ++trC_[k];
          }
      }
    trV_.resize (n, 0.0);
    trVoid_.resize (n, 0);
    trV_[j] = p_.advertisedV (j);
    trVoid_[j] = p_.advertisedVoid (j) ? 1 : 0;
  }
  // NOVELTY step 2 (design choice): adaptive HELLO baselines on MARV.
  // nb[j][k] = 1 iff j decodes k now (same gate as a periodic HELLO).
  void neighbourSets (std::vector<std::vector<char>> &nb) const
  {
    const uint32_t n = static_cast<uint32_t> (p_.nodes ().size ());
    nb.assign (n, std::vector<char> (n, 0));
    for (uint32_t j = 0; j < n; ++j)
      {
        if (!isAlive (j))
          {
            continue;
          }
        for (uint32_t k = 0; k < n; ++k)
          {
            if (heard_ && p_.heard ().leakPlant != 3)
              {
                // heard: own table (j's fresh records); amend1 per-policy rule
                if (k != j && p_.fresh (j, k))
                  {
                    nb[j][k] = 1;
                  }
                continue;
              }
            if (k != j && isAlive (k) && p_.inRange (k, j)
                && gate_->receptionOk (p_.linkSample (k, j)))
              {
                nb[j][k] = 1;
              }
          }
      }
  }
  double saPeriod (uint32_t j) const
  {
    const uint32_t n = static_cast<uint32_t> (p_.nodes ().size ());
    const auto &a = p_.nodes ()[j];
    double S = 0.0;
    for (uint32_t k = 0; k < n; ++k)
      {
        if (k == j || heldVelOk_.empty () || !heldVelOk_[j][k])
          {
            continue;
          }
        if (heard_ ? !p_.fresh (j, k) : (!isAlive (k) || !p_.inRange (j, k)))
          {
            continue;
          }
        const auto &v = heldVel_[j][k];
        S = std::max (S, std::sqrt ((a.vx - v[0]) * (a.vx - v[0]) + (a.vy - v[1]) * (a.vy - v[1])
                                    + (a.vz - v[2]) * (a.vz - v[2])));
      }
    double T = kSaTmax;
    if (S > 1e-12)
      {
        T = kSaA * p_.rRangeM () / (2.0 * S);
      }
    return std::min (kSaTmax, std::max (kSaTmin, T));
  }
  void trickleStart (uint32_t j, double t0)
  {
    std::uniform_real_distribution<double> u (0.5, 1.0);
    trStart_[j] = t0;
    trC_[j] = 0;
    trSent_[j] = 0;
    trT_[j] = t0 + trI_[j] * u (adRng_);
  }
  void adaptiveInit ()
  {
    const uint32_t n = static_cast<uint32_t> (p_.nodes ().size ());
    adRng_.seed (adSeed_);
    neighbourSets (nbPrev_);
    adNext_.assign (n, 0.0);
    trI_.assign (n, kTrImin);
    trStart_.assign (n, 0.0);
    trT_.assign (n, 0.0);
    trC_.assign (n, 0);
    trSent_.assign (n, 0);
    for (uint32_t j = 0; j < n; ++j)
      {
        if (ctrl_ == Ctrl::Trickle)
          {
            trickleStart (j, 0.0);
            trSent_[j] = 1;
          }
        else if (ctrl_ == Ctrl::Let)
          {
            adNext_[j] = letPeriod (j, 0.0);
            letStats_.add (adNext_[j]); // t31 output-only
          }
        else if (ctrl_ == Ctrl::Naive)
          {
            adNext_[j] = naiveRefresh_;
          }
        else
          {
            adNext_[j] = saPeriod (j);
          }
      }
    if (ctrl_ == Ctrl::Naive)
      {
        nvVoid_.assign (n, 0);
        for (uint32_t j = 0; j < n; ++j)
          {
            nvVoid_[j] = p_.advertisedVoid (j) ? 1 : 0;
          }
      }
  }
  void adaptiveTick (double now)
  {
    const uint32_t n = static_cast<uint32_t> (p_.nodes ().size ());
    std::vector<std::vector<char>> nb;
    neighbourSets (nb);
    std::vector<uint32_t> due;
    if (ctrl_ == Ctrl::Trickle)
      {
        const double imax = kTrImin * std::ldexp (1.0, kTrDoublings);
        for (uint32_t j = 0; j < n; ++j)
          {
            if (!isAlive (j))
              {
                continue;
              }
            while (now + 1e-9 >= trStart_[j] + trI_[j])
              {
                const double end = trStart_[j] + trI_[j];
                trI_[j] = std::min (2.0 * trI_[j], imax);
                trickleStart (j, end);
              }
            // Inconsistency: neighbour set changed, or own advertised MARV
            // state (void flag, V beyond theta_V = g_c) differs from the
            // state in j's last HELLO (RPL analogue: rank change).
            const bool hostChange = !trStrict_ && (((p_.advertisedVoid (j) ? 1 : 0) != trVoid_[j])
                                    || std::fabs (p_.advertisedV (j) - trV_[j]) > p_.config ().gc);
            if ((nb[j] != nbPrev_[j] || hostChange) && trI_[j] > kTrImin + 1e-9)
              {
                trI_[j] = kTrImin;
                trickleStart (j, now);
                ++adResets_;
              }
            if (!trSent_[j] && now + 1e-9 >= trT_[j])
              {
                trSent_[j] = 1;
                if (trC_[j] < kTrK)
                  {
                    due.push_back (j);
                  }
                else
                  {
                    ++adSuppressed_;
                  }
              }
          }
        for (uint32_t j : due)
          {
            sendOne (j, now);
          }
      }
    else if (ctrl_ == Ctrl::Naive)
      {
        // t25 naive: HELLO iff MARV's own void flag changed, or refresh due.
        for (uint32_t j = 0; j < n; ++j)
          {
            const char v = p_.advertisedVoid (j) ? 1 : 0;
            const bool changed = (v != nvVoid_[j]);
            nvVoid_[j] = v;
            if (!isAlive (j))
              {
                continue;
              }
            if (changed)
              {
                ++nvVoidChanges_;
              }
            if (changed || now + 1e-9 >= adNext_[j])
              {
                due.push_back (j);
                ++(changed ? nvVoidSends_ : nvRefreshSends_); // t31 output-only
              }
          }
        for (uint32_t j : due)
          {
            sendOne (j, now);
          }
        for (uint32_t j : due)
          {
            adNext_[j] = now + naiveRefresh_;
          }
      }
    else
      {
        for (uint32_t j = 0; j < n; ++j)
          {
            if (isAlive (j) && now + 1e-9 >= adNext_[j])
              {
                due.push_back (j);
              }
          }
        for (uint32_t j : due)
          {
            sendOne (j, now);
          }
        for (uint32_t j : due)
          {
            if (ctrl_ == Ctrl::Let)
              {
                const double th = letPeriod (j, now);
                letStats_.add (th); // t31 output-only
                adNext_[j] = now + th;
              }
            else
              {
                adNext_[j] = now + saPeriod (j);
              }
          }
      }
    nbPrev_ = nb;
  }

  // A HELLO (bootstrap, event or periodic) from j that i demodulated shows
  // up as a new lastZohTime(i,j); it carries j's current V / void flag and
  // the residual the scheduler copied (energy_j_zoh). An energy-only
  // beacon moves energy_j_zoh without a new lastZohTime.
  void pollPair (uint32_t i, uint32_t j) const
  {
    const double lz = sched_->lastZohTime (i, j);
    const auto &s = sched_->ekfMeta (i, j); // OPT-4: energy_j_zoh / zohHeard only
    const double now = Simulator::Now ().GetSeconds ();
    if (std::isfinite (lz) && (!std::isfinite (polledZoh_[i][j]) || lz != polledZoh_[i][j]))
      {
        if (pollStored_)
          {
            // p15 A2.2(e) --pollStored=1: V / void / t from i's stored record of j (as last
            // written by a decoded frame); energy from the stored hold. No live read.
            const dahello::MarvRecord r = p_.record (i, j);
            if (r.heard)
              {
                p_.hear (i, j, r.V, r.voidFlag, s.energy_j_zoh, r.t);
                ++pollStoredW_;
              }
          }
        else
          {
            p_.hear (i, j, p_.advertisedV (j), p_.advertisedVoid (j), s.energy_j_zoh, now);
            ++pollLiveW_;
          }
        polledZoh_[i][j] = lz;
        polledE_[i][j] = s.energy_j_zoh;
        return;
      }
    if (s.zohHeard && s.energy_j_zoh != polledE_[i][j])
      {
        if (pollStored_)
          {
            const dahello::MarvRecord r = p_.record (i, j);
            p_.hearEnergy (i, j, s.energy_j_zoh, r.t); // p15 (e): keep stored freshness time
            ++pollEnergyStoredW_;
          }
        else
          {
            p_.hearEnergy (i, j, s.energy_j_zoh, now);
            ++pollEnergyLiveW_;
          }
        polledE_[i][j] = s.energy_j_zoh;
      }
  }
  void pollRow (uint32_t i) const
  {
    const uint32_t n = static_cast<uint32_t> (p_.nodes ().size ());
    for (uint32_t j = 0; j < n; ++j)
      {
        if (j != i)
          {
            pollPair (i, j);
          }
      }
  }
  void pollAll () const
  {
    const uint32_t n = static_cast<uint32_t> (p_.nodes ().size ());
    for (uint32_t i = 0; i < n; ++i)
      {
        pollRow (i);
      }
  }

  dahello::ChannelGate *gate_;
  mutable dahello::MarvProtocol p_;
  std::unique_ptr<dahello::DahelloProtocol> sched_;
  std::string label_;
  Ctrl ctrl_;
  double tCtrl_;
  double nextPeriodic_ = 0.0;
  uint64_t periodicTx_ = 0;
  std::function<bool (uint32_t)> alive_;
  mutable std::vector<std::vector<double>> polledZoh_;
  mutable std::vector<std::vector<double>> polledE_;
  bool hs_ = false;
  double thetaV_ = 0.0;
  mutable std::vector<double> vLast_;
  mutable std::vector<char> voidLast_;
  mutable std::vector<uint64_t> bootSeen_;
  std::vector<int> pendCause_;
  uint64_t hsVoid_ = 0, hsV_ = 0, hsPreempt_ = 0;
  int daMode_ = 0;
  double daMargin_ = 0.0, daImin_ = 5.0, daImax_ = 80.0, daTnb_ = 80.0;
  mutable std::vector<double> eLast_;
  std::vector<double> daI_, daHold_;
  uint64_t daStrong_ = 0, daWeak_ = 0, daDeferred_ = 0, daNoUp_ = 0;
  bool trStrict_ = false;
  // NOVELTY step 2 state. Parameters: Trickle Imin 5 s, 4 doublings,
  // k = 5 (RFC 6206); speed-adaptive a = 0.1, T in [5, 60] s.
  static constexpr double kTrImin = 5.0;
  static constexpr int kTrDoublings = 4;
  static constexpr int kTrK = 5;
  static constexpr double kSaA = 0.1;
  static constexpr double kSaTmin = 5.0;
  static constexpr double kSaTmax = 60.0;
  // t25 (marv-let / marv-naive): see DahelloConfig letFrac/letTminS/letTmaxS/naiveRefreshS.
  double kLetTmin = 0.0; // p2: opt-in --letFloor (default 0 = QLAR, no floor)
  static constexpr double kLetTmax = 120.0;
  double letFrac_ = 0.3;
  double naiveRefresh_ = 90.0;
  std::vector<std::vector<std::array<double, 4>>> heldPos_;
  std::vector<std::vector<char>> heldPosOk_;
  std::vector<char> nvVoid_;
  uint64_t nvVoidChanges_ = 0;
  uint64_t nvVoidSends_ = 0, nvRefreshSends_ = 0; // t31 output-only
  dahello::LetIntervalStats letStats_;             // t31 output-only
  uint32_t adSeed_ = 1;
  // p15 A2.2(a)/(e) state + output-only counters (written to --recWriteOut, never stdout)
  bool pjOn_ = false;
  uint64_t pjSeed_ = 0;
  std::vector<double> pjOff_, pjFirst_;
  bool pollStored_ = false;
  uint64_t buildRoundRx_ = 0;
  mutable uint64_t pollLiveW_ = 0, pollStoredW_ = 0, pollEnergyLiveW_ = 0, pollEnergyStoredW_ = 0;
  uint64_t periodicRx_ = 0, adResets_ = 0, adSuppressed_ = 0;
  std::mt19937 adRng_;
  std::vector<double> adNext_, trI_, trStart_, trT_, trV_;
  std::vector<int> trC_;
  std::vector<char> trSent_, trVoid_;
  std::vector<std::vector<char>> nbPrev_;
  std::vector<std::vector<std::array<double, 3>>> heldVel_;
  std::vector<std::vector<char>> heldVelOk_;
};



struct Pkt
{
  uint64_t id = 0;
  uint32_t src = 0;
  uint32_t at = 0;
  double genTime = 0.0;
  uint32_t hops = 0;
  uint32_t detourHops = 0;
  // True once any hop on this packet was admitted by the frozen
  // candidate filters while the live geometry would have rejected it.
  // Diagnostic only; next-hop selection does not read it.
  bool viaStale = false;
  std::vector<uint32_t> visited;
  // Live range∩SNR∩depth hop distance from the source at generation.
  // -2 means the diagnostic did not run. Not read by next-hop selection.
  int liveBfsAtSrc = -2;
  // MARV engine only (unused by the other hosts).
  int prevSender = -1;     // node whose transmission delivered this copy
  int prevTop = -1;        // first entry of that sender's priority list
  uint32_t pos = 0;        // 1-based position in that priority list
  double tRecv = 0.0;      // reception time at this node
  int64_t parentTx = -1;   // id of the transmission that delivered this copy
  // Port A (medium mode only): unicast ARQ attempt index of this hop.
  int medAttempt = 0;
};

struct SnrSample
{
  double t = 0.0;
  double x = 0.0;
  double y = 0.0;
  double z = 0.0;
  double rangeH = 0.0;
  double snrDb = std::numeric_limits<double>::quiet_NaN ();
  bool valid = false;
};

struct Census
{
  double t = 0.0;
  uint32_t nAliveAuv = 0;
};

struct VRelAcc
{
  uint64_t nEval = 0;
  uint64_t nAuvAuv = 0;
  uint64_t nToSink = 0;
  uint64_t nFromSink = 0;
  uint64_t nAbsGt1e6 = 0;
  uint64_t nAbsGt01 = 0;
  uint64_t nAbsGt05 = 0;
  uint64_t nAbsGt10 = 0;
  double absSum = 0.0;
  double absMax = 0.0;
  double absSumAuvAuv = 0.0;
  double absMaxAuvAuv = 0.0;
  double absSumToSink = 0.0;
  double absMaxToSink = 0.0;
  uint64_t auvAuvGt1e6 = 0;
  uint64_t auvAuvGt01 = 0;
  uint64_t toSinkGt01 = 0;
  uint64_t nHeard = 0;
  uint64_t nUnheard = 0;
  uint64_t nAuvAuvHeard = 0;
  uint64_t nAuvAuvUnheard = 0;
  uint64_t auvAuvHeardGt1e6 = 0;
  uint64_t auvAuvHeardGt01 = 0;
  double absSumHeard = 0.0;
  double absSumUnheard = 0.0;
  double absSumAuvHeard = 0.0;
  double absSumAuvUnheard = 0.0;
  double absMaxHeard = 0.0;
  double absMaxAuvHeard = 0.0;
  uint64_t hist[7] = {};
  uint64_t histAuvAuv[7] = {};
  uint64_t histToSink[7] = {};

  static int bucket (double a)
  {
    if (a < 1.0e-6)
      {
        return 0;
      }
    if (a < 0.01)
      {
        return 1;
      }
    if (a < 0.1)
      {
        return 2;
      }
    if (a < 0.5)
      {
        return 3;
      }
    if (a < 1.0)
      {
        return 4;
      }
    if (a < 2.0)
      {
        return 5;
      }
    return 6;
  }

  void add (double vr, bool iIsSink, bool jIsSink)
  {
    const double a = std::fabs (vr);
    const int b = bucket (a);
    ++nEval;
    absSum += a;
    absMax = std::max (absMax, a);
    hist[b]++;
    if (a > 1.0e-6)
      {
        ++nAbsGt1e6;
      }
    if (a > 0.01)
      {
        ++nAbsGt01;
      }
    if (a > 0.05)
      {
        ++nAbsGt05;
      }
    if (a > 0.1)
      {
        ++nAbsGt10;
      }
    if (!iIsSink && !jIsSink)
      {
        ++nAuvAuv;
        absSumAuvAuv += a;
        absMaxAuvAuv = std::max (absMaxAuvAuv, a);
        histAuvAuv[b]++;
        if (a > 1.0e-6)
          {
            ++auvAuvGt1e6;
          }
        if (a > 0.01)
          {
            ++auvAuvGt01;
          }
      }
    else if (jIsSink)
      {
        ++nToSink;
        absSumToSink += a;
        absMaxToSink = std::max (absMaxToSink, a);
        histToSink[b]++;
        if (a > 0.01)
          {
            ++toSinkGt01;
          }
      }
    else
      {
        ++nFromSink;
      }
  }

  void addHeard (bool heard, double vr, bool auvAuv)
  {
    const double a = std::fabs (vr);
    if (heard)
      {
        ++nHeard;
        absSumHeard += a;
        absMaxHeard = std::max (absMaxHeard, a);
        if (auvAuv)
          {
            ++nAuvAuvHeard;
            absSumAuvHeard += a;
            absMaxAuvHeard = std::max (absMaxAuvHeard, a);
            if (a > 1.0e-6)
              {
                ++auvAuvHeardGt1e6;
              }
            if (a > 0.01)
              {
                ++auvAuvHeardGt01;
              }
          }
      }
    else
      {
        ++nUnheard;
        absSumUnheard += a;
        if (auvAuv)
          {
            ++nAuvAuvUnheard;
            absSumAuvUnheard += a;
          }
      }
  }
};

struct PVelSample
{
  double t = 0.0;
  bool heard = false;
  int bc = 0;
  double p33 = 0.0;
  double p44 = 0.0;
  double p55 = 0.0;
  double vrel = 0.0;
  double gap = std::numeric_limits<double>::quiet_NaN ();
};

struct EkfTick
{
  double t = 0.0;
  double x[7] = {};
  double xForecast[7] = {};
  double e_k = 0.0;
  double e_km1 = 0.0;
  double epsilon_k = 0.0;
  double S_k = 0.0;
  double eTrigger = 0.0;
  double STrigger = 0.0;
  double epsilonTrigger = 0.0;
  int bc = 0;
  bool heard = false;
  double vrelZoh = 0.0;
  double rangeHTrue = 0.0;
  double rangeHEkf = 0.0;
  double rangeHForecast = 0.0;
  double pijTrueX = 0.0;
  double pijTrueY = 0.0;
  double pijTrueZ = 0.0;
  double vTrueX = 0.0;
  double vTrueY = 0.0;
  double vTrueZ = 0.0;
  double snrMeas = std::numeric_limits<double>::quiet_NaN ();
  double dsnrDr = std::numeric_limits<double>::quiet_NaN ();
  // Measurement-function Jacobian of predicted SNR vs kinematics.
  // Kalman update itself uses H = [0 0 0 0 0 0 1] on the SNR state.
  double hPx = 0.0;
  double hPy = 0.0;
  double hPz = 0.0;
  double hVx = 0.0;
  double hVy = 0.0;
  double hVz = 0.0;
};

// Port A (2026-10-12): scenario payload of one medium frame (medium mode only).
// The header fields are filled at transmission start from the sender's own state.
struct PFrame
{
  Pkt pkt;                     // data frames: the packet copy
  std::vector<uint32_t> list;  // MARV: priority list
  int64_t marvTxId = -1;       // MARV: transmission id (siblings share it as parentTx)
  bool detour = false;         // unicast: hop chosen from the detour set
  bool staleHop = false;       // unicast: diagnostic, as legacy
  uint64_t ackFor = 0;         // ACK: packet id
  int ackPrevSender = -1, ackPrevTop = -1; // MARV sink ACK: implicit-ack fields
  double txStart = 0.0;
  // header (sender's own state at tx start)
  double hz = 0, he = 0, hV = 0;
  bool hVoid = false;
  // heard mode: full tx-time snapshot (HELLO/beacon pose+velocity, data/hello count)
  double hx = 0, hy = 0, hvx = 0, hvy = 0, hvz = 0, hnum = 0;
  double hG = std::numeric_limits<double>::quiet_NaN (); // QELAR group-average energy
  int cause = 0;               // control frames: scheduler cause
};

struct Pipeline
{
  uint32_t nAuv = 20;
  uint32_t sinkId = 0;
  double tSim = 1600.0;
  double pktInterval = 30.0;
  double drainS = 120.0;
  uint32_t seed = 20260921;
  uint32_t rngRun = 1;
  // Bellhop query depth for the sink only. Kinematic z stays kSinkDepthM.
  double sinkAcousticDepthM = 50.0;
  bool diagSnrToSink = false;
  bool diagCandidates = false;
  bool diagRlor = false;
  bool diagKinematics = false;
  bool quiet = false;
  // Step 1d: count sink admission vs reward. Does not change next hop.
  bool hopDiag = false;
  std::string protoName = "lqarp"; // lqarp | dahello | rlor | periodic
  std::string scenario = "i";      // i | ii | iii
  double driftSpeed = 2.0;         // II: ||v_drift||; III: V_eddy (m/s)
  std::string boundary = "remove"; // Step 30: remove on XY exit (open kept for ablation)
  double beta1 = std::numeric_limits<double>::quiet_NaN ();
  double beta2 = std::numeric_limits<double>::quiet_NaN ();
  double epsilon0 = std::numeric_limits<double>::quiet_NaN ();
  // Fixed-Periodic control period override (s). NaN keeps DahelloConfig::T_ctrl (5 s).
  double tCtrl = std::numeric_limits<double>::quiet_NaN ();
  // Communication range r_range override (m) for every protocol. NaN keeps
  // ChannelGate::rangeAtSnrThreshold() (R*, the SNR-gate crossing).
  double rRangeOverride = std::numeric_limits<double>::quiet_NaN ();
  // Opt-in hop-distance census (live candidate graph BFS to the sink every
  // geoEveryS). It calls SyncPoses, so it is a separate measurement run.
  bool geoCensus = false;
  // Opt-in: one DEC line per primary forwarding decision with every
  // candidate's Q / R / c_ene / c_del / c_nei (read after learnFrom).
  // Step 5: reception-gate every HELLO / beacon hold write (default on).
  bool helloRxGate = true;
  // DECISION-AWARE (design choices; all default off => code of record)
  int daMode = 0;            // 1 DA, 2 DA + back-off (MARV host-state arms)
  double daMargin = 0.0;     // rank-crossing margin m (score units)
  double daImin = 5.0;       // back-off Imin (s)
  int daDoublings = 4;       // Imax = Imin * 2^doublings
  double daTnb = 80.0;       // neighbour-record recency window (s)
  bool daBootFix = false;    // one bootstrap broadcast per sender per first contact
  bool daLinkOff = false;    // link (Kalman) trigger off
  bool decisionTrace = false;
  std::ostringstream decTrace;
  double geoEveryS = 60.0;
  uint64_t geoN = 0, geo1 = 0, geo2 = 0, geo3p = 0, geoUnreach = 0, geoOutsideR = 0;
  uint64_t geoRange1 = 0, geoRange2 = 0, geoRange3p = 0, geoRangeUnreach = 0;
  // Energy beacon: residual quantized to this many bits, plus an 8-bit sender id.
  int energyQuantBits = 8;
  int energyAddrBits = 8;
  double tickDt = 1.0;
  uint32_t snrTraceId = 1;         // representative AUV (sink is 0)
  double snrTraceEvery = 100.0;
  std::string dataDir = "${WORKSPACE}/data/channel_db";
  std::string seasonName = "january";
  std::string outPath = "${WORKSPACE}/results/scenario-i-lqarp.txt";

  dahello::AcousticEnergyModel energy;
  std::unique_ptr<dahello::ChannelGate> gate;
  std::unique_ptr<Fwd> proto;
  NodeContainer nodes;

  // MARV engine state (unused by the other hosts).
  std::string marvPsatScope;     // list | top (required for marv-*)
  std::string marvSinkOverheard; // ack | literal (required for marv-*)
  MarvFwd *marv = nullptr;
  static constexpr int kMarvVoid = 0;
  static constexpr int kMarvSnr = 1;
  static constexpr int kMarvEnergy = 2;
  static constexpr int kMarvTtl = 3;
  static constexpr int kMarvEnvelope = 4;
  struct MarvFwdEvt
  {
    double t = 0.0;
    uint32_t node = 0;
    int64_t parentTx = -1; // -1: sink acceptance (heard by every holder)
  };
  struct MarvPk
  {
    bool delivered = false;
    bool closed = false;
    uint32_t live = 0;
    int pendFate = -1;
    Pkt pendPkt;
    std::vector<MarvFwdEvt> fwd;
  };
  std::vector<MarvPk> marvPk;
  std::vector<std::vector<char>> marvSeen;
  int64_t marvNextTxId = 0;
  uint64_t marvTx = 0, marvTxAfterDelivery = 0, marvCopies = 0, marvRxFail = 0;
  uint64_t marvSuppressed = 0, marvDupDiscard = 0, marvDupAtSink = 0;
  uint64_t marvLossAfterDelivery = 0, marvNoFate = 0, marvListLenSum = 0;

  // ---- Port A (2026-10-12): shared acoustic medium. Every feature is behind a
  // flag whose default reproduces the legacy engine (macMode = legacy). ----
  std::string macMode = "legacy";   // legacy | ideal | aloha | cs | slotted
  std::string chanMode = "bellhop"; // medium only: bellhop (legacy table via ChannelGate) | formula
  std::string powerName = "legacy"; // medium only: s2c | s2c-tx28 | s2c-wake | whoi2 | ns3mm | legacy
  double phyBkHz = 5.0, phyRbBps = 5000.0, phyTpreS = 0.05, phyRopM = 1200.0;
  double phyShipping = 0.2, phyWindMps = 2.0, phySl = std::numeric_limits<double>::quiet_NaN ();
  std::string phyPer = "auto";      // auto: formula -> bfsk, bellhop -> thr at the legacy gate
  double phyThr = std::numeric_limits<double>::quiet_NaN (); // thr mode; NaN = 8.68 dB
  std::string trafficPhase = "sync"; // sync (legacy: all nodes at k*interval) | random (U[0,interval) per node)
  std::string topo = "t0";           // t0 (legacy 6x6x0.5 km) | t1 (4x4x3.5 km deep column)
  double letFrac = std::numeric_limits<double>::quiet_NaN ();      // t23: letadapt T = letFrac*minLET
  double letFloor = std::numeric_limits<double>::quiet_NaN ();     // p2: opt-in LET interval floor (s); NaN = off
  int lifetimeOut = 0;                                             // p2: 1 = print output-only LIFETIME_PROJ line
  double naiveRefresh = std::numeric_limits<double>::quiet_NaN (); // t23: naivevoid refresh (s)
  std::string hostSched = "off";      // PORT A: QELAR/SAQR baseline HELLO schedule
  std::string discMode = "oracle";   // oracle (legacy) | heard (Gate 2; requires medium)
  int leakTest = 0;                  // 0 off; 1..4 planted leaks (must exit 3)
  int lqarpPosDa = 0;                // 0 off; 1 displacement-proxy HELLO (scaffold); 2 membership/rank test (p6)
  double lqarpPosDaMargin = 0.0;     // p6: rank margin m (score units)
  int lqarpNoPosePiggyback = 0;      // p14: 0 = Liu III-D pose+E in LQARP data hdr (default); 1 = pre-fix ablation
  // p9 comparators (each off by default; MARV host, heard mode, shared medium only)
  int b6Dist = 0;                    // B6 distance/kinematic trigger (host marv-ctrl-off)
  double b6Dth = 250.0, b6Zth = 50.0, b6Tmax = 300.0, b6CheckS = 1.0;
  int b8NaiveB = 0;                  // B8 Naive-B = marv-naive + first-contact reply
  double b8Tr = 120.0;               // B8 refresh T_r (s); 0 = infinity
  int b9FcOnly = 0;                  // B9 FC-only = marv-ctrl-off + first-contact reply
  bool fcOn = false;                 // derived: first-contact reply active (B8 or B9)
  std::vector<cmp::FcState> fcSt;
  std::mt19937_64 fcRng{0};
  uint64_t fcSched = 0, fcTx = 0, fcCancel = 0, fcRateLim = 0;
  cmp::OctrlTally octrlTally;        // p12 Fix B (output only): medium HELLO/beacon tx on [0,T_sim]
  std::vector<std::array<double, 4>> b6Anchor; // own x, y, z, t at the last HELLO this node sent
  std::vector<char> b6Pend;
  uint64_t b6Dist1 = 0, b6Depth = 0, b6TmaxN = 0, b6Evals = 0;
  void InstallComparators ();
  void CmpB6Tick ();
  // p13 (2026-10-16): B2 Trickle, B3 TAP, B4 SpeedOpt on marv-ctrl-off; P refresh T_r (decisions D2)
  int b2Trickle = 0; double b2Imin = 5.0; int b2Doublings = 4; int b2K = 5; std::string b2Incons = "table";
  int b3Tap = 0; double b3Ropt = 0.04;
  int b4Speed = 0; double b4A = 0.1;
  double daRefresh = 0.0;            // P refresh T_r (s); 0 = infinity (off)
  bool newCmpOn = false;             // derived: any p13 rule active
  std::vector<double> chT, chVx, chVy, chVz; // per (r,s): own held record time + held velocity (HELLO/beacon)
  std::vector<char> chFresh, chTabChg, nfPend;
  std::vector<double> chNew, nfF, nfNext, ownLastHello;
  std::vector<int> chOwnVoid;
  std::vector<cmp::TrickleState> trSt;
  std::mt19937_64 nfRng{0};
  uint64_t n13Tx = 0, trResets = 0, trSuppressed = 0, trCons = 0, trIncons = 0, n13Evals = 0;
  void CmpNewTick ();
  void CmpNewOnOwnHello (uint32_t s, double now);
  void CmpFcFire (uint32_t r, uint64_t tok);
  // p6 state (mode 2 only)
  std::vector<dahello::PosDaPose> pdAdv;   // own pose as last advertised in a HELLO
  std::vector<char> pdAdvSet;
  std::vector<double> pdI, pdH;          // Trickle-style back-off for rank events
  std::vector<char> pdPending;
  uint64_t pdMem = 0, pdRank = 0, pdRankDeferred = 0, pdEvals = 0, pdSkipNonfinite = 0;
  double tNbS = 3600.0;              // neighbour freshness (heard mode)
  bool tNbExplicit = false;          // item 4: --tNb given -> also applied to scheduler hosts
  double sinkBeaconS = 0.0;          // item 4: sink pose beacon period (s); 0 = off (pre-item-4 behaviour)
  // p15 Amendment 2 A2.2 switches (all default to p14 behaviour)
  bool periodicJitter = false;       // (a) marv-T<s>: per-node phase U[0,T)
  bool helloOnlyHost = false;        // (b) neighbour state only from HELLO/beacon decodes
  double sinkBeaconPhase = 0.0;      // (d) beacons at phase + sinkBeaconS*k, k >= 1
  bool pollStored = false;           // (e) pollPair reads the stored record
  std::string recWriteOut;           // output-only counter file (empty = none)
  // output-only neighbour-record write counters (no behaviour effect)
  uint64_t rwDec[4] = {0, 0, 0, 0};  // decodes reaching the record-write stage, by frame kind index
  uint64_t rwAckDec = 0;             // ACK decodes (never write)
  uint64_t rwDataSkipped = 0;        // data decodes whose writes were suppressed (--helloOnlyHost=1)
  uint64_t rwMarvHdr = 0, rwMarvHdrSkipped = 0; // 2nd MARV data-header write (porta.inc:577)
  uint64_t rwCmpData = 0, rwCmpDataSkipped = 0; // comparator table writes from data frames
  uint64_t rwCmpCtrl = 0;            // comparator table writes from HELLO/beacon
  void WriteRecWrites () const;
  uint64_t medSinkBcnTx = 0, medSinkBcnOk = 0;
  // amend1 enforcement 2: poison of other nodes' kinematics/energy in each protocol
  // copy for the duration of a host/policy call of actor i (heard mode only).
  std::string poisonMode = "nan";    // nan | big (seeded +-1e9, differential test) | off
  uint32_t poisonSeed = 1;
  uint64_t poisonScopes = 0;
  struct PoisonScope
  {
    Pipeline *p;
    std::vector<std::vector<dahello::LqarpNode>> saved;
    bool on = false;
    PoisonScope (Pipeline *pp, uint32_t actor) : p (pp)
    {
      if (p->discMode != "heard" || p->poisonMode == "off" || !p->proto)
        {
          return;
        }
      on = true;
      ++p->poisonScopes;
      const uint32_t sink = p->sinkId;
      const bool big = (p->poisonMode == "big");
      const uint32_t seed = p->poisonSeed;
      p->proto->forEachNodeCopy ([&] (std::vector<dahello::LqarpNode> &v) {
        saved.push_back (v);
        for (uint32_t k = 0; k < v.size (); ++k)
          {
            if (k == actor)
              {
                continue; // own state is allowed information (I1)
              }
            double val = std::numeric_limits<double>::quiet_NaN ();
            if (big)
              {
                const uint32_t h = (k * 2654435761u) ^ (seed * 40503u);
                val = (h & 1u) ? 1.0e9 : -1.0e9;
              }
            v[k].energy = val;
            if (k == sink)
              {
                continue; // I5: fixed, known sink deployment position (and zero velocity)
              }
            v[k].x = val; v[k].y = val; v[k].z = val;
            v[k].vx = val; v[k].vy = val; v[k].vz = val;
          }
      });
    }
    ~PoisonScope ()
    {
      if (!on)
        {
          return;
        }
      size_t c = 0;
      p->proto->forEachNodeCopy ([&] (std::vector<dahello::LqarpNode> &v) {
        v = saved[c++];
      });
    }
  };
  int dataBits = 400, helloBits = 400, ackBits = 64;
  int arqMax = 4, queueCap = 20, boMax = 5;
  double eBattJ = 864000.0;
  double warmS = 0.0;               // medium report: measurement window [warmS, tSim)
  static constexpr int kMarvMac = 5;
  static constexpr int kMarvArq = 6;
  std::unique_ptr<dahello::AcousticPhy> aphy;
  std::unique_ptr<dahello::AcousticMedium> med;
  dahello::PowerProfile pwr;
  double medMaxPropS = 10.0;
  struct ArqSt
  {
    Pkt pkt;
    uint32_t nh = 0;
    uint64_t gen = 0;
    bool waiting = false;
    bool detour = false;
    bool staleHop = false;
  };
  std::vector<std::map<uint64_t, ArqSt>> medArq;            // per sender, by packet id
  std::vector<std::unordered_set<uint64_t>> medSeenU;       // unicast duplicates per receiver
  std::vector<std::unordered_map<uint64_t, int64_t>> medHold; // MARV: held packet -> parentTx
  std::vector<std::unordered_set<uint64_t>> medSupp;        // MARV: suppressed packets
  std::unordered_map<int64_t, uint32_t> medTxRx;            // MARV tx id -> list members decoded
  std::vector<char> medDelivered;
  std::vector<std::unordered_map<uint64_t, int64_t>> medQd; // MARV: queued packet -> parentTx
  uint64_t medArqGen = 0;
  uint64_t medCtrlByCause[6] = {0, 0, 0, 0, 0, 0};
  uint64_t medRxByKind[dahello::kFrameKinds] = {0, 0, 0, 0};
  std::vector<double> medSnap0, medSnap1;
  std::array<double, 2 + 2 * dahello::kFrameKinds> medSplit0{}, medSplit1{};
  dahello::MediumStats medStats1;
  bool medSnapped1 = false;
  void MakePhy ();
  std::unique_ptr<dahello::ChannelGate> MakeFormulaGate ();
  void MedPos (uint32_t i, double &x, double &y, double &z) const;
  void MedSnapshot (int which);
  void InstallLeakTest ();
  void InstallLqarpPosDa ();
  void LqarpPosDaTick ();
  void ApplyHeardFromDecode (uint32_t r, uint32_t s, double snrDb, const PFrame &pf, uint8_t kind);
  // DIAG (2026-10-12, heard mode only, no behaviour effect): inter-frame gaps of
  // hold-refreshing frames (data/HELLO/beacon) per directed pair r<-s.
  std::vector<double> hgLast;
  uint64_t hgFirst = 0, hgGaps = 0, hgGapsAuv = 0;
  uint64_t hgOver[4] = {0, 0, 0, 0}, hgOverAuv[4] = {0, 0, 0, 0};
  double hgMaxGap = 0.0;

  uint64_t nMacDrop = 0, nQDrop = 0, nArqDrop = 0, medRetx = 0, medAcks = 0, medDupRx = 0;
  uint64_t medDupAtSink = 0, medSuppQueued = 0, medGenW = 0, medDelW = 0;
  double medLatW = 0.0, medHopW = 0.0;
  bool medOn () const { return macMode != "legacy"; }
  void SetupMedium ();
  void MedSyncEnergy ();
  void MedEnqueue (uint32_t n, uint8_t kind, int32_t dst, int bits, std::shared_ptr<PFrame> pf,
                   bool front = false);
  void MedOnTxStart (uint32_t s, dahello::MediumFrame &f);
  void MedOnTxEnd (uint32_t s, const dahello::MediumFrame &f);
  bool MedWanted (uint32_t n, const dahello::MediumFrame &f);
  void MedOnMacDrop (uint32_t n, const dahello::MediumFrame &f, int why);
  void MedOnDecode (uint32_t r, const dahello::MediumFrame &f, double snrDb);
  void MedUnicastSend (const Pkt &pkt, uint32_t nh, bool detour, bool staleHop);
  void MedArqTimeout (uint32_t s, uint64_t pid, uint64_t gen);
  void MedMarvSend (const Pkt &pkt, const std::vector<uint32_t> &list, int64_t txId);
  void MedMarvTxDone (Pkt pkt, int64_t txId);
  void MedNoteDelivery (const Pkt &pkt);
  void WriteMediumReport (std::ostringstream &oss) const;

  uint64_t nGen = 0;
  uint64_t nSucc = 0;
  uint64_t nVoid = 0;
  uint64_t nVoidSource = 0;
  uint64_t nVoidRelay = 0;
  uint64_t hopsFailed = 0; // hops already completed when a packet VOIDs at a relay
  uint64_t nSnr = 0;
  // Reception failed on a hop the frozen filters admitted and the live
  // range/SNR/depth filters (depth omitted for RLOR and for detours)
  // would have rejected. Diagnostic; does not change the decision.
  uint64_t nSnrStale = 0;
  // Reception failed on a hop the live filters would still admit.
  uint64_t nSnrGenuine = 0;
  // Empty frozen forwarder set while a live filter would still admit
  // an unvisited alive neighbor.
  uint64_t nVoidStale = 0;
  // Empty frozen and live sets, after the packet already used a stale hop.
  uint64_t nVoidViaStale = 0;
  // Empty frozen and live sets, and no stale hop on the path.
  uint64_t nVoidLive = 0;
  // Transmissions whose chosen neighbor fails the live filters.
  uint64_t nStaleAdmit = 0;
  uint64_t nStaleAdmitOk = 0;
  // Envelope exit of a packet that had already used a stale hop.
  uint64_t nEnvelopeViaStale = 0;
  uint64_t nEnergy = 0;
  uint64_t nTtl = 0;
  uint64_t nSimEnd = 0;
  uint64_t nNonfinite = 0;
  uint64_t nTx = 0;
  uint64_t nDetourHops = 0;
  uint64_t nDetourPkts = 0;
  uint64_t hopSum = 0;
  double hopDelaySum = 0.0;
  uint64_t nHopDelay = 0;
  uint64_t nOutOfEnvelope = 0; // nodes removed (XY exit)
  uint64_t nLostNodeExit = 0;  // packets at/arriving at a removed node
  uint64_t nGenSkipped = 0;    // not generated; src already gone
  double latencySum = 0.0;
  double latencyMax = 0.0;
  std::vector<bool> inflight;

  Vector driftVel = Vector (0.0, 0.0, 0.0);
  double driftAzimuthRad = 0.0;
  dahello::EddyConfig eddy;
  dahello::GaussMarkovConfig gmCfg;
  std::vector<dahello::GaussMarkovState> gmSt;
  std::vector<std::mt19937> gmRng;
  double gmLastT = 0.0;
  uint32_t nWouldExitXy = 0;
  uint32_t nWouldExitZ = 0;
  double minExitTimeS = std::numeric_limits<double>::infinity ();
  std::vector<uint32_t> exitIds;
  std::vector<Vector> pos0;
  std::vector<SnrSample> snrTrace;
  double lastSnrTraceT = -1.0e99;
  std::vector<bool> alive;
  std::vector<Census> census;
  double lastCensusT = -1.0e99;
  VRelAcc vrel;
  std::vector<double> zohGaps;
  uint32_t traceI = 1;
  uint32_t traceJ = 0;
  std::vector<PVelSample> pvelTrace;
  std::vector<EkfTick> ekfTicks;
  int lastTraceBc = 0;

  struct SatSnap
  {
    uint32_t i = 0;
    uint32_t j = 0;
    bool heard = false;
    bool aliveI = false;
    bool aliveJ = false;
    double maxE = 0.0;
    double maxEUnheard = 0.0;
    double maxEpsUnheard = 0.0;
    double fcVel = 0.0;
    double hatVel = 0.0;
    double firstBc = std::numeric_limits<double>::quiet_NaN ();
    uint64_t nRising = 0;
  };
  std::vector<SatSnap> satSnap;
  bool satSnapped = false;
  struct HopDiagAcc
  {
    uint64_t nDec = 0;
    uint64_t nPrimary = 0;
    uint64_t nDetourDec = 0;
    uint64_t sinkInActions = 0;
    uint64_t sinkChosen = 0;
    uint64_t sinkInNotChosen = 0;
    uint64_t sinkAbsent = 0;
    uint64_t failUnheard = 0;
    uint64_t failRange = 0;
    uint64_t failSnr = 0;
    uint64_t failDepth = 0;
    uint64_t failStab = 0;
    uint64_t failVisited = 0;
    uint64_t failDead = 0;
    uint64_t failPeek = 0;
    uint64_t absentButLive = 0;
    uint64_t absentLiveFail = 0;
    uint64_t qNhGt = 0;
    uint64_t qEq = 0;
    uint64_t qSinkGt = 0;
    uint64_t chosenTie = 0;
    uint64_t chosenStrict = 0;
    uint64_t chosenOnly = 0;
    uint64_t chosenWorse = 0;
    uint64_t ceneNhGt = 0;
    uint64_t ceneNhLt = 0;
    uint64_t ceneEq = 0;
    uint64_t cdelNhGt = 0;
    uint64_t cneiNhGt = 0;
    uint64_t nRew = 0;
    double sumCeneNh = 0.0;
    double sumCeneSink = 0.0;
    double sumCdelNh = 0.0;
    double sumCdelSink = 0.0;
    double sumCneiNh = 0.0;
    double sumCneiSink = 0.0;
    double sumRNh = 0.0;
    double sumRSink = 0.0;
    double sumQNh = 0.0;
    double sumQSink = 0.0;
    double sumAge = 0.0;
    uint64_t nAge = 0;
    double sumPose = 0.0;
    uint64_t nPose = 0;
    double sumAbsE = 0.0;
    double maxAbsE = 0.0;
    uint64_t nAbsE = 0;
    double sumEps = 0.0;
    uint64_t stabBc1 = 0;
    uint64_t nSrc = 0;
    uint64_t srcUnreach = 0;
    double sumSrcLive = 0.0;
    uint64_t nDeliv = 0;
    uint64_t delivLive1 = 0;
    uint64_t delivLive1Multi = 0;
    uint64_t delivLiveGt1 = 0;
    double sumActual = 0.0;
    double sumLive = 0.0;
    uint64_t decLive1 = 0;
    uint64_t decLive1NotSink = 0;
    uint64_t nPathShown = 0;
    uint64_t nExclShown = 0;
    uint64_t nRewShown = 0;
    uint64_t liveHist[8] = {};
    uint64_t actualHist[8] = {};
  };
  HopDiagAcc hop;
  std::vector<std::string> hopSamples;

  // ---- NOVELTY (design choices; all default off) ----
  double hetDriftMax = std::numeric_limits<double>::quiet_NaN (); // II: |v_i|~U(0,max), heading~U(0,2pi)
  uint32_t nWpAuv = 0;      // II: AUVs 1..nWpAuv follow random 3-D waypoints over ground
  double wpSpeed = 1.5;     // m/s over ground
  double gmSigma = std::numeric_limits<double>::quiet_NaN (); // III: GM sigma_xy override
  double gmZeta = std::numeric_limits<double>::quiet_NaN ();  // III: GM zeta override
  bool novDiag = false;     // per-tick live SNR-rate / v_rel census (observation only)
  std::vector<Vector> nodeVel0;
  std::vector<Vector> wpPos, wpTarget;
  std::vector<std::mt19937> wpRng;
  double wpLastT = 0.0;
  uint64_t wpLegs = 0;
  std::vector<double> ndPrevSnr;
  double ndSumDSnr = 0.0, ndSumDSnr2 = 0.0, ndSumVrel = 0.0, ndSumAbsRR = 0.0;
  uint64_t ndN = 0;
  bool nsSnapped = false;
  uint64_t nsRxHello = 0, nsRxBeacon = 0, nsRxBoot = 0, nsPerRx = 0, nsNE = 0, nsNEx = 0;
  uint64_t nsRes = 0, nsSupp = 0;
  double nsSumE = 0.0;
  void AdvanceWaypointsTo (double now);
  void NovDiagTick ();
  void NovSnapshot ();

  uint64_t ctrlTxAtTsim = 0;
  uint64_t hsVoidAtTsim = 0, hsVAtTsim = 0, hsPreAtTsim = 0, hsHelloAtTsim = 0;
  uint64_t eventHellosAtTsim = 0;
  uint64_t bootstrapHellosAtTsim = 0;
  uint64_t energyHellosAtTsim = 0;
  uint64_t risingAtTsim = 0;
  uint64_t onesAtTsim = 0;
  bool ctrlTxSnapped = false;

  uint32_t nNodes () const { return nAuv + 1; }

  bool isScenarioII () const { return scenario == "ii" || scenario == "II"; }
  bool isScenarioIII () const { return scenario == "iii" || scenario == "III"; }
  bool isMobile () const { return isScenarioII () || isScenarioIII (); }
  bool removeOnExit () const { return isMobile () && boundary == "remove"; }
  bool nodeAlive (uint32_t i) const
  {
    if (alive.empty ())
      {
        return i < nNodes ();
      }
    return i < alive.size () && alive[i];
  }

  // RLOR's candidate set is N_range ∩ N_SNR. LQARP, DA-HELLO, and
  // Fixed-Periodic also require N_depth. Detour admission is range∩SNR
  // for every protocol that detours.
  bool candidateUsesDepth () const
  {
    if (!proto)
      {
        return false;
      }
    const std::string n = proto->name ();
    return n != "rlor" && n.compare (0, 5, "qelar") != 0 && n.compare (0, 4, "saqr") != 0;
  }

  // Live geometry from nodes_[] (SyncPoses just wrote it) and the live
  // channel query. Does not read held/frozen neighbor state and does
  // not change routing.
  bool liveGeometricPass (uint32_t i, uint32_t j, bool requireDepth) const
  {
    dahello::ObserverScope obs; // attribution only (no decision effect)
    if (!proto || !gate || i == j || !nodeAlive (i) || !nodeAlive (j))
      {
        return false;
      }
    const dahello::LqarpNode &a = proto->nodes ()[i];
    const dahello::LqarpNode &b = proto->nodes ()[j];
    const double d = dahello::ChannelGate::distance3D (a.x, a.y, a.z, b.x, b.y, b.z);
    if (!(d < proto->rRangeM ()))
      {
        return false;
      }
    const dahello::ChannelSample samp = proto->linkSample (i, j);
    if (gate->sampleFailsCandidateSnr (samp))
      {
        return false;
      }
    if (requireDepth && !((a.z - b.z) > 0.0))
      {
        return false;
      }
    return true;
  }

  bool liveForwarderExists (const Pkt &pkt) const
  {
    const bool depth = candidateUsesDepth ();
    for (uint32_t j = 0; j < nNodes (); ++j)
      {
        if (j == pkt.at)
          {
            continue;
          }
        if (dahello::LqarpProtocol::containsId (pkt.visited, j))
          {
            continue;
          }
        if (liveGeometricPass (pkt.at, j, depth))
          {
            return true;
          }
      }
    return false;
  }

  void noteVoid (Pkt &pkt)
  {
    ++nVoid;
    if (pkt.hops == 0)
      {
        ++nVoidSource;
      }
    else
      {
        ++nVoidRelay;
        hopsFailed += pkt.hops;
      }
    if (liveForwarderExists (pkt))
      {
        ++nVoidStale;
      }
    else if (pkt.viaStale)
      {
        ++nVoidViaStale;
      }
    else
      {
        ++nVoidLive;
      }
    if (pkt.id < inflight.size ())
      {
        inflight[pkt.id] = false;
      }
  }

  void noteEnvelope (const Pkt &pkt)
  {
    ++nLostNodeExit;
    if (pkt.viaStale)
      {
        ++nEnvelopeViaStale;
      }
  }

  void Place ();
  void BuildProtocol ();
  void ScheduleTraffic ();
  void AdvanceEddyTo (double now);
  void SyncPoses ();
  void MaybeTraceSnr ();
  void MaybeCensus ();
  void SweepExits ();
  void RemoveNode (uint32_t i);
  void RecordVRel (uint32_t i, const std::vector<uint32_t> &actions);
  void MaybePVelTrace ();
  bool DropIfDead (const Pkt &pkt);
  void OnGenerate (uint32_t src);
  void OnArrive (Pkt pkt);
  void Forward (Pkt pkt);
  int LiveBfsHops (uint32_t src) const;
  int BfsHopsTo (uint32_t src, bool requireDepth) const;
  void GeoCensus ();
  void NoteHopDiag (Pkt &pkt, const std::vector<uint32_t> &actions, int nh, bool detour);
  void NoteHopDiagDelivery (const Pkt &pkt);
  void WriteHopDiag (std::ostringstream &oss) const;
  void OnTick ();
  void FailInflight ();
  void WriteReport ();
  void MarvTransmit (Pkt pkt);
  void MarvReceive (Pkt pkt);
  void MarvHoldExpire (Pkt pkt);
  void MarvAccept (Pkt pkt);
  void MarvHeader (uint32_t k, uint32_t i, double V, bool voidFlag, double e);
  void MarvCopyEnd (const Pkt &pkt, int fate);
};

std::vector<Vector>
SamplePositions (uint32_t nAuv, uint32_t seed, uint32_t rngRun)
{
  RngSeedManager::SetSeed (seed);
  RngSeedManager::SetRun (rngRun);

  std::vector<Vector> pos;
  pos.push_back (Vector (kVolumeXyM * 0.5, kVolumeXyM * 0.5, kSinkDepthM));

  Ptr<UniformRandomVariable> u = CreateObject<UniformRandomVariable> ();
  u->SetStream (1);
  const uint32_t maxTries = 100000;
  for (uint32_t i = 1; i <= nAuv; ++i)
    {
      bool ok = false;
      Vector p;
      for (uint32_t t = 0; t < maxTries; ++t)
        {
          p = Vector (u->GetValue (0.0, kVolumeXyM),
                      u->GetValue (0.0, kVolumeXyM),
                      u->GetValue (kMinAuvDepthM, kVolumeZM));
          ok = true;
          for (const Vector &q : pos)
            {
              const double d = std::sqrt ((p.x - q.x) * (p.x - q.x)
                                          + (p.y - q.y) * (p.y - q.y)
                                          + (p.z - q.z) * (p.z - q.z));
              if (d < kMinSepM)
                {
                  ok = false;
                  break;
                }
            }
          if (ok)
            {
              break;
            }
        }
      if (!ok)
        {
          NS_FATAL_ERROR ("failed to place AUV " << i << " with min separation");
        }
      pos.push_back (p);
    }
  return pos;
}

// First time the ray p + v t leaves [0,xy]² × [0,zMax]. Infinity if v=0
// or the ray stays inside forever.
double
FirstExitTimeS (const Vector &p, const Vector &v, double xy, double zMax)
{
  double tHit = std::numeric_limits<double>::infinity ();
  auto axis = [&] (double pos, double vel, double lo, double hi) {
    if (vel > 1e-15)
      {
        tHit = std::min (tHit, (hi - pos) / vel);
      }
    else if (vel < -1e-15)
      {
        tHit = std::min (tHit, (lo - pos) / vel);
      }
  };
  axis (p.x, v.x, 0.0, xy);
  axis (p.y, v.y, 0.0, xy);
  axis (p.z, v.z, 0.0, zMax);
  return tHit;
}

void
Pipeline::Place ()
{
  nodes.Create (nNodes ());
  const std::vector<Vector> pos = SamplePositions (nAuv, seed, rngRun);
  pos0 = pos;

  Ptr<ListPositionAllocator> alloc = CreateObject<ListPositionAllocator> ();
  for (const Vector &p : pos)
    {
      alloc->Add (p);
    }

  MobilityHelper mobility;
  const bool scII = isScenarioII ();
  const bool scIII = isScenarioIII ();
  if (scII || scIII)
    {
      mobility.SetMobilityModel ("ns3::ConstantVelocityMobilityModel");
    }
  else
    {
      mobility.SetMobilityModel ("ns3::ConstantPositionMobilityModel");
    }
  mobility.SetPositionAllocator (alloc);
  mobility.Install (nodes);

  if (scIII)
    {
      if (boundary != "open" && boundary != "remove")
        {
          NS_FATAL_ERROR ("Scenario III boundary='"
                          << boundary
                          << "': use 'remove' (default) or 'open' (HOLD ablation).");
        }
      eddy.V = driftSpeed;
      eddy.cx = g_eddyCx; eddy.cy = g_eddyCy; eddy.R = g_eddyR; // p15 A2.2(c)
      gmCfg.zeta = 0.95;
      gmCfg.dt = tickDt;
      gmCfg.sigma_x = 1.0;
      gmCfg.sigma_y = 1.0;
      gmCfg.sigma_z = 0.0; // permanent, SPEC Part 9
      if (std::isfinite (gmSigma))
        {
          gmCfg.sigma_x = gmSigma; // NOVELTY: stronger GM fluctuation
          gmCfg.sigma_y = gmSigma;
        }
      if (std::isfinite (gmZeta))
        {
          gmCfg.zeta = gmZeta;
        }
      gmSt.assign (nNodes (), dahello::GaussMarkovState{});
      gmRng.resize (nNodes ());
      gmLastT = 0.0;
      uint32_t nCore = 0;
      for (uint32_t i = 0; i < nNodes (); ++i)
        {
          gmSt[i].x = pos[i].x;
          gmSt[i].y = pos[i].y;
          gmSt[i].z = pos[i].z;
          gmRng[i].seed (seed * 10007u + i * 17u + 3u);
          Ptr<ConstantVelocityMobilityModel> m =
              nodes.Get (i)->GetObject<ConstantVelocityMobilityModel> ();
          if (i == sinkId)
            {
              gmSt[i].vx = gmSt[i].vy = gmSt[i].vz = 0.0;
              m->SetVelocity (Vector (0.0, 0.0, 0.0));
              continue;
            }
          dahello::eddyMeanFlow (gmSt[i].x, gmSt[i].y, eddy,
                               gmSt[i].vx, gmSt[i].vy, gmSt[i].vz);
          m->SetVelocity (Vector (gmSt[i].vx, gmSt[i].vy, gmSt[i].vz));
          const double r = std::hypot (pos[i].x - eddy.cx, pos[i].y - eddy.cy);
          if (r < eddy.R)
            {
              ++nCore;
            }
        }
      nWouldExitXy = 0;
      nWouldExitZ = 0;
      minExitTimeS = std::numeric_limits<double>::infinity ();
      exitIds.clear ();
      std::cout << "placement seed=" << seed << " run=" << rngRun
                << " model=ns3::ConstantVelocityMobilityModel\n";
      std::cout << "  Scenario III: Rankine eddy at (" << eddy.cx << ","
                << eddy.cy << ") V_eddy=" << eddy.V << " m/s R_core="
                << eddy.R << " m CCW. Gauss-Markov zeta=" << gmCfg.zeta
                << " sigma=(" << gmCfg.sigma_x << "," << gmCfg.sigma_y
                << "," << gmCfg.sigma_z << ") m/s. sigma_z=0 is the "
                   "permanent geostrophic choice (SPEC Part 9). "
                   "v_cmd=0. Sink velocity identically 0. "
                << nCore << "/" << nAuv << " AUVs start inside R_core.\n";
      std::cout << "  AUV-AUV v_rel is nonzero (per-node eddy mean + GM). "
                   "Radial range-rate is the EKF-observable component.\n";
      if (boundary == "remove")
        {
          std::cout << "  BOUNDARY POLICY = remove: an AUV that leaves "
                       "[0," << kVolumeXyM << "]^2 is taken out "
                       "(OUT_OF_ENVELOPE). In-flight packets at that node "
                       "are LOST_NODE_EXIT. Exits are runtime (velocity "
                       "is time-varying; no closed-form first-exit).\n";
        }
      else
        {
          std::cout << "  BOUNDARY POLICY = open (HOLD ablation): nodes "
                       "keep moving after leaving the envelope.\n";
        }
    }
  else if (scII)
    {
      if (boundary != "open" && boundary != "remove")
        {
          NS_FATAL_ERROR ("Scenario II boundary='"
                          << boundary
                          << "': use 'remove' (Step 30 default) or 'open' (HOLD ablation). "
                             "Reflect/clamp are not implemented.");
        }
      // Horizontal current: azimuth Uniform on stream 2 so Scenario I
      // placements (stream 1) stay bit-identical for the same seed.
      // vz=0. A 3-D isotropic draw at 2 m/s would exit the 500 m column
      // in at most 250 s; FLAG'd, not silently 3-D.
      Ptr<UniformRandomVariable> az = CreateObject<UniformRandomVariable> ();
      az->SetStream (2);
      const double twoPi = 2.0 * std::acos (-1.0);
      driftAzimuthRad = az->GetValue (0.0, twoPi);
      driftVel = Vector (driftSpeed * std::cos (driftAzimuthRad),
                         driftSpeed * std::sin (driftAzimuthRad), 0.0);
      // NOVELTY: per-node velocity (default = shared driftVel, unchanged).
      nodeVel0.assign (nNodes (), driftVel);
      nodeVel0[sinkId] = Vector (0.0, 0.0, 0.0);
      if (std::isfinite (hetDriftMax))
        {
          std::mt19937 hr (seed * 7919u + 101u);
          std::uniform_real_distribution<double> us (0.0, hetDriftMax);
          std::uniform_real_distribution<double> ua (0.0, twoPi);
          for (uint32_t i = 1; i < nNodes (); ++i)
            {
              const double sp = us (hr);
              const double hd = ua (hr);
              nodeVel0[i] = Vector (sp * std::cos (hd), sp * std::sin (hd), 0.0);
            }
        }
      if (nWpAuv > 0)
        {
          wpPos.assign (nNodes (), Vector ());
          wpTarget.assign (nNodes (), Vector ());
          wpRng.resize (nNodes ());
          for (uint32_t i = 1; i <= std::min (nWpAuv, nAuv); ++i)
            {
              wpRng[i].seed (seed * 7919u + 202u + i);
              std::uniform_real_distribution<double> ux (0.0, kVolumeXyM);
              std::uniform_real_distribution<double> uz (kMinAuvDepthM, kVolumeZM);
              wpPos[i] = pos[i];
              wpTarget[i] = Vector (ux (wpRng[i]), ux (wpRng[i]), uz (wpRng[i]));
              const Vector d (wpTarget[i].x - pos[i].x, wpTarget[i].y - pos[i].y,
                              wpTarget[i].z - pos[i].z);
              const double L = std::sqrt (d.x * d.x + d.y * d.y + d.z * d.z);
              nodeVel0[i] = (L > 1e-9) ? Vector (wpSpeed * d.x / L, wpSpeed * d.y / L, wpSpeed * d.z / L)
                                       : Vector (0.0, 0.0, 0.0);
            }
          wpLastT = 0.0;
        }
      for (uint32_t i = 0; i < nNodes (); ++i)
        {
          Ptr<ConstantVelocityMobilityModel> m =
              nodes.Get (i)->GetObject<ConstantVelocityMobilityModel> ();
          if (i == sinkId)
            {
              m->SetVelocity (Vector (0.0, 0.0, 0.0));
            }
          else
            {
              m->SetVelocity (nodeVel0[i]);
            }
        }

      nWouldExitXy = 0;
      nWouldExitZ = 0;
      minExitTimeS = std::numeric_limits<double>::infinity ();
      exitIds.clear ();
      const Vector vAuv = driftVel;
      const Vector vSink (0.0, 0.0, 0.0);
      for (uint32_t i = 0; i < nNodes (); ++i)
        {
          const Vector v = (i == sinkId) ? vSink : (nodeVel0.empty () ? vAuv : nodeVel0[i]);
          const double tEx = FirstExitTimeS (pos[i], v, kVolumeXyM, kVolumeZM);
          if (tEx <= tSim)
            {
              if (i != sinkId)
                {
                  ++nWouldExitXy;
                  exitIds.push_back (i);
                }
              minExitTimeS = std::min (minExitTimeS, tEx);
            }
          const Vector pT (pos[i].x + v.x * tSim, pos[i].y + v.y * tSim,
                           pos[i].z + v.z * tSim);
          if (pT.z < 0.0 || pT.z > kVolumeZM)
            {
              ++nWouldExitZ;
            }
        }

      std::cout << "placement seed=" << seed << " run=" << rngRun
                << " model=ns3::ConstantVelocityMobilityModel\n";
      std::cout << "  Scenario II: v_cmd=0, ||v_drift||=" << driftSpeed
                << " m/s, azimuth=" << driftAzimuthRad << " rad ("
                << (driftAzimuthRad * 180.0 / std::acos (-1.0))
                << " deg), v_drift=(" << driftVel.x << ", " << driftVel.y
                << ", " << driftVel.z << ") m/s. Stream 2. Horizontal "
                   "(vz=0). Sink velocity identically 0.\n";
      std::cout << "  AUV-AUV relative velocity is exactly 0 (shared "
                   "v_drift, v_cmd=0). Only AUV-to-sink relative velocity "
                   "is nonzero.\n";
      std::cout << "  Displacement over T_sim=" << tSim
                << " s: " << (driftSpeed * tSim) << " m. Volume "
                << "[0," << kVolumeXyM << "]^2 x [0," << kVolumeZM << "] m.\n";
      if (boundary == "remove")
        {
          std::cout << "  BOUNDARY POLICY = remove: an AUV that leaves "
                       "[0," << kVolumeXyM << "]^2 is taken out of the "
                       "active simulation (velocity zeroed, excluded from "
                       "I / traffic). Counted as OUT_OF_ENVELOPE (node "
                       "event). In-flight packets at/arriving at that node "
                       "are LOST_NODE_EXIT, not VOID. "
                    << nWouldExitXy << "/" << nAuv
                    << " AUVs are predicted to exit XY within T_sim"
                    << (std::isfinite (minExitTimeS)
                            ? (std::string (" (first exit t=")
                               + std::to_string (minExitTimeS) + " s)")
                            : "")
                    << ". z-exits=" << nWouldExitZ << " (expect 0 with vz=0).\n";
        }
      else
        {
          std::cout << "  BOUNDARY POLICY = open (HOLD ablation): nodes KEEP "
                       "drifting; no removal. "
                    << nWouldExitXy << "/" << nAuv
                    << " AUVs would leave the XY footprint within T_sim"
                    << (std::isfinite (minExitTimeS)
                            ? (std::string (" (first exit t=")
                               + std::to_string (minExitTimeS) + " s)")
                            : "")
                    << ". z-exits=" << nWouldExitZ << ".\n";
        }
      std::cout << "  FLAG: drift direction is a uniform *horizontal* "
                   "azimuth, not uniform-on-a-sphere. A 3-D isotropic "
                << driftSpeed << " m/s draw would exit the 500 m column "
                   "in at most "
                << (kVolumeZM / driftSpeed)
                << " s. Ocean current is horizontal; vz=0 is the "
                   "physically motivated reading of Section 4.2.\n";
    }
  else
    {
      std::cout << "placement seed=" << seed << " run=" << rngRun
                << " model=ns3::ConstantPositionMobilityModel\n";
    }

  std::cout << "  strategy: sink at volume XY centre, kinematic z="
            << kSinkDepthM << " m (unique shallowest); acoustic query z="
            << sinkAcousticDepthM << " m. " << nAuv
            << " AUVs i.i.d. uniform in [0," << kVolumeXyM << "]^2 x ["
            << kMinAuvDepthM << "," << kVolumeZM
            << "] m, reject 3-D duplicates closer than " << kMinSepM
            << " m. z>=5 m (transducer floor); AUVs z>=10 m so N_depth to the sink is feasible.\n";
  for (uint32_t i = 0; i < nNodes (); ++i)
    {
      const Vector p = nodes.Get (i)->GetObject<MobilityModel> ()->GetPosition ();
      if (!quiet || i == sinkId)
        {
          std::cout << "  node " << i << (i == sinkId ? " SINK" : " AUV ")
                    << " x=" << p.x << " y=" << p.y << " z_depth=" << p.z << "\n";
        }
    }
  if (quiet)
    {
      std::cout << "  (" << nAuv << " AUV positions omitted, --quiet)\n";
    }
  if (scII && !exitIds.empty () && !quiet)
    {
      const uint32_t show = std::min<uint32_t> (8, static_cast<uint32_t> (exitIds.size ()));
      std::cout << "  first " << show << " AUV ids that exit XY: ";
      for (uint32_t k = 0; k < show; ++k)
        {
          std::cout << exitIds[k] << (k + 1 < show ? "," : "\n");
        }
    }
}

void
Pipeline::BuildProtocol ()
{
  const dahello::Season season = (seasonName == "july") ? dahello::Season::July
                                                      : dahello::Season::January;
  const char *tl = (season == dahello::Season::July)
                       ? "/TL_grid_extended_july.csv"
                       : "/TL_grid_extended_january.csv";
  const char *sn = (season == dahello::Season::July)
                       ? "/SNR_grid_extended_july.csv"
                       : "/SNR_grid_extended_january.csv";
  auto loader = std::make_shared<dahello::BellhopDataLoader> (
      dataDir + tl, dataDir + sn, season);
  gate = std::make_unique<dahello::ChannelGate> (loader);
  if (chanMode == "formula")
    {
      // Port A: formula channel for every host (N_SNR, c_lin, r_range) and the medium.
      gate = MakeFormulaGate ();
    }

  dahello::LqarpConfig cfg;
  cfg.packetBits = energy.packetBits;
  if (std::isfinite (rRangeOverride))
    {
      NS_ABORT_MSG_IF (!(rRangeOverride > 0.0), "--rRange must be > 0");
      cfg.rRangeM = rRangeOverride;
    }
  cfg.eIni = dahello::kDefaultEiniJ;
  std::vector<dahello::LqarpNode> ln (nNodes ());
  for (uint32_t i = 0; i < nNodes (); ++i)
    {
      const Vector p = nodes.Get (i)->GetObject<MobilityModel> ()->GetPosition ();
      ln[i].id = i;
      ln[i].x = p.x;
      ln[i].y = p.y;
      ln[i].z = p.z;
      ln[i].energy = cfg.eIni;
      ln[i].vx = ln[i].vy = ln[i].vz = 0.0;
      if (isScenarioII () && i != sinkId)
        {
          const Vector v0 = (i < nodeVel0.size ()) ? nodeVel0[i] : driftVel;
          ln[i].vx = v0.x;
          ln[i].vy = v0.y;
          ln[i].vz = v0.z;
        }
      else if (isScenarioIII () && i != sinkId && i < gmSt.size ())
        {
          ln[i].vx = gmSt[i].vx;
          ln[i].vy = gmSt[i].vy;
          ln[i].vz = gmSt[i].vz;
        }
      if (i == sinkId)
        {
          ln[i].acousticZ = sinkAcousticDepthM;
        }
    }
  if (protoName == "lqarp")
    {
      proto = std::make_unique<LqarpFwd> (*gate, cfg, ln);
    }
  else if (protoName == "lqarp-refreshed")
    {
      cfg.holdRefreshS = 5.0;
      proto = std::make_unique<LqarpFwd> (*gate, cfg, ln, "lqarp-refreshed");
      std::cout << "LQARP-refreshed: held pose/energy on T_ctrl=" << cfg.holdRefreshS
                << " s grid (Fixed-Periodic cadence). O_ctrl stays 0.\n";
    }
  else if (protoName == "dahello")
    {
      dahello::DahelloConfig ec;
      static_cast<dahello::LqarpConfig &> (ec) = cfg;
      ec.skipInitialControlRound = (discMode == "heard"); // p4
      ec.helloRxGate = helloRxGate;
      // p15 A2.2(a) v2: FixedPeriodic (--hostSched=periodic / protocol=periodic) and
      // energyRefreshS (qelar/saqr/marv -energy) take per-node U[0,T) when set.
      // Default false = p14 lock-step. marv-T jitter is handled in MarvFwd.
      ec.periodicJitter = periodicJitter;
      ec.periodicJitterSeed = static_cast<uint64_t> (seed) * 0xD1B54A32D192ED03ull + 41ull;
      ec.bootstrapBroadcast = daBootFix;
      ec.linkTriggerOff = daLinkOff;
      if (isScenarioII () || isScenarioIII ())
        {
          // Steps 60–67: validated default is beta_1=beta_2=0
          // (LQARP reward + event trigger). beta=1/1 was a
          // diagnostic placeholder; do not restore it silently.
          ec.beta_1 = std::isfinite (beta1) ? beta1 : 0.0;
          ec.beta_2 = std::isfinite (beta2) ? beta2 : 0.0;
          if (std::isfinite (epsilon0))
            {
              ec.epsilon_0 = epsilon0;
            }
          ec.broadcastMode = dahello::BroadcastMode::EventTriggered;
          proto = std::make_unique<DahelloFwd> (*gate, ec, ln, "dahello", true);
          std::cout << "DA-HELLO (Scenario " << (isScenarioIII () ? "III" : "II")
                    << ", trigger on, validated reward default 0/0 unless overridden): beta_1="
                    << ec.beta_1 << " beta_2=" << ec.beta_2
                    << " epsilon_0=" << ec.epsilon_0
                    << " s_cap=" << ec.s_cap << " W=" << ec.W
                    << " W_mp=" << ec.W_mp << " rho=" << ec.rho
                    << " lambda=" << ec.lambda
                    << ". EKF ticked every " << tickDt << " s.\n";
        }
      else
        {
          ec.beta_1 = std::isfinite (beta1) ? beta1 : 0.0;
          ec.beta_2 = std::isfinite (beta2) ? beta2 : 0.0;
          ec.epsilon_0 = std::isfinite (epsilon0) ? epsilon0 : kIdentityEpsilon0;
          ec.broadcastMode = dahello::BroadcastMode::EventTriggered;
          // Leave the EKF unstepped. Bootstrap discovery runs from the
          // first candidate-set query, so each pair still gets one held
          // snapshot without a trigger tick.
          proto = std::make_unique<DahelloFwd> (*gate, ec, ln, "dahello", false);
          std::cout << "DA-HELLO identity: beta_1=" << ec.beta_1
                    << " beta_2=" << ec.beta_2 << " epsilon_0=" << ec.epsilon_0
                    << " (trigger off). EKF not ticked; matches unit-test identity.\n";
        }
    }
  else if (protoName == "dahello-energy")
    {
      dahello::DahelloConfig ec;
      static_cast<dahello::LqarpConfig &> (ec) = cfg;
      ec.skipInitialControlRound = (discMode == "heard"); // p4
      ec.helloRxGate = helloRxGate;
      // p15 A2.2(a) v2: FixedPeriodic (--hostSched=periodic / protocol=periodic) and
      // energyRefreshS (qelar/saqr/marv -energy) take per-node U[0,T) when set.
      // Default false = p14 lock-step. marv-T jitter is handled in MarvFwd.
      ec.periodicJitter = periodicJitter;
      ec.periodicJitterSeed = static_cast<uint64_t> (seed) * 0xD1B54A32D192ED03ull + 41ull;
      ec.bootstrapBroadcast = daBootFix;
      ec.linkTriggerOff = daLinkOff;
      ec.beta_1 = std::isfinite (beta1) ? beta1 : 0.0;
      ec.beta_2 = std::isfinite (beta2) ? beta2 : 0.0;
      if (std::isfinite (epsilon0))
        {
          ec.epsilon_0 = epsilon0;
        }
      ec.broadcastMode = dahello::BroadcastMode::EventTriggered;
      ec.energyRefreshS = 5.0;
      ec.energyQuantBits = energyQuantBits;
      ec.energyAddrBits = energyAddrBits;
      proto = std::make_unique<DahelloFwd> (*gate, ec, ln, "dahello-energy", true);
      std::cout << "DA-HELLO energy channel: energy_j_zoh every " << ec.energyRefreshS
                << " s, quantized to " << ec.energyQuantBits
                << " bits over [0, E_ini], plus " << ec.energyAddrBits
                << "-bit sender id (" << (ec.energyQuantBits + ec.energyAddrBits)
                << " bit beacon). Pose and velocity stay on the event trigger. "
                << "O_ene is not in O_ctrl. beta_1=" << ec.beta_1
                << " beta_2=" << ec.beta_2 << "\n";
    }
  else if (protoName == "periodic")
    {
      dahello::DahelloConfig ec;
      static_cast<dahello::LqarpConfig &> (ec) = cfg;
      ec.skipInitialControlRound = (discMode == "heard"); // p4
      ec.helloRxGate = helloRxGate;
      // p15 A2.2(a) v2: FixedPeriodic (--hostSched=periodic / protocol=periodic) and
      // energyRefreshS (qelar/saqr/marv -energy) take per-node U[0,T) when set.
      // Default false = p14 lock-step. marv-T jitter is handled in MarvFwd.
      ec.periodicJitter = periodicJitter;
      ec.periodicJitterSeed = static_cast<uint64_t> (seed) * 0xD1B54A32D192ED03ull + 41ull;
      ec.bootstrapBroadcast = daBootFix;
      ec.linkTriggerOff = daLinkOff;
      // Same reward betas as DA-HELLO so the ablation isolates
      // Broadcast_k timing (T_ctrl grid vs rising-edge), not the
      // c_pred / c_drift weights (SPEC Part 4).
      if (isScenarioII () || isScenarioIII ())
        {
          // Same validated default as DA-HELLO (SPEC Part 4).
          ec.beta_1 = std::isfinite (beta1) ? beta1 : 0.0;
          ec.beta_2 = std::isfinite (beta2) ? beta2 : 0.0;
        }
      else
        {
          ec.beta_1 = std::isfinite (beta1) ? beta1 : 0.0;
          ec.beta_2 = std::isfinite (beta2) ? beta2 : 0.0;
        }
      if (std::isfinite (epsilon0))
        {
          ec.epsilon_0 = epsilon0;
        }
      ec.broadcastMode = dahello::BroadcastMode::FixedPeriodic;
      if (std::isfinite (tCtrl))
        {
          NS_ABORT_MSG_IF (!(tCtrl > 0.0), "--tCtrl must be > 0");
          ec.T_ctrl = tCtrl;
        }
      proto = std::make_unique<DahelloFwd> (*gate, ec, ln, "periodic", true);
      std::cout << "Fixed-Periodic: beta_1=" << ec.beta_1 << " beta_2=" << ec.beta_2
                << " epsilon_0=" << ec.epsilon_0 << " T_ctrl=" << ec.T_ctrl
                << " s. EKF ticked every " << tickDt << " s.\n";
    }
  else if (protoName == "trickle" || protoName == "speedadapt" || protoName == "letadapt" || protoName == "naivevoid")
    {
      // NOVELTY step 2: adaptive HELLO baselines on the LQARP host. Same
      // reward/filters as the periodic arm; only HELLO timing differs.
      dahello::DahelloConfig ec;
      static_cast<dahello::LqarpConfig &> (ec) = cfg;
      ec.skipInitialControlRound = (discMode == "heard"); // p4
      ec.helloRxGate = helloRxGate;
      // p15 A2.2(a) v2: FixedPeriodic (--hostSched=periodic / protocol=periodic) and
      // energyRefreshS (qelar/saqr/marv -energy) take per-node U[0,T) when set.
      // Default false = p14 lock-step. marv-T jitter is handled in MarvFwd.
      ec.periodicJitter = periodicJitter;
      ec.periodicJitterSeed = static_cast<uint64_t> (seed) * 0xD1B54A32D192ED03ull + 41ull;
      ec.bootstrapBroadcast = daBootFix;
      ec.linkTriggerOff = daLinkOff;
      ec.beta_1 = std::isfinite (beta1) ? beta1 : 0.0;
      ec.beta_2 = std::isfinite (beta2) ? beta2 : 0.0;
      if (std::isfinite (epsilon0))
        {
          ec.epsilon_0 = epsilon0;
        }
      ec.broadcastMode = dahello::BroadcastMode::Adaptive;
      ec.adaptiveScheme = (protoName == "trickle") ? 1 : 2;
      // t23 (2026-10-16): new baselines, only reachable via these new names.
      if (protoName == "letadapt")
        {
          ec.adaptiveScheme = 3;
          if (std::isfinite (letFrac))
            {
              NS_ABORT_MSG_IF (!(letFrac > 0.0 && letFrac < 1.0), "--letFrac (QLAR tau) must be in (0,1)");
              ec.letFrac = letFrac;
            }
          if (std::isfinite (letFloor))
            {
              NS_ABORT_MSG_IF (!(letFloor >= 0.0 && letFloor <= 120.0), "--letFloor must be in [0,120] s");
              ec.letTminS = letFloor;
            }
        }
      else if (protoName == "naivevoid")
        {
          ec.adaptiveScheme = 4;
          if (std::isfinite (naiveRefresh))
            {
              NS_ABORT_MSG_IF (!(naiveRefresh >= 60.0 && naiveRefresh <= 120.0), "--naiveRefresh must be in [60,120] s");
              ec.naiveRefreshS = naiveRefresh;
            }
        }
      ec.adaptiveSeed = seed * 7919u + 303u;
      if (ec.adaptiveScheme >= 3)
        std::cout << "t23 baseline " << protoName << ": LET frac=" << ec.letFrac << " T=[" << ec.letTminS << ","
                  << ec.letTmaxS << "] s | naive refresh=" << ec.naiveRefreshS << " s\n";
      proto = std::make_unique<DahelloFwd> (*gate, ec, ln, protoName, true);
      std::cout << "Adaptive HELLO " << protoName << ": Trickle Imin=" << ec.trickleIminS
                << " doublings=" << ec.trickleDoublings << " k=" << ec.trickleK
                << " | speed a=" << ec.saA << " T=[" << ec.saTminS << "," << ec.saTmaxS << "] s\n";
    }
  else if (protoName == "rlor")
    {
      proto = std::make_unique<RlorFwd> (*gate, cfg, ln);
      std::cout << "RLOR-inspired: I = N_range ∩ N_SNR (no N_depth, no detour).\n";
    }
  else if (protoName == "qelar" || protoName == "qelar-hello" || protoName == "qelar-hello-energy")
    {
      dahello::QelarConfig qc;
      qc.eIni = cfg.eIni;
      qc.rRangeM = cfg.rRangeM;
      dahello::DahelloConfig ec;
      static_cast<dahello::LqarpConfig &> (ec) = cfg;
      ec.skipInitialControlRound = (discMode == "heard"); // p4
      ec.helloRxGate = helloRxGate;
      // p15 A2.2(a) v2: FixedPeriodic (--hostSched=periodic / protocol=periodic) and
      // energyRefreshS (qelar/saqr/marv -energy) take per-node U[0,T) when set.
      // Default false = p14 lock-step. marv-T jitter is handled in MarvFwd.
      ec.periodicJitter = periodicJitter;
      ec.periodicJitterSeed = static_cast<uint64_t> (seed) * 0xD1B54A32D192ED03ull + 41ull;
      ec.bootstrapBroadcast = daBootFix;
      ec.linkTriggerOff = daLinkOff;
      ec.beta_1 = 0.0;
      ec.beta_2 = 0.0;
      ec.broadcastMode = dahello::BroadcastMode::EventTriggered;
      QelarFwd::Hello hello = QelarFwd::Hello::Off;
      if (protoName == "qelar-hello")
        {
          hello = QelarFwd::Hello::Pose;
        }
      else if (protoName == "qelar-hello-energy")
        {
          hello = QelarFwd::Hello::Energy;
          ec.energyRefreshS = 5.0;
          ec.energyQuantBits = energyQuantBits;
          ec.energyAddrBits = energyAddrBits;
        }
      // PORT A (2026-10-12): baseline HELLO schedules on QELAR/SAQR hosts
      // (--hostSched=periodic|trickle|speedadapt; default off = legacy behaviour).
      if ((protoName == "qelar") && hostSched != "off")
        {
          NS_ABORT_MSG_IF (hostSched != "periodic" && hostSched != "trickle" && hostSched != "speedadapt",
                           "--hostSched must be off|periodic|trickle|speedadapt");
          hello = QelarFwd::Hello::Pose;
          if (hostSched == "periodic")
            {
              ec.broadcastMode = dahello::BroadcastMode::FixedPeriodic;
              if (std::isfinite (tCtrl))
                {
                  NS_ABORT_MSG_IF (!(tCtrl > 0.0), "--tCtrl must be > 0");
                  ec.T_ctrl = tCtrl;
                }
            }
          else
            {
              ec.broadcastMode = dahello::BroadcastMode::Adaptive;
              ec.adaptiveScheme = (hostSched == "trickle") ? 1 : 2;
              ec.adaptiveSeed = seed * 7919u + 303u;
            }
        }
      auto q = std::make_unique<QelarFwd> (*gate, qc, ln, protoName, hello, ec);
      q->setSink (sinkId);
      proto = std::move (q);
      std::cout << "QELAR (TMC 2010): model-based, gamma=0.5, g=1, alpha1=0.5, alpha2=0.05, "
                << "failure branch off, acoustic range only, hello=" << protoName << "\n";
    }
  else if (protoName == "saqr" || protoName == "saqr-hello" || protoName == "saqr-hello-energy")
    {
      dahello::SaqrConfig sc;
      sc.eIni = cfg.eIni;
      sc.rRangeM = cfg.rRangeM;
      sc.packetBits = cfg.packetBits;
      sc.rngSeed = seed;
      dahello::DahelloConfig ec;
      static_cast<dahello::LqarpConfig &> (ec) = cfg;
      ec.skipInitialControlRound = (discMode == "heard"); // p4
      ec.helloRxGate = helloRxGate;
      // p15 A2.2(a) v2: FixedPeriodic (--hostSched=periodic / protocol=periodic) and
      // energyRefreshS (qelar/saqr/marv -energy) take per-node U[0,T) when set.
      // Default false = p14 lock-step. marv-T jitter is handled in MarvFwd.
      ec.periodicJitter = periodicJitter;
      ec.periodicJitterSeed = static_cast<uint64_t> (seed) * 0xD1B54A32D192ED03ull + 41ull;
      ec.bootstrapBroadcast = daBootFix;
      ec.linkTriggerOff = daLinkOff;
      ec.beta_1 = 0.0;
      ec.beta_2 = 0.0;
      ec.broadcastMode = dahello::BroadcastMode::EventTriggered;
      SaqrFwd::Hello hello = SaqrFwd::Hello::Off;
      if (protoName == "saqr-hello")
        {
          hello = SaqrFwd::Hello::Pose;
        }
      else if (protoName == "saqr-hello-energy")
        {
          hello = SaqrFwd::Hello::Energy;
          ec.energyRefreshS = 5.0;
          ec.energyQuantBits = energyQuantBits;
          ec.energyAddrBits = energyAddrBits;
        }
      // PORT A (2026-10-12): baseline HELLO schedules on QELAR/SAQR hosts
      // (--hostSched=periodic|trickle|speedadapt; default off = legacy behaviour).
      if ((protoName == "saqr") && hostSched != "off")
        {
          NS_ABORT_MSG_IF (hostSched != "periodic" && hostSched != "trickle" && hostSched != "speedadapt",
                           "--hostSched must be off|periodic|trickle|speedadapt");
          hello = SaqrFwd::Hello::Pose;
          if (hostSched == "periodic")
            {
              ec.broadcastMode = dahello::BroadcastMode::FixedPeriodic;
              if (std::isfinite (tCtrl))
                {
                  NS_ABORT_MSG_IF (!(tCtrl > 0.0), "--tCtrl must be > 0");
                  ec.T_ctrl = tCtrl;
                }
            }
          else
            {
              ec.broadcastMode = dahello::BroadcastMode::Adaptive;
              ec.adaptiveScheme = (hostSched == "trickle") ? 1 : 2;
              ec.adaptiveSeed = seed * 7919u + 303u;
            }
        }
      auto s = std::make_unique<SaqrFwd> (*gate, sc, ln, protoName, hello, ec);
      s->setSink (sinkId);
      proto = std::move (s);
      std::cout << "SAQR (Computer Networks 2025): Service A 0.7/0.1/0.1/0.1, alpha=0.1, "
                << "gamma=0.8, epsilon 0.5 then 0.01 after 20 decisions, "
                << "min-max star, delay=prop+airtime, hello=" << protoName << "\n";
    }
  else if (protoName.compare (0, 5, "marv-") == 0)
    {
      dahello::MarvConfig mc;
      mc.eIni = cfg.eIni;
      mc.rRangeM = cfg.rRangeM;
      if (marvPsatScope == "list")
        {
          mc.psatScope = dahello::MarvPsatScope::List;
        }
      else if (marvPsatScope == "top")
        {
          mc.psatScope = dahello::MarvPsatScope::Top;
        }
      else
        {
          NS_FATAL_ERROR ("marv-*: --marvPsatScope=list|top is required (signed D15: top)");
        }
      if (marvSinkOverheard == "ack")
        {
          mc.sinkOverheard = dahello::MarvSinkOverheard::Ack;
        }
      else if (marvSinkOverheard == "literal")
        {
          mc.sinkOverheard = dahello::MarvSinkOverheard::Literal;
        }
      else
        {
          NS_FATAL_ERROR ("marv-*: --marvSinkOverheard=ack|literal is required (signed D14: ack)");
        }
      dahello::DahelloConfig ec;
      static_cast<dahello::LqarpConfig &> (ec) = cfg;
      ec.skipInitialControlRound = (discMode == "heard"); // p4
      ec.helloRxGate = helloRxGate;
      // p15 A2.2(a) v2: FixedPeriodic (--hostSched=periodic / protocol=periodic) and
      // energyRefreshS (qelar/saqr/marv -energy) take per-node U[0,T) when set.
      // Default false = p14 lock-step. marv-T jitter is handled in MarvFwd.
      ec.periodicJitter = periodicJitter;
      ec.periodicJitterSeed = static_cast<uint64_t> (seed) * 0xD1B54A32D192ED03ull + 41ull;
      ec.bootstrapBroadcast = daBootFix;
      ec.linkTriggerOff = daLinkOff;
      ec.beta_1 = 0.0;
      ec.beta_2 = 0.0;
      ec.broadcastMode = dahello::BroadcastMode::EventTriggered;
      MarvFwd::Ctrl ctrl = MarvFwd::Ctrl::Off;
      double tc = 0.0;
      bool hs = false;
      if (protoName == "marv-ctrl-off")
        {
          ctrl = MarvFwd::Ctrl::Off;
        }
      else if (protoName == "marv-dahello")
        {
          ctrl = MarvFwd::Ctrl::Hello;
        }
      else if (protoName == "marv-dahello-hs")
        {
          ctrl = MarvFwd::Ctrl::Hello;
          hs = true;
        }
      else if (protoName == "marv-dahello-hsonly")
        {
          // NOVELTY ablation: host-state trigger only (link trigger off)
          ctrl = MarvFwd::Ctrl::Hello;
          hs = true;
          ec.linkTriggerOff = true;
        }
      else if (protoName == "marv-dahello-energy" || protoName == "marv-dahello-hs-energy")
        {
          ctrl = MarvFwd::Ctrl::HelloEnergy;
          hs = (protoName == "marv-dahello-hs-energy");
          ec.energyRefreshS = 5.0;
          ec.energyQuantBits = energyQuantBits;
          ec.energyAddrBits = energyAddrBits;
        }
      else if (protoName == "marv-trickle" || protoName == "marv-trickle-strict")
        {
          ctrl = MarvFwd::Ctrl::Trickle;
        }
      else if (protoName == "marv-sa")
        {
          ctrl = MarvFwd::Ctrl::Speed;
        }
      else if (protoName == "marv-let")
        {
          ctrl = MarvFwd::Ctrl::Let; // t25: QLAR LET-scaled HELLO on MARV
        }
      else if (protoName == "marv-naive")
        {
          ctrl = MarvFwd::Ctrl::Naive; // t25: void-change HELLO + slow refresh on MARV
        }
      else if (protoName.compare (0, 6, "marv-T") == 0)
        {
          ctrl = MarvFwd::Ctrl::Periodic;
          tc = std::stod (protoName.substr (6));
          NS_ABORT_MSG_IF (!(tc > 0.0), "marv-T<s>: interval must be > 0");
        }
      else
        {
          NS_FATAL_ERROR ("unknown MARV host '" << protoName << "'");
        }
      auto m = std::make_unique<MarvFwd> (*gate, mc, ln, protoName, ctrl, tc, ec, hs);
      m->setSink (sinkId);
      m->setAlive ([this] (uint32_t i) { return nodeAlive (i); });
      marv = m.get ();
      m->setAdaptiveSeed (seed * 7919u + 303u);
      // A2.2(a) v2: marv-T uses MarvFwd per-node schedule; other arms use DahelloConfig::periodicJitter
      // (FixedPeriodic / energyRefresh). No abort: flag is allowed on any arm (no-op if unused).
      m->setPeriodicJitter (periodicJitter && ctrl == MarvFwd::Ctrl::Periodic,
                            static_cast<uint64_t> (seed) * 0xD1B54A32D192ED03ull + 41ull);
      m->setPollStored (pollStored);
      if (ctrl == MarvFwd::Ctrl::Let && std::isfinite (letFrac))
        {
          NS_ABORT_MSG_IF (!(letFrac > 0.0 && letFrac < 1.0), "--letFrac (QLAR tau) must be in (0,1)");
          m->setLetFrac (letFrac);
        }
      if (ctrl == MarvFwd::Ctrl::Let && std::isfinite (letFloor))
        {
          NS_ABORT_MSG_IF (!(letFloor >= 0.0 && letFloor <= 120.0), "--letFloor must be in [0,120] s");
          m->setLetFloor (letFloor);
        }
      if (ctrl == MarvFwd::Ctrl::Naive && std::isfinite (naiveRefresh))
        {
          NS_ABORT_MSG_IF (!(naiveRefresh >= 60.0 && naiveRefresh <= 120.0), "--naiveRefresh must be in [60,120] s");
          m->setNaiveRefresh (naiveRefresh);
        }
      m->setTrickleStrict (protoName == "marv-trickle-strict");
      if (daMode > 0)
        {
          NS_ABORT_MSG_IF (!hs, "--daMode needs a host-state MARV arm (marv-dahello-hs[-energy])");
          m->setDa (daMode, daMargin, daImin, daDoublings, daTnb);
        }
      marvSeen.assign (nNodes (), std::vector<char> ());
      m->startPeriodic ();
      proto = std::move (m);
      const auto &c = marv->marv ().config ();
      std::cout << "MARV (IEEE Sensors J 2024): alpha=" << c.alpha << " gamma=" << c.gamma
                << " omega=" << c.omega << " delta=" << c.delta << " g_c=" << c.gc << " k=" << c.k
                << " lambda1=" << c.lambda1 << " lambda2=" << c.lambda2 << " VoI=" << c.voi
                << " h=" << c.h << " Q0=" << c.qInit << " Psat0=" << c.psatInit
                << " proc=" << c.procDelayS << " s v=" << c.soundSpeedMps
                << " R=" << marv->marv ().rRangeM () << " Tmax=" << marv->marv ().tMaxS ()
                << " s T(2)-T(1)=" << marv->marv ().holdingTimeS (2) << " s ctrl=" << protoName
                << " psatScope=" << marvPsatScope << " sinkOverheard=" << marvSinkOverheard << "\n";
    }
  else
    {
      NS_FATAL_ERROR ("unknown protocol '" << protoName
                                          << "' (use lqarp|lqarp-refreshed|dahello|dahello-energy|rlor|periodic|qelar|qelar-hello|qelar-hello-energy|saqr|saqr-hello|saqr-hello-energy|marv-ctrl-off|marv-T<s>|marv-dahello|marv-dahello-energy)");
    }
  if (auto *lq = dynamic_cast<LqarpFwd *> (proto.get ()))
    {
      if (lq->name () == "lqarp-refreshed")
        {
          SyncPoses ();
          lq->syncHoldRefresh ();
        }
    }
  std::cout << "protocol=" << proto->name () << "  r_range=" << proto->rRangeM ()
            << " m  season=" << seasonName << "  E_ini=" << cfg.eIni << " J\n";
  std::cout << "sink kinematic z=" << kSinkDepthM
            << " m  sink acoustic query z=" << sinkAcousticDepthM << " m\n";
  std::cout << "energy model (INVENTED this step; not previously in dahello):\n"
            << "  SL=" << energy.sourceLevelDb << " dB, P_ac="
            << energy.pAcousticWatt () << " W, eta=" << energy.projectorEta
            << " => P_tx=" << energy.pTxWatt () << " W, P_rx=" << energy.pRxWatt
            << " W, R_b=" << energy.bitRateBps << " bit/s, T="
            << energy.airTimeS () << " s\n"
            << "  E_tx=" << energy.eTxJ () << " J/attempt, E_rx="
            << energy.eRxJ () << " J/success. No idle listen. No MAC collisions.\n";
  std::cout << "control: LQARP/RLOR have none (O_ctrl=0). DA-HELLO identity "
               "emits none while epsilon_0 is huge. Fixed-Periodic emits one "
               "control packet per node every T_ctrl (first-pass size = l = "
            << kCtrlPacketBits << " bits; SPEC gives no distinct HELLO length). "
               "O_ctrl = ctrl bytes on [0, T_sim] / T_sim (drain excluded).\n";

  uint32_t nEmptyI = 0;
  uint32_t nSinkInI = 0;
  for (uint32_t i = 1; i <= nAuv; ++i)
    {
      const auto I = proto->candidateSet (i);
      if (I.empty ())
        {
          ++nEmptyI;
        }
      for (uint32_t j : I)
        {
          if (j == sinkId)
            {
              ++nSinkInI;
              break;
            }
        }
    }
  std::cout << "t=0 topology: AUVs with empty I=" << nEmptyI << "/" << nAuv
            << "  AUVs with sink in I=" << nSinkInI << "/" << nAuv << "\n";
  alive.assign (nNodes (), true);
  SyncPoses ();
  if (proto && proto->hasVRel ())
    {
      traceI = 1;
      traceJ = 0;
      // Steps 35–43 pin pair (1,2) when both AUVs exist so the
      // frozen-geometry diagnostic stays on the same directed link.
      if (nAuv >= 2)
        {
          traceI = 1;
          traceJ = 2;
        }
      else
        {
          for (uint32_t i = 1; i <= nAuv && traceJ == 0; ++i)
            {
              for (uint32_t j : proto->candidateSet (i))
                {
                  if (j != sinkId && j != i)
                    {
                      traceI = i;
                      traceJ = j;
                      break;
                    }
                }
            }
        }
      if (traceJ != 0)
        {
          const auto &ni = proto->nodes ()[traceI];
          const auto &nj = proto->nodes ()[traceJ];
          double zvx = 0, zvy = 0, zvz = 0;
          proto->zohVelocity (traceI, traceJ, zvx, zvy, zvz);
          std::cout << "ZOH init pair=(" << traceI << "," << traceJ << ")"
                    << " zohHeard=" << (proto->zohHeard (traceI, traceJ) ? "true" : "false")
                    << " vx_i=" << ni.vx << " vy_i=" << ni.vy
                    << " vx_j_true=" << nj.vx << " vy_j_true=" << nj.vy
                    << " pdot_j_ZOH=(" << zvx << "," << zvy << "," << zvz << ")"
                    << " v_rel=" << proto->vRel (traceI, traceJ)
                    << " (pdot_j ZOH is explicit 0,0,0 before any broadcast;"
                    << " c_drift uses c_del until first HELLO)\n";
          double p33 = 0, p44 = 0, p55 = 0;
          int bc = 0;
          proto->ekfVelP (traceI, traceJ, p33, p44, p55, bc);
          std::cout << "  EKF P vel diag P33=" << p33 << " P44=" << p44 << " P55=" << p55
                    << " (init velocity-block diagonal = 1.0)\n";
        }
    }
  MaybeTraceSnr ();
  MaybeCensus ();
  MaybePVelTrace ();
}

void
Pipeline::ScheduleTraffic ()
{
  // First packet at t=0, last at the greatest k*interval < tSim.
  if (trafficPhase == "random")
    {
      // Port A: periodic per node with a random phase U[0, interval).
      std::mt19937_64 ph (static_cast<uint64_t> (seed) * 2654435761ull + rngRun * 97ull + 11ull);
      std::uniform_real_distribution<double> u (0.0, pktInterval);
      for (uint32_t src = 1; src <= nAuv; ++src)
        {
          const double phase = u (ph);
          for (double t = phase; t < tSim - 1e-12; t += pktInterval)
            {
              Simulator::Schedule (Seconds (t), &Pipeline::OnGenerate, this, src);
            }
        }
    }
  else
  for (uint32_t src = 1; src <= nAuv; ++src)
    {
      for (double t = 0.0; t < tSim - 1e-12; t += pktInterval)
        {
          Simulator::Schedule (Seconds (t), &Pipeline::OnGenerate, this, src);
        }
    }
  Simulator::Schedule (Seconds (tSim + drainS), &Pipeline::FailInflight, this);
  if (geoCensus && geoEveryS > 0.0)
    {
      for (double t = 0.0; t < tSim + 1e-12; t += geoEveryS)
        {
          Simulator::Schedule (Seconds (t), &Pipeline::GeoCensus, this);
        }
    }
  const bool needTick = (proto && proto->wantsTick () && tickDt > 0.0) || isMobile ();
  if (needTick && tickDt > 0.0)
    {
      for (double t = tickDt; t < tSim + drainS + 1e-12; t += tickDt)
        {
          Simulator::Schedule (Seconds (t), &Pipeline::OnTick, this);
        }
    }
}

void
Pipeline::OnGenerate (uint32_t src)
{
  SweepExits ();
  SyncPoses ();
  if (!nodeAlive (src))
    {
      ++nGenSkipped;
      return;
    }
  Pkt pkt;
  pkt.id = nGen;
  pkt.src = src;
  pkt.at = src;
  pkt.genTime = Simulator::Now ().GetSeconds ();
  pkt.hops = 0;
  pkt.detourHops = 0;
  pkt.visited = {src};
  ++nGen;
  if (medOn () && pkt.genTime >= warmS && pkt.genTime < tSim)
    {
      ++medGenW;
    }
  if (inflight.size () <= pkt.id)
    {
      inflight.resize (pkt.id + 1, false);
    }
  inflight[pkt.id] = true;
  if (marv)
    {
      if (marvPk.size () <= pkt.id)
        {
          marvPk.resize (pkt.id + 1);
        }
      marvPk[pkt.id].live = 1;
      if (marvSeen[src].size () <= pkt.id)
        {
          marvSeen[src].resize (pkt.id + 1, 0);
        }
      marvSeen[src][pkt.id] = 1;
      MarvTransmit (pkt);
      return;
    }
  Forward (pkt);
}

void
Pipeline::OnArrive (Pkt pkt)
{
  SweepExits ();
  if (DropIfDead (pkt))
    {
      return;
    }
  if (pkt.at == sinkId)
    {
      const double now = Simulator::Now ().GetSeconds ();
      const double lat = now - pkt.genTime;
      if (medOn ())
        {
          if (medDelivered.size () <= pkt.id) medDelivered.resize (pkt.id + 1, 0);
          if (medDelivered[pkt.id])
            {
              ++medDupAtSink;
              return;
            }
          medDelivered[pkt.id] = 1;
          MedNoteDelivery (pkt);
        }
      ++nSucc;
      hopSum += pkt.hops;
      latencySum += lat;
      latencyMax = std::max (latencyMax, lat);
      if (pkt.id < inflight.size ())
        {
          inflight[pkt.id] = false;
        }
      if (!std::isfinite (lat))
        {
          ++nNonfinite;
        }
      if (hopDiag)
        {
          NoteHopDiagDelivery (pkt);
        }
      return;
    }
  Forward (pkt);
}

int
Pipeline::BfsHopsTo (uint32_t src, bool requireDepth) const
{
  if (src == sinkId)
    {
      return 0;
    }
  const uint32_t n = nNodes ();
  std::vector<int> dist (n, -1);
  std::vector<uint32_t> q;
  dist[src] = 0;
  q.push_back (src);
  for (size_t head = 0; head < q.size (); ++head)
    {
      const uint32_t u = q[head];
      for (uint32_t v = 0; v < n; ++v)
        {
          if (dist[v] >= 0 || !liveGeometricPass (u, v, requireDepth))
            {
              continue;
            }
          dist[v] = dist[u] + 1;
          if (v == sinkId)
            {
              return dist[v];
            }
          q.push_back (v);
        }
    }
  return -1;
}

void
Pipeline::GeoCensus ()
{
  SweepExits ();
  SyncPoses ();
  if (!proto)
    {
      return;
    }
  const dahello::LqarpNode &s = proto->nodes ()[sinkId];
  for (uint32_t i = 1; i <= nAuv; ++i)
    {
      if (!nodeAlive (i))
        {
          continue;
        }
      ++geoN;
      const dahello::LqarpNode &a = proto->nodes ()[i];
      if (!(dahello::ChannelGate::distance3D (a.x, a.y, a.z, s.x, s.y, s.z) < proto->rRangeM ()))
        {
          ++geoOutsideR;
        }
      // Routing graph: the protocol's own live filter (depth-progressive
      // except RLOR). Range graph: N_range ∩ N_SNR only.
      const int h = BfsHopsTo (i, candidateUsesDepth ());
      if (h == 1) ++geo1; else if (h == 2) ++geo2; else if (h >= 3) ++geo3p; else ++geoUnreach;
      const int g = BfsHopsTo (i, false);
      if (g == 1) ++geoRange1; else if (g == 2) ++geoRange2; else if (g >= 3) ++geoRange3p; else ++geoRangeUnreach;
    }
}

int
Pipeline::LiveBfsHops (uint32_t src) const
{
  if (src == sinkId)
    {
      return 0;
    }
  const uint32_t n = nNodes ();
  std::vector<int> dist (n, -1);
  std::vector<uint32_t> q;
  q.reserve (n);
  dist[src] = 0;
  q.push_back (src);
  const bool depth = candidateUsesDepth ();
  for (size_t head = 0; head < q.size (); ++head)
    {
      const uint32_t u = q[head];
      if (dist[u] >= static_cast<int> (kMaxHops))
        {
          continue;
        }
      for (uint32_t v = 0; v < n; ++v)
        {
          if (dist[v] >= 0)
            {
              continue;
            }
          if (!liveGeometricPass (u, v, depth))
            {
              continue;
            }
          dist[v] = dist[u] + 1;
          if (v == sinkId)
            {
              return dist[v];
            }
          q.push_back (v);
        }
    }
  return -1;
}

void
Pipeline::NoteHopDiag (Pkt &pkt, const std::vector<uint32_t> &actions, int nh, bool detour)
{
  if (pkt.hops == 0 && pkt.liveBfsAtSrc < -1)
    {
      pkt.liveBfsAtSrc = LiveBfsHops (pkt.at);
      ++hop.nSrc;
      if (pkt.liveBfsAtSrc < 0)
        {
          ++hop.srcUnreach;
        }
      else
        {
          hop.sumSrcLive += pkt.liveBfsAtSrc;
        }
    }

  ++hop.nDec;
  if (detour)
    {
      ++hop.nDetourDec;
    }
  else
    {
      ++hop.nPrimary;
    }

  const bool liveDirect = liveGeometricPass (pkt.at, sinkId, candidateUsesDepth ());
  if (liveDirect)
    {
      ++hop.decLive1;
      if (nh != static_cast<int> (sinkId))
        {
          ++hop.decLive1NotSink;
        }
    }

  const bool inActions = dahello::LqarpProtocol::containsId (actions, sinkId);
  if (inActions)
    {
      ++hop.sinkInActions;
      if (nh == static_cast<int> (sinkId))
        {
          ++hop.sinkChosen;
        }
      else
        {
          ++hop.sinkInNotChosen;
        }
    }
  else
    {
      ++hop.sinkAbsent;
      if (liveDirect)
        {
          ++hop.absentButLive;
        }
      else
        {
          ++hop.absentLiveFail;
        }
    }

  dahello::FilterPeek g;
  const bool peeked = proto->peekFilter (pkt.at, sinkId, g);
  const bool visited = dahello::LqarpProtocol::containsId (pkt.visited, sinkId);
  const char *why = "peek_unsupported";
  if (!peeked)
    {
      ++hop.failPeek;
    }
  else if (!inActions)
    {
      if (!nodeAlive (sinkId))
        {
          why = "dead";
          ++hop.failDead;
        }
      else if (visited)
        {
          why = "visited";
          ++hop.failVisited;
        }
      else if (!g.heard)
        {
          why = "unheard";
          ++hop.failUnheard;
        }
      else if (!g.range)
        {
          why = "held_range";
          ++hop.failRange;
        }
      else if (!g.snr)
        {
          why = "held_snr";
          ++hop.failSnr;
        }
      else if (!g.depth)
        {
          why = "held_depth";
          ++hop.failDepth;
        }
      else if (!g.stab)
        {
          why = "inNStab";
          ++hop.failStab;
          ++hop.stabBc1;
          hop.sumAbsE += g.absE;
          hop.sumEps += g.epsilon;
          ++hop.nAbsE;
          hop.maxAbsE = std::max (hop.maxAbsE, g.absE);
        }
      else
        {
          why = "filtered_after_raw_set";
          ++hop.failPeek;
        }
      if (g.heard)
        {
          hop.sumPose += g.poseErrM;
          ++hop.nPose;
          if (std::isfinite (g.lastZohS))
            {
              hop.sumAge += std::max (0.0, Simulator::Now ().GetSeconds () - g.lastZohS);
              ++hop.nAge;
            }
        }
      if (hop.nExclShown < 8 && hopSamples.size () < 40)
        {
          std::ostringstream s;
          s << std::fixed << std::setprecision (3);
          s << "EXCL t=" << Simulator::Now ().GetSeconds ()
            << " at=" << pkt.at << " why=" << why
            << " heard=" << (g.heard ? 1 : 0)
            << " range=" << (g.range ? 1 : 0)
            << " snr=" << (g.snr ? 1 : 0)
            << " depth=" << (g.depth ? 1 : 0)
            << " stab=" << (g.stab ? 1 : 0)
            << " bc=" << g.broadcast
            << " |e|=" << g.absE << " eps=" << g.epsilon
            << " pose_err_m=" << g.poseErrM
            << " dE_J=" << g.energyGapJ
            << " live_direct=" << (liveDirect ? 1 : 0)
            << " detour=" << (detour ? 1 : 0);
          if (std::isfinite (g.lastZohS))
            {
              s << " age_s=" << (Simulator::Now ().GetSeconds () - g.lastZohS);
            }
          hopSamples.push_back (s.str ());
          ++hop.nExclShown;
        }
    }

  if (!inActions || nh < 0)
    {
      return;
    }

  double qSink = 0.0;
  double qNh = 0.0;
  proto->peekQ (pkt.at, sinkId, qSink);
  proto->peekQ (pkt.at, static_cast<uint32_t> (nh), qNh);
  if (nh == static_cast<int> (sinkId))
    {
      double bestOther = -std::numeric_limits<double>::infinity ();
      bool anyOther = false;
      for (uint32_t j : actions)
        {
          if (j == sinkId)
            {
              continue;
            }
          double qj = 0.0;
          if (!proto->peekQ (pkt.at, j, qj))
            {
              continue;
            }
          anyOther = true;
          bestOther = std::max (bestOther, qj);
        }
      if (!anyOther)
        {
          ++hop.chosenOnly;
        }
      else if (qSink > bestOther)
        {
          ++hop.chosenStrict;
        }
      else if (qSink == bestOther)
        {
          ++hop.chosenTie;
        }
      else
        {
          ++hop.chosenWorse;
        }
      return;
    }
  if (qNh > qSink)
    {
      ++hop.qNhGt;
    }
  else if (qNh == qSink)
    {
      ++hop.qEq;
    }
  else
    {
      ++hop.qSinkGt;
    }

  RewardPeek rwSink;
  RewardPeek rwNh;
  if (proto->peekReward (pkt.at, sinkId, rwSink)
      && proto->peekReward (pkt.at, static_cast<uint32_t> (nh), rwNh))
    {
      ++hop.nRew;
      hop.sumCeneNh += rwNh.c_ene;
      hop.sumCeneSink += rwSink.c_ene;
      hop.sumCdelNh += rwNh.c_del;
      hop.sumCdelSink += rwSink.c_del;
      hop.sumCneiNh += rwNh.c_nei;
      hop.sumCneiSink += rwSink.c_nei;
      hop.sumRNh += rwNh.R;
      hop.sumRSink += rwSink.R;
      hop.sumQNh += rwNh.q;
      hop.sumQSink += rwSink.q;
      if (rwNh.c_ene > rwSink.c_ene)
        {
          ++hop.ceneNhGt;
        }
      else if (rwNh.c_ene < rwSink.c_ene)
        {
          ++hop.ceneNhLt;
        }
      else
        {
          ++hop.ceneEq;
        }
      if (rwNh.c_del > rwSink.c_del)
        {
          ++hop.cdelNhGt;
        }
      if (rwNh.c_nei > rwSink.c_nei)
        {
          ++hop.cneiNhGt;
        }
      if (nh != static_cast<int> (sinkId) && hop.nRewShown < 8 && hopSamples.size () < 40)
        {
          std::ostringstream s;
          s << std::scientific << std::setprecision (4);
          s << "REWARD t=" << std::fixed << std::setprecision (1)
            << Simulator::Now ().GetSeconds ()
            << " at=" << pkt.at << " nh=" << nh
            << std::scientific << std::setprecision (4)
            << " Qnh=" << qNh << " Qsink=" << qSink
            << " Rnh=" << rwNh.R << " Rsink=" << rwSink.R
            << " c_ene nh/sink=" << rwNh.c_ene << "/" << rwSink.c_ene
            << " c_del nh/sink=" << rwNh.c_del << "/" << rwSink.c_del
            << " c_nei nh/sink=" << rwNh.c_nei << "/" << rwSink.c_nei
            << " heldE sink=" << g.heldE;
          dahello::FilterPeek gn;
          if (proto->peekFilter (pkt.at, static_cast<uint32_t> (nh), gn))
            {
              s << "/" << gn.heldE;
            }
          hopSamples.push_back (s.str ());
          ++hop.nRewShown;
        }
    }
}

void
Pipeline::NoteHopDiagDelivery (const Pkt &pkt)
{
  ++hop.nDeliv;
  const int live = pkt.liveBfsAtSrc;
  const int actual = static_cast<int> (pkt.hops);
  hop.sumActual += actual;
  if (live < 0)
    {
      return;
    }
  hop.sumLive += live;
  const auto bin = [] (int h) {
    if (h < 0)
      {
        return 0;
      }
    return h >= 7 ? 7 : h;
  };
  ++hop.liveHist[bin (live)];
  ++hop.actualHist[bin (actual)];
  if (live == 1)
    {
      ++hop.delivLive1;
      if (actual > 1)
        {
          ++hop.delivLive1Multi;
        }
    }
  else if (live > 1)
    {
      ++hop.delivLiveGt1;
    }
  if (live == 1 && actual > 1 && hop.nPathShown < 8 && hopSamples.size () < 40)
    {
      std::ostringstream s;
      s << "PATH src_live_bfs=" << live << " actual_hops=" << actual << " nodes=";
      for (uint32_t id : pkt.visited)
        {
          s << id << ">";
        }
      s << sinkId;
      hopSamples.push_back (s.str ());
      ++hop.nPathShown;
    }
}

void
Pipeline::WriteHopDiag (std::ostringstream &oss) const
{
  if (!hopDiag)
    {
      return;
    }
  const auto frac = [] (uint64_t n, uint64_t d) {
    return d ? static_cast<double> (n) / static_cast<double> (d) : 0.0;
  };
  const auto mean = [] (double s, uint64_t n) {
    return n ? s / static_cast<double> (n) : 0.0;
  };
  oss << std::fixed << std::setprecision (4);
  oss << "HOPDIAG_BEGIN proto=" << (proto ? proto->name () : "?")
      << " scenario=" << scenario << " seed=" << seed
      << " drift=" << driftSpeed << "\n";
  oss << "decisions=" << hop.nDec
      << " primary=" << hop.nPrimary
      << " detour_decisions=" << hop.nDetourDec << "\n";
  oss << "sink_in_action_set=" << hop.sinkInActions
      << " (" << frac (hop.sinkInActions, hop.nDec) << ")"
      << " sink_selected=" << hop.sinkChosen
      << " (" << frac (hop.sinkChosen, hop.sinkInActions) << " of those)"
      << " sink_in_set_not_selected=" << hop.sinkInNotChosen << "\n";
  oss << "sink_absent=" << hop.sinkAbsent
      << " (" << frac (hop.sinkAbsent, hop.nDec) << ")"
      << " of_which_live_direct=" << hop.absentButLive
      << " live_not_direct=" << hop.absentLiveFail << "\n";
  oss << "absent_first_filter unheard=" << hop.failUnheard
      << " held_range=" << hop.failRange
      << " held_snr=" << hop.failSnr
      << " held_depth=" << hop.failDepth
      << " inNStab=" << hop.failStab
      << " visited=" << hop.failVisited
      << " dead=" << hop.failDead
      << " other=" << hop.failPeek << "\n";
  oss << "stab_exclusions |e|_mean=" << mean (hop.sumAbsE, hop.nAbsE)
      << " eps_mean=" << mean (hop.sumEps, hop.nAbsE)
      << " |e|_max=" << hop.maxAbsE
      << " (every inNStab miss has broadcast_k!=0 and |e|>=eps)\n";
  oss << "held_read_when_absent_and_heard pose_err_m=" << mean (hop.sumPose, hop.nPose)
      << " n=" << hop.nPose
      << " zoh_age_s=" << mean (hop.sumAge, hop.nAge)
      << " n_age=" << hop.nAge
      << " (LQARP has no hold timestamp, so n_age stays 0)\n";
  oss << "when_sink_selected only_action=" << hop.chosenOnly
      << " tie_with_best_other=" << hop.chosenTie
      << " strictly_better_Q=" << hop.chosenStrict
      << " worse_than_other=" << hop.chosenWorse << "\n";
  oss << "when_passed_over c_ene_nh>sink=" << hop.ceneNhGt
      << " c_ene_nh<sink=" << hop.ceneNhLt
      << " c_ene_eq=" << hop.ceneEq
      << " c_del_nh>sink=" << hop.cdelNhGt
      << " c_nei_nh>sink=" << hop.cneiNhGt
      << " of " << hop.nRew << "\n";
  oss << "when_sink_in_set_not_selected q_nh>q_sink=" << hop.qNhGt
      << " q_equal=" << hop.qEq
      << " q_sink>q_nh=" << hop.qSinkGt << "\n";
  uint64_t nReach = 0;
  for (int b = 0; b < 8; ++b)
    {
      nReach += hop.liveHist[b];
    }
  oss << "reward_means_when_sink_passed_over n=" << hop.nRew
      << " c_ene nh/sink=" << mean (hop.sumCeneNh, hop.nRew) << "/"
      << mean (hop.sumCeneSink, hop.nRew)
      << " c_del nh/sink=" << mean (hop.sumCdelNh, hop.nRew) << "/"
      << mean (hop.sumCdelSink, hop.nRew)
      << " c_nei nh/sink=" << mean (hop.sumCneiNh, hop.nRew) << "/"
      << mean (hop.sumCneiSink, hop.nRew)
      << " R nh/sink=" << mean (hop.sumRNh, hop.nRew) << "/"
      << mean (hop.sumRSink, hop.nRew)
      << " Q nh/sink=" << mean (hop.sumQNh, hop.nRew) << "/"
      << mean (hop.sumQSink, hop.nRew) << "\n";
  oss << "source_packets=" << hop.nSrc
      << " live_bfs_mean=" << mean (hop.sumSrcLive, hop.nSrc - hop.srcUnreach)
      << " unreachable=" << hop.srcUnreach << "\n";
  oss << "delivered=" << hop.nDeliv
      << " live_bfs_1=" << hop.delivLive1
      << " live_bfs_1_but_actual>1=" << hop.delivLive1Multi
      << " (" << frac (hop.delivLive1Multi, hop.delivLive1) << " of live-1 deliveries)"
      << " live_bfs>1=" << hop.delivLiveGt1 << "\n";
  oss << "delivered_mean_actual_hops=" << mean (hop.sumActual, hop.nDeliv)
      << " delivered_mean_live_bfs=" << mean (hop.sumLive, nReach) << "\n";
  oss << "decisions_with_live_direct_sink=" << hop.decLive1
      << " of_which_not_selected=" << hop.decLive1NotSink
      << " (" << frac (hop.decLive1NotSink, hop.decLive1) << ")\n";
  oss << "live_bfs_hist";
  for (int b = 0; b < 8; ++b)
    {
      oss << " " << (b == 7 ? "7+" : std::to_string (b)) << ":" << hop.liveHist[b];
    }
  oss << "\nactual_hops_hist";
  for (int b = 0; b < 8; ++b)
    {
      oss << " " << (b == 7 ? "7+" : std::to_string (b)) << ":" << hop.actualHist[b];
    }
  oss << "\n";
  for (const std::string &line : hopSamples)
    {
      oss << "  " << line << "\n";
    }
  oss << "HOPDIAG proto=" << (proto ? proto->name () : "?")
      << " scenario=" << scenario << " seed=" << seed << " drift=" << driftSpeed
      << " decisions=" << hop.nDec
      << " sink_in=" << hop.sinkInActions
      << " sink_chosen=" << hop.sinkChosen
      << " sink_not_chosen=" << hop.sinkInNotChosen
      << " sink_absent=" << hop.sinkAbsent
      << " fail_unheard=" << hop.failUnheard
      << " fail_range=" << hop.failRange
      << " fail_snr=" << hop.failSnr
      << " fail_depth=" << hop.failDepth
      << " fail_stab=" << hop.failStab
      << " q_nh_gt=" << hop.qNhGt
      << " q_eq=" << hop.qEq
      << " q_sink_gt=" << hop.qSinkGt
      << " chosen_tie=" << hop.chosenTie
      << " chosen_only=" << hop.chosenOnly
      << " chosen_strict=" << hop.chosenStrict
      << " chosen_worse=" << hop.chosenWorse
      << " cene_nh_gt=" << hop.ceneNhGt
      << " cene_nh_lt=" << hop.ceneNhLt
      << " cene_eq=" << hop.ceneEq
      << " cdel_nh_gt=" << hop.cdelNhGt
      << " cnei_nh_gt=" << hop.cneiNhGt
      << " deliv=" << hop.nDeliv
      << " deliv_live1=" << hop.delivLive1
      << " deliv_live1_multi=" << hop.delivLive1Multi
      << " deliv_live_gt1=" << hop.delivLiveGt1
      << " dec_live1=" << hop.decLive1
      << " dec_live1_not_sink=" << hop.decLive1NotSink
      << " absent_but_live=" << hop.absentButLive
      << " mean_cene_nh=" << mean (hop.sumCeneNh, hop.nRew)
      << " mean_cene_sink=" << mean (hop.sumCeneSink, hop.nRew)
      << " mean_cdel_nh=" << mean (hop.sumCdelNh, hop.nRew)
      << " mean_cdel_sink=" << mean (hop.sumCdelSink, hop.nRew)
      << " mean_cnei_nh=" << mean (hop.sumCneiNh, hop.nRew)
      << " mean_cnei_sink=" << mean (hop.sumCneiSink, hop.nRew)
      << " mean_R_nh=" << mean (hop.sumRNh, hop.nRew)
      << " mean_R_sink=" << mean (hop.sumRSink, hop.nRew)
      << " mean_Q_nh=" << mean (hop.sumQNh, hop.nRew)
      << " mean_Q_sink=" << mean (hop.sumQSink, hop.nRew)
      << " pose_err_m=" << mean (hop.sumPose, hop.nPose)
      << " zoh_age_s=" << mean (hop.sumAge, hop.nAge)
      << " stab_abs_e=" << mean (hop.sumAbsE, hop.nAbsE)
      << " stab_eps=" << mean (hop.sumEps, hop.nAbsE)
      << "\n";
  oss << "HOPDIAG_END\n";
}

void
Pipeline::Forward (Pkt pkt)
{
  SweepExits ();
  SyncPoses ();
  if (DropIfDead (pkt))
    {
      return;
    }
  if (pkt.hops >= kMaxHops)
    {
      ++nTtl;
      if (pkt.id < inflight.size ())
        {
          inflight[pkt.id] = false;
        }
      return;
    }

  if (!dahello::LqarpProtocol::containsId (pkt.visited, pkt.at))
    {
      pkt.visited.push_back (pkt.at);
    }

  std::vector<uint32_t> I;
  std::vector<uint32_t> candRaw;
  {
    PoisonScope ps (this, pkt.at);
    candRaw = proto->candidateSet (pkt.at);
  }
  for (uint32_t j : candRaw)
    {
      if (!dahello::LqarpProtocol::containsId (pkt.visited, j) && (discMode == "heard" || nodeAlive (j)))
        {
          I.push_back (j);
        }
    }

  bool detour = false;
  int nh = -1;
  if (!I.empty ())
    {
      RecordVRel (pkt.at, I);
      {
        PoisonScope ps (this, pkt.at);
        proto->learnFrom (pkt.at, I);
        nh = proto->nextHop (pkt.at, I);
      }
      if (decisionTrace)
        {
          decTrace << std::setprecision (6) << "DEC t=" << Simulator::Now ().GetSeconds ()
                   << " pkt=" << pkt.id << " at=" << pkt.at << " nh=" << nh
                   << " n=" << I.size () << " sink_in="
                   << (dahello::LqarpProtocol::containsId (I, sinkId) ? 1 : 0);
          for (uint32_t j : I)
            {
              RewardPeek rp;
              if (proto->peekReward (pkt.at, j, rp))
                {
                  decTrace << " |" << j << ":" << rp.q << "," << rp.R << "," << rp.c_ene
                           << "," << rp.c_del << "," << rp.c_nei;
                }
            }
          decTrace << "\n";
        }
      if (hopDiag)
        {
          NoteHopDiag (pkt, I, nh, false);
        }
    }
  else
    {
      if (!proto->useDetour () || pkt.detourHops >= dahello::kMaxDetourHops)
        {
          if (hopDiag)
            {
              NoteHopDiag (pkt, I, -1, false);
            }
          noteVoid (pkt);
          return;
        }
      std::vector<uint32_t> rawT;
      {
        PoisonScope ps (this, pkt.at);
        rawT = proto->detourTargets (pkt.at, sinkId, pkt.visited);
      }
      std::vector<uint32_t> T;
      for (uint32_t j : rawT)
        {
          if (discMode == "heard" || nodeAlive (j))
            {
              T.push_back (j);
            }
        }
      if (T.empty ())
        {
          if (hopDiag)
            {
              NoteHopDiag (pkt, T, -1, true);
            }
          noteVoid (pkt);
          return;
        }
      RecordVRel (pkt.at, T);
      {
        PoisonScope ps (this, pkt.at);
        proto->learnFrom (pkt.at, T);
        nh = proto->nextHop (pkt.at, T);
      }
      detour = true;
      if (hopDiag)
        {
          NoteHopDiag (pkt, T, nh, true);
        }
    }

  if (nh < 0 || !nodeAlive (static_cast<uint32_t> (nh)))
    {
      if (nh >= 0 && !nodeAlive (static_cast<uint32_t> (nh)))
        {
          noteEnvelope (pkt);
          hopsFailed += pkt.hops;
          if (pkt.id < inflight.size ())
            {
              inflight[pkt.id] = false;
            }
          return;
        }
      noteVoid (pkt);
      return;
    }

  // Frozen admission already selected nh. Compare that choice with the
  // live range / SNR / depth filters. Depth is not required on a detour.
  const bool staleHop = !liveGeometricPass (pkt.at, static_cast<uint32_t> (nh),
                                             detour ? false : candidateUsesDepth ());

  const double eTx = energy.eTxJ ();
  if (!(proto->energy (pkt.at) >= eTx))
    {
      ++nEnergy;
      if (pkt.id < inflight.size ())
        {
          inflight[pkt.id] = false;
        }
      return;
    }

  const auto rwOk = proto->hopMathFinite (pkt.at, static_cast<uint32_t> (nh));
  if (!rwOk)
    {
      ++nNonfinite;
    }

  if (!medOn ())
    {
      proto->setEnergy (pkt.at, proto->energy (pkt.at) - eTx);
    }
  ++nTx;
  proto->noteTxAttempt (pkt.at);
  proto->noteOwnSend (pkt.at, static_cast<uint32_t> (nh));
  if (detour)
    {
      if (pkt.detourHops == 0)
        {
          ++nDetourPkts;
        }
      ++nDetourHops;
    }

  if (staleHop)
    {
      ++nStaleAdmit;
    }

  if (medOn ())
    {
      // Port A: the hop goes through the shared medium (ARQ, MAC, SINR/PER);
      // energy is billed by modem state time, not per packet.
      MedUnicastSend (pkt, static_cast<uint32_t> (nh), detour, staleHop);
      return;
    }

  const dahello::ChannelSample samp =
      proto->linkSample (pkt.at, static_cast<uint32_t> (nh));
  if (!gate->receptionOk (samp))
    {
      ++nSnr;
      if (staleHop)
        {
          ++nSnrStale;
        }
      else
        {
          ++nSnrGenuine;
        }
      if (pkt.id < inflight.size ())
        {
          inflight[pkt.id] = false;
        }
      return;
    }
  if (staleHop)
    {
      ++nStaleAdmitOk;
    }
  proto->noteDataReception (static_cast<uint32_t> (nh), pkt.at);

  const double eRx = energy.eRxJ ();
  proto->setEnergy (static_cast<uint32_t> (nh),
                    proto->energy (static_cast<uint32_t> (nh)) - eRx);

  const dahello::LqarpNode &a = proto->nodes ()[pkt.at];
  const dahello::LqarpNode &b = proto->nodes ()[static_cast<uint32_t> (nh)];
  const double dist = dahello::ChannelGate::distance3D (a.x, a.y, a.z, b.x, b.y, b.z);
  Pkt next = pkt;
  next.at = static_cast<uint32_t> (nh);
  next.hops = pkt.hops + 1;
  next.viaStale = pkt.viaStale || staleHop;
  if (detour)
    {
      next.detourHops = pkt.detourHops + 1;
    }
  const double flight = energy.txDelayS (dist);
  hopDelaySum += flight;
  ++nHopDelay;
  Simulator::Schedule (Seconds (flight),
                       &Pipeline::OnArrive, this, next);
}

// ---------------------------------------------------------------------
// MARV packet engine (Alg. 1, p.7049). One transmission per holder;
// every listed receiver that demodulates holds the copy for
// processing delay + T(n) and forwards unless, during the hold, it
// overheard a forward of the same packet by another receiver of the same
// transmission (or, with --marvSinkOverheard=ack, the sink's acceptance).
// Overhearing is perfect: no MAC, no collisions, no loss.
// ---------------------------------------------------------------------

void
Pipeline::MarvCopyEnd (const Pkt &pkt, int fate)
{
  MarvPk &s = marvPk[pkt.id];
  if (fate >= 0)
    {
      if (s.delivered)
        {
          ++marvLossAfterDelivery;
        }
      else
        {
          s.pendFate = fate;
          s.pendPkt = pkt;
        }
    }
  if (s.live > 0)
    {
      --s.live;
    }
  if (s.live > 0 || s.delivered || s.closed)
    {
      return;
    }
  s.closed = true;
  const Pkt &p = s.pendPkt;
  switch (s.pendFate)
    {
    case kMarvVoid:
      noteVoid (s.pendPkt);
      break;
    case kMarvSnr:
      ++nSnr;
      ++nSnrGenuine;
      break;
    case kMarvEnergy:
      ++nEnergy;
      break;
    case kMarvTtl:
      ++nTtl;
      break;
    case kMarvMac:
      ++nMacDrop;
      break;
    case kMarvArq:
      ++nArqDrop;
      break;
    case kMarvEnvelope:
      noteEnvelope (p);
      if (p.hops > 0)
        {
          hopsFailed += p.hops;
        }
      break;
    default:
      ++marvNoFate;
      break;
    }
  if (pkt.id < inflight.size ())
    {
      inflight[pkt.id] = false;
    }
}

void
Pipeline::MarvHeader (uint32_t k, uint32_t i, double V, bool voidFlag, double e)
{
  if (!nodeAlive (k))
    {
      return;
    }
  if (helloOnlyHost)
    {
      ++rwMarvHdrSkipped; // p15 A2.2(b): no state from data headers
      return;
    }
  ++rwMarvHdr;
  marv->marv ().hear (k, i, V, voidFlag, e, Simulator::Now ().GetSeconds ());
}

// p15: output-only neighbour-record write counters (A2.5 item 3) and jitter phases (item 2),
// written to a separate file so the per-run report / stdout stay byte-identical to p14.
void
Pipeline::WriteRecWrites () const
{
  if (recWriteOut.empty ())
    {
      return;
    }
  std::ofstream f (recWriteOut);
  f << std::setprecision (12);
  f << "RECWRITES_BEGIN p15\n";
  f << "switches periodicJitter=" << periodicJitter << " helloOnlyHost=" << helloOnlyHost
    << " pollStored=" << pollStored << " sinkBeaconPhase=" << sinkBeaconPhase << " eddyCx=" << g_eddyCx
    << " eddyCy=" << g_eddyCy << " eddyR=" << g_eddyR << "\n";
  f << "decode_writes hello=" << rwDec[dahello::kFrameHello] << " beacon=" << rwDec[dahello::kFrameBeacon]
    << " data=" << rwDec[dahello::kFrameData] << " ack=0 ack_decodes=" << rwAckDec << "\n";
  f << "data_decodes_suppressed=" << rwDataSkipped << "\n";
  f << "marv_header_writes=" << rwMarvHdr << " marv_header_suppressed=" << rwMarvHdrSkipped << "\n";
  f << "cmp_table_writes ctrl=" << rwCmpCtrl << " data=" << rwCmpData << " data_suppressed=" << rwCmpDataSkipped << "\n";
  if (marv)
    {
      f << "pollpair_writes live=" << marv->pollLiveW () << " stored=" << marv->pollStoredW ()
        << " energy_live=" << marv->pollEnergyLiveW () << " energy_stored=" << marv->pollEnergyStoredW () << "\n";
      f << "build_round_oracle_rx=" << marv->buildRoundRx () << " tCtrl=" << marv->tCtrl () << "\n";
      if (marv->periodicJitter ())
        {
          const auto &off = marv->pjOffsets ();
          const auto &fs = marv->pjFirstSend ();
          for (size_t j = 0; j < off.size (); ++j)
            {
              f << "pj node=" << j << " offset=" << off[j] << " first_send=" << fs[j] << "\n";
            }
        }
    }
  f << "RECWRITES_END\n";
}

void
Pipeline::MarvTransmit (Pkt pkt)
{
  SweepExits ();
  SyncPoses ();
  const uint32_t i = pkt.at;
  dahello::MarvProtocol &mp = marv->marv ();
  if (!nodeAlive (i))
    {
      MarvCopyEnd (pkt, kMarvEnvelope);
      return;
    }
  if (pkt.hops >= kMaxHops)
    {
      MarvCopyEnd (pkt, kMarvTtl);
      return;
    }
  if (!dahello::LqarpProtocol::containsId (pkt.visited, i))
    {
      pkt.visited.push_back (i);
    }
  std::vector<uint32_t> U;
  std::vector<uint32_t> candRawM;
  {
    PoisonScope ps (this, i);
    candRawM = proto->candidateSet (i);
  }
  for (uint32_t j : candRawM)
    {
      if (!dahello::LqarpProtocol::containsId (pkt.visited, j) && (discMode == "heard" || nodeAlive (j)))
        {
          U.push_back (j);
        }
    }
  std::vector<uint32_t> list;
  {
    PoisonScope ps (this, i);
    list = mp.updateAndRank (i, U);
  }
  if (list.empty ())
    {
      MarvCopyEnd (pkt, kMarvVoid);
      return;
    }
  const double eTx = energy.eTxJ ();
  if (!(proto->energy (i) >= eTx))
    {
      MarvCopyEnd (pkt, kMarvEnergy);
      return;
    }
  if (!proto->hopMathFinite (i, list.front ()))
    {
      ++nNonfinite;
    }
  const double now = Simulator::Now ().GetSeconds ();
  if (!medOn ())
    {
      proto->setEnergy (i, proto->energy (i) - eTx);
    }
  ++nTx;
  ++marvTx;
  marvListLenSum += list.size ();
  proto->noteTxAttempt (i);
  MarvPk &st = marvPk[pkt.id];
  if (st.delivered)
    {
      ++marvTxAfterDelivery;
    }
  const int64_t txId = marvNextTxId++;
  if (medOn ())
    {
      // Port A: header, implicit ack, suppression and list receptions all come
      // from frames actually decoded on the shared medium (MedOnDecode).
      MedMarvSend (pkt, list, txId);
      MarvCopyEnd (pkt, -1);
      return;
    }
  // This forward is what the holder's siblings overhear (suppression) and
  // what the previous sender overhears as the implicit ack (D2).
  if (pkt.parentTx >= 0)
    {
      st.fwd.push_back (MarvFwdEvt{now, i, pkt.parentTx});
    }
  if (pkt.prevSender >= 0)
    {
      mp.noteForwardOverheard (static_cast<uint32_t> (pkt.prevSender), i,
                               static_cast<uint32_t> (pkt.prevTop));
    }
  // Header fields (V, void, residual, depth, congestion 0) reach every
  // alive node in range after the propagation delay (perfect overhearing).
  const double Vi = mp.advertisedV (i);
  const bool voidI = mp.advertisedVoid (i);
  marv->noteStateSent (i);
  const double eI = proto->energy (i);
  const dahello::LqarpNode &a = proto->nodes ()[i];
  for (uint32_t k = 0; k < nNodes (); ++k)
    {
      if (k == i || !nodeAlive (k) || !mp.inRange (i, k))
        {
          continue;
        }
      const dahello::LqarpNode &b = proto->nodes ()[k];
      const double d = dahello::ChannelGate::distance3D (a.x, a.y, a.z, b.x, b.y, b.z);
      Simulator::Schedule (Seconds (energy.txDelayS (d)), &Pipeline::MarvHeader, this, k, i, Vi,
                           voidI, eI);
    }
  mp.noteSent (i, list);
  uint32_t nRx = 0;
  for (uint32_t n = 1; n <= list.size (); ++n)
    {
      const uint32_t j = list[n - 1];
      const dahello::ChannelSample samp = proto->linkSample (i, j);
      if (!gate->receptionOk (samp))
        {
          ++marvRxFail;
          continue;
        }
      ++nRx;
      proto->noteDataReception (j, i);
      proto->setEnergy (j, proto->energy (j) - energy.eRxJ ());
      const dahello::LqarpNode &b = proto->nodes ()[j];
      const double d = dahello::ChannelGate::distance3D (a.x, a.y, a.z, b.x, b.y, b.z);
      const double flight = energy.txDelayS (d);
      hopDelaySum += flight;
      ++nHopDelay;
      Pkt next = pkt;
      next.at = j;
      next.hops = pkt.hops + 1;
      next.prevSender = static_cast<int> (i);
      next.prevTop = static_cast<int> (list.front ());
      next.pos = n;
      next.tRecv = now + flight;
      next.parentTx = txId;
      ++st.live;
      ++marvCopies;
      Simulator::Schedule (Seconds (flight), &Pipeline::MarvReceive, this, next);
    }
  if (nRx == 0)
    {
      MarvCopyEnd (pkt, kMarvSnr);
      return;
    }
  MarvCopyEnd (pkt, -1);
}

void
Pipeline::MarvReceive (Pkt pkt)
{
  SweepExits ();
  const uint32_t j = pkt.at;
  if (!nodeAlive (j))
    {
      MarvCopyEnd (pkt, kMarvEnvelope);
      return;
    }
  const double proc = marv->marv ().config ().procDelayS;
  if (j == sinkId)
    {
      Simulator::Schedule (Seconds (proc), &Pipeline::MarvAccept, this, pkt);
      return;
    }
  if (marvSeen[j].size () <= pkt.id)
    {
      marvSeen[j].resize (pkt.id + 1, 0);
    }
  if (marvSeen[j][pkt.id])
    {
      ++marvDupDiscard;
      MarvCopyEnd (pkt, -1);
      return;
    }
  marvSeen[j][pkt.id] = 1;
  if (medOn ())
    {
      medHold[j][pkt.id] = pkt.parentTx;
    }
  const double hold = proc + marv->marv ().holdingTimeS (pkt.pos);
  Simulator::Schedule (Seconds (hold), &Pipeline::MarvHoldExpire, this, pkt);
}

void
Pipeline::MarvAccept (Pkt pkt)
{
  MarvPk &st = marvPk[pkt.id];
  const double now = Simulator::Now ().GetSeconds ();
  const bool ack = marv->marv ().config ().sinkOverheard == dahello::MarvSinkOverheard::Ack;
  if (ack && medOn ())
    {
      // Port A: the sink acceptance is an ACK frame on the medium.
      auto pf = std::make_shared<PFrame> ();
      pf->ackFor = pkt.id;
      pf->ackPrevSender = pkt.prevSender;
      pf->ackPrevTop = pkt.prevTop;
      MedEnqueue (sinkId, dahello::kFrameAck, -1, ackBits, pf, true);
    }
  else if (ack)
    {
      // Sink acceptance overheard like a forward: implicit ack to the
      // sender and suppression of every holder of this packet.
      st.fwd.push_back (MarvFwdEvt{now, sinkId, -1});
      if (pkt.prevSender >= 0)
        {
          marv->marv ().noteForwardOverheard (static_cast<uint32_t> (pkt.prevSender), sinkId,
                                              static_cast<uint32_t> (pkt.prevTop));
        }
    }
  if (!st.delivered)
    {
      st.delivered = true;
      if (medOn ())
        {
          MedNoteDelivery (pkt);
        }
      const double lat = now - pkt.genTime;
      ++nSucc;
      hopSum += pkt.hops;
      latencySum += lat;
      latencyMax = std::max (latencyMax, lat);
      if (!std::isfinite (lat))
        {
          ++nNonfinite;
        }
      if (pkt.id < inflight.size ())
        {
          inflight[pkt.id] = false;
        }
    }
  else
    {
      ++marvDupAtSink;
    }
  MarvCopyEnd (pkt, -1);
}

void
Pipeline::MarvHoldExpire (Pkt pkt)
{
  SweepExits ();
  SyncPoses ();
  const uint32_t j = pkt.at;
  const double now = Simulator::Now ().GetSeconds ();
  const double c = marv->marv ().config ().soundSpeedMps;
  const MarvPk &st = marvPk[pkt.id];
  if (medOn ())
    {
      medHold[j].erase (pkt.id);
      if (medSupp[j].count (pkt.id))
        {
          ++marvSuppressed;
          MarvCopyEnd (pkt, -1);
          return;
        }
      MarvTransmit (pkt);
      return;
    }
  const dahello::LqarpNode &b = proto->nodes ()[j];
  for (const MarvFwdEvt &f : st.fwd)
    {
      if (f.node == j)
        {
          continue;
        }
      if (f.parentTx != -1 && f.parentTx != pkt.parentTx)
        {
          continue;
        }
      const dahello::LqarpNode &a = proto->nodes ()[f.node];
      const double heardAt = f.t + dahello::ChannelGate::distance3D (a.x, a.y, a.z, b.x, b.y, b.z) / c;
      if (heardAt <= now + 1e-12)
        {
          ++marvSuppressed;
          MarvCopyEnd (pkt, -1);
          return;
        }
    }
  MarvTransmit (pkt);
}

bool
Pipeline::DropIfDead (const Pkt &pkt)
{
  if (nodeAlive (pkt.at))
    {
      return false;
    }
  noteEnvelope (pkt);
  if (pkt.hops > 0)
    {
      hopsFailed += pkt.hops;
    }
  if (pkt.id < inflight.size ())
    {
      inflight[pkt.id] = false;
    }
  return true;
}

void
Pipeline::RecordVRel (uint32_t i, const std::vector<uint32_t> &actions)
{
  if (!proto || !proto->hasVRel ())
    {
      return;
    }
  const double now = Simulator::Now ().GetSeconds ();
  for (uint32_t j : actions)
    {
      const double vr = proto->vRel (i, j);
      const bool auvAuv = (i != sinkId && j != sinkId);
      vrel.add (vr, i == sinkId, j == sinkId);
      const bool heard = proto->zohHeard (i, j);
      vrel.addHeard (heard, vr, auvAuv);
      if (heard)
        {
          const double t0 = proto->lastZohTime (i, j);
          if (std::isfinite (t0))
            {
              zohGaps.push_back (std::max (0.0, now - t0));
            }
        }
    }
}

void
Pipeline::RemoveNode (uint32_t i)
{
  if (i == sinkId || !nodeAlive (i))
    {
      return;
    }
  alive[i] = false;
  ++nOutOfEnvelope;
  if (med)
    {
      med->kill (i);
    }
  Ptr<ConstantVelocityMobilityModel> m =
      nodes.Get (i)->GetObject<ConstantVelocityMobilityModel> ();
  if (m)
    {
      m->SetVelocity (Vector (0.0, 0.0, 0.0));
    }
  if (proto)
    {
      const Vector p = nodes.Get (i)->GetObject<MobilityModel> ()->GetPosition ();
      proto->setKinematics (i, p.x, p.y, p.z, 0.0, 0.0, 0.0);
    }
}

void
Pipeline::SweepExits ()
{
  AdvanceEddyTo (Simulator::Now ().GetSeconds ());
  if (!removeOnExit ())
    {
      return;
    }
  if (alive.size () != nNodes ())
    {
      alive.assign (nNodes (), true);
    }
  for (uint32_t i = 1; i <= nAuv; ++i)
    {
      if (!nodeAlive (i))
        {
          continue;
        }
      const Vector p = nodes.Get (i)->GetObject<MobilityModel> ()->GetPosition ();
      if (p.x < 0.0 || p.x > kVolumeXyM || p.y < 0.0 || p.y > kVolumeXyM)
        {
          RemoveNode (i);
        }
    }
}

void
Pipeline::MaybePVelTrace ()
{
  dahello::ObserverScope obs; // diagnostic trace only (no decision effect)
  if (!proto || !proto->hasVRel () || traceJ == 0 || traceI == traceJ)
    {
      return;
    }
  if (traceI >= nNodes () || traceJ >= nNodes ())
    {
      return;
    }
  const double t = Simulator::Now ().GetSeconds ();
  double p33 = 0, p44 = 0, p55 = 0;
  int bc = 0;
  if (!proto->ekfVelP (traceI, traceJ, p33, p44, p55, bc))
    {
      return;
    }

  EkfView ev;
  if (proto->ekfView (traceI, traceJ, ev) && gate)
    {
      const auto &ni = proto->nodes ()[traceI];
      const auto &nj = proto->nodes ()[traceJ];
      EkfTick tk;
      tk.t = t;
      for (int k = 0; k < 7; ++k)
        {
          tk.x[k] = ev.x[k];
          tk.xForecast[k] = ev.xForecast[k];
        }
      tk.e_k = ev.e_k;
      tk.e_km1 = ev.e_km1;
      tk.epsilon_k = ev.epsilon_k;
      tk.S_k = ev.S_k;
      tk.eTrigger = ev.eTrigger;
      tk.STrigger = ev.STrigger;
      tk.epsilonTrigger = ev.epsilonTrigger;
      tk.bc = ev.broadcast_k;
      tk.heard = ev.heard;
      tk.vrelZoh = proto->vRel (traceI, traceJ);
      tk.pijTrueX = ni.x - nj.x;
      tk.pijTrueY = ni.y - nj.y;
      tk.pijTrueZ = ni.z - nj.z;
      tk.vTrueX = ni.vx - nj.vx;
      tk.vTrueY = ni.vy - nj.vy;
      tk.vTrueZ = ni.vz - nj.vz;
      tk.rangeHTrue = dahello::ChannelGate::horizontalRangeM (ni.x, ni.y, nj.x, nj.y);
      tk.rangeHEkf = std::hypot (ev.x[0], ev.x[1]);
      tk.rangeHForecast = std::hypot (ev.xForecast[0], ev.xForecast[1]);
      const double zRx = nj.queryDepthM ();
      const dahello::ChannelSample meas = proto->linkSample (traceI, traceJ);
      tk.snrMeas = meas.snr_db;
      const dahello::ChannelSample s0 = gate->queryRangeDepth (tk.rangeHTrue, zRx);
      const dahello::ChannelSample s1 = gate->queryRangeDepth (tk.rangeHTrue + 10.0, zRx);
      if (std::isfinite (s0.snr_db) && std::isfinite (s1.snr_db))
        {
          tk.dsnrDr = (s1.snr_db - s0.snr_db) / 10.0;
        }
      if (tk.rangeHEkf > 1e-9 && std::isfinite (tk.dsnrDr))
        {
          tk.hPx = tk.dsnrDr * (ev.x[0] / tk.rangeHEkf);
          tk.hPy = tk.dsnrDr * (ev.x[1] / tk.rangeHEkf);
        }
      tk.hPz = 0.0;
      tk.hVx = 0.0;
      tk.hVy = 0.0;
      tk.hVz = 0.0;
      if (!ekfTicks.empty () && std::fabs (ekfTicks.back ().t - t) < 1e-9)
        {
          ekfTicks.back () = tk;
        }
      else
        {
          ekfTicks.push_back (tk);
        }
    }

  bool onGrid = pvelTrace.empty ();
  const double early[] = {0.0, 1.0, 2.0, 5.0, 10.0, 20.0, 50.0};
  for (double te : early)
    {
      if (std::fabs (t - te) < 0.5 * tickDt + 1e-9)
        {
          onGrid = true;
          break;
        }
    }
  if (!onGrid && t >= 50.0 - 1e-9)
    {
      const double k = std::round (t / 50.0);
      if (std::fabs (t - k * 50.0) < 0.5 * tickDt + 1e-9)
        {
          onGrid = true;
        }
    }
  const bool rising = (bc == 1 && lastTraceBc != 1);
  lastTraceBc = bc;
  const bool due = onGrid || rising;
  if (!due)
    {
      return;
    }

  PVelSample sm;
  sm.t = t;
  sm.heard = proto->zohHeard (traceI, traceJ);
  sm.bc = bc;
  sm.p33 = p33;
  sm.p44 = p44;
  sm.p55 = p55;
  sm.vrel = proto->vRel (traceI, traceJ);
  const double t0 = proto->lastZohTime (traceI, traceJ);
  sm.gap = std::isfinite (t0) ? std::max (0.0, t - t0) : t0;

  if (!pvelTrace.empty () && std::fabs (pvelTrace.back ().t - t) < 1e-9)
    {
      pvelTrace.back () = sm;
      return;
    }
  pvelTrace.push_back (sm);
}

void
Pipeline::MaybeCensus ()
{
  const double t = Simulator::Now ().GetSeconds ();
  const bool due = census.empty ()
                   || (t + 1e-9 >= lastCensusT + 200.0)
                   || (std::fabs (t - tSim) < 0.5 * tickDt + 1e-9);
  if (!due)
    {
      return;
    }
  lastCensusT = t;
  Census c;
  c.t = t;
  for (uint32_t i = 1; i <= nAuv; ++i)
    {
      if (nodeAlive (i))
        {
          ++c.nAliveAuv;
        }
    }
  census.push_back (c);
}

void
Pipeline::AdvanceEddyTo (double now)
{
  if (isScenarioII () && nWpAuv > 0)
    {
      AdvanceWaypointsTo (now);
      return;
    }
  if (!isScenarioIII () || gmSt.size () != nNodes ())
    {
      return;
    }
  while (gmLastT + gmCfg.dt <= now + 1e-9)
    {
      for (uint32_t i = 1; i <= nAuv; ++i)
        {
          if (!nodeAlive (i))
            {
              continue;
            }
          dahello::eddyMeanFlow (gmSt[i].x, gmSt[i].y, eddy,
                               gmCfg.mu_x, gmCfg.mu_y, gmCfg.mu_z);
          dahello::gaussMarkovStep (gmSt[i], gmCfg, gmRng[i]);
          Ptr<ConstantVelocityMobilityModel> m =
              nodes.Get (i)->GetObject<ConstantVelocityMobilityModel> ();
          if (m)
            {
              m->SetPosition (Vector (gmSt[i].x, gmSt[i].y, gmSt[i].z));
              m->SetVelocity (Vector (gmSt[i].vx, gmSt[i].vy, gmSt[i].vz));
            }
        }
      gmLastT += gmCfg.dt;
    }
}

void
Pipeline::SyncPoses ()
{
  AdvanceEddyTo (Simulator::Now ().GetSeconds ());
  if (!proto)
    {
      return;
    }
  for (uint32_t i = 0; i < nNodes (); ++i)
    {
      Ptr<MobilityModel> m = nodes.Get (i)->GetObject<MobilityModel> ();
      const Vector p = m->GetPosition ();
      const Vector v = m->GetVelocity ();
      proto->setKinematics (i, p.x, p.y, p.z, v.x, v.y, v.z);
    }
  if (med)
    {
      MedSyncEnergy ();
    }
}

void
Pipeline::MaybeTraceSnr ()
{
  dahello::ObserverScope obs; // diagnostic trace only (no decision effect)
  if (!proto || snrTraceId == sinkId || snrTraceId >= nNodes () || !nodeAlive (snrTraceId))
    {
      return;
    }
  const double t = Simulator::Now ().GetSeconds ();
  const bool due = (snrTrace.empty () && t <= 1e-12)
                   || (t + 1e-9 >= lastSnrTraceT + snrTraceEvery)
                   || (std::fabs (t - tSim) < 0.5 * tickDt + 1e-9);
  if (!due)
    {
      return;
    }
  lastSnrTraceT = t;
  const dahello::LqarpNode &a = proto->nodes ()[snrTraceId];
  const dahello::LqarpNode &s = proto->nodes ()[sinkId];
  SnrSample sm;
  sm.t = t;
  sm.x = a.x;
  sm.y = a.y;
  sm.z = a.z;
  sm.rangeH = dahello::ChannelGate::horizontalRangeM (a.x, a.y, s.x, s.y);
  const dahello::ChannelSample samp = proto->linkSample (snrTraceId, sinkId);
  sm.snrDb = samp.snr_db;
  sm.valid = samp.valid && std::isfinite (samp.snr_db);
  snrTrace.push_back (sm);
}

void
Pipeline::OnTick ()
{
  SweepExits ();
  SyncPoses ();
  MaybeCensus ();
  MaybeTraceSnr ();
  if (proto)
    {
      tt::GuardedTick (*this);
    }
  MaybePVelTrace ();
  if (novDiag && Simulator::Now ().GetSeconds () <= tSim + 1e-9)
    {
      NovDiagTick ();
    }
  if (!nsSnapped && proto && Simulator::Now ().GetSeconds () + 1e-9 >= tSim)
    {
      NovSnapshot ();
    }
  if (!ctrlTxSnapped && proto
      && Simulator::Now ().GetSeconds () + 1e-9 >= tSim)
    {
      ctrlTxAtTsim = proto->controlTxCount ();
      eventHellosAtTsim = proto->eventHelloCount ();
      bootstrapHellosAtTsim = proto->bootstrapHelloCount ();
      energyHellosAtTsim = proto->energyHelloCount ();
      risingAtTsim = proto->broadcastRising ();
      onesAtTsim = proto->broadcastKOnes ();
      if (marv && marv->hostState ())
        {
          hsVoidAtTsim = marv->hsVoidFires ();
          hsVAtTsim = marv->hsVFires ();
          hsPreAtTsim = marv->hsPreempted ();
          hsHelloAtTsim = marv->hsHellos ();
        }
      ctrlTxSnapped = true;
    }
  if (!satSnapped && proto && proto->hasVRel ()
      && Simulator::Now ().GetSeconds () + 1e-9 >= tSim)
    {
      satSnap.clear ();
      for (uint32_t i = 1; i <= nAuv; ++i)
        {
          for (uint32_t j = 1; j <= nAuv; ++j)
            {
              if (i == j)
                {
                  continue;
                }
              EkfView ev;
              if (!proto->ekfViewVel (i, j, ev)) // OPT-4: reads velocities + counters only
                {
                  continue;
                }
              SatSnap sm;
              sm.i = i;
              sm.j = j;
              sm.heard = ev.heard;
              sm.aliveI = nodeAlive (i);
              sm.aliveJ = nodeAlive (j);
              sm.maxE = ev.maxAbsETrigger;
              sm.maxEUnheard = ev.maxAbsETriggerUnheard;
              sm.maxEpsUnheard = ev.maxEpsilonTriggerUnheard;
              sm.fcVel = std::sqrt (ev.xFcVel[0] * ev.xFcVel[0] + ev.xFcVel[1] * ev.xFcVel[1]
                                    + ev.xFcVel[2] * ev.xFcVel[2]);
              sm.hatVel = std::sqrt (ev.xHatVel[0] * ev.xHatVel[0] + ev.xHatVel[1] * ev.xHatVel[1]
                                     + ev.xHatVel[2] * ev.xHatVel[2]);
              sm.firstBc = ev.firstBroadcastTime;
              sm.nRising = ev.nBroadcastRising;
              satSnap.push_back (sm);
            }
        }
      satSnapped = true;
    }
}

void
Pipeline::FailInflight ()
{
  for (bool live : inflight)
    {
      if (live)
        {
          ++nSimEnd;
        }
    }
}

void
Pipeline::AdvanceWaypointsTo (double now)
{
  // NOVELTY scenario: AUVs 1..nWpAuv steer at wpSpeed over ground toward
  // i.i.d. uniform 3-D waypoints in the volume (random-waypoint, no pause).
  // The other AUVs keep the shared Scenario II drift (ConstantVelocity).
  const double dt = tickDt;
  while (wpLastT + dt <= now + 1e-9)
    {
      for (uint32_t i = 1; i <= std::min (nWpAuv, nAuv); ++i)
        {
          if (!nodeAlive (i))
            {
              continue;
            }
          Vector p = wpPos[i];
          double step = wpSpeed * dt;
          for (int guard = 0; guard < 8 && step > 0.0; ++guard)
            {
              const Vector d (wpTarget[i].x - p.x, wpTarget[i].y - p.y, wpTarget[i].z - p.z);
              const double L = std::sqrt (d.x * d.x + d.y * d.y + d.z * d.z);
              if (L <= step)
                {
                  p = wpTarget[i];
                  step -= L;
                  std::uniform_real_distribution<double> ux (0.0, kVolumeXyM);
                  std::uniform_real_distribution<double> uz (kMinAuvDepthM, kVolumeZM);
                  wpTarget[i] = Vector (ux (wpRng[i]), ux (wpRng[i]), uz (wpRng[i]));
                  ++wpLegs;
                }
              else
                {
                  p = Vector (p.x + step * d.x / L, p.y + step * d.y / L, p.z + step * d.z / L);
                  step = 0.0;
                }
            }
          wpPos[i] = p;
          const Vector d (wpTarget[i].x - p.x, wpTarget[i].y - p.y, wpTarget[i].z - p.z);
          const double L = std::sqrt (d.x * d.x + d.y * d.y + d.z * d.z);
          const Vector v = (L > 1e-9) ? Vector (wpSpeed * d.x / L, wpSpeed * d.y / L, wpSpeed * d.z / L)
                                      : Vector (0.0, 0.0, 0.0);
          Ptr<ConstantVelocityMobilityModel> m =
              nodes.Get (i)->GetObject<ConstantVelocityMobilityModel> ();
          if (m)
            {
              m->SetPosition (p);
              m->SetVelocity (v);
            }
        }
      wpLastT += dt;
    }
}

void
Pipeline::NovDiagTick ()
{
  dahello::ObserverScope obs; // diagnostic trace only (no decision effect)
  // Observation only: live SNR change rate and true relative speed over
  // alive pairs inside r_range (sink included). Reads proto->nodes(),
  // which SyncPoses() just wrote; does not touch protocol state.
  if (!proto)
    {
      return;
    }
  const uint32_t n = nNodes ();
  if (ndPrevSnr.size () != static_cast<size_t> (n) * n)
    {
      ndPrevSnr.assign (static_cast<size_t> (n) * n, std::numeric_limits<double>::quiet_NaN ());
    }
  const auto &N = proto->nodes ();
  for (uint32_t i = 0; i < n; ++i)
    {
      for (uint32_t j = i + 1; j < n; ++j)
        {
          double &prev = ndPrevSnr[static_cast<size_t> (i) * n + j];
          if (!nodeAlive (i) || !nodeAlive (j))
            {
              prev = std::numeric_limits<double>::quiet_NaN ();
              continue;
            }
          const double d = dahello::ChannelGate::distance3D (N[i].x, N[i].y, N[i].z, N[j].x, N[j].y, N[j].z);
          if (!(d < proto->rRangeM ()))
            {
              prev = std::numeric_limits<double>::quiet_NaN ();
              continue;
            }
          const double snr = proto->linkSample (j, i).snr_db;
          const double dvx = N[i].vx - N[j].vx, dvy = N[i].vy - N[j].vy, dvz = N[i].vz - N[j].vz;
          if (std::isfinite (snr) && std::isfinite (prev))
            {
              const double ds = std::fabs (snr - prev) / tickDt;
              ndSumDSnr += ds;
              ndSumDSnr2 += ds * ds;
              ndSumVrel += std::sqrt (dvx * dvx + dvy * dvy + dvz * dvz);
              const double rx = N[i].x - N[j].x, ry = N[i].y - N[j].y, rz = N[i].z - N[j].z;
              ndSumAbsRR += (d > 1e-9) ? std::fabs ((rx * dvx + ry * dvy + rz * dvz) / d) : 0.0;
              ++ndN;
            }
          prev = snr;
        }
    }
}

void
Pipeline::NovSnapshot ()
{
  nsSnapped = true;
  if (const dahello::DahelloProtocol *s = proto->scheduler ())
    {
      nsRxHello = s->ctrlRxHello ();
      nsRxBeacon = s->ctrlRxBeacon ();
      nsRxBoot = s->ctrlRxBootstrap ();
      nsSumE = s->sumAbsETrigger ();
      nsNE = s->nETrigger ();
      nsNEx = s->nExceed ();
      nsRes = s->adaptiveResets ();
      nsSupp = s->adaptiveSuppressed ();
    }
  if (marv)
    {
      nsPerRx = marv->periodicRx ();
      nsRes += marv->adResets ();
      nsSupp += marv->adSuppressed ();
    }
}

void
Pipeline::WriteReport ()
{
  std::cout << "NOVELTY proto=" << proto->name () << " seed=" << seed
            << " rx_hello=" << nsRxHello << " rx_beacon=" << nsRxBeacon
            << " rx_bootstrap=" << nsRxBoot << " rx_marv_ctrl=" << nsPerRx
            << " etrig_sum=" << std::setprecision (10) << nsSumE << " etrig_n=" << nsNE
            << " exceed_n=" << nsNEx << " ad_resets=" << nsRes << " ad_suppressed=" << nsSupp
            << " diag_pairticks=" << ndN
            << " mean_abs_dsnr_dbps=" << (ndN ? ndSumDSnr / ndN : std::numeric_limits<double>::quiet_NaN ())
            << " rms_dsnr_dbps=" << (ndN ? std::sqrt (ndSumDSnr2 / ndN) : std::numeric_limits<double>::quiet_NaN ())
            << " mean_vrel_mps=" << (ndN ? ndSumVrel / ndN : std::numeric_limits<double>::quiet_NaN ())
            << " mean_abs_rangerate_mps=" << (ndN ? ndSumAbsRR / ndN : std::numeric_limits<double>::quiet_NaN ())
            << " wp_legs=" << wpLegs
            << " E_tx_J=" << energy.eTxJ () << " E_rx_J=" << energy.eRxJ ()
            << " P_tx_W=" << energy.pTxWatt () << " P_rx_W=" << energy.pRxWatt
            << " R_b=" << energy.bitRateBps << "\n";
  // t31 output-only diagnostics: new lines, printed only for the t23/t25 arms (whole run).
  {
    const dahello::DahelloProtocol *sc = proto->scheduler ();
    const bool lqNaive = (protoName == "naivevoid" && sc), lqLet = (protoName == "letadapt" && sc);
    if (lqNaive || (marv && marv->isNaive ()))
      {
        std::cout << "NAIVE_HELLO proto=" << protoName << " void_changes="
                  << (lqNaive ? sc->naiveVoidChanges () : marv->nvVoidChanges ())
                  << " hello_void=" << (lqNaive ? sc->naiveVoidSends () : marv->nvVoidSends ())
                  << " hello_refresh=" << (lqNaive ? sc->naiveRefreshSends () : marv->nvRefreshSends ())
                  << " (scheduled sends by cause, whole run; excludes t=0 round)\n";
      }
    if (lqLet || (marv && marv->isLet ()))
      {
        std::cout << (lqLet ? sc->letStats () : marv->letStats ()).line (protoName, 120.0) << "\n";
      }
  }
  if (daMode > 0 || daBootFix || daLinkOff)
    {
      const dahello::DahelloProtocol *sc = proto->scheduler ();
      std::cout << "DA mode=" << daMode << " margin=" << daMargin << " imin=" << daImin
                << " doublings=" << daDoublings << " tnb=" << daTnb << " bootfix=" << daBootFix
                << " linkoff=" << daLinkOff
                << " boot_bcast=" << (sc ? sc->bootstrapBroadcasts () : 0)
                << " strong=" << (marv ? marv->daStrong () : 0) << " weak=" << (marv ? marv->daWeak () : 0)
                << " deferred=" << (marv ? marv->daDeferred () : 0)
                << " noup=" << (marv ? marv->daNoUpstream () : 0) << "\n";
    }

  const double T = tSim;
  const double pdr = (nGen == 0) ? std::numeric_limits<double>::quiet_NaN ()
                                 : static_cast<double> (nSucc) / static_cast<double> (nGen);
  // Shared metric: control bytes on [0, T_sim] only / T_sim. Drain
  // exists for in-flight PDR/latency, not to extend O_ctrl.
  const uint64_t ctrlTxWindow = ctrlTxSnapped ? ctrlTxAtTsim
                                             : proto->controlTxCount ();
  const uint64_t eventHelloWindow = ctrlTxSnapped ? eventHellosAtTsim
                                                 : proto->eventHelloCount ();
  const uint64_t bootstrapHelloWindow = ctrlTxSnapped ? bootstrapHellosAtTsim
                                                     : proto->bootstrapHelloCount ();
  const uint64_t energyHelloWindow = ctrlTxSnapped ? energyHellosAtTsim
                                                  : proto->energyHelloCount ();
  const uint64_t risingWindow = ctrlTxSnapped ? risingAtTsim
                                             : proto->broadcastRising ();
  const uint64_t onesWindow = ctrlTxSnapped ? onesAtTsim
                                           : proto->broadcastKOnes ();
  double oCtrl = static_cast<double> (ctrlTxWindow)
                       * (kCtrlPacketBits / 8.0) / T;
  const int beaconBits = proto->energyBeaconBits ();
  const double oEne = static_cast<double> (energyHelloWindow)
                      * (static_cast<double> (beaconBits) / 8.0) / T;
  // p12 Fix B (output only): heard mode charges every HELLO / beacon frame the medium
  // actually started on [0, T_sim] (all causes, incl. sink beacon and first-contact reply),
  // at its actual size, / T_sim. Oracle mode keeps the legacy count above, unchanged.
  const bool octrlHeard = (discMode == "heard" && medOn ());
  const double oCtrlLegacy = oCtrl;
  if (octrlHeard)
    {
      oCtrl = octrlTally.oCtrl (T);
    }
  const double oTotal = oCtrl + oEne;
  const double tE2e = (nSucc == 0) ? std::numeric_limits<double>::quiet_NaN ()
                                   : latencySum / static_cast<double> (nSucc);
  double eUsed = 0.0;
  for (uint32_t i = 0; i < nNodes (); ++i)
    {
      eUsed += (dahello::kDefaultEiniJ - proto->energy (i));
    }
  const double eBit = (nSucc == 0)
                          ? std::numeric_limits<double>::quiet_NaN ()
                          : eUsed / (static_cast<double> (nSucc) * static_cast<double> (energy.packetBits));
  const double meanHops = (nSucc == 0)
                              ? std::numeric_limits<double>::quiet_NaN ()
                              : static_cast<double> (hopSum) / static_cast<double> (nSucc);
  // MARV: a packet whose last live copies were all discarded as
  // duplicates (Alg. 1 line 5) has no other drop class; counted here.
  const uint64_t nFail = nVoid + nSnr + nEnergy + nTtl + nSimEnd + nLostNodeExit + marvNoFate;
  const bool finite = std::isfinite (pdr) && std::isfinite (oCtrl) && std::isfinite (tE2e)
                      && std::isfinite (eBit) && nNonfinite == 0;

  std::ostringstream oss;
  oss << std::scientific << std::setprecision (10);
  const char *scLabel = isScenarioIII () ? "III" : (isScenarioII () ? "II" : "I");
  oss << "=== Scenario " << scLabel << " / " << proto->name () << " ===\n"
      << "N_auv=" << nAuv << " N_sink=1 T_sim=" << T
      << " s pktInterval=" << pktInterval << " s drain=" << drainS
      << " s seed=" << seed << " run=" << rngRun << "\n"
      << "protocol=" << proto->name () << "  volume=6000x6000x500 m  mobility="
      << (isMobile () ? "ConstantVelocityMobilityModel" : "ConstantPositionMobilityModel")
      << "\n";
  if (isScenarioIII ())
    {
      oss << "v_cmd=0  V_eddy=" << eddy.V << " m/s  R_core=" << eddy.R
          << " m  centre=(" << eddy.cx << "," << eddy.cy
          << ")  zeta=" << gmCfg.zeta
          << "  sigma=(" << gmCfg.sigma_x << "," << gmCfg.sigma_y
          << "," << gmCfg.sigma_z << ")  boundary=" << boundary << "\n"
          << "OUT_OF_ENVELOPE_nodes=" << nOutOfEnvelope
          << "  LOST_NODE_EXIT_pkts=" << nLostNodeExit
          << "  gen_skipped_dead_src=" << nGenSkipped << "\n";
      double maxPairDrift = 0.0;
      for (uint32_t i = 1; i < nNodes (); ++i)
        {
          if (!nodeAlive (i))
            {
              continue;
            }
          for (uint32_t j = i + 1; j < nNodes (); ++j)
            {
              if (!nodeAlive (j))
                {
                  continue;
                }
              const Vector pi0 = pos0[i];
              const Vector pj0 = pos0[j];
              const double d0 = dahello::ChannelGate::distance3D (pi0.x, pi0.y, pi0.z,
                                                                pj0.x, pj0.y, pj0.z);
              const dahello::LqarpNode &a = proto->nodes ()[i];
              const dahello::LqarpNode &b = proto->nodes ()[j];
              const double d1 = dahello::ChannelGate::distance3D (a.x, a.y, a.z, b.x, b.y, b.z);
              maxPairDrift = std::max (maxPairDrift, std::fabs (d1 - d0));
            }
        }
      oss << "max_AUV_AUV_pairwise_distance_change_m=" << maxPairDrift
          << "  (alive-alive only; Scenario II this is ~0 under uniform drift)\n";
    }
  else if (isScenarioII ())
    {
      oss << "v_cmd=0  ||v_drift||=" << driftSpeed << " m/s  azimuth_rad="
          << driftAzimuthRad << "  v_drift=(" << driftVel.x << "," << driftVel.y
          << "," << driftVel.z << ") m/s  boundary=" << boundary << "\n"
          << "AUV_XY_exits_predicted=" << nWouldExitXy << "/" << nAuv
          << "  first_exit_s=" << minExitTimeS << "  z_exits=" << nWouldExitZ
          << "  OUT_OF_ENVELOPE_nodes=" << nOutOfEnvelope
          << "  LOST_NODE_EXIT_pkts=" << nLostNodeExit
          << "  gen_skipped_dead_src=" << nGenSkipped << "\n";
      double maxPairDrift = 0.0;
      for (uint32_t i = 1; i < nNodes (); ++i)
        {
          if (!nodeAlive (i))
            {
              continue;
            }
          for (uint32_t j = i + 1; j < nNodes (); ++j)
            {
              if (!nodeAlive (j))
                {
                  continue;
                }
              const Vector pi0 = pos0[i];
              const Vector pj0 = pos0[j];
              const double d0 = dahello::ChannelGate::distance3D (pi0.x, pi0.y, pi0.z,
                                                                pj0.x, pj0.y, pj0.z);
              const dahello::LqarpNode &a = proto->nodes ()[i];
              const dahello::LqarpNode &b = proto->nodes ()[j];
              const double d1 = dahello::ChannelGate::distance3D (a.x, a.y, a.z, b.x, b.y, b.z);
              maxPairDrift = std::max (maxPairDrift, std::fabs (d1 - d0));
            }
        }
      oss << "max_AUV_AUV_pairwise_distance_change_m=" << maxPairDrift
          << "  (alive-alive only; expect ~0 under uniform drift)\n";
    }
  oss << "sink_kinematic_z=" << kSinkDepthM
      << " m  sink_acoustic_z=" << sinkAcousticDepthM << " m\n";
  if (proto->name () == "dahello" || proto->name () == "periodic")
    {
      oss << "beta_1=" << proto->beta1 () << " beta_2=" << proto->beta2 ()
          << " epsilon_0=" << proto->epsilon0Used ()
          << " s_cap=" << dahello::kSCapDb
          << " Broadcast_k_ones_T_sim=" << onesWindow
          << " Broadcast_k_rising_T_sim=" << risingWindow
          << " Broadcast_k_ones_full=" << proto->broadcastKOnes ()
          << " Broadcast_k_rising_full=" << proto->broadcastRising ()
          << " max_|e_k|=" << proto->maxAbsInnovation ()
          << " nan_meas=" << proto->nanMeasCount () << "\n";
    }
  oss << "N_gen=" << nGen << " N_succ=" << nSucc << " N_fail=" << nFail << "\n"
      << "  VOID (empty I)=" << nVoid
      << "  (source=" << nVoidSource << " relay=" << nVoidRelay << ")\n"
      << "  DETOUR hops=" << nDetourHops << " packets_with_detour=" << nDetourPkts << "\n"
      << "  SNR-reception-failure=" << nSnr
      << "  (stale-admit=" << nSnrStale
      << " genuine-live-pass=" << nSnrGenuine << ")\n"
      << "  VOID split stale-empty=" << nVoidStale
      << " via-stale-hop=" << nVoidViaStale
      << " live-empty=" << nVoidLive << "\n"
      << "  stale_hops_attempted=" << nStaleAdmit
      << " stale_hops_received=" << nStaleAdmitOk
      << " envelope_after_stale_hop=" << nEnvelopeViaStale << "\n"
      << "  ENERGY (E_res < E_tx)=" << nEnergy << "\n"
      << "  TTL (>" << kMaxHops << " hops)=" << nTtl << "\n"
      << "  SIM_END still inflight=" << nSimEnd << "\n"
      << "  OUT_OF_ENVELOPE nodes_removed=" << nOutOfEnvelope << "\n"
      << "  LOST_NODE_EXIT packets=" << nLostNodeExit << "\n"
      << "  gen_skipped_dead_src=" << nGenSkipped << "\n"
      << "  nonfinite R or Q or latency events=" << nNonfinite << "\n"
      << "N_tx_attempts=" << nTx << " hops_delivered=" << hopSum
      << " hops_on_voided_paths=" << hopsFailed
      << "  identity nTx==hops_deliv+hops_void_path is "
      << ((nTx == hopSum + hopsFailed) ? "true" : "FALSE") << "\n"
      << "mean_hops_succ=" << meanHops << " latency_max_s=" << latencyMax << "\n"
      << "E_used_total_J=" << eUsed << " E_tx_J=" << energy.eTxJ ()
      << " E_rx_J=" << energy.eRxJ () << "\n"
      << "PDR=" << pdr << "\n"
      << "O_ctrl=" << oCtrl << " byte/s  ";
  if (octrlHeard)
    {
      const auto &q = octrlTally;
      oss << "(heard: medium HELLO+beacon frames started on [0,T_sim]=" << q.totalFrames ()
          << " frames, " << q.totalBytes () << " bytes / T_sim; drain excluded)\n"
          << "OCTRL_SPLIT T_sim=" << T << " link=" << q.frames[0] << " periodic=" << q.frames[1]
          << " join_or_boot=" << q.frames[2] << " hoststate=" << q.frames[3] << " ebeacon=" << q.frames[4]
          << " adaptive=" << q.frames[5] << " sink_beacon=" << q.frames[6] << " fc_reply=" << q.frames[7]
          << " b6=" << q.frames[8] << " other=" << q.frames[9] << " total_frames=" << q.totalFrames ()
          << " total_bytes=" << q.totalBytes () << " O_ctrl_x_T=" << oCtrl * T
          << " legacy_O_ctrl=" << oCtrlLegacy << " (legacy ctrl_tx_T_sim=" << ctrlTxWindow << ")\n";
    }
  else
    {
      oss << "(ctrl_tx_T_sim=" << ctrlTxWindow
          << " pkts * " << kCtrlPacketBits << " bits / T_sim; drain excluded)\n";
    }
  oss
      << "event_hellos=" << eventHelloWindow
      << " bootstrap_hellos=" << bootstrapHelloWindow
      << "  (O_ctrl charges both; bootstrap is one 400-bit HELLO per directed pair)\n"
      << "O_ene=" << oEne << " byte/s  (energy_hellos=" << energyHelloWindow
      << " pkts * " << beaconBits
      << " bits / T_sim; " << energyQuantBits << "-bit residual + "
      << energyAddrBits << "-bit sender id; not included in O_ctrl)\n"
      << "O_total=" << oTotal << " byte/s  (O_ctrl + O_ene)\n"
      << "ctrl_tx_full_run=" << proto->controlTxCount ()
      << "  (includes drain; not used in O_ctrl)\n"
      << "T_e2e_bar=" << tE2e << " s\n"
      << "mean_hop_flight_s="
      << (nHopDelay ? hopDelaySum / static_cast<double> (nHopDelay) : 0.0)
      << "  (airtime + range/c; decision-to-next-decision on a path)\n"
      << "ebit=" << eBit << " J/bit\n"
      << "metrics_all_finite=" << (finite ? "true" : "false") << "\n"
      << "ROW proto=" << proto->name () << " N=" << nAuv << " seed=" << seed
      << " sink_ac_z=" << sinkAcousticDepthM << " PDR=" << pdr
      << " O_ctrl=" << oCtrl << " T_e2e=" << tE2e << " ebit=" << eBit
      << " mean_hops=" << meanHops
      << " VOID=" << nVoid << " VOID_src=" << nVoidSource
      << " VOID_rel=" << nVoidRelay
      << " VOID_stale=" << nVoidStale
      << " VOID_via_stale=" << nVoidViaStale
      << " VOID_live=" << nVoidLive
      << " SNR=" << nSnr
      << " SNR_stale=" << nSnrStale
      << " SNR_genuine=" << nSnrGenuine
      << " stale_admit=" << nStaleAdmit
      << " stale_admit_ok=" << nStaleAdmitOk
      << " envelope_via_stale=" << nEnvelopeViaStale
      << " ENERGY=" << nEnergy << " TTL=" << nTtl << " SIMEND=" << nSimEnd
      << " OUT_OF_ENVELOPE=" << nOutOfEnvelope
      << " LOST_NODE_EXIT=" << nLostNodeExit
      << " DETOUR_hops=" << nDetourHops << " DETOUR_pkts=" << nDetourPkts
      << " N_gen=" << nGen << " N_succ=" << nSucc << " N_tx=" << nTx
      << " hops_deliv=" << hopSum << " hops_void_path=" << hopsFailed;
  if (isScenarioII ())
    {
      oss << " scenario=ii drift=" << driftSpeed << " azimuth=" << driftAzimuthRad
          << " boundary=" << boundary
          << " Broadcast_k_rising=" << risingWindow
          << " Broadcast_k_ones=" << onesWindow
          << " max_ek=" << proto->maxAbsInnovation ();
    }
  else if (isScenarioIII ())
    {
      oss << " scenario=iii V_eddy=" << eddy.V << " R_core=" << eddy.R
          << " boundary=" << boundary
          << " Broadcast_k_rising=" << risingWindow
          << " Broadcast_k_ones=" << onesWindow
          << " max_ek=" << proto->maxAbsInnovation ();
    }
  oss << "\n";
  if (marv)
    {
      oss << "MARV tx=" << marvTx << " tx_after_delivery=" << marvTxAfterDelivery
          << " listed_rx_ok=" << marvCopies << " listed_rx_fail=" << marvRxFail
          << " suppressed=" << marvSuppressed << " dup_discard=" << marvDupDiscard
          << " dup_at_sink=" << marvDupAtSink << " loss_after_delivery=" << marvLossAfterDelivery
          << " DUP_DISCARD_END=" << marvNoFate << " mean_list_len="
          << (marvTx ? static_cast<double> (marvListLenSum) / static_cast<double> (marvTx) : 0.0)
          << " q_updates=" << marv->marv ().qUpdates () << " void_sets=" << marv->marv ().voidSets ()
          << " psatScope=" << marvPsatScope << " sinkOverheard=" << marvSinkOverheard
          << " (nTx identity does not hold for MARV: redundant copies)\n";
      if (marv->hostState ())
        {
          oss << "MARVHS link_event_hellos=" << eventHelloWindow
              << " hs_event_hellos=" << hsHelloAtTsim << " hs_void_flip=" << hsVoidAtTsim
              << " hs_V_change=" << hsVAtTsim << " hs_preempted_by_link=" << hsPreAtTsim
              << " theta_V=" << marv->thetaV ()
              << " (window [0,T_sim], same as O_ctrl) full_run: link=" << marv->linkHellos ()
              << " hs=" << marv->hsHellos () << " void_flip=" << marv->hsVoidFires ()
              << " V_change=" << marv->hsVFires () << " preempted=" << marv->hsPreempted () << "\n";
        }
    }

  {
    uint64_t livePairs = 0;
    uint64_t heardLive = 0;
    for (const SatSnap &s : satSnap)
      {
        if (s.aliveI && s.aliveJ)
          {
            ++livePairs;
            if (s.heard)
              {
                ++heardLive;
              }
          }
      }
    const double firstFire = (livePairs == 0)
                                 ? std::numeric_limits<double>::quiet_NaN ()
                                 : static_cast<double> (heardLive)
                                       / static_cast<double> (livePairs);
    if (decisionTrace)
      {
        oss << decTrace.str ();
      }
    if (geoCensus)
      {
        oss << "HOPGEO proto=" << proto->name () << " seed=" << seed
            << " r_range=" << proto->rRangeM () << " samples=" << geoN
            << " outside_r=" << geoOutsideR
            << " route_1=" << geo1 << " route_2=" << geo2 << " route_3p=" << geo3p
            << " route_unreach=" << geoUnreach
            << " range_1=" << geoRange1 << " range_2=" << geoRange2
            << " range_3p=" << geoRange3p << " range_unreach=" << geoRangeUnreach << "\n";
      }
    oss << "SWEEP proto=" << proto->name () << " seed=" << seed
        << " N=" << nAuv << " drift=" << (isMobile () ? driftSpeed : 0.0)
        << " azimuth=" << (isScenarioII () ? driftAzimuthRad : 0.0)
        << " T_sim=" << T
        << " PDR=" << pdr << " T_e2e=" << tE2e << " ebit=" << eBit
        << " O_ctrl=" << oCtrl << " mean_hops=" << meanHops
        << " VOID=" << nVoid << " LOST_NODE_EXIT=" << nLostNodeExit
        << " VOID_stale=" << nVoidStale
        << " VOID_via_stale=" << nVoidViaStale
        << " VOID_live=" << nVoidLive
        << " SNR=" << nSnr
        << " SNR_stale=" << nSnrStale
        << " SNR_genuine=" << nSnrGenuine
        << " stale_admit=" << nStaleAdmit
        << " stale_admit_ok=" << nStaleAdmitOk
        << " envelope_via_stale=" << nEnvelopeViaStale
        << " rising=" << risingWindow << " ones=" << onesWindow
        << " ctrl_tx=" << ctrlTxWindow
        << " event_hellos=" << eventHelloWindow
        << " bootstrap_hellos=" << bootstrapHelloWindow
        << " energy_hellos=" << energyHelloWindow
        << " beacon_bits=" << beaconBits
        << " energy_quant_bits=" << energyQuantBits
        << " energy_addr_bits=" << energyAddrBits
        << " O_ene=" << oEne
        << " O_total=" << oTotal
        << " r_range=" << proto->rRangeM ()
        << " rx_events=" << proto->receptionCount ()
        << " z_updates=" << proto->measuredUpdates ()
        << " z_silent=" << proto->silentPredicts ()
        << " heard_live=" << heardLive << " live_pairs=" << livePairs
        << " first_fire=" << firstFire
        << " n_exit_tsim=" << (isScenarioIII () ? nOutOfEnvelope : nWouldExitXy)
        << " n_envelope=" << nOutOfEnvelope
        << " n_gen=" << nGen << " n_succ=" << nSucc << " n_tx=" << nTx
        << " finite=" << (finite ? 1 : 0)
        << " scenario=" << (isScenarioIII () ? "iii" : (isScenarioII () ? "ii" : "i"))
        << " beta_1=" << proto->beta1 () << " beta_2=" << proto->beta2 ()
        << " hop_flight=" << (nHopDelay ? hopDelaySum / static_cast<double> (nHopDelay)
                                        : 0.0)
        << "\n";
  }

  if (!census.empty ())
    {
      oss << std::fixed << std::setprecision (1);
      oss << "ALIVE_AUV_CENSUS every=200 s (sink excluded)\n";
      for (const Census &c : census)
        {
          oss << "  t=" << c.t << " alive=" << c.nAliveAuv << "/" << nAuv
              << " exited=" << (nAuv - c.nAliveAuv) << "\n";
        }
    }

  if (vrel.nEval > 0)
    {
      oss << std::scientific << std::setprecision (6);
      const double fracNz = (vrel.nEval == 0) ? 0.0
          : static_cast<double> (vrel.nAbsGt01) / static_cast<double> (vrel.nEval);
      const double fracMean = (vrel.nEval == 0) ? 0.0
          : static_cast<double> (vrel.nAbsGt05) / static_cast<double> (vrel.nEval);
      const double fracAuvNz = (vrel.nAuvAuv == 0) ? 0.0
          : static_cast<double> (vrel.auvAuvGt01) / static_cast<double> (vrel.nAuvAuv);
      oss << "VREL_REWARD_EVALS n=" << vrel.nEval
          << " auv_auv=" << vrel.nAuvAuv
          << " to_sink=" << vrel.nToSink
          << " from_sink=" << vrel.nFromSink << "\n"
          << "  |v_rel| mean=" << (vrel.nEval ? vrel.absSum / vrel.nEval : 0.0)
          << " max=" << vrel.absMax
          << " frac>|0.01|=" << fracNz
          << " frac>|0.05|=" << fracMean << "\n"
          << "  AUV-AUV |v_rel| mean="
          << (vrel.nAuvAuv ? vrel.absSumAuvAuv / vrel.nAuvAuv : 0.0)
          << " max=" << vrel.absMaxAuvAuv
          << " frac>|1e-6|="
          << (vrel.nAuvAuv ? static_cast<double> (vrel.auvAuvGt1e6) / vrel.nAuvAuv : 0.0)
          << " frac>|0.01|=" << fracAuvNz << "\n"
          << "  to-sink |v_rel| mean="
          << (vrel.nToSink ? vrel.absSumToSink / vrel.nToSink : 0.0)
          << " max=" << vrel.absMaxToSink
          << " frac>|0.01|="
          << (vrel.nToSink ? static_cast<double> (vrel.toSinkGt01) / vrel.nToSink : 0.0)
          << "\n";
      const char *lab[7] = {"<1e-6", "[1e-6,0.01)", "[0.01,0.1)", "[0.1,0.5)",
                            "[0.5,1)", "[1,2)", ">=2"};
      oss << "  hist_all";
      for (int b = 0; b < 7; ++b)
        {
          oss << " " << lab[b] << "=" << vrel.hist[b];
        }
      oss << "\n  hist_auv_auv";
      for (int b = 0; b < 7; ++b)
        {
          oss << " " << lab[b] << "=" << vrel.histAuvAuv[b];
        }
      oss << "\n  hist_to_sink";
      for (int b = 0; b < 7; ++b)
        {
          oss << " " << lab[b] << "=" << vrel.histToSink[b];
        }
      oss << "\n";
      oss << "  heard n=" << vrel.nHeard
          << " |v_rel| mean="
          << (vrel.nHeard ? vrel.absSumHeard / vrel.nHeard : 0.0)
          << " max=" << vrel.absMaxHeard << "\n"
          << "  unheard n=" << vrel.nUnheard
          << " |v_rel| mean="
          << (vrel.nUnheard ? vrel.absSumUnheard / vrel.nUnheard : 0.0)
          << "  (c_drift=c_del for these; beta_2 off)\n"
          << "  AUV-AUV heard n=" << vrel.nAuvAuvHeard
          << " |v_rel| mean="
          << (vrel.nAuvAuvHeard ? vrel.absSumAuvHeard / vrel.nAuvAuvHeard : 0.0)
          << " max=" << vrel.absMaxAuvHeard
          << " frac>|1e-6|="
          << (vrel.nAuvAuvHeard
                  ? static_cast<double> (vrel.auvAuvHeardGt1e6) / vrel.nAuvAuvHeard
                  : 0.0)
          << " frac>|0.01|="
          << (vrel.nAuvAuvHeard
                  ? static_cast<double> (vrel.auvAuvHeardGt01) / vrel.nAuvAuvHeard
                  : 0.0)
          << "\n"
          << "  AUV-AUV unheard n=" << vrel.nAuvAuvUnheard
          << " |v_rel| mean="
          << (vrel.nAuvAuvUnheard ? vrel.absSumAuvUnheard / vrel.nAuvAuvUnheard : 0.0)
          << "\n";
      const double auvAllMean = vrel.nAuvAuv ? vrel.absSumAuvAuv / vrel.nAuvAuv : 0.0;
      const double auvHeardMean =
          vrel.nAuvAuvHeard ? vrel.absSumAuvHeard / vrel.nAuvAuvHeard : 0.0;
      const double auvUnheardMean =
          vrel.nAuvAuvUnheard ? vrel.absSumAuvUnheard / vrel.nAuvAuvUnheard : 0.0;
      const double unheardMass =
          vrel.nAuvAuv ? static_cast<double> (vrel.nAuvAuvUnheard) / vrel.nAuvAuv : 0.0;
      oss << "  AUV-AUV init-bias mass (unheard fraction)=" << unheardMass
          << "  unheard_mean=" << auvUnheardMean
          << "  heard_mean=" << auvHeardMean
          << "  all_mean=" << auvAllMean
          << "  bias_removed_if_gated=" << (auvAllMean - auvHeardMean) << "\n";
    }

  if (!zohGaps.empty ())
    {
      std::vector<double> g = zohGaps;
      std::sort (g.begin (), g.end ());
      const double n = static_cast<double> (g.size ());
      double sum = 0.0;
      for (double x : g)
        {
          sum += x;
        }
      const double mean = sum / n;
      const double med = g[g.size () / 2];
      auto pct = [&] (double p) -> double {
        const size_t i = std::min (g.size () - 1,
                                   static_cast<size_t> (std::floor (p * (n - 1.0))));
        return g[i];
      };
      uint64_t gb[8] = {};
      const double edges[8] = {1.0, 5.0, 10.0, 30.0, 60.0, 120.0, 300.0, 1.0e99};
      const char *glab[8] = {"<1s", "[1,5)", "[5,10)", "[10,30)", "[30,60)",
                             "[60,120)", "[120,300)", ">=300s"};
      for (double x : g)
        {
          int b = 0;
          while (b < 7 && x >= edges[b])
            {
              ++b;
            }
          gb[b]++;
        }
      oss << std::scientific << std::setprecision (6);
      oss << "ZOH_STALENESS_AT_REWARD_EVAL n=" << g.size ()
          << " mean_s=" << mean << " median_s=" << med
          << " p90_s=" << pct (0.90) << " p99_s=" << pct (0.99)
          << " max_s=" << g.back () << " min_s=" << g.front () << "\n"
          << "  hist";
      for (int b = 0; b < 8; ++b)
        {
          oss << " " << glab[b] << "=" << gb[b];
        }
      oss << "\n";
    }
  else if (vrel.nEval > 0)
    {
      oss << "ZOH_STALENESS_AT_REWARD_EVAL n=0  (no pair had a HELLO before the eval;"
          << " all c_drift used c_del)\n";
    }

  if (proto && proto->hasVRel ())
    {
      const double now = Simulator::Now ().GetSeconds ();
      uint64_t nPairs = 0;
      uint64_t nHeardEnd = 0;
      uint64_t nHeardAuvAuv = 0;
      uint64_t nHeardToSink = 0;
      uint64_t nBc1 = 0;
      uint64_t nHeardAliveAuv = 0;
      uint64_t nAliveAuvPairs = 0;
      double lastSum = 0.0;
      double lastMin = std::numeric_limits<double>::infinity ();
      double lastMax = -std::numeric_limits<double>::infinity ();
      uint32_t exampleI = 0;
      uint32_t exampleJ = 0;
      for (uint32_t i = 0; i < nNodes (); ++i)
        {
          for (uint32_t j = 0; j < nNodes (); ++j)
            {
              if (i == j)
                {
                  continue;
                }
              ++nPairs;
              const bool heard = proto->zohHeard (i, j);
              int bc = 0;
              proto->ekfBroadcastK (i, j, bc); // OPT-4: p33..p55 were unused here
              if (bc == 1)
                {
                  ++nBc1;
                }
              const bool auvAuv = (i != sinkId && j != sinkId);
              if (auvAuv && nodeAlive (i) && nodeAlive (j))
                {
                  ++nAliveAuvPairs;
                }
              if (!heard)
                {
                  continue;
                }
              ++nHeardEnd;
              if (exampleJ == 0)
                {
                  exampleI = i;
                  exampleJ = j;
                }
              if (j == sinkId)
                {
                  ++nHeardToSink;
                }
              else if (auvAuv)
                {
                  ++nHeardAuvAuv;
                  if (nodeAlive (i) && nodeAlive (j))
                    {
                      ++nHeardAliveAuv;
                    }
                }
              const double t0 = proto->lastZohTime (i, j);
              if (std::isfinite (t0))
                {
                  lastSum += t0;
                  lastMin = std::min (lastMin, t0);
                  lastMax = std::max (lastMax, t0);
                }
            }
        }
      oss << std::scientific << std::setprecision (6);
      oss << "ZOH_CENSUS_AT_END t=" << now
          << " pairs=" << nPairs
          << " heard=" << nHeardEnd
          << " heard_auv_auv=" << nHeardAuvAuv
          << " heard_alive_auv_auv=" << nHeardAliveAuv
          << "/" << nAliveAuvPairs
          << " heard_to_sink=" << nHeardToSink
          << " Broadcast_k=1_now=" << nBc1 << "\n";
      if (nHeardEnd > 0)
        {
          oss << "  lastZohTime min=" << lastMin << " max=" << lastMax
              << " mean=" << (lastSum / static_cast<double> (nHeardEnd))
              << "  example_heard_pair=(" << exampleI << "," << exampleJ << ")\n";
        }
      else
        {
          oss << "  no pair ever copied a neighbor velocity ZOH\n";
        }
    }

  if (!satSnap.empty ())
    {
      auto percentile = [] (std::vector<double> v, double p) -> double {
        if (v.empty ())
          {
            return std::numeric_limits<double>::quiet_NaN ();
          }
        std::sort (v.begin (), v.end ());
        if (v.size () == 1)
          {
            return v.front ();
          }
        const double idx = p * static_cast<double> (v.size () - 1);
        const size_t lo = static_cast<size_t> (std::floor (idx));
        const size_t hi = static_cast<size_t> (std::ceil (idx));
        const double w = idx - static_cast<double> (lo);
        return v[lo] * (1.0 - w) + v[hi] * w;
      };
      auto summarize = [&] (const std::vector<double> &v, const char *label) {
        if (v.empty ())
          {
            oss << "STEP44_SATURATION " << label << " n=0\n";
            return;
          }
        double mn = v[0], mx = v[0], sum = 0.0;
        uint64_t nGe15 = 0, nGe20 = 0, nGe25 = 0, nGe30 = 0, nGe36 = 0, nGe40 = 0;
        for (double x : v)
          {
            mn = std::min (mn, x);
            mx = std::max (mx, x);
            sum += x;
            if (x >= 1.5)
              {
                ++nGe15;
              }
            if (x >= 2.0)
              {
                ++nGe20;
              }
            if (x >= 2.5)
              {
                ++nGe25;
              }
            if (x >= 3.0)
              {
                ++nGe30;
              }
            if (x >= 3.6)
              {
                ++nGe36;
              }
            if (x >= 4.0)
              {
                ++nGe40;
              }
          }
        const double n = static_cast<double> (v.size ());
        oss << "STEP44_SATURATION " << label << " n=" << v.size ()
            << " min=" << mn
            << " p25=" << percentile (v, 0.25)
            << " median=" << percentile (v, 0.50)
            << " p75=" << percentile (v, 0.75)
            << " p80=" << percentile (v, 0.80)
            << " max=" << mx
            << " mean=" << (sum / n) << "\n"
            << "  frac>=1.5=" << (nGe15 / n)
            << " >=2.0=" << (nGe20 / n)
            << " >=2.5=" << (nGe25 / n)
            << " >=3.0=" << (nGe30 / n)
            << " >=3.6=" << (nGe36 / n)
            << " >=4.0=" << (nGe40 / n) << "\n";
      };

      std::vector<double> neverHeardAlive;
      std::vector<double> allAliveUnheardWin;
      std::vector<double> heardAliveUnheardWin;
      std::vector<double> neverHeardAliveEps;
      double pair12 = std::numeric_limits<double>::quiet_NaN ();
      bool pair12heard = false;
      uint64_t nAliveAuv = 0;
      uint64_t nNeverHeardAlive = 0;
      uint64_t nHeardAlive = 0;
      for (const SatSnap &s : satSnap)
        {
          if (!(s.aliveI && s.aliveJ))
            {
              continue;
            }
          ++nAliveAuv;
          allAliveUnheardWin.push_back (s.maxEUnheard);
          if (s.heard)
            {
              ++nHeardAlive;
              heardAliveUnheardWin.push_back (s.maxEUnheard);
            }
          else
            {
              ++nNeverHeardAlive;
              neverHeardAlive.push_back (s.maxEUnheard);
              neverHeardAliveEps.push_back (s.maxEpsUnheard);
            }
          if (s.i == 1 && s.j == 2)
            {
              pair12 = s.maxEUnheard;
              pair12heard = s.heard;
            }
        }
      oss << std::scientific << std::setprecision (6);
      oss << "STEP44_SATURATION_CEILING snapshot_at_T_sim=" << tSim
          << " directed_auv_auv=" << satSnap.size ()
          << " live_auv_auv=" << nAliveAuv
          << " never_heard_live=" << nNeverHeardAlive
          << " heard_live=" << nHeardAlive << "\n"
          << "  pair(1,2) max_|e_trigger|_unheard=" << pair12
          << " heard=" << (pair12heard ? "1" : "0")
          << "  (primary set = live AUV-AUV, never HELLO, same class)\n";
      summarize (neverHeardAlive, "never_heard_live_max_|e_trigger|");
      summarize (neverHeardAliveEps, "never_heard_live_max_epsilon_trigger");
      summarize (allAliveUnheardWin, "all_live_unheard_window_max_|e_trigger|");
      summarize (heardAliveUnheardWin, "heard_live_unheard_window_max_|e_trigger|");

      std::vector<double> heardFcVel;
      std::vector<double> heardHatVel;
      uint64_t nHeardRising1 = 0;
      uint64_t nHeardRisingMore = 0;
      uint64_t nFcVelNear0 = 0;
      uint64_t risingHeardSum = 0;
      const SatSnap *early = nullptr;
      for (const SatSnap &s : satSnap)
        {
          if (!(s.aliveI && s.aliveJ && s.heard))
            {
              continue;
            }
          heardFcVel.push_back (s.fcVel);
          heardHatVel.push_back (s.hatVel);
          risingHeardSum += s.nRising;
          if (s.nRising == 1)
            {
              ++nHeardRising1;
            }
          else if (s.nRising > 1)
            {
              ++nHeardRisingMore;
            }
          if (s.fcVel < 1e-6)
            {
              ++nFcVelNear0;
            }
          if (std::isfinite (s.firstBc)
              && (early == nullptr || s.firstBc < early->firstBc))
            {
              early = &s;
            }
        }
      oss << "STEP48_VEL_INGEST heard_live=" << heardFcVel.size ()
          << " rising_sum=" << risingHeardSum
          << " rising_eq_1=" << nHeardRising1
          << " rising_gt_1=" << nHeardRisingMore
          << " |x_fc_vel|<1e-6=" << nFcVelNear0 << "/" << heardFcVel.size () << "\n";
      summarize (heardFcVel, "heard_live_|x_forecast_vel|");
      summarize (heardHatVel, "heard_live_|x_hat_vel|");
      if (early != nullptr)
        {
          oss << "  earliest_fire pair=(" << early->i << "," << early->j << ")"
              << " t=" << early->firstBc
              << " n_rising=" << early->nRising
              << " |x_fc_vel|=" << early->fcVel
              << " |x_hat_vel|=" << early->hatVel
              << " (AUV-AUV true v_rel=0; expect |x_fc_vel|~0 and n_rising=1)\n";
        }
    }

  if (!pvelTrace.empty ())
    {
      oss << std::scientific << std::setprecision (6);
      oss << "EKF_P_VEL_TRACE pair=(" << traceI << "," << traceJ << ")"
          << "  values are P_{k|k} after the SNR update (H=[0..0 1], every dt=1s);"
          << " Broadcast_k=1 is the ZOH copy, not an EKF velocity measurement\n";
      for (const PVelSample &s : pvelTrace)
        {
          oss << "  t=" << s.t
              << " heard=" << (s.heard ? "1" : "0")
              << " Broadcast_k=" << s.bc
              << " P33=" << s.p33 << " P44=" << s.p44 << " P55=" << s.p55
              << " |Pvel|=" << std::sqrt (s.p33 * s.p33 + s.p44 * s.p44 + s.p55 * s.p55)
              << " v_rel=" << s.vrel
              << " gap_s=" << s.gap << "\n";
        }
    }

  if (!ekfTicks.empty ())
    {
      oss << std::scientific << std::setprecision (6);
      oss << "EKF_INTERNAL_STATE pair=(" << traceI << "," << traceJ << ")"
          << "  x[3:5] is the EKF velocity estimate (NOT odometry-ZOH);"
          << " true AUV-AUV v_rel = (0,0,0) under shared v_drift\n";
      const double keyT[] = {0.0, 50.0, 100.0, 200.0, 400.0, 600.0};
      auto nearest = [&] (double want) -> const EkfTick * {
        const EkfTick *best = nullptr;
        double bestD = 1.0e99;
        for (const EkfTick &s : ekfTicks)
          {
            const double d = std::fabs (s.t - want);
            if (d < bestD)
              {
                bestD = d;
                best = &s;
              }
          }
        return (best && bestD <= 1.0) ? best : nullptr;
      };
      oss << "STEP35_XVEL key times (x3,x4,x5 vs true 0,0,0)\n";
      if (!ekfTicks.empty ())
        {
          const EkfTick &x0 = ekfTicks.front ();
          const double n0 = std::sqrt (x0.x[3] * x0.x[3] + x0.x[4] * x0.x[4] + x0.x[5] * x0.x[5]);
          oss << "  t0 |x_vel|=" << n0
              << " x=(" << x0.x[3] << "," << x0.x[4] << "," << x0.x[5] << ")\n";
        }
      for (double want : keyT)
        {
          const EkfTick *s = nearest (want);
          if (!s)
            {
              oss << "  t=" << want << " MISSING\n";
              continue;
            }
          const double nvel = std::sqrt (s->x[3] * s->x[3] + s->x[4] * s->x[4] + s->x[5] * s->x[5]);
          const double ntrue = std::sqrt (s->vTrueX * s->vTrueX + s->vTrueY * s->vTrueY
                                          + s->vTrueZ * s->vTrueZ);
          const double nposT = std::sqrt (s->pijTrueX * s->pijTrueX + s->pijTrueY * s->pijTrueY
                                          + s->pijTrueZ * s->pijTrueZ);
          const double nposE = std::sqrt (s->x[0] * s->x[0] + s->x[1] * s->x[1] + s->x[2] * s->x[2]);
          oss << "  t=" << s->t
              << " x_vel=(" << s->x[3] << "," << s->x[4] << "," << s->x[5] << ")"
              << " |x_vel|=" << nvel
              << " v_true=(" << s->vTrueX << "," << s->vTrueY << "," << s->vTrueZ << ")"
              << " |v_true|=" << ntrue
              << " |x_vel-v_true|="
              << std::sqrt ((s->x[3] - s->vTrueX) * (s->x[3] - s->vTrueX)
                            + (s->x[4] - s->vTrueY) * (s->x[4] - s->vTrueY)
                            + (s->x[5] - s->vTrueZ) * (s->x[5] - s->vTrueZ))
              << " heard=" << (s->heard ? "1" : "0")
              << " bc=" << s->bc << "\n"
              << "    p_true=(" << s->pijTrueX << "," << s->pijTrueY << "," << s->pijTrueZ
              << ") |p_true|=" << nposT
              << " x_pos=(" << s->x[0] << "," << s->x[1] << "," << s->x[2] << ")"
              << " |x_pos|=" << nposE
              << " |p_err|="
              << std::sqrt ((s->x[0] - s->pijTrueX) * (s->x[0] - s->pijTrueX)
                            + (s->x[1] - s->pijTrueY) * (s->x[1] - s->pijTrueY)
                            + (s->x[2] - s->pijTrueZ) * (s->x[2] - s->pijTrueZ))
              << "\n";
        }

      oss << "STEP38_H_K pair=(" << traceI << "," << traceJ << ")"
          << "  Kalman H = [0 0 0 0 0 0 1]; below is ∂SNR/∂(p,pdot)"
          << " from dSNR/dr * p_horiz/||p_horiz||\n";
      const double hTimes[] = {0.0, 400.0};
      for (double want : hTimes)
        {
          const EkfTick *s = nearest (want);
          if (!s)
            {
              oss << "  t=" << want << " MISSING\n";
              continue;
            }
          const double rh = (s->rangeHEkf > 1e-9) ? s->rangeHEkf : 1.0;
          const double ur = s->x[0] / rh;
          const double ut = s->x[1] / rh;
          const double hpos = std::sqrt (s->hPx * s->hPx + s->hPy * s->hPy + s->hPz * s->hPz);
          const double align = (hpos > 1e-15)
                                   ? (s->hPx * ur + s->hPy * ut + s->hPz * 0.0) / hpos
                                   : 0.0;
          oss << "  t=" << s->t
              << " H_p=(" << s->hPx << "," << s->hPy << "," << s->hPz << ")"
              << " H_v=(" << s->hVx << "," << s->hVy << "," << s->hVz << ")"
              << " dSNR/dr=" << s->dsnrDr
              << " u_radial=(" << ur << "," << ut << ",0)"
              << " |H_p|=" << hpos
              << " align_H_to_radial=" << align
              << " Kalman_H_snr=1\n";
        }

      oss << "STEP36_DSNR_DRANGE pair=(" << traceI << "," << traceJ << ")"
          << " finite-diff SNR(r+10)-SNR(r) / 10 m on January grid\n";
      for (double want : keyT)
        {
          const EkfTick *s = nearest (want);
          if (!s)
            {
              continue;
            }
          const double absD = std::fabs (s->dsnrDr);
          const double err2 = absD * 2.0;
          const double errV = absD * std::sqrt (s->vTrueX * s->vTrueX + s->vTrueY * s->vTrueY
                                                + s->vTrueZ * s->vTrueZ
                                                + s->x[3] * s->x[3] + s->x[4] * s->x[4]
                                                + s->x[5] * s->x[5]);
          // worst-case 1s radial slip uses |x_vel| (false-ZOH prediction), ~2 m/s
          const double nXvel = std::sqrt (s->x[3] * s->x[3] + s->x[4] * s->x[4] + s->x[5] * s->x[5]);
          const double errPred = absD * nXvel; // |dSNR/dr| * |v_pred| * 1s
          oss << "  t=" << s->t
              << " rangeH_true=" << s->rangeHTrue
              << " rangeH_ekf=" << s->rangeHEkf
              << " z_rx=" << proto->nodes ()[traceJ].queryDepthM ()
              << " SNR_meas=" << s->snrMeas
              << " SNR_pred_x6=" << s->x[6]
              << " dSNR/dr=" << s->dsnrDr << " dB/m"
              << " |dSNR|*2m=" << err2 << " dB"
              << " |dSNR|*|x_vel|*1s=" << errPred << " dB"
              << " vs epsilon_0=4"
              << (err2 < 4.0 ? "  2m_UNDER" : "  2m_OVER") << "\n";
          (void) errV;
        }

      double eSum = 0.0;
      double eSum2 = 0.0;
      double eAbsMax = 0.0;
      double eMin = std::numeric_limits<double>::infinity ();
      double eMax = -std::numeric_limits<double>::infinity ();
      uint64_t nE = 0;
      uint64_t nGe4 = 0;
      uint64_t nGeEps = 0;
      uint64_t nBc1 = 0;
      uint64_t run = 0;
      uint64_t runMax = 0;
      std::vector<std::pair<double, double>> absE; // |e|, t
      for (const EkfTick &s : ekfTicks)
        {
          if (s.t > tSim + 1e-9)
            {
              continue; // Step 37 is 0..600; drain is extra
            }
          ++nE;
          eSum += s.e_k;
          eSum2 += s.e_k * s.e_k;
          eMin = std::min (eMin, s.e_k);
          eMax = std::max (eMax, s.e_k);
          eAbsMax = std::max (eAbsMax, std::fabs (s.e_k));
          absE.push_back ({std::fabs (s.e_k), s.t});
          if (std::fabs (s.e_k) >= 4.0)
            {
              ++nGe4;
            }
          if (std::fabs (s.e_k) >= s.epsilon_k)
            {
              ++nGeEps;
              ++run;
              runMax = std::max (runMax, run);
            }
          else
            {
              run = 0;
            }
          if (s.bc == 1)
            {
              ++nBc1;
            }
        }
      std::sort (absE.begin (), absE.end (),
                 [] (const std::pair<double, double> &a, const std::pair<double, double> &b) {
                   return a.first > b.first;
                 });
      const double meanE = nE ? eSum / static_cast<double> (nE) : 0.0;
      const double varE = nE ? (eSum2 / static_cast<double> (nE) - meanE * meanE) : 0.0;
      const double stdE = (varE > 0.0) ? std::sqrt (varE) : 0.0;
      oss << "STEP37_E_K pair=(" << traceI << "," << traceJ << ")"
          << " samples=" << nE << " in [0,T_sim]"
          << " min=" << eMin << " max=" << eMax
          << " mean=" << meanE << " std=" << stdE
          << " max_|e_k|=" << eAbsMax << "\n"
          << "  |e_k|>=4 n=" << nGe4
          << "  |e_k|>=epsilon_k n=" << nGeEps
          << "  max_consecutive_exceed=" << runMax
          << "  Broadcast_k=1_samples=" << nBc1
          << "  persistence W=5 rho=0.6 needs 3/5 exceed\n"
          << "  top5_|e_k|";
      for (size_t k = 0; k < absE.size () && k < 5; ++k)
        {
          oss << " (t=" << absE[k].second << ",|e|=" << absE[k].first << ")";
        }
      oss << "\n";
      oss << "STEP37_E_K_SERIES every=1s (t, e_k, epsilon_k, |e|>=eps, Broadcast_k)\n";
      for (const EkfTick &s : ekfTicks)
        {
          if (s.t > tSim + 1e-9)
            {
              continue;
            }
          oss << "  t=" << s.t
              << " e_k=" << s.e_k
              << " eps=" << s.epsilon_k
              << " exceed=" << (std::fabs (s.e_k) >= s.epsilon_k ? "1" : "0")
              << " bc=" << s.bc
              << " |x_vel|="
              << std::sqrt (s.x[3] * s.x[3] + s.x[4] * s.x[4] + s.x[5] * s.x[5])
              << "\n";
        }

      oss << "STEP42_X_FORECAST pair=(" << traceI << "," << traceJ << ")"
          << "  open-loop x_forecast (never measurement-corrected;"
          << " reset only on Broadcast_k=1)\n";
      for (double want : keyT)
        {
          const EkfTick *s = nearest (want);
          if (!s)
            {
              oss << "  t=" << want << " MISSING\n";
              continue;
            }
          const double nposT = std::sqrt (s->pijTrueX * s->pijTrueX + s->pijTrueY * s->pijTrueY
                                          + s->pijTrueZ * s->pijTrueZ);
          const double nposE = std::sqrt (s->x[0] * s->x[0] + s->x[1] * s->x[1]
                                          + s->x[2] * s->x[2]);
          const double nposF = std::sqrt (s->xForecast[0] * s->xForecast[0]
                                          + s->xForecast[1] * s->xForecast[1]
                                          + s->xForecast[2] * s->xForecast[2]);
          const double perrE = std::sqrt ((s->x[0] - s->pijTrueX) * (s->x[0] - s->pijTrueX)
                                          + (s->x[1] - s->pijTrueY) * (s->x[1] - s->pijTrueY)
                                          + (s->x[2] - s->pijTrueZ) * (s->x[2] - s->pijTrueZ));
          const double perrF = std::sqrt (
              (s->xForecast[0] - s->pijTrueX) * (s->xForecast[0] - s->pijTrueX)
              + (s->xForecast[1] - s->pijTrueY) * (s->xForecast[1] - s->pijTrueY)
              + (s->xForecast[2] - s->pijTrueZ) * (s->xForecast[2] - s->pijTrueZ));
          const double nvelF = std::sqrt (s->xForecast[3] * s->xForecast[3]
                                          + s->xForecast[4] * s->xForecast[4]
                                          + s->xForecast[5] * s->xForecast[5]);
          const double rerrE = std::fabs (s->rangeHEkf - s->rangeHTrue);
          const double rerrF = std::fabs (s->rangeHForecast - s->rangeHTrue);
          oss << "  t=" << s->t
              << " x_fc=(" << s->xForecast[0] << "," << s->xForecast[1] << ","
              << s->xForecast[2] << ")"
              << " |p_fc|=" << nposF
              << " |p_true|=" << nposT
              << " |p_fc-p_true|=" << perrF
              << " |p_hat-p_true|=" << perrE
              << " rangeH_fc=" << s->rangeHForecast
              << " rangeH_hat=" << s->rangeHEkf
              << " rangeH_true=" << s->rangeHTrue
              << " |r_fc-r_true|=" << rerrF
              << " |r_hat-r_true|=" << rerrE
              << " x_fc_vel=(" << s->xForecast[3] << "," << s->xForecast[4] << ","
              << s->xForecast[5] << ")"
              << " |x_fc_vel|=" << nvelF
              << " |x_hat|=" << nposE
              << " heard=" << (s->heard ? "1" : "0")
              << " bc=" << s->bc << "\n";
        }

      double tSum = 0.0;
      double tSum2 = 0.0;
      double tAbsMax = 0.0;
      double tMin = std::numeric_limits<double>::infinity ();
      double tMax = -std::numeric_limits<double>::infinity ();
      uint64_t nT = 0;
      uint64_t nTGe4 = 0;
      uint64_t nTGeEps = 0;
      uint64_t nTBc1 = 0;
      uint64_t tRun = 0;
      uint64_t tRunMax = 0;
      double firstRising = std::numeric_limits<double>::quiet_NaN ();
      int prevBc = 0;
      std::vector<std::pair<double, double>> absT;
      for (const EkfTick &s : ekfTicks)
        {
          if (s.t > tSim + 1e-9)
            {
              continue;
            }
          ++nT;
          tSum += s.eTrigger;
          tSum2 += s.eTrigger * s.eTrigger;
          tMin = std::min (tMin, s.eTrigger);
          tMax = std::max (tMax, s.eTrigger);
          tAbsMax = std::max (tAbsMax, std::fabs (s.eTrigger));
          absT.push_back ({std::fabs (s.eTrigger), s.t});
          if (std::fabs (s.eTrigger) >= 4.0)
            {
              ++nTGe4;
            }
          if (std::fabs (s.eTrigger) >= s.epsilonTrigger)
            {
              ++nTGeEps;
              ++tRun;
              tRunMax = std::max (tRunMax, tRun);
            }
          else
            {
              tRun = 0;
            }
          if (s.bc == 1)
            {
              ++nTBc1;
              if (prevBc != 1 && !std::isfinite (firstRising))
                {
                  firstRising = s.t;
                }
            }
          prevBc = s.bc;
        }
      std::sort (absT.begin (), absT.end (),
                 [] (const std::pair<double, double> &a, const std::pair<double, double> &b) {
                   return a.first > b.first;
                 });
      const double meanT = nT ? tSum / static_cast<double> (nT) : 0.0;
      const double varT = nT ? (tSum2 / static_cast<double> (nT) - meanT * meanT) : 0.0;
      const double stdT = (varT > 0.0) ? std::sqrt (varT) : 0.0;
      oss << "STEP43_E_TRIGGER pair=(" << traceI << "," << traceJ << ")"
          << " samples=" << nT << " in [0,T_sim]"
          << " min=" << tMin << " max=" << tMax
          << " mean=" << meanT << " std=" << stdT
          << " max_|e_trigger|=" << tAbsMax << "\n"
          << "  |e_trigger|>=4 n=" << nTGe4
          << "  |e_trigger|>=epsilon_trigger n=" << nTGeEps
          << "  max_consecutive_exceed=" << tRunMax
          << "  Broadcast_k=1_samples=" << nTBc1
          << "  first_rising_edge_t="
          << (std::isfinite (firstRising) ? firstRising : -1.0)
          << (std::isfinite (firstRising) ? "  FIRED" : "  NO_FIRE")
          << "  persistence W=5 rho=0.6 needs 3/5 exceed\n"
          << "  top5_|e_trigger|";
      for (size_t k = 0; k < absT.size () && k < 5; ++k)
        {
          oss << " (t=" << absT[k].second << ",|e|=" << absT[k].first << ")";
        }
      oss << "\n";
      oss << "STEP43_E_TRIGGER_SERIES every=1s"
          << " (t, e_trigger, eps_trigger, |e|>=eps, e_k, Broadcast_k)\n";
      for (const EkfTick &s : ekfTicks)
        {
          if (s.t > tSim + 1e-9)
            {
              continue;
            }
          oss << "  t=" << s.t
              << " e_tr=" << s.eTrigger
              << " eps_tr=" << s.epsilonTrigger
              << " S_tr=" << s.STrigger
              << " exceed=" << (std::fabs (s.eTrigger) >= s.epsilonTrigger ? "1" : "0")
              << " e_k=" << s.e_k
              << " bc=" << s.bc
              << " |r_fc-r_true|=" << std::fabs (s.rangeHForecast - s.rangeHTrue)
              << " |r_hat-r_true|=" << std::fabs (s.rangeHEkf - s.rangeHTrue)
              << "\n";
        }
    }

  if (!snrTrace.empty ())
    {
      oss << "SNR_TO_SINK_TRACE node=" << snrTraceId << " every=" << snrTraceEvery
          << " s (x,y,z kinematic; rangeH to sink; Bellhop SNR at acousticZ)\n";
      oss << std::fixed << std::setprecision (4);
      for (const SnrSample &sm : snrTrace)
        {
          oss << "  t=" << sm.t << " x=" << sm.x << " y=" << sm.y << " z=" << sm.z
              << " rangeH=" << sm.rangeH << " SNR=" << sm.snrDb
              << " valid=" << (sm.valid ? "true" : "false") << "\n";
        }
    }

  if (hopDiag)
    {
      WriteHopDiag (oss);
    }
  if (medOn ())
    {
      WriteMediumReport (oss);
    }

  const std::string text = oss.str ();
  std::cout << "\n" << text;
  std::ofstream out (outPath);
  if (out)
    {
      out << text;
      std::cout << "wrote " << outPath << "\n";
    }
}

void
DiagSnrToSink (const std::string &dataDir, const std::string &outPath)
{
  auto loader = std::make_shared<dahello::BellhopDataLoader> (
      dataDir + "/TL_grid_extended_january.csv",
      dataDir + "/SNR_grid_extended_january.csv",
      dahello::Season::January);
  dahello::ChannelGate gate (loader);
  const double rRange = gate.rangeAtSnrThreshold ();
  constexpr uint32_t nDepths = sizeof (kDiagSinkDepthsM) / sizeof (kDiagSinkDepthsM[0]);
  constexpr uint32_t nSeeds = sizeof (kStep22Seeds) / sizeof (kStep22Seeds[0]);
  constexpr uint32_t nNs = sizeof (kStep22Ns) / sizeof (kStep22Ns[0]);

  std::ostringstream oss;
  oss << std::fixed << std::setprecision (3);
  oss << "# Step 23: SNR-to-sink vs sink acoustic query depth\n\n"
      << "January Bellhop grid (no new run). Sink XY = (3000, 3000) m. "
         "SNR-feasible = N_SNR: SNR(AUV, sink) > "
      << dahello::ChannelGate::kDefaultSnrThresholdDb
      << " dB, NaN/invalid/nearPressureRelease fail. "
         "Placements are the Step 22 set (seeds 20260921–20260925, "
         "UniformRandomVariable stream 1, AUVs uniform [0,6000]^2 x [10,500] m).\n\n"
      << "r_range at canonical 50 m = " << std::setprecision (1) << rRange
      << " m. N_range uses 3-D Euclidean with sink kinematic z=5 m. "
         "N_depth uses kinematic z (AUVs >= 10 m, sink = 5 m).\n\n"
      << "| N | seed | 5 m | 10 m | 50 m | 100 m | max horiz (m) | N_range | I@5 = SNR@5 |\n"
      << "|---|------|-----|------|------|-------|---------------|---------|-------------|\n";

  double meanByN[nNs][nDepths] = {};
  uint32_t nRangeMissTotal = 0;
  uint32_t nISnrDisagree = 0;

  for (uint32_t ni = 0; ni < nNs; ++ni)
    {
      const uint32_t nAuv = kStep22Ns[ni];
      for (uint32_t si = 0; si < nSeeds; ++si)
        {
          const uint32_t seed = kStep22Seeds[si];
          const std::vector<Vector> pos = SamplePositions (nAuv, seed, 1);
          const Vector sink = pos[0];
          uint32_t nSnr[nDepths] = {};
          uint32_t nI[nDepths] = {};
          uint32_t nRange = 0;
          double maxHoriz = 0.0;
          for (uint32_t i = 1; i <= nAuv; ++i)
            {
              const double horiz = dahello::ChannelGate::horizontalRangeM (
                  pos[i].x, pos[i].y, sink.x, sink.y);
              maxHoriz = std::max (maxHoriz, horiz);
              const double d3 = dahello::ChannelGate::distance3D (
                  pos[i].x, pos[i].y, pos[i].z, sink.x, sink.y, sink.z);
              const bool inRange = d3 < rRange;
              const bool inDepth = (pos[i].z - sink.z) > 0.0;
              if (inRange)
                {
                  ++nRange;
                }
              else
                {
                  ++nRangeMissTotal;
                }
              for (uint32_t di = 0; di < nDepths; ++di)
                {
                  const dahello::ChannelSample samp =
                      loader->query (horiz, kDiagSinkDepthsM[di]);
                  const bool inSnr = !gate.sampleFailsCandidateSnr (samp);
                  if (inSnr)
                    {
                      ++nSnr[di];
                    }
                  if (inRange && inSnr && inDepth)
                    {
                      ++nI[di];
                    }
                  if ((inRange && inSnr && inDepth) != inSnr)
                    {
                      ++nISnrDisagree;
                    }
                }
            }
          oss << "| " << nAuv << " | " << seed;
          for (uint32_t di = 0; di < nDepths; ++di)
            {
              const double frac = static_cast<double> (nSnr[di])
                                  / static_cast<double> (nAuv);
              meanByN[ni][di] += frac;
              oss << " | " << nSnr[di] << "/" << nAuv << " ("
                  << std::setprecision (3) << frac << ")";
            }
          const bool iEqSnr = (nI[0] == nSnr[0]);
          oss << " | " << std::setprecision (1) << maxHoriz << " | " << nRange
              << "/" << nAuv << " | " << (iEqSnr ? "yes" : "NO") << " |\n";
        }
    }

  oss << "\nPer-N mean fraction with sink in SNR-feasible set:\n\n"
      << "| N | 5 m | 10 m | 50 m | 100 m | 50 m minus 5 m |\n"
      << "|---|-----|------|------|-------|----------------|\n";
  for (uint32_t ni = 0; ni < nNs; ++ni)
    {
      oss << "| " << kStep22Ns[ni];
      for (uint32_t di = 0; di < nDepths; ++di)
        {
          meanByN[ni][di] /= static_cast<double> (nSeeds);
          oss << " | " << std::setprecision (3) << meanByN[ni][di];
        }
      oss << " | " << std::setprecision (3)
          << (meanByN[ni][2] - meanByN[ni][0]) << " |\n";
    }

  const double mean5 =
      (meanByN[0][0] + meanByN[1][0] + meanByN[2][0]) / 3.0;
  const double mean50 =
      (meanByN[0][2] + meanByN[1][2] + meanByN[2][2]) / 3.0;
  oss << "\nGrand mean fraction @5 m = " << std::setprecision (3) << mean5
      << "; @50 m = " << mean50 << "; delta = " << (mean50 - mean5) << ".\n";
  if (mean50 - mean5 >= 0.10)
    {
      oss << "Verdict: fraction with sink-in-range **increases substantially** "
             "at 50 m vs 5 m. Sink acoustic depth is a major contributor to "
             "the Step 22 connectivity bottleneck.\n";
    }
  else
    {
      oss << "Verdict: fraction with sink-in-range **does not increase "
             "substantially** at 50 m vs 5 m (delta < 0.10). The remaining "
             "bottleneck is elsewhere (horizontal range/geometry), not the "
             "pressure-release slice at the sink.\n";
    }
  oss << "N_range misses across all 15 placements = " << nRangeMissTotal
      << ". I vs N_SNR disagreements (any depth) = " << nISnrDisagree
      << " (nonzero means N_range or N_depth, not SNR, excluded the sink).\n";

  const std::string text = oss.str ();
  std::cout << "\n" << text;
  std::ofstream out (outPath);
  if (out)
    {
      out << text;
      std::cout << "wrote " << outPath << "\n";
    }
}

void
DiagCandidates (const std::string &dataDir, const std::string &outPath,
                double sinkAcousticDepthM)
{
  auto loader = std::make_shared<dahello::BellhopDataLoader> (
      dataDir + "/TL_grid_extended_january.csv",
      dataDir + "/SNR_grid_extended_january.csv",
      dahello::Season::January);
  dahello::ChannelGate gate (loader);
  constexpr uint32_t nSeeds = sizeof (kStep22Seeds) / sizeof (kStep22Seeds[0]);
  constexpr uint32_t nNs = sizeof (kStep22Ns) / sizeof (kStep22Ns[0]);

  std::ostringstream oss;
  oss << std::fixed << std::setprecision (3);
  oss << "# Step 25: is N_range / N_depth still binding at sink z_ac="
      << sinkAcousticDepthM << " m?\n\n"
      << "Same 15 Step-22 placements. Directed pairs from each AUV. "
         "I = N_range ∩ N_SNR ∩ N_depth. D = N_range ∩ N_SNR (RLOR's I). "
         "r_range from the January 50 m crossing.\n\n"
      << "| N | seed | r_range | AUV-AUV |D|/pairs | |I|/pairs | N_depth drop | "
         "AUV-all |I| mean | |D| mean | I={sink} only | has AUV in I |\n"
      << "|---|------|---------|----------------|-----------|--------------|"
         "--------------|---------|---------------|---------------|\n";

  double dropSum[nNs] = {};
  double iFracSum[nNs] = {};
  double dFracSum[nNs] = {};
  double onlySinkSum[nNs] = {};
  double hasAuvSum[nNs] = {};
  double rangeFailSum[nNs] = {};

  for (uint32_t ni = 0; ni < nNs; ++ni)
    {
      const uint32_t nAuv = kStep22Ns[ni];
      for (uint32_t si = 0; si < nSeeds; ++si)
        {
          const uint32_t seed = kStep22Seeds[si];
          const std::vector<Vector> pos = SamplePositions (nAuv, seed, 1);
          dahello::LqarpConfig cfg;
          cfg.eIni = dahello::kDefaultEiniJ;
          std::vector<dahello::LqarpNode> ln (nAuv + 1);
          for (uint32_t i = 0; i <= nAuv; ++i)
            {
              ln[i].id = i;
              ln[i].x = pos[i].x;
              ln[i].y = pos[i].y;
              ln[i].z = pos[i].z;
              ln[i].energy = cfg.eIni;
              if (i == 0)
                {
                  ln[i].acousticZ = sinkAcousticDepthM;
                }
            }
          dahello::LqarpProtocol proto (gate, cfg, ln);
          const uint32_t nAuvPairs = nAuv * (nAuv - 1); // directed, no self
          uint32_t nRangeAuv = 0;
          uint32_t nDAuv = 0;
          uint32_t nIAuv = 0;
          uint32_t nOnlySink = 0;
          uint32_t nHasAuv = 0;
          uint64_t iCard = 0;
          uint64_t dCard = 0;
          for (uint32_t i = 1; i <= nAuv; ++i)
            {
              uint32_t iSize = 0;
              uint32_t dSize = 0;
              bool sinkInI = false;
              bool auvInI = false;
              for (uint32_t j = 0; j <= nAuv; ++j)
                {
                  if (j == i)
                    {
                      continue;
                    }
                  const bool rng = proto.inNRange (i, j);
                  const bool snr = proto.inNSnr (i, j);
                  const bool dep = proto.inNDepth (i, j);
                  const bool inD = rng && snr;
                  const bool inI = inD && dep;
                  if (j != 0)
                    {
                      if (rng)
                        {
                          ++nRangeAuv;
                        }
                      if (inD)
                        {
                          ++nDAuv;
                        }
                      if (inI)
                        {
                          ++nIAuv;
                        }
                    }
                  if (inD)
                    {
                      ++dSize;
                    }
                  if (inI)
                    {
                      ++iSize;
                      if (j == 0)
                        {
                          sinkInI = true;
                        }
                      else
                        {
                          auvInI = true;
                        }
                    }
                }
              iCard += iSize;
              dCard += dSize;
              if (sinkInI && iSize == 1)
                {
                  ++nOnlySink;
                }
              if (auvInI)
                {
                  ++nHasAuv;
                }
            }
          const double dFrac = static_cast<double> (nDAuv) / static_cast<double> (nAuvPairs);
          const double iFrac = static_cast<double> (nIAuv) / static_cast<double> (nAuvPairs);
          const double drop = (nDAuv == 0) ? 0.0
                                           : 1.0 - static_cast<double> (nIAuv)
                                                      / static_cast<double> (nDAuv);
          const double rangeFail = 1.0 - static_cast<double> (nRangeAuv)
                                             / static_cast<double> (nAuvPairs);
          dropSum[ni] += drop;
          iFracSum[ni] += iFrac;
          dFracSum[ni] += dFrac;
          onlySinkSum[ni] += static_cast<double> (nOnlySink) / nAuv;
          hasAuvSum[ni] += static_cast<double> (nHasAuv) / nAuv;
          rangeFailSum[ni] += rangeFail;
          oss << "| " << nAuv << " | " << seed << " | " << std::setprecision (0)
              << proto.rRangeM () << " | " << std::setprecision (3) << dFrac
              << " | " << iFrac << " | " << drop << " | "
              << (static_cast<double> (iCard) / nAuv) << " | "
              << (static_cast<double> (dCard) / nAuv) << " | "
              << nOnlySink << "/" << nAuv << " | " << nHasAuv << "/" << nAuv
              << " |\n";
        }
    }

  oss << "\nPer-N means (5 seeds):\n\n"
      << "| N | AUV-AUV |D| frac | |I| frac | N_depth drop of D | "
         "N_range fail frac | I={sink} only | has AUV in I |\n"
      << "|---|----------------|---------|-------------------|"
         "-------------------|---------------|---------------|\n";
  for (uint32_t ni = 0; ni < nNs; ++ni)
    {
      const double s = static_cast<double> (nSeeds);
      oss << "| " << kStep22Ns[ni] << " | " << std::setprecision (3)
          << (dFracSum[ni] / s) << " | " << (iFracSum[ni] / s) << " | "
          << (dropSum[ni] / s) << " | " << (rangeFailSum[ni] / s) << " | "
          << (onlySinkSum[ni] / s) << " | " << (hasAuvSum[ni] / s) << " |\n";
    }
  oss << "\nN_depth drop = 1 - |I_AUV-AUV| / |D_AUV-AUV|: fraction of "
         "range∩SNR AUV-AUV pairs that N_depth removes (shallower-only). "
         "A value near 0.5 is the random-depth expectation and means "
         "N_depth is still doing real work. I={sink} only is the fraction "
         "of AUVs whose candidate set is exactly the sink — true "
         "single-hop-only sources. has AUV in I is the fraction with at "
         "least one AUV (not just the sink) in I, i.e. a multi-hop option.\n";

  const std::string text = oss.str ();
  std::cout << "\n" << text;
  std::ofstream out (outPath);
  if (out)
    {
      out << text;
      std::cout << "wrote " << outPath << "\n";
    }
}

void
DiagRlor (const std::string &dataDir, const std::string &outPath,
          uint32_t nAuv, uint32_t seed, uint32_t rngRun,
          double sinkAcousticDepthM, double driftSpeed)
{
  // Same placement + stream-2 azimuth as Scenario II. Snapshot I_RLOR
  // and argmax P_succ at t=0 (first packet) and after the rigid
  // translation at t=300 and t=600.
  const std::vector<Vector> pos0 = SamplePositions (nAuv, seed, rngRun);
  RngSeedManager::SetSeed (seed);
  RngSeedManager::SetRun (rngRun);
  Ptr<UniformRandomVariable> az = CreateObject<UniformRandomVariable> ();
  az->SetStream (2);
  const double twoPi = 2.0 * std::acos (-1.0);
  const double azimuth = az->GetValue (0.0, twoPi);
  const Vector vDrift (driftSpeed * std::cos (azimuth),
                       driftSpeed * std::sin (azimuth), 0.0);

  auto loader = std::make_shared<dahello::BellhopDataLoader> (
      dataDir + "/TL_grid_extended_january.csv",
      dataDir + "/SNR_grid_extended_january.csv",
      dahello::Season::January);
  dahello::ChannelGate gate (loader);

  std::ostringstream oss;
  oss << std::scientific << std::setprecision (6);
  oss << "# Step 56: RLOR one-hop confirmation (not a new experiment)\n\n"
      << "seed=" << seed << " N=" << nAuv << " v=" << driftSpeed
      << " m/s azimuth=" << azimuth << " rad sink_ac_z=" << sinkAcousticDepthM
      << " m. I_RLOR = N_range ∩ N_SNR. NextHop = argmax P_succ, "
         "ties → smallest id (sink is 0).\n\n";

  const double times[] = {0.0, 300.0, 600.0};
  for (double t : times)
    {
      std::vector<dahello::LqarpNode> ln (nAuv + 1);
      for (uint32_t i = 0; i <= nAuv; ++i)
        {
          ln[i].id = i;
          const double dt = (i == 0) ? 0.0 : t;
          ln[i].x = pos0[i].x + vDrift.x * dt;
          ln[i].y = pos0[i].y + vDrift.y * dt;
          ln[i].z = pos0[i].z;
          ln[i].energy = dahello::kDefaultEiniJ;
          if (i == 0)
            {
              ln[i].acousticZ = sinkAcousticDepthM;
            }
        }
      dahello::LqarpConfig cfg;
      cfg.eIni = dahello::kDefaultEiniJ;
      dahello::RlorInspiredProtocol proto (gate, cfg, ln);

      uint32_t nSinkInI = 0;
      uint32_t nEmpty = 0;
      uint32_t nHopSink = 0;
      uint32_t nHopAuv = 0;
      uint32_t nHopNone = 0;
      uint32_t nStrict = 0;
      uint32_t nTieFloor = 0;
      uint32_t nTieSinkSnr = 0;
      uint32_t nHasAuvAlt = 0;
      oss << "## t=" << t << " s  (displacement=" << (driftSpeed * t) << " m)\n"
          << "id |I| nextHop P_succ(sink) SNR(sink) bestAUV P_succ(AUV) SNR(AUV)\n";
      for (uint32_t i = 1; i <= nAuv; ++i)
        {
          const auto I = proto.candidateSet (i);
          if (I.empty ())
            {
              ++nEmpty;
            }
          bool sinkIn = false;
          for (uint32_t j : I)
            {
              if (j == 0)
                {
                  sinkIn = true;
                }
            }
          if (sinkIn)
            {
              ++nSinkInI;
            }
          const int nh = proto.nextHop (i);
          if (nh < 0)
            {
              ++nHopNone;
            }
          else if (nh == 0)
            {
              ++nHopSink;
            }
          else
            {
              ++nHopAuv;
            }
          const double ps = proto.pSucc (i, 0);
          const double snrS = proto.linkSample (i, 0).snr_db;
          int bestA = -1;
          double pA = std::numeric_limits<double>::quiet_NaN ();
          double snrA = std::numeric_limits<double>::quiet_NaN ();
          for (uint32_t j : I)
            {
              if (j == 0)
                {
                  continue;
                }
              const double p = proto.pSucc (i, j);
              if (bestA < 0 || p > pA || (p == pA && static_cast<int> (j) < bestA))
                {
                  bestA = static_cast<int> (j);
                  pA = p;
                  snrA = proto.linkSample (i, j).snr_db;
                }
            }
          if (bestA >= 0)
            {
              ++nHasAuvAlt;
              if (ps > pA)
                {
                  ++nStrict;
                }
              else if (ps == pA)
                {
                  ++nTieFloor;
                  if (std::isfinite (snrS) && std::isfinite (snrA) && snrS >= snrA)
                    {
                      ++nTieSinkSnr;
                    }
                }
            }
          if (i <= 8 || i == nAuv)
            {
              oss << "  " << i << " |" << I.size () << "| nh=" << nh
                  << " P_sink=" << ps << " SNR_sink=" << snrS
                  << " alt=" << bestA << " P_alt=" << pA
                  << " SNR_alt=" << snrA << "\n";
            }
        }
      oss << "  sink_in_I=" << nSinkInI << "/" << nAuv
          << " empty_I=" << nEmpty
          << " nextHop_sink=" << nHopSink
          << " nextHop_AUV=" << nHopAuv
          << " nextHop_none=" << nHopNone
          << " has_AUV_alt=" << nHasAuvAlt << "\n"
          << "  P_succ(sink)>P_succ(best AUV)=" << nStrict
          << "  P_succ tie=" << nTieFloor
          << "  of those, SNR_sink>=SNR_alt=" << nTieSinkSnr << "\n\n";
    }

  const std::string text = oss.str ();
  std::cout << "\n" << text;
  std::ofstream out (outPath);
  if (out)
    {
      out << text;
      std::cout << "wrote " << outPath << "\n";
    }
}

double
PercentileSorted (const std::vector<double> &v, double p)
{
  if (v.empty ())
    {
      return 0.0;
    }
  const double u = std::min (1.0, std::max (0.0, p));
  const double idx = u * static_cast<double> (v.size () - 1);
  const size_t lo = static_cast<size_t> (std::floor (idx));
  const size_t hi = static_cast<size_t> (std::ceil (idx));
  const double w = idx - static_cast<double> (lo);
  return v[lo] * (1.0 - w) + v[hi] * w;
}

struct CompStats
{
  std::vector<double> rad;
  std::vector<double> tan;
  std::vector<double> rel;
  uint64_t nRadGt01 = 0;
  uint64_t nRadGt05 = 0;
  uint64_t nRadGt10 = 0;

  void
  add (double vRad, double vTan, double vRel)
  {
    rad.push_back (vRad);
    tan.push_back (vTan);
    rel.push_back (vRel);
    if (vRad > 0.1)
      {
        ++nRadGt01;
      }
    if (vRad > 0.5)
      {
        ++nRadGt05;
      }
    if (vRad > 1.0)
      {
        ++nRadGt10;
      }
  }

  void
  finalize ()
  {
    std::sort (rad.begin (), rad.end ());
    std::sort (tan.begin (), tan.end ());
    std::sort (rel.begin (), rel.end ());
  }
};

void
WriteCompStats (std::ostream &os, const char *label, CompStats s)
{
  s.finalize ();
  const auto line = [&] (const char *name, const std::vector<double> &v) {
    if (v.empty ())
      {
        os << "  " << name << " n=0\n";
        return;
      }
    double sum = 0.0;
    for (double x : v)
      {
        sum += x;
      }
    os << "  " << name << " n=" << v.size ()
       << " mean=" << (sum / static_cast<double> (v.size ()))
       << " median=" << PercentileSorted (v, 0.5)
       << " p25=" << PercentileSorted (v, 0.25)
       << " p75=" << PercentileSorted (v, 0.75)
       << " min=" << v.front () << " max=" << v.back () << "\n";
  };
  os << label << "\n";
  line ("|v_rel|          ", s.rel);
  line ("|v_rel_radial|   ", s.rad);
  line ("|v_rel_tangential|", s.tan);
  const double n = s.rad.empty () ? 0.0 : static_cast<double> (s.rad.size ());
  os << "  frac |v_rad|>0.1=" << (n ? s.nRadGt01 / n : 0.0)
     << "  >0.5=" << (n ? s.nRadGt05 / n : 0.0)
     << "  >1.0=" << (n ? s.nRadGt10 / n : 0.0) << "\n";
}

void
DiagKinematics (const std::string &outPath, uint32_t nAuv, uint32_t seed,
                uint32_t rngRun, double V, double T)
{
  // Kinematics only. Same placement as Scenario II. No traffic.
  // Two passes: full GM (σ_xy=1, σ_z=0) and mean-flow-only (σ=0).
  // Radial = range-rate along the pair LOS (H_k / SNR can see this).
  // Tangential = remainder (unobservable, Steps 35-38).
  const std::vector<Vector> pos0 = SamplePositions (nAuv, seed, rngRun);
  dahello::EddyConfig eddy;
  eddy.V = V;
  eddy.cx = g_eddyCx; eddy.cy = g_eddyCy; eddy.R = g_eddyR; // p15 A2.2(c)

  uint32_t nCore0 = 0;
  uint32_t nOuter0 = 0;
  for (uint32_t i = 1; i <= nAuv; ++i)
    {
      const double r = std::hypot (pos0[i].x - eddy.cx, pos0[i].y - eddy.cy);
      if (r < eddy.R)
        {
          ++nCore0;
        }
      else
        {
          ++nOuter0;
        }
    }

  auto runPass = [&] (double sigmaXy, bool seriesOut, std::ostringstream &series,
                      CompStats &all, CompStats &core, CompStats &mixed,
                      CompStats &outer) {
    dahello::GaussMarkovConfig gcfg;
    gcfg.zeta = 0.95;
    gcfg.dt = 1.0;
    gcfg.sigma_x = sigmaXy;
    gcfg.sigma_y = sigmaXy;
    gcfg.sigma_z = 0.0;

    std::vector<dahello::GaussMarkovState> st (nAuv + 1);
    std::vector<std::mt19937> rngs (nAuv + 1);
    for (uint32_t i = 0; i <= nAuv; ++i)
      {
        st[i].x = pos0[i].x;
        st[i].y = pos0[i].y;
        st[i].z = pos0[i].z;
        rngs[i].seed (seed * 10007u + i * 17u + 3u);
        if (i == 0)
          {
            continue; // sink fixed
          }
        dahello::eddyMeanFlow (st[i].x, st[i].y, eddy, st[i].vx, st[i].vy, st[i].vz);
      }

    const int nSteps = static_cast<int> (T / gcfg.dt + 0.5);
    for (int k = 0; k <= nSteps; ++k)
      {
        const double t = k * gcfg.dt;
        if (seriesOut && nAuv >= 2)
          {
            double vRad12 = 0, vTan12 = 0, vRel12 = 0;
            dahello::vRelRadialTangential (
                st[1].x, st[1].y, st[1].z, st[1].vx, st[1].vy, st[1].vz,
                st[2].x, st[2].y, st[2].z, st[2].vx, st[2].vy, st[2].vz,
                vRad12, vTan12, vRel12);
            if (k % 10 == 0 || k == nSteps)
              {
                series << "  t=" << t << " |v_rel|=" << vRel12
                       << " |v_rad|=" << vRad12 << " |v_tan|=" << vTan12
                       << " v1=(" << st[1].vx << "," << st[1].vy << ")"
                       << " v2=(" << st[2].vx << "," << st[2].vy << ")\n";
              }
          }
        for (uint32_t i = 1; i <= nAuv; ++i)
          {
            const double ri = std::hypot (st[i].x - eddy.cx, st[i].y - eddy.cy);
            const bool iCore = ri < eddy.R;
            for (uint32_t j = i + 1; j <= nAuv; ++j)
              {
                double vRad = 0, vTan = 0, vRel = 0;
                dahello::vRelRadialTangential (
                    st[i].x, st[i].y, st[i].z, st[i].vx, st[i].vy, st[i].vz,
                    st[j].x, st[j].y, st[j].z, st[j].vx, st[j].vy, st[j].vz,
                    vRad, vTan, vRel);
                all.add (vRad, vTan, vRel);
                const double rj = std::hypot (st[j].x - eddy.cx, st[j].y - eddy.cy);
                const bool jCore = rj < eddy.R;
                if (iCore && jCore)
                  {
                    core.add (vRad, vTan, vRel);
                  }
                else if (!iCore && !jCore)
                  {
                    outer.add (vRad, vTan, vRel);
                  }
                else
                  {
                    mixed.add (vRad, vTan, vRel);
                  }
              }
          }
        if (k == nSteps)
          {
            break;
          }
        for (uint32_t i = 1; i <= nAuv; ++i)
          {
            dahello::eddyMeanFlow (st[i].x, st[i].y, eddy,
                                 gcfg.mu_x, gcfg.mu_y, gcfg.mu_z);
            dahello::gaussMarkovStep (st[i], gcfg, rngs[i]);
          }
      }
  };

  CompStats gmAll, gmCore, gmMixed, gmOuter;
  CompStats mfAll, mfCore, mfMixed, mfOuter;
  std::ostringstream series;
  series << std::scientific << std::setprecision (6);
  series << "PAIR_1_2 full-GM (t, |v_rel|, |v_rad|, |v_tan|, v1, v2)\n";
  runPass (1.0, true, series, gmAll, gmCore, gmMixed, gmOuter);
  std::ostringstream unused;
  runPass (0.0, false, unused, mfAll, mfCore, mfMixed, mfOuter);

  std::ostringstream oss;
  oss << std::scientific << std::setprecision (6);
  oss << "# Step 59: Scenario III radial vs tangential v_rel (kinematics only)\n\n"
      << "seed=" << seed << " N=" << nAuv << " T=" << T
      << " s dt=1 V_eddy=" << V << " R_core=" << eddy.R
      << " zeta=0.95 sigma_z=0 (permanent, SPEC Part 9)\n"
      << "eddy centre=(" << eddy.cx << "," << eddy.cy
      << ") CCW Rankine-capped. Sink fixed. v_cmd=0.\n"
      << "placement t=0: n_core(r<R)=" << nCore0
      << " n_outer(r>=R)=" << nOuter0 << " / " << nAuv << "\n\n"
      << "Radial = |(v_b-v_a)·rhat| (range-rate; H_k / SNR observes this).\n"
      << "Tangential = |v_rel - v_radial rhat| (perpendicular; unobservable).\n"
      << "Solid-body core: mean-flow (v_b-v_a) is identically perpendicular "
         "to r_ij (unit test 22g).\n\n"
      << "=== PASS A: full Gauss-Markov (sigma_xy=1, sigma_z=0) ===\n";
  WriteCompStats (oss, "all AUV-AUV pair-ticks", gmAll);
  WriteCompStats (oss, "both-in-core (r_i,r_j < R)", gmCore);
  WriteCompStats (oss, "mixed (one in core, one outer)", gmMixed);
  WriteCompStats (oss, "both-outer (r_i,r_j >= R)", gmOuter);
  oss << "\n=== PASS B: mean-flow only (sigma=0; Rankine geometry alone) ===\n";
  WriteCompStats (oss, "all AUV-AUV pair-ticks", mfAll);
  WriteCompStats (oss, "both-in-core (r_i,r_j < R)", mfCore);
  WriteCompStats (oss, "mixed (one in core, one outer)", mfMixed);
  WriteCompStats (oss, "both-outer (r_i,r_j >= R)", mfOuter);
  oss << "\n" << series.str ();

  // Step 61: same-sign run length of signed v_rad (full GM only).
  {
    dahello::GaussMarkovConfig gcfg;
    gcfg.zeta = 0.95;
    gcfg.dt = 1.0;
    gcfg.sigma_x = 1.0;
    gcfg.sigma_y = 1.0;
    gcfg.sigma_z = 0.0;
    std::vector<dahello::GaussMarkovState> st (nAuv + 1);
    std::vector<std::mt19937> rngs (nAuv + 1);
    for (uint32_t i = 0; i <= nAuv; ++i)
      {
        st[i].x = pos0[i].x;
        st[i].y = pos0[i].y;
        st[i].z = pos0[i].z;
        rngs[i].seed (seed * 10007u + i * 17u + 3u);
        if (i != 0)
          {
            dahello::eddyMeanFlow (st[i].x, st[i].y, eddy,
                                 st[i].vx, st[i].vy, st[i].vz);
          }
      }
    const int nSteps = static_cast<int> (T / gcfg.dt + 0.5);
    const uint32_t nPairs = (nAuv * (nAuv - 1)) / 2;
    std::vector<std::vector<double> > signedRad (
        nPairs, std::vector<double> (static_cast<size_t> (nSteps + 1), 0.0));
    auto pairIdx = [nAuv] (uint32_t i, uint32_t j) {
      // i,j in 1..nAuv, i<j
      const uint32_t a = i - 1;
      const uint32_t b = j - 1;
      return a * nAuv - (a * (a + 1)) / 2 + (b - a - 1);
    };
    for (int k = 0; k <= nSteps; ++k)
      {
        uint32_t p = 0;
        for (uint32_t i = 1; i <= nAuv; ++i)
          {
            for (uint32_t j = i + 1; j <= nAuv; ++j)
              {
                signedRad[p][static_cast<size_t> (k)] = dahello::signedRangeRate (
                    st[i].x, st[i].y, st[i].z, st[i].vx, st[i].vy, st[i].vz,
                    st[j].x, st[j].y, st[j].z, st[j].vx, st[j].vy, st[j].vz);
                ++p;
              }
          }
        if (k == nSteps)
          {
            break;
          }
        for (uint32_t i = 1; i <= nAuv; ++i)
          {
            dahello::eddyMeanFlow (st[i].x, st[i].y, eddy,
                                 gcfg.mu_x, gcfg.mu_y, gcfg.mu_z);
            dahello::gaussMarkovStep (st[i], gcfg, rngs[i]);
          }
      }

    auto sgn = [] (double v) -> int {
      if (v > 1.0e-12)
        {
          return 1;
        }
      if (v < -1.0e-12)
        {
          return -1;
        }
      return 0;
    };

    std::vector<double> runDur;
    uint64_t nCloseRuns = 0;
    uint64_t nOpenRuns = 0;
    uint64_t nFlips = 0;
    uint64_t nPairsFlip = 0;
    uint64_t nCloseTicks = 0;
    uint64_t nOpenTicks = 0;
    uint64_t nZeroTicks = 0;
    for (uint32_t p = 0; p < nPairs; ++p)
      {
        const auto &seq = signedRad[p];
        int prev = 0;
        int runSign = 0;
        int runLen = 0;
        bool flipped = false;
        for (size_t t = 0; t < seq.size (); ++t)
          {
            const int s = sgn (seq[t]);
            if (s < 0)
              {
                ++nCloseTicks;
              }
            else if (s > 0)
              {
                ++nOpenTicks;
              }
            else
              {
                ++nZeroTicks;
              }
            if (s == 0)
              {
                if (runLen > 0)
                  {
                    runDur.push_back (runLen * gcfg.dt);
                    if (runSign < 0)
                      {
                        ++nCloseRuns;
                      }
                    else
                      {
                        ++nOpenRuns;
                      }
                    runLen = 0;
                    runSign = 0;
                  }
                prev = 0;
                continue;
              }
            if (runLen == 0)
              {
                runSign = s;
                runLen = 1;
              }
            else if (s == runSign)
              {
                ++runLen;
              }
            else
              {
                runDur.push_back (runLen * gcfg.dt);
                if (runSign < 0)
                  {
                    ++nCloseRuns;
                  }
                else
                  {
                    ++nOpenRuns;
                  }
                ++nFlips;
                flipped = true;
                runSign = s;
                runLen = 1;
              }
            prev = s;
          }
        if (runLen > 0)
          {
            runDur.push_back (runLen * gcfg.dt);
            if (runSign < 0)
              {
                ++nCloseRuns;
              }
            else
              {
                ++nOpenRuns;
              }
          }
        if (flipped)
          {
            ++nPairsFlip;
          }
        (void) prev;
      }

    std::sort (runDur.begin (), runDur.end ());
    double runSum = 0.0;
    for (double x : runDur)
      {
        runSum += x;
      }
    const double nTickAll = static_cast<double> (nPairs) * (nSteps + 1);

    // Sign autocorrelation, zeros as 0. Mean over pairs with variance.
    const int maxLag = std::min (60, nSteps);
    std::vector<double> acf (static_cast<size_t> (maxLag + 1), 0.0);
    for (int lag = 0; lag <= maxLag; ++lag)
      {
        double num = 0.0;
        double den = 0.0;
        uint64_t n = 0;
        for (uint32_t p = 0; p < nPairs; ++p)
          {
            const auto &seq = signedRad[p];
            for (int t = 0; t + lag <= nSteps; ++t)
              {
                const double a = static_cast<double> (sgn (seq[static_cast<size_t> (t)]));
                const double b = static_cast<double> (
                    sgn (seq[static_cast<size_t> (t + lag)]));
                num += a * b;
                den += a * a;
                ++n;
              }
          }
        acf[static_cast<size_t> (lag)] = (den > 0.0) ? (num / den) : 0.0;
        (void) n;
      }
    double tauE = static_cast<double> (maxLag);
    const double thresh = 1.0 / std::exp (1.0);
    for (int lag = 1; lag <= maxLag; ++lag)
      {
        if (acf[static_cast<size_t> (lag)] < thresh)
          {
            const double a0 = acf[static_cast<size_t> (lag - 1)];
            const double a1 = acf[static_cast<size_t> (lag)];
            const double w = (a0 - thresh) / std::max (1e-15, a0 - a1);
            tauE = (lag - 1) + w;
            break;
          }
      }

    oss << "\n=== Step 61: signed v_rad persistence (full GM, sigma_xy=1) ===\n"
        << "sign: v_rad<0 closing, v_rad>0 opening (SPEC 2.1). dt="
        << gcfg.dt << " s. deadband=1e-12.\n"
        << "n_pairs=" << nPairs << " n_ticks_per_pair=" << (nSteps + 1)
        << " n_runs=" << runDur.size ()
        << " closing_runs=" << nCloseRuns << " opening_runs=" << nOpenRuns
        << "\n"
        << "same-sign run duration (s):";
    if (runDur.empty ())
      {
        oss << " n=0\n";
      }
    else
      {
        oss << " n=" << runDur.size ()
            << " mean=" << (runSum / static_cast<double> (runDur.size ()))
            << " median=" << PercentileSorted (runDur, 0.5)
            << " p25=" << PercentileSorted (runDur, 0.25)
            << " p75=" << PercentileSorted (runDur, 0.75)
            << " min=" << runDur.front () << " max=" << runDur.back () << "\n";
      }
    oss << "frac ticks closing=" << (nTickAll ? nCloseTicks / nTickAll : 0.0)
        << " opening=" << (nTickAll ? nOpenTicks / nTickAll : 0.0)
        << " zero=" << (nTickAll ? nZeroTicks / nTickAll : 0.0) << "\n"
        << "sign flips (nonzero→opposite nonzero) n=" << nFlips
        << " pairs_that_flip=" << nPairsFlip << "/" << nPairs << "\n"
        << "sign ACF tau_1/e=" << tauE << " s  (first lag ACF < 1/e)\n"
        << "  ACF[0]=" << acf[0];
    for (int lag : {1, 2, 5, 10, 20, 40})
      {
        if (lag <= maxLag)
          {
            oss << " ACF[" << lag << "]=" << acf[static_cast<size_t> (lag)];
          }
      }
    oss << "\n";

    // pair (1,2) sign trace
    if (nAuv >= 2)
      {
        const uint32_t p12 = pairIdx (1, 2);
        oss << "PAIR_1_2 signed v_rad / sign (every 10 s)\n";
        int lastS = 0;
        int hold = 0;
        for (int k = 0; k <= nSteps; ++k)
          {
            const double vr = signedRad[p12][static_cast<size_t> (k)];
            const int s = sgn (vr);
            if (k == 0)
              {
                lastS = s;
                hold = 1;
              }
            else if (s == lastS)
              {
                ++hold;
              }
            else
              {
                lastS = s;
                hold = 1;
              }
            if (k % 10 == 0 || k == nSteps)
              {
                oss << "  t=" << (k * gcfg.dt) << " v_rad=" << vr
                    << " sign=" << s << " run_so_far=" << hold << "\n";
              }
          }
      }
    oss << "Inter-hop decision interval: a routing decision is made in "
           "Forward() at generation and at every hop arrival. The packet "
           "then flies airtime (0.4 s) + range/1500 s. Empirical mean "
           "from Step 60 is T_e2e/hops (see STEP61_62 report).\n";
  }

  const std::string text = oss.str ();
  std::cout << "\n" << text;
  std::ofstream out (outPath);
  if (out)
    {
      out << text;
      std::cout << "wrote " << outPath << "\n";
    }
}

void
DiagEkfTrend (const std::string &dataDir, const std::string &outPath,
              uint32_t nAuv, uint32_t seed, uint32_t rngRun, double V,
              double T)
{
  // Same placement + GM as Step 61. Live EKF (Kalman e_k, not
  // e_k^{trigger}) on every AUV-AUV directed pair. c_pred uses
  // max(0, e_k - e_{k-1}) (SPEC 2.6).
  const std::vector<Vector> pos0 = SamplePositions (nAuv, seed, rngRun);
  dahello::EddyConfig eddy;
  eddy.V = V;
  eddy.cx = g_eddyCx; eddy.cy = g_eddyCy; eddy.R = g_eddyR; // p15 A2.2(c)
  dahello::GaussMarkovConfig gcfg;
  gcfg.zeta = 0.95;
  gcfg.dt = 1.0;
  gcfg.sigma_x = 1.0;
  gcfg.sigma_y = 1.0;
  gcfg.sigma_z = 0.0;

  auto loader = std::make_shared<dahello::BellhopDataLoader> (
      dataDir + "/TL_grid_extended_january.csv",
      dataDir + "/SNR_grid_extended_january.csv", dahello::Season::January);
  dahello::ChannelGate gate (loader);

  dahello::DahelloConfig ec;
  ec.eIni = dahello::kDefaultEiniJ;
  ec.beta_1 = 1.0; // unused for e_k; trigger defaults match Scenario III
  ec.beta_2 = 0.0;
  ec.broadcastMode = dahello::BroadcastMode::EventTriggered;

  std::vector<dahello::LqarpNode> ln (nAuv + 1);
  std::vector<dahello::GaussMarkovState> st (nAuv + 1);
  std::vector<std::mt19937> rngs (nAuv + 1);
  for (uint32_t i = 0; i <= nAuv; ++i)
    {
      st[i].x = pos0[i].x;
      st[i].y = pos0[i].y;
      st[i].z = pos0[i].z;
      rngs[i].seed (seed * 10007u + i * 17u + 3u);
      ln[i].id = i;
      ln[i].x = pos0[i].x;
      ln[i].y = pos0[i].y;
      ln[i].z = pos0[i].z;
      ln[i].energy = ec.eIni;
      if (i == 0)
        {
          ln[i].acousticZ = 50.0;
          continue;
        }
      dahello::eddyMeanFlow (st[i].x, st[i].y, eddy, st[i].vx, st[i].vy, st[i].vz);
      ln[i].vx = st[i].vx;
      ln[i].vy = st[i].vy;
      ln[i].vz = st[i].vz;
    }

  dahello::DahelloProtocol proto (gate, ec, ln);
  const int nSteps = static_cast<int> (T / gcfg.dt + 0.5);
  const uint32_t nDir = nAuv * (nAuv - 1);
  // de[p][k] = e_k - e_{k-1} after tick k (k=1..nSteps). k=0 unused.
  std::vector<std::vector<double> > de (
      nDir, std::vector<double> (static_cast<size_t> (nSteps + 1),
                                 std::numeric_limits<double>::quiet_NaN ()));
  std::vector<std::vector<double> > ek (
      nDir, std::vector<double> (static_cast<size_t> (nSteps + 1),
                                 std::numeric_limits<double>::quiet_NaN ()));

  auto dirIdx = [nAuv] (uint32_t i, uint32_t j) {
    // i,j in 1..nAuv, i!=j
    const uint32_t a = i - 1;
    uint32_t b = j - 1;
    if (b > a)
      {
        --b;
      }
    return a * (nAuv - 1) + b;
  };

  for (int k = 1; k <= nSteps; ++k)
    {
      for (uint32_t i = 1; i <= nAuv; ++i)
        {
          dahello::eddyMeanFlow (st[i].x, st[i].y, eddy,
                               gcfg.mu_x, gcfg.mu_y, gcfg.mu_z);
          dahello::gaussMarkovStep (st[i], gcfg, rngs[i]);
          proto.setKinematics (i, st[i].x, st[i].y, st[i].z,
                               st[i].vx, st[i].vy, st[i].vz);
        }
      proto.tick (gcfg.dt);
      for (uint32_t i = 1; i <= nAuv; ++i)
        {
          for (uint32_t j = 1; j <= nAuv; ++j)
            {
              if (i == j)
                {
                  continue;
                }
              const auto &s = proto.ekfMeta (i, j); // OPT-4: e_k / e_km1 only
              const uint32_t p = dirIdx (i, j);
              ek[p][static_cast<size_t> (k)] = s.e_k;
              de[p][static_cast<size_t> (k)] = s.e_k - s.e_km1;
            }
        }
    }

  auto sgn = [] (double v) -> int {
    if (!std::isfinite (v))
      {
        return 0;
      }
    if (v > 1.0e-12)
      {
        return 1;
      }
    if (v < -1.0e-12)
      {
        return -1;
      }
    return 0;
  };

  // Analyse from k=2 (first real e_k - e_{k-1}; k=1 has e_{k-1}=0 init).
  const int k0 = 2;
  std::vector<double> runDur;
  std::vector<double> absDe;
  std::vector<double> posDe;
  uint64_t nPos = 0, nNeg = 0, nZero = 0, nNan = 0;
  uint64_t nFlips = 0, nPairsFlip = 0;
  uint64_t nPosRuns = 0, nNegRuns = 0;
  for (uint32_t p = 0; p < nDir; ++p)
    {
      int runSign = 0;
      int runLen = 0;
      bool flipped = false;
      for (int k = k0; k <= nSteps; ++k)
        {
          const double v = de[p][static_cast<size_t> (k)];
          const int s = sgn (v);
          if (!std::isfinite (v))
            {
              ++nNan;
            }
          else
            {
              absDe.push_back (std::fabs (v));
              if (v > 0.0)
                {
                  posDe.push_back (v);
                }
            }
          if (s > 0)
            {
              ++nPos;
            }
          else if (s < 0)
            {
              ++nNeg;
            }
          else
            {
              ++nZero;
            }
          if (s == 0)
            {
              if (runLen > 0)
                {
                  runDur.push_back (runLen * gcfg.dt);
                  if (runSign > 0)
                    {
                      ++nPosRuns;
                    }
                  else
                    {
                      ++nNegRuns;
                    }
                  runLen = 0;
                  runSign = 0;
                }
              continue;
            }
          if (runLen == 0)
            {
              runSign = s;
              runLen = 1;
            }
          else if (s == runSign)
            {
              ++runLen;
            }
          else
            {
              runDur.push_back (runLen * gcfg.dt);
              if (runSign > 0)
                {
                  ++nPosRuns;
                }
              else
                {
                  ++nNegRuns;
                }
              ++nFlips;
              flipped = true;
              runSign = s;
              runLen = 1;
            }
        }
      if (runLen > 0)
        {
          runDur.push_back (runLen * gcfg.dt);
          if (runSign > 0)
            {
              ++nPosRuns;
            }
          else
            {
              ++nNegRuns;
            }
        }
      if (flipped)
        {
          ++nPairsFlip;
        }
    }
  std::sort (runDur.begin (), runDur.end ());
  std::sort (absDe.begin (), absDe.end ());
  std::sort (posDe.begin (), posDe.end ());
  double runSum = 0.0;
  for (double x : runDur)
    {
      runSum += x;
    }
  const uint64_t nTickAll = static_cast<uint64_t> (nDir)
                            * static_cast<uint64_t> (nSteps - k0 + 1);

  const int maxLag = std::min (60, nSteps - k0);
  std::vector<double> acf (static_cast<size_t> (maxLag + 1), 0.0);
  for (int lag = 0; lag <= maxLag; ++lag)
    {
      double num = 0.0;
      double den = 0.0;
      for (uint32_t p = 0; p < nDir; ++p)
        {
          for (int t = k0; t + lag <= nSteps; ++t)
            {
              const double a = static_cast<double> (
                  sgn (de[p][static_cast<size_t> (t)]));
              const double b = static_cast<double> (
                  sgn (de[p][static_cast<size_t> (t + lag)]));
              num += a * b;
              den += a * a;
            }
        }
      acf[static_cast<size_t> (lag)] = (den > 0.0) ? (num / den) : 0.0;
    }
  double tauE = static_cast<double> (maxLag);
  const double thresh = 1.0 / std::exp (1.0);
  for (int lag = 1; lag <= maxLag; ++lag)
    {
      if (acf[static_cast<size_t> (lag)] < thresh)
        {
          const double a0 = acf[static_cast<size_t> (lag - 1)];
          const double a1 = acf[static_cast<size_t> (lag)];
          const double w = (a0 - thresh) / std::max (1e-15, a0 - a1);
          tauE = (lag - 1) + w;
          break;
        }
    }

  auto line = [&] (std::ostream &os, const char *name,
                   const std::vector<double> &v) {
    if (v.empty ())
      {
        os << "  " << name << " n=0\n";
        return;
      }
    double sum = 0.0;
    for (double x : v)
      {
        sum += x;
      }
    os << "  " << name << " n=" << v.size ()
       << " mean=" << (sum / static_cast<double> (v.size ()))
       << " median=" << PercentileSorted (v, 0.5)
       << " p25=" << PercentileSorted (v, 0.25)
       << " p75=" << PercentileSorted (v, 0.75)
       << " min=" << v.front () << " max=" << v.back () << "\n";
  };

  std::ostringstream oss;
  oss << std::scientific << std::setprecision (6);
  oss << "# Step 63: Kalman (e_k - e_{k-1}) sign persistence\n\n"
      << "seed=" << seed << " N=" << nAuv << " T=" << T
      << " s dt=1 V_eddy=" << V << " R_core=" << eddy.R
      << " zeta=0.95 sigma_xy=1 sigma_z=0\n"
      << "Same GM / placement as Step 61. Live DahelloProtocol EKF: "
         "e_k = z_tilde - h(x_hat) (SPEC 2.3 / 2.6), NOT e_k^{trigger}.\n"
      << "Directed AUV-AUV pairs (c_pred is per directed slot). "
         "Analyse k=2..T (k=1 has e_{k-1}=0 ctor init).\n"
      << "Event trigger on (epsilon_0=2, s_cap=2) so HELLO / velocity "
         "write matches Scenario III. beta_1 does not enter e_k.\n"
      << "n_directed=" << nDir << " n_ticks_used=" << (nSteps - k0 + 1) << "\n\n";
  oss << "same-sign run duration of (e_k - e_{k-1}) (s):";
  if (runDur.empty ())
    {
      oss << " n=0\n";
    }
  else
    {
      oss << " n=" << runDur.size ()
          << " mean=" << (runSum / static_cast<double> (runDur.size ()))
          << " median=" << PercentileSorted (runDur, 0.5)
          << " p25=" << PercentileSorted (runDur, 0.25)
          << " p75=" << PercentileSorted (runDur, 0.75)
          << " min=" << runDur.front () << " max=" << runDur.back () << "\n";
    }
  oss << "frac ticks de>0 (beta_1 binds)="
      << (nTickAll ? static_cast<double> (nPos) / nTickAll : 0.0)
      << " de<0=" << (nTickAll ? static_cast<double> (nNeg) / nTickAll : 0.0)
      << " zero/nan=" << (nTickAll ? static_cast<double> (nZero) / nTickAll : 0.0)
      << "  (nan=" << nNan << ")\n"
      << "sign flips n=" << nFlips << " pairs_that_flip=" << nPairsFlip
      << "/" << nDir << "  pos_runs=" << nPosRuns << " neg_runs=" << nNegRuns
      << "\n"
      << "sign ACF tau_1/e=" << tauE << " s\n"
      << "  ACF[0]=" << acf[0];
  for (int lag : {1, 2, 5, 10, 20, 40})
    {
      if (lag <= maxLag)
        {
          oss << " ACF[" << lag << "]=" << acf[static_cast<size_t> (lag)];
        }
    }
  oss << "\n";
  line (oss, "|e_k - e_{k-1}|", absDe);
  line (oss, "max(0, de) when de>0", posDe);
  oss << "\nPAIR_1_2 Kalman (t, e_k, de=e_k-e_{k-1}, sign(de))\n";
  if (nAuv >= 2)
    {
      const uint32_t p12 = dirIdx (1, 2);
      for (int k = 0; k <= nSteps; ++k)
        {
          if (k % 10 == 0 || k == nSteps)
            {
              const double e = ek[p12][static_cast<size_t> (k)];
              const double d = de[p12][static_cast<size_t> (k)];
              oss << "  t=" << (k * gcfg.dt) << " e_k=" << e
                  << " de=" << d << " sign=" << sgn (d) << "\n";
            }
        }
    }
  oss << "Compare to Step 61 radial-sign: median run 3.0 s, p25=1.0 s, "
         "tau_1/e=10.4 s, hop flight 2.03 s.\n";

  const std::string text = oss.str ();
  std::cout << "\n" << text;
  std::ofstream out (outPath);
  if (out)
    {
      out << text;
      std::cout << "wrote " << outPath << "\n";
    }
}

} // namespace

#include "scenario-porta.inc"

int
main (int argc, char *argv[])
{
  Pipeline pipe;
  CommandLine cmd (__FILE__);
  cmd.AddValue ("dataDir", "Bellhop CSV directory", pipe.dataDir);
  cmd.AddValue ("season", "january or july", pipe.seasonName);
  cmd.AddValue ("nAuv", "Non-sink nodes", pipe.nAuv);
  cmd.AddValue ("simTime", "Generation window T_sim (s)", pipe.tSim);
  cmd.AddValue ("pktInterval", "Per-AUV data interval (s)", pipe.pktInterval);
  cmd.AddValue ("drain", "Extra seconds after T_sim for in-flight packets", pipe.drainS);
  cmd.AddValue ("seed", "ns-3 RngSeedManager seed", pipe.seed);
  cmd.AddValue ("run", "ns-3 RngSeedManager run", pipe.rngRun);
  cmd.AddValue ("sinkAcousticDepth",
                "Bellhop query depth for the sink only (m). Kinematic z stays 5.",
                pipe.sinkAcousticDepthM);
  cmd.AddValue ("protocol", "lqarp | lqarp-refreshed | letadapt | naivevoid | marv-let | marv-naive | dahello | dahello-energy | rlor | periodic | qelar | qelar-hello | qelar-hello-energy | saqr | saqr-hello | saqr-hello-energy | marv-ctrl-off | marv-T<s> | marv-dahello | marv-dahello-energy",
                pipe.protoName);
  cmd.AddValue ("energyBits",
                "dahello-energy residual quantizer width (bits). Beacon adds an 8-bit sender id.",
                pipe.energyQuantBits);
  cmd.AddValue ("scenario", "i (stationary), ii (linear drift), iii (Rankine eddy + GM)",
                pipe.scenario);
  cmd.AddValue ("driftSpeed", "Scenario II ||v_drift|| (m/s); default 2.0", pipe.driftSpeed);
  cmd.AddValue ("boundary", "Scenario II XY policy: remove (default) or open", pipe.boundary);
  cmd.AddValue ("beta_1", "DA-HELLO beta_1 (NaN = 0; validated default in I/II/III)", pipe.beta1);
  cmd.AddValue ("beta_2", "DA-HELLO beta_2 (NaN = 0; validated default in I/II/III)", pipe.beta2);
  cmd.AddValue ("epsilon_0", "DA-HELLO / periodic epsilon_0 (NaN = protocol default)",
                pipe.epsilon0);
  cmd.AddValue ("snrTraceId", "AUV id for SNR-to-sink samples (sink is 0)", pipe.snrTraceId);
  cmd.AddValue ("quiet", "Omit per-AUV placement dump", pipe.quiet);
  cmd.AddValue ("hopDiag",
                "Step 1d: record sink admission versus reward. Does not change the next hop.",
                pipe.hopDiag);
  cmd.AddValue ("diagSnrToSink",
                "Step 23: SNR-to-sink fractions at 5/10/50/100 m for the 15 Step-22 placements, then exit",
                pipe.diagSnrToSink);
  cmd.AddValue ("diagCandidates",
                "Step 25: N_range/N_SNR/N_depth binding among AUV pairs at sinkAcousticDepth, then exit",
                pipe.diagCandidates);
  cmd.AddValue ("diagRlor",
                "Step 56: RLOR P_succ(sink) vs best AUV on the Scenario II placement, then exit",
                pipe.diagRlor);
  cmd.AddValue ("diagKinematics",
                "Steps 57/59/61/63: eddy kinematics, radial v_rel, Kalman e_k trend, then exit",
                pipe.diagKinematics);
  cmd.AddValue ("rRange", "r_range (m) for all protocols; NaN = R* (SNR-gate crossing)", pipe.rRangeOverride);
  cmd.AddValue ("helloRxGate", "Gate HELLO/beacon hold writes on reception (Step 5)", pipe.helloRxGate);
  cmd.AddValue ("daMode", "DECISION-AWARE: 0 off, 1 DA, 2 DA+back-off (MARV hs arms)", pipe.daMode);
  cmd.AddValue ("daMargin", "DECISION-AWARE: rank-crossing margin", pipe.daMargin);
  cmd.AddValue ("daImin", "DECISION-AWARE: back-off Imin (s)", pipe.daImin);
  cmd.AddValue ("daDoublings", "DECISION-AWARE: back-off doublings", pipe.daDoublings);
  cmd.AddValue ("daTnb", "DECISION-AWARE: record recency window (s)", pipe.daTnb);
  cmd.AddValue ("bootFix", "DECISION-AWARE: one bootstrap broadcast per sender per first contact", pipe.daBootFix);
  cmd.AddValue ("linkOff", "DECISION-AWARE: link (Kalman) trigger off", pipe.daLinkOff);
  cmd.AddValue ("decisionTrace", "Per-decision candidate Q/reward dump (diagnostic)", pipe.decisionTrace);
  cmd.AddValue ("geoCensus", "Hop-distance census every geoEveryS (separate run)", pipe.geoCensus);
  cmd.AddValue ("geoEveryS", "Census period (s)", pipe.geoEveryS);
  cmd.AddValue ("tCtrl", "Fixed-Periodic control period T_ctrl (s); NaN = default 5 s", pipe.tCtrl);
  cmd.AddValue ("marvPsatScope", "MARV: n_ij counts every listed node (list) or the top of the list (top). Required for marv-*", pipe.marvPsatScope);
  cmd.AddValue ("marvSinkOverheard", "MARV: sink acceptance overheard as a forward (ack) or not (literal). Required for marv-*", pipe.marvSinkOverheard);
  cmd.AddValue ("out", "Report path", pipe.outPath);
  cmd.AddValue ("hetDriftMax", "NOVELTY II: per-AUV drift speed U(0,max), heading U(0,2pi)", pipe.hetDriftMax);
  cmd.AddValue ("nWpAuv", "NOVELTY II: AUVs 1..n follow random 3-D waypoints", pipe.nWpAuv);
  cmd.AddValue ("wpSpeed", "NOVELTY II: waypoint AUV ground speed (m/s)", pipe.wpSpeed);
  cmd.AddValue ("gmSigma", "NOVELTY III: Gauss-Markov sigma_xy override (m/s)", pipe.gmSigma);
  cmd.AddValue ("gmZeta", "NOVELTY III: Gauss-Markov zeta override", pipe.gmZeta);
  cmd.AddValue ("novDiag", "NOVELTY: live SNR-rate / v_rel census (observation only)", pipe.novDiag);
  // ---- Port A flags (defaults = legacy engine, bit-identical) ----
  cmd.AddValue ("mac", "PORT A: legacy (default, no medium) | ideal | aloha | cs | slotted", pipe.macMode);
  cmd.AddValue ("chan", "PORT A (medium only): bellhop (legacy table) | formula", pipe.chanMode);
  cmd.AddValue ("power", "PORT A (medium only): power profile", pipe.powerName);
  cmd.AddValue ("phyB", "PORT A: bandwidth (kHz)", pipe.phyBkHz);
  cmd.AddValue ("phyRb", "PORT A: bit rate (bit/s)", pipe.phyRbBps);
  cmd.AddValue ("phyTpre", "PORT A: preamble (s)", pipe.phyTpreS);
  cmd.AddValue ("phyRop", "PORT A: R_op for SL calibration and MAC timing (m)", pipe.phyRopM);
  cmd.AddValue ("phyShipping", "PORT A: Wenz shipping", pipe.phyShipping);
  cmd.AddValue ("phyWind", "PORT A: Wenz wind (m/s)", pipe.phyWindMps);
  cmd.AddValue ("phySL", "PORT A: source level (dB); NaN = calibrated", pipe.phySl);
  cmd.AddValue ("phyPer", "PORT A: bfsk | thr", pipe.phyPer);
  cmd.AddValue ("dataBits", "PORT A: data frame bits", pipe.dataBits);
  cmd.AddValue ("helloBits", "PORT A: HELLO / beacon bits", pipe.helloBits);
  cmd.AddValue ("ackBits", "PORT A: ACK bits", pipe.ackBits);
  cmd.AddValue ("arqMax", "PORT A: unicast attempts per hop", pipe.arqMax);
  cmd.AddValue ("queueCap", "PORT A: MAC FIFO capacity", pipe.queueCap);
  cmd.AddValue ("boMax", "PORT A: CS busy attempts before MAC drop", pipe.boMax);
  cmd.AddValue ("eBatt", "PORT A: battery (J)", pipe.eBattJ);
  cmd.AddValue ("warm", "PORT A: medium report measurement window start (s)", pipe.warmS);
  cmd.AddValue ("phyThr", "PORT A: thr-mode SINR threshold (dB); NaN = 8.68", pipe.phyThr);
  cmd.AddValue ("phase", "PORT A: traffic phase sync (legacy) | random", pipe.trafficPhase);
  cmd.AddValue ("topo", "PORT A: t0 (legacy slab) | t1 (4x4x3.5 km deep column)", pipe.topo);
  cmd.AddValue ("letFloor", "p2: opt-in letadapt|marv-let interval floor in s, [0,120]; default off (no floor, as QLAR)", pipe.letFloor);
  cmd.AddValue ("lifetime", "p2: 1 = print output-only LIFETIME_PROJ line (projected first/half node death); default 0 = off", pipe.lifetimeOut);
  cmd.AddValue ("letFrac", "t23/t25: letadapt|marv-let QLAR tau in (0,1) (default 0.3, QLAR Table 2)", pipe.letFrac);
  cmd.AddValue ("naiveRefresh", "t23/t25: naivevoid|marv-naive periodic refresh in [60,120] s (default 90)", pipe.naiveRefresh);
  cmd.AddValue ("hostSched", "PORT A: QELAR/SAQR baseline HELLO schedule off|periodic|trickle|speedadapt", pipe.hostSched);
  cmd.AddValue ("disc", "PORT A Gate 2: oracle (legacy default) | heard", pipe.discMode);
  cmd.AddValue ("tickPoison", "bin_tt: poison node copies during proto->tick + held-state guard (heard)", tt::tickPoison);
  cmd.AddValue ("leakTest", "PORT A Gate 2: 0 off; 1..4 planted oracle leak (exit 3); 5 tick scope (tickPoison); 6 --lqarpPosDa=2 scope (p7); 7 marv-dahello-hs legacy HS-HELLO delivery (p8); 8 marv daFire true peer z (p8, with --tickPoison=1); 9 B6 reads a peer pose (p9); 10 B8/B9 first-contact test via the live range gate (p9); 11 B4 reads peer true velocities (p13)", pipe.leakTest);
  cmd.AddValue ("lqarpPosDa", "PORT A Gate 2: LQARP position-aware DA HELLO: 0 off; 1 displacement proxy (scaffold); 2 membership/rank test (p6, protocol=lqarp)", pipe.lqarpPosDa);
  cmd.AddValue ("lqarpNoPosePiggyback", "p14: 1 = pre-fix LQARP data header (no pose/energy piggyback); 0 = Liu III-D position+energy+V+neighbour count (default)", pipe.lqarpNoPosePiggyback);
  cmd.AddValue ("b6Dist", "p9 B6: distance/kinematic-triggered HELLO (EE-Hello/CAM style) on marv-ctrl-off, heard; 0 off [0]", pipe.b6Dist);
  cmd.AddValue ("b6Dth", "p9 B6: d_th (m), 3-D displacement since own last HELLO [250]", pipe.b6Dth);
  cmd.AddValue ("b6Zth", "p9 B6: z_th (m), depth change since own last HELLO [50]", pipe.b6Zth);
  cmd.AddValue ("b6Tmax", "p9 B6: T_max (s), HELLO at the latest after T_max [300]", pipe.b6Tmax);
  cmd.AddValue ("b6CheckS", "p9 B6: trigger evaluation period (s) [1]", pipe.b6CheckS);
  cmd.AddValue ("b8NaiveB", "p9 B8: Naive-B = marv-naive + reactive first-contact reply (plan 2.4), heard; 0 off [0]", pipe.b8NaiveB);
  cmd.AddValue ("b8Tr", "p9 B8: refresh T_r (s) in [60,300], 0 = infinity [120]", pipe.b8Tr);
  cmd.AddValue ("b2Trickle", "p13 B2: Trickle (RFC 6206) HELLO on marv-ctrl-off, heard; 0 off [0]", pipe.b2Trickle);
  cmd.AddValue ("b2Imin", "p13 B2: I_min (s) [5]", pipe.b2Imin);
  cmd.AddValue ("b2Doublings", "p13 B2: doublings (I_max = I_min*2^d) [4]", pipe.b2Doublings);
  cmd.AddValue ("b2K", "p13 B2: redundancy constant k; 0 = infinity [5]", pipe.b2K);
  cmd.AddValue ("b2Incons", "p13 B2: inconsistency = table (own held table change) | host (own void flag change) [table]", pipe.b2Incons);
  cmd.AddValue ("b3Tap", "p13 B3: TAP turnover-adaptive HELLO on marv-ctrl-off, heard; 0 off [0]", pipe.b3Tap);
  cmd.AddValue ("b3Ropt", "p13 B3: target turnover r_opt [0.04 = paper]", pipe.b3Ropt);
  cmd.AddValue ("b4Speed", "p13 B4: SpeedOpt f = 2S/(aR) HELLO on marv-ctrl-off, heard; 0 off [0]", pipe.b4Speed);
  cmd.AddValue ("b4A", "p13 B4: a [0.1]", pipe.b4A);
  cmd.AddValue ("daRefresh", "p13 P: refresh T_r (s) for marv-dahello-hs heard; 0 = infinity [0]", pipe.daRefresh);
  cmd.AddValue ("b9FcOnly", "p9 B9: FC-only = marv-ctrl-off + join + reactive first-contact reply, no other HELLO; 0 off [0]", pipe.b9FcOnly);
  cmd.AddValue ("lqarpPosDaMargin", "p6: rank margin m for --lqarpPosDa=2 (score units, default 0)", pipe.lqarpPosDaMargin);
  cmd.AddValue ("tNb", "PORT A Gate 2: neighbour freshness T_nb (s), heard mode; if given, also applies to scheduler hosts (default there: no expiry)", pipe.tNbS);
  cmd.AddValue ("sinkBeacon", "PORT A item 4: sink pose beacon period (s), medium on, all arms; 0 = off", pipe.sinkBeaconS);
  cmd.AddValue ("periodicJitter", "p15 A2.2(a) v2: 1 = per-node phase U[0,T) for marv-T<s>, QELAR/SAQR energyRefresh, and --hostSched=periodic; 0 = p14", pipe.periodicJitter);
  cmd.AddValue ("helloOnlyHost", "p15 A2.2(b): 1 = neighbour state written ONLY from HELLO/beacon decodes (forces --pollStored=1); 0 = p14", pipe.helloOnlyHost);
  cmd.AddValue ("eddyCx", "p15 A2.2(d): eddy centre x (m), default 3000 = p14", g_eddyCx);
  cmd.AddValue ("eddyCy", "p15 A2.2(d): eddy centre y (m), default 3000 = p14", g_eddyCy);
  cmd.AddValue ("eddyR", "p15 A2.2(d): eddy core radius (m), default 2000 = p14", g_eddyR);
  cmd.AddValue ("sinkBeaconPhase", "p15 A2.2(e): sink beacons at phase + sinkBeacon*k, k>=1 (s); default 0 = p14", pipe.sinkBeaconPhase);
  cmd.AddValue ("pollStored", "p15 A2.2(c) v2: 1 = pollPair reads the stored neighbour record, not live V/void; 0 = p14 (campaign runners force 1 on scheduler arms)", pipe.pollStored);
  cmd.AddValue ("recWriteOut", "p15: output-only neighbour-record write counter file (empty = not written)", pipe.recWriteOut);
  cmd.AddValue ("poison", "PORT A Gate 2 (heard): nan | big (seeded +-1e9; differential test) | off", pipe.poisonMode);
  cmd.AddValue ("poisonSeed", "PORT A Gate 2: seed of the +-1e9 poison pattern", pipe.poisonSeed);
  // p3 (amend1 A6(d)): evaluation-seed guard. Defaults leave non-eval runs unchanged.
  bool evalMode = false;
  bool evalGuardOnly = false;
  std::string evalAddendum;
  std::string evalAddendumRegistry = portaguard::kDefaultRegistry;
  cmd.AddValue ("eval", "PORT A A6(d): 1 = evaluation run; required for seeds >= 20261201 (refused otherwise)", evalMode);
  cmd.AddValue ("evalAddendum", "PORT A A6(d): path of the hashed tuning addendum (required with --eval=1)", evalAddendum);
  cmd.AddValue ("evalAddendumRegistry", "PORT A A6(d): file whose first token is the recorded addendum sha256", evalAddendumRegistry);
  cmd.AddValue ("evalGuardOnly", "PORT A A6(d): evaluate the seed guard, print EVAL_GUARD, exit (0 accept / 4 refuse); never simulates", evalGuardOnly);
  for (int a = 1; a < argc; ++a)
    {
      if (std::string (argv[a]).rfind ("--tNb=", 0) == 0)
        {
          pipe.tNbExplicit = true;
        }
    }
  cmd.Parse (argc, argv);
  NS_ABORT_MSG_IF (pipe.helloOnlyHost && pipe.discMode != "heard", "--helloOnlyHost=1 needs --disc=heard");
  if (pipe.helloOnlyHost)
    {
      pipe.pollStored = true; // A2.2(e): --helloOnlyHost=1 forces --pollStored=1
    }
  NS_ABORT_MSG_IF (!(g_eddyR > 0.0) || !std::isfinite (g_eddyCx) || !std::isfinite (g_eddyCy),
                   "--eddyR must be > 0 and --eddyCx/--eddyCy finite");
  NS_ABORT_MSG_IF (!(pipe.sinkBeaconPhase >= 0.0) || (pipe.sinkBeaconS > 0.0 && !(pipe.sinkBeaconPhase < pipe.sinkBeaconS)),
                   "--sinkBeaconPhase must be in [0, sinkBeacon)");

  if (pipe.protoName == "lqarp" || pipe.protoName == "lqarp-refreshed")
    {
      std::cout << "PORTA lqarpNoPosePiggyback=" << pipe.lqarpNoPosePiggyback
                << " (0=Liu III-D pose+E+V+num in every LQARP data header; 1=pre-fix ablation)\n";
    }
  {
    // Before ANY placement, protocol build, diagnostic or simulation.
    const portaguard::EvalGuardResult g =
        portaguard::CheckEvalGuard (pipe.seed, evalMode, evalAddendum, evalAddendumRegistry);
    if (!g.accept)
      {
        std::cerr << "EVAL_GUARD REFUSED seed=" << pipe.seed << " eval=" << (evalMode ? 1 : 0)
                  << " reason=" << g.reason << " (no simulation run)\n";
        return portaguard::kEvalGuardExit;
      }
    if (evalGuardOnly || g.evalSeed)
      {
        std::cout << "EVAL_GUARD ACCEPT seed=" << pipe.seed << " eval=" << (evalMode ? 1 : 0)
                  << " addendum=" << (evalAddendum.empty () ? "-" : evalAddendum)
                  << " addendum_sha256=" << (g.addendumSha.empty () ? "-" : g.addendumSha)
                  << " reason=" << (g.reason.empty () ? "non-evaluation seed" : g.reason) << "\n";
      }
    if (evalGuardOnly)
      {
        return 0; // guard-only mode: never places, builds or simulates
      }
  }
  if (pipe.topo == "t1")
    {
      kVolumeXyM = 4000.0;
      kVolumeZM = 3500.0;
    }
  else if (pipe.topo != "t0")
    {
      NS_FATAL_ERROR ("--topo must be t0|t1");
    }
  if (pipe.trafficPhase != "sync" && pipe.trafficPhase != "random")
    {
      NS_FATAL_ERROR ("--phase must be sync|random");
    }

  if (pipe.diagSnrToSink)
    {
      if (pipe.outPath == "${WORKSPACE}/results/scenario-i-lqarp.txt")
        {
          pipe.outPath = "${WORKSPACE}/results/sink-depth-snr-diag.md";
        }
      DiagSnrToSink (pipe.dataDir, pipe.outPath);
      return 0;
    }
  if (pipe.diagCandidates)
    {
      if (pipe.outPath == "${WORKSPACE}/results/scenario-i-lqarp.txt")
        {
          pipe.outPath = "${WORKSPACE}/results/scenario-i-candidates.md";
        }
      DiagCandidates (pipe.dataDir, pipe.outPath, pipe.sinkAcousticDepthM);
      return 0;
    }
  if (pipe.diagRlor)
    {
      if (pipe.outPath == "${WORKSPACE}/results/scenario-i-lqarp.txt")
        {
          pipe.outPath = "${WORKSPACE}/results/STEP56_RLOR_PSUCC.md";
        }
      DiagRlor (pipe.dataDir, pipe.outPath, pipe.nAuv, pipe.seed, pipe.rngRun,
                pipe.sinkAcousticDepthM, pipe.driftSpeed);
      return 0;
    }
  if (pipe.diagKinematics)
    {
      if (pipe.outPath == "${WORKSPACE}/results/scenario-i-lqarp.txt")
        {
          pipe.outPath = "${WORKSPACE}/results/STEP59_RADIAL.md";
        }
      DiagKinematics (pipe.outPath, pipe.nAuv, pipe.seed, pipe.rngRun,
                      pipe.driftSpeed, 120.0);
      DiagEkfTrend (pipe.dataDir, "${WORKSPACE}/results/STEP63_EKF_TREND.md",
                    pipe.nAuv, pipe.seed, pipe.rngRun, pipe.driftSpeed, 120.0);
      return 0;
    }
  pipe.Place ();
  pipe.BuildProtocol ();
  if (pipe.discMode == "heard")
    {
      NS_ABORT_MSG_IF (!pipe.medOn (), "--disc=heard requires --mac != legacy (shared medium)");
      if (pipe.proto->scheduler () && !pipe.daLinkOff)
        {
          NS_ABORT_MSG ("--disc=heard refuses the scheduler link trigger: pass --linkOff=1");
        }
      pipe.proto->setHeardOnly (true);
      pipe.proto->setTnb (pipe.tNbS);
      if (pipe.proto->scheduler ())
        {
          auto *sch = const_cast<dahello::DahelloProtocol *> (pipe.proto->scheduler ());
          sch->setHeardOnly (true);
          if (pipe.tNbExplicit)
            {
              sch->setTnbSched (pipe.tNbS); // item 4: scheduler-host freshness
            }
        }
    }
  else if (pipe.discMode != "oracle")
    {
      NS_FATAL_ERROR ("--disc must be oracle|heard");
    }
  if (pipe.leakTest != 0)
    {
      pipe.InstallLeakTest ();
    }
  if (pipe.medOn ())
    {
      pipe.SetupMedium ();
    }
  if (pipe.lqarpPosDa > 0)
    {
      pipe.InstallLqarpPosDa ();
    }
  if (pipe.b6Dist || pipe.b8NaiveB || pipe.b9FcOnly || pipe.b2Trickle || pipe.b3Tap || pipe.b4Speed
      || (pipe.protoName == "marv-dahello-hs" && pipe.discMode == "heard"))
    {
      pipe.InstallComparators ();
    }
  pipe.ScheduleTraffic ();
  Simulator::Stop (Seconds (pipe.tSim + pipe.drainS + 1.0));
  Simulator::Run ();
  pipe.WriteReport ();
  pipe.WriteRecWrites ();
  Simulator::Destroy ();
  return 0;
}
