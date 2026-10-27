// lqarp-unit-check-common.h
// Shared Step 10 unit tests. Included by the g++ standalone driver
// and the ns-3 example. Not part of the protocol library.

#ifndef DAHELLO_LQARP_UNIT_CHECK_COMMON_H
#define DAHELLO_LQARP_UNIT_CHECK_COMMON_H

#if defined(DAHELLO_NS3_INCLUDES)
#include "ns3/channel-gate.h"
#include "ns3/lqarp-protocol.h"
#include "ns3/dahello-protocol.h"
#include "ns3/rlor-inspired-protocol.h"
#include "ns3/link-quality.h"
#include "ns3/gauss-markov.h"
#else
#include "channel-gate.h"
#include "lqarp-protocol.h"
#include "dahello-protocol.h"
#include "rlor-inspired-protocol.h"
#include "link-quality.h"
#include "gauss-markov.h"
#endif

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace {

int g_pass = 0;
int g_fail = 0;

void
Report (const std::string &name, bool ok, const std::string &detail)
{
  std::cout << (ok ? "PASS  " : "FAIL  ") << name << "\n        " << detail << "\n";
  (ok ? g_pass : g_fail)++;
}

bool
Close (double a, double b, double tol)
{
  if (!std::isfinite (a) || !std::isfinite (b))
    {
      return false;
    }
  return std::abs (a - b) <= tol;
}

bool
RelClose (double a, double b, double rtol, double atol = 0.0)
{
  if (!std::isfinite (a) || !std::isfinite (b))
    {
      return false;
    }
  return std::abs (a - b) <= atol + rtol * std::max (std::abs (a), std::abs (b));
}

std::string
F (double v, int prec = 12)
{
  std::ostringstream oss;
  oss << std::scientific << std::setprecision (prec) << v;
  return oss.str ();
}

// Test double: constant SNR for every non-zero horizontal range.
class FixedSnrGate : public dahello::ChannelGate
{
public:
  FixedSnrGate (double snrDb, double rStarM,
                double thresholdDb = dahello::ChannelGate::kDefaultSnrThresholdDb)
      : snrDb_ (snrDb),
        rStarM_ (rStarM)
  {
    setSnrThresholdDb (thresholdDb);
  }

  dahello::ChannelSample queryLink (double txX, double txY, double txZ,
                                  double rxX, double rxY, double rxZ) const override
  {
    (void) txZ;
    (void) rxZ;
    dahello::ChannelSample s;
    const double r = horizontalRangeM (txX, txY, rxX, rxY);
    if (r <= 0.0)
      {
        return s; // NaN, valid=false (range=0 singularity analogue)
      }
    s.snr_db = snrDb_;
    s.tl_db = 0.0;
    s.valid = true;
    s.nearPressureRelease = false;
    return s;
  }

  dahello::ChannelSample queryRangeDepth (double rangeM, double receiverZ) const override
  {
    (void) receiverZ;
    dahello::ChannelSample s;
    if (rangeM <= 0.0)
      {
        return s;
      }
    s.snr_db = snrDb_;
    s.tl_db = 0.0;
    s.valid = true;
    return s;
  }

  double rangeAtSnrThreshold () const override { return rStarM_; }

private:
  double snrDb_;
  double rStarM_;
};

// SNR falls with horizontal range, so a position jump moves z_k and
// can raise Broadcast_k. Above the candidate threshold out to 1 km.
class RangeSlopeGate : public dahello::ChannelGate
{
public:
  RangeSlopeGate (double snr0Db, double slopeDbPerM, double rStarM)
      : snr0Db_ (snr0Db),
        slopeDbPerM_ (slopeDbPerM),
        rStarM_ (rStarM)
  {
    setSnrThresholdDb (dahello::ChannelGate::kDefaultSnrThresholdDb);
  }

  dahello::ChannelSample queryLink (double txX, double txY, double txZ,
                                  double rxX, double rxY, double rxZ) const override
  {
    (void) txZ;
    (void) rxZ;
    const double r = horizontalRangeM (txX, txY, rxX, rxY);
    return atRange (r);
  }

  dahello::ChannelSample queryRangeDepth (double rangeM, double receiverZ) const override
  {
    (void) receiverZ;
    return atRange (rangeM);
  }

  double rangeAtSnrThreshold () const override { return rStarM_; }

private:
  dahello::ChannelSample atRange (double rangeM) const
  {
    dahello::ChannelSample s;
    if (!(rangeM > 0.0))
      {
        return s;
      }
    s.snr_db = snr0Db_ - slopeDbPerM_ * rangeM;
    s.tl_db = 0.0;
    s.valid = true;
    s.nearPressureRelease = false;
    return s;
  }

  double snr0Db_;
  double slopeDbPerM_;
  double rStarM_;
};

// Valid inside maxRange, out-of-envelope beyond it (valid=false, snr=NaN),
// mimicking BellhopDataLoader past the 6 km grid edge.
class EnvelopeGate : public dahello::ChannelGate
{
public:
  EnvelopeGate (double snr0Db, double slopeDbPerM, double rStarM, double maxRangeM)
      : snr0Db_ (snr0Db), slopeDbPerM_ (slopeDbPerM), rStarM_ (rStarM),
        maxRangeM_ (maxRangeM)
  {
    setSnrThresholdDb (dahello::ChannelGate::kDefaultSnrThresholdDb);
  }
  dahello::ChannelSample queryLink (double txX, double txY, double txZ,
                                  double rxX, double rxY, double rxZ) const override
  {
    (void) txZ; (void) rxZ;
    return atRange (horizontalRangeM (txX, txY, rxX, rxY));
  }
  dahello::ChannelSample queryRangeDepth (double rangeM, double receiverZ) const override
  {
    (void) receiverZ; return atRange (rangeM);
  }
  double rangeAtSnrThreshold () const override { return rStarM_; }

private:
  dahello::ChannelSample atRange (double rangeM) const
  {
    dahello::ChannelSample s;             // default: valid=false, snr=NaN
    if (!(rangeM > 0.0) || rangeM > maxRangeM_)
      {
        return s;
      }
    s.snr_db = snr0Db_ - slopeDbPerM_ * rangeM;
    s.tl_db = 0.0;
    s.valid = true;
    return s;
  }
  double snr0Db_, slopeDbPerM_, rStarM_, maxRangeM_;
};

bool
Contains (const std::vector<uint32_t> &I, uint32_t id)
{
  return std::find (I.begin (), I.end (), id) != I.end ();
}

int
RunLqarpUnitChecks (const std::string &dataDir)
{
  using dahello::BellhopDataLoader;
  using dahello::ChannelGate;
  using dahello::LqarpConfig;
  using dahello::LqarpNode;
  using dahello::LqarpProtocol;
  using dahello::Season;

  std::cout << std::scientific << std::setprecision (12);
  std::cout << "dataDir=" << dataDir << "\n";
  std::cout << "gamma=0.8 is DA-HELLO Sec. 4.1; LQARP-paper gamma was not in the extraction matrix.\n";
  {
    const dahello::DahelloConfig d;
    const dahello::GaussMarkovConfig gm;
    std::cout << "compiled first-pass defaults: W_mp=" << d.W_mp
              << " W=" << d.W << " rho=" << d.rho
              << " epsilon_0=" << d.epsilon_0
              << " lambda=" << d.lambda
              << " T_ctrl=" << d.T_ctrl
              << " beta_1=" << d.beta_1
              << " beta_2=" << d.beta_2
              << " zeta=" << gm.zeta << "\n\n";
  }

  auto janLoader = std::make_shared<BellhopDataLoader> (
      dataDir + "/TL_grid_extended_january.csv",
      dataDir + "/SNR_grid_extended_january.csv",
      Season::January);
  auto julLoader = std::make_shared<BellhopDataLoader> (
      dataDir + "/TL_grid_extended_july.csv",
      dataDir + "/SNR_grid_extended_july.csv",
      Season::July);
  ChannelGate janGate (janLoader);
  ChannelGate julGate (julLoader);

  // ------------------------------------------------------------------
  // Channel gate: R* scan at 50 m (Step G3 / SPEC Part 5 item 4)
  // ------------------------------------------------------------------
  {
    const double rJan = janGate.rangeAtSnrThreshold ();
    Report ("rangeAtSnrThreshold January at 50 m is 5125 m",
            Close (rJan, 5125.0, 1e-9),
            "actual=" + F (rJan, 6) + " expected=5.125000e+03");
    const double rJul = julGate.rangeAtSnrThreshold ();
    Report ("rangeAtSnrThreshold July at 50 m is 4525 m",
            Close (rJul, 4525.0, 1e-9),
            "actual=" + F (rJul, 6) + " expected=4.525000e+03");
  }

  {
    const bool floorOk = Close (ChannelGate::depthQueryM (1.0), 5.0, 1e-12)
                         && Close (ChannelGate::depthQueryM (50.0), 50.0, 1e-12);
    Report ("depth_query = max(receiver_z, 5.0)",
            floorOk,
            "depthQueryM(1)=" + F (ChannelGate::depthQueryM (1.0), 6)
                + " depthQueryM(50)=" + F (ChannelGate::depthQueryM (50.0), 6));
  }

  // ------------------------------------------------------------------
  // 1. Candidate-set restriction — one failing condition at a time
  // ------------------------------------------------------------------
  std::cout << "\n--- 1. Candidate-set restriction ---\n";

  {
    LqarpConfig cfg;
    cfg.rRangeM = 250.0;
    cfg.eIni = 100.0;
    std::vector<LqarpNode> nodes (3);
    nodes[0] = {0, 0.0, 0.0, 100.0, 100.0};
    nodes[1] = {1, 400.0, 0.0, 50.0, 100.0}; // far: range fails, SNR and depth pass
    nodes[2] = {2, 100.0, 0.0, 50.0, 100.0}; // near control
    LqarpProtocol proto (janGate, cfg, nodes);
    const double d01 = ChannelGate::distance3D (0, 0, 100, 400, 0, 50);
    const auto I = proto.candidateSet (0);
    const auto s01 = proto.linkSample (0, 1);
    // s1 never enters the hold (live range fails), so every routing
    // filter on that pair stays closed. s2 is discovered and kept.
    const bool ok = d01 >= 250.0
                    && s01.valid && s01.snr_db > 8.68
                    && proto.inNRange (0, 1) == false
                    && proto.inNSnr (0, 1) == false
                    && proto.inNDepth (0, 1) == false
                    && !Contains (I, 1)
                    && Contains (I, 2)
                    && proto.inNRange (0, 2) && proto.inNSnr (0, 2) && proto.inNDepth (0, 2);
    Report ("1a. N_range fails alone (d>=r_range, SNR and depth pass)",
            ok,
            "d(s0,s1)=" + F (d01, 6) + " r_range=250"
                + " live snr=" + F (s01.snr_db, 4)
                + " held inNRange=" + std::string (proto.inNRange (0, 1) ? "true" : "false")
                + " (no discovery, so held SNR/depth filters stay closed)"
                + " I(s0) contains 1=" + std::string (Contains (I, 1) ? "yes" : "no")
                + " contains 2=" + std::string (Contains (I, 2) ? "yes" : "no"));
  }

  {
    LqarpConfig cfg;
    cfg.rRangeM = 6000.0;
    cfg.eIni = 100.0;
    std::vector<LqarpNode> nodes (3);
    nodes[0] = {0, 0.0, 0.0, 100.0, 100.0};
    nodes[1] = {1, 5500.0, 0.0, 50.0, 100.0}; // SNR 5.52 dB at (5500,50)
    nodes[2] = {2, 200.0, 0.0, 50.0, 100.0};
    LqarpProtocol proto (janGate, cfg, nodes);
    const auto s01 = proto.linkSample (0, 1);
    const auto I = proto.candidateSet (0);
    const bool ok = s01.valid && !(s01.snr_db > 8.68)
                    && proto.inNRange (0, 1) == false
                    && proto.inNSnr (0, 1) == false
                    && proto.inNDepth (0, 1) == false
                    && !Contains (I, 1)
                    && Contains (I, 2);
    Report ("1b. N_SNR fails alone (SNR<=8.68, range and depth pass)",
            ok,
            "snr(s0,s1)=" + F (s01.snr_db, 8)
                + " valid=" + std::string (s01.valid ? "true" : "false")
                + " held filters closed (SNR blocks discovery)="
                + std::string (proto.inNRange (0, 1) ? "no" : "yes")
                + " I(s0) contains 1=" + std::string (Contains (I, 1) ? "yes" : "no")
                + " contains 2=" + std::string (Contains (I, 2) ? "yes" : "no"));
  }

  {
    LqarpConfig cfg;
    cfg.rRangeM = 6000.0;
    cfg.eIni = 100.0;
    std::vector<LqarpNode> nodes (3);
    nodes[0] = {0, 0.0, 0.0, 50.0, 100.0};
    nodes[1] = {1, 200.0, 0.0, 100.0, 100.0}; // deeper than s0
    nodes[2] = {2, 200.0, 0.0, 20.0, 100.0};  // shallower control
    LqarpProtocol proto (janGate, cfg, nodes);
    const auto I = proto.candidateSet (0);
    const bool ok = proto.inNRange (0, 1) == true
                    && proto.inNSnr (0, 1) == true
                    && proto.inNDepth (0, 1) == false
                    && !Contains (I, 1)
                    && Contains (I, 2);
    Report ("1c. N_depth fails alone (deeper neighbor, range and SNR pass)",
            ok,
            "depth0=50 depth1=100 delta=" + F (50.0 - 100.0, 4)
                + " inNRange=" + std::string (proto.inNRange (0, 1) ? "true" : "false")
                + " inNSnr=" + std::string (proto.inNSnr (0, 1) ? "true" : "false")
                + " inNDepth=" + std::string (proto.inNDepth (0, 1) ? "true" : "false")
                + " I(s0) contains 1=" + std::string (Contains (I, 1) ? "yes" : "no")
                + " contains 2=" + std::string (Contains (I, 2) ? "yes" : "no"));
  }

  // ------------------------------------------------------------------
  // 2. Reward factors vs independently computed values
  // ------------------------------------------------------------------
  std::cout << "\n--- 2. Reward factors (independent arithmetic) ---\n";

  {
    const double snr = 5.0;
    const double actualPe = LqarpProtocol::ofdmBitErrorRate (snr, 2);
    const double actualPs = LqarpProtocol::pSucc (snr, 400, 2);
    const double actual = LqarpProtocol::cLin (snr, 400, 2);
    const double expPe = 1.2122952770944309e-03;
    const double expPs = 6.1556651007993546e-01;
    const double expCl = 4.8521228057663601e-01;
    Report ("2a. c_lin at SNR=5 dB, l=400, M=2",
            Close (actualPe, expPe, 1e-14) && Close (actualPs, expPs, 1e-14)
                && Close (actual, expCl, 1e-14),
            "p_e actual=" + F (actualPe) + " expected=" + F (expPe) + "\n        "
                + "P_succ actual=" + F (actualPs) + " expected=" + F (expPs) + "\n        "
                + "c_lin actual=" + F (actual) + " expected=" + F (expCl)
                + "  (p_e=1.21e-3 >> kBerFloor=1e-9, floor does not bind)");
  }

  {
    const double actual = LqarpProtocol::cEne (80.0, 50.0, 100.0);
    const double expected = 0.1855471581554847; // (2/pi)*atan(0.3)
    Report ("2b. c_ene E_sj=80 E_avg=50 E_ini=100",
            Close (actual, expected, 1e-14),
            "actual=" + F (actual) + " expected=" + F (expected));
  }

  {
    const double actual = LqarpProtocol::cDel (80.0, 100.0, 100.0, 40.0);
    const double expected = 48.0; // (80/100)*(100-40)
    Report ("2c. c_del E_sj=80 E_ini=100 depth_i=100 depth_j=40",
            Close (actual, expected, 1e-14),
            "actual=" + F (actual, 6) + " expected=" + F (expected, 6));
  }

  {
    const double actual3 = LqarpProtocol::cNei (3);
    const double actual0 = LqarpProtocol::cNei (0);
    const double exp3 = 20.0855369231876679; // exp(3)
    Report ("2d. c_nei number=3 and number=0 (void → 1)",
            Close (actual3, exp3, 1e-14) && Close (actual0, 1.0, 1e-15),
            "c_nei(3) actual=" + F (actual3) + " expected=" + F (exp3) + "\n        "
                + "c_nei(0) actual=" + F (actual0, 6) + " expected=1.000000e+00");
  }

  {
    const auto samp = janGate.queryLink (0.0, 0.0, 50.0, 1000.0, 0.0, 50.0);
    const double rawPe = LqarpProtocol::ofdmBitErrorRate (samp.snr_db, 2);
    const double effPe = dahello::effectiveBer (samp.snr_db, 2);
    const double actualCl = LqarpProtocol::cLin (samp.snr_db, 400, 2);
    const double floorCl = dahello::cLinAtBerFloor (400);
    const bool clamped = rawPe < dahello::kBerFloor
                         && Close (effPe, dahello::kBerFloor, 0.0)
                         && Close (actualCl, floorCl, 1e-18)
                         && std::isfinite (actualCl) && actualCl > 0.0;
    Report ("2e. high-SNR January query(1000 m, 50 m): p_e clamps to 1e-9, c_lin finite",
            clamped && samp.valid && samp.snr_db > 40.0,
            "snr=" + F (samp.snr_db, 8)
                + " raw p_e=" + F (rawPe)
                + " p_e_effective=" + F (effPe)
                + " kBerFloor=" + F (dahello::kBerFloor)
                + "\n        c_lin actual=" + F (actualCl)
                + " c_lin(floor)=" + F (floorCl)
                + " (must be finite and equal, not 0)");
  }

  {
    const double actual = dahello::cLinAtBerFloor (400);
    const double expected = -400.0 * std::log1p (-1.0e-9);
    Report ("2f. high-SNR plateau of c_lin for l=400 is ln(1/(1-1e-9)^400)",
            Close (actual, expected, 1e-18) && std::isfinite (actual) && actual > 0.0,
            "actual=" + F (actual) + " expected=" + F (expected)
                + "  (SPEC addendum; this is the finite lower bound on c_lin)");
  }

  // ------------------------------------------------------------------
  // 3. One Q-update, alpha=0.9 gamma=0.8, round geometry + SNR=8.7
  // ------------------------------------------------------------------
  std::cout << "\n--- 3. Q-update ---\n";

  {
    FixedSnrGate fake (8.7, 1000.0);
    LqarpConfig cfg;
    cfg.alpha = 0.9;
    cfg.gamma = 0.8;
    cfg.rRangeM = 1000.0;
    cfg.eIni = 100.0;
    cfg.packetBits = 400;
    cfg.constellationM = 2;
    std::vector<LqarpNode> nodes (3);
    nodes[0] = {0, 0.0, 0.0, 100.0, 100.0};
    nodes[1] = {1, 100.0, 0.0, 60.0, 90.0};
    nodes[2] = {2, 200.0, 0.0, 20.0, 70.0};
    LqarpProtocol proto (fake, cfg, nodes);

    // Pre-conditions used in the hand calculation:
    // I(0)={1,2} ⇒ E_avg=80; I(1)={2} ⇒ number_1=1; I(2)={} ⇒ maxQ next=0
    const auto I0 = proto.candidateSet (0);
    const auto I1 = proto.candidateSet (1);
    const auto I2 = proto.candidateSet (2);
    const auto rw = proto.reward (0, 1);
    proto.updateQ (0, 1);
    const double q = proto.qValue (0, 1);

    const double exp_c_ene = 0.0634510348611071; // (2/pi)*atan(0.1)
    const double exp_c_del = 36.0;
    const double exp_c_nei = 2.7182818284590451; // exp(1)
    const double exp_c_lin = 5.6398491767771573e-04;
    const double exp_R = 1.1009515374493534e+04;
    const double exp_Q = 9.9085638370441811e+03;

    const bool setsOk = I0.size () == 2 && Contains (I0, 1) && Contains (I0, 2)
                        && I1.size () == 1 && Contains (I1, 2)
                        && I2.empty ();
    const bool factorsOk = Close (rw.c_ene, exp_c_ene, 1e-14)
                           && Close (rw.c_del, exp_c_del, 1e-12)
                           && Close (rw.c_nei, exp_c_nei, 1e-14)
                           && Close (rw.c_lin, exp_c_lin, 1e-15)
                           && Close (rw.R, exp_R, 1e-8)
                           && Close (q, exp_Q, 1e-8);
    Report ("3. Q(s0,a1) after one TD step (Q_old=0, max_I(s1)=0)",
            setsOk && factorsOk,
            "I(s0) size=" + std::to_string (I0.size ())
                + " I(s1) size=" + std::to_string (I1.size ())
                + " I(s2) size=" + std::to_string (I2.size ()) + "\n        "
                + "c_ene actual=" + F (rw.c_ene) + " expected=" + F (exp_c_ene) + "\n        "
                + "c_del actual=" + F (rw.c_del, 6) + " expected=" + F (exp_c_del, 6) + "\n        "
                + "c_nei actual=" + F (rw.c_nei) + " expected=" + F (exp_c_nei) + "\n        "
                + "c_lin actual=" + F (rw.c_lin) + " expected=" + F (exp_c_lin) + "\n        "
                + "R actual=" + F (rw.R) + " expected=" + F (exp_R) + "\n        "
                + "Q actual=" + F (q) + " expected=" + F (exp_Q) + "\n        "
                + "check: Q = (1-0.9)*0 + 0.9*(R + 0.8*0) = 0.9*R");
  }

  // ------------------------------------------------------------------
  // 4. number_{s_j} = |I(s_j)|, not a raw neighbor count
  // ------------------------------------------------------------------
  std::cout << "\n--- 4. number_{s_j} = |I(s_j)| ---\n";

  {
    LqarpConfig cfg;
    cfg.rRangeM = 1000.0;
    cfg.eIni = 100.0;
    std::vector<LqarpNode> nodes (4);
    nodes[0] = {0, 0.0, 0.0, 100.0, 100.0};
    nodes[1] = {1, 150.0, 0.0, 50.0, 100.0};
    nodes[2] = {2, 300.0, 0.0, 20.0, 100.0};
    nodes[3] = {3, 80.0, 0.0, 80.0, 100.0}; // shallower than 0, deeper than 1
    LqarpProtocol proto (janGate, cfg, nodes);
    const uint32_t n0 = proto.numberSj (0);
    const uint32_t n1 = proto.numberSj (1);
    const uint32_t n2 = proto.numberSj (2);
    const uint32_t n3 = proto.numberSj (3);
    // I(0)={1,2,3}, I(1)={2} only (3 is deeper than 1), I(2)={}, I(3)={1,2}
    const bool ok = n0 == 3 && n1 == 1 && n2 == 0 && n3 == 2;
    Report ("4. number_sj is |I(sj)| not |all nodes| or unfiltered neighbors",
            ok,
            "number_s0=" + std::to_string (n0) + " expected=3  (I={1,2,3})\n        "
                + "number_s1=" + std::to_string (n1) + " expected=1  (I={2}; s3 is deeper than s1)\n        "
                + "number_s2=" + std::to_string (n2) + " expected=0  (shallowest)\n        "
                + "number_s3=" + std::to_string (n3) + " expected=2  (I={1,2})\n        "
                + "c_nei(s1)=exp(1)=" + F (LqarpProtocol::cNei (n1), 6)
                + "  not exp(3) which would be a raw-count bug");
  }

  // ------------------------------------------------------------------
  // Smoke: stationary 4-node field, real January grid
  // ------------------------------------------------------------------
  std::cout << "\n--- Smoke: zero-mobility, real January Bellhop grid ---\n";

  {
    LqarpConfig cfg;
    cfg.eIni = 100.0;
    // rRangeM NaN → January R* ≈ 5125 m
    std::vector<LqarpNode> nodes (4);
    nodes[0] = {0, 0.0, 0.0, 10.0, 100.0};     // surface USV
    nodes[1] = {1, 2000.0, 0.0, 40.0, 90.0};
    nodes[2] = {2, 3500.0, 0.0, 70.0, 70.0};
    nodes[3] = {3, 4800.0, 0.0, 100.0, 50.0};  // deepest
    LqarpProtocol proto (janGate, cfg, nodes);

    std::cout << "        r_range (January 50 m scan)=" << proto.rRangeM () << " m\n";
    bool anyEmptyDeep = false;
    bool anyNanQ = false;
    bool surfaceEmpty = proto.candidateSet (0).empty ();
    for (uint32_t i = 0; i < 4; ++i)
      {
        const auto I = proto.candidateSet (i);
        std::cout << "        I(s" << i << ") size=" << I.size () << " {";
        for (size_t k = 0; k < I.size (); ++k)
          {
            std::cout << I[k] << (k + 1 < I.size () ? "," : "");
          }
        std::cout << "}\n";
        if (i > 0 && I.empty ())
          {
            anyEmptyDeep = true;
          }
        proto.learnFrom (i);
        for (uint32_t j : I)
          {
            const double q = proto.qValue (i, j);
            const auto rw = proto.reward (i, j);
            std::cout << "        Q(s" << i << ",a" << j << ")=" << F (q)
                      << " R=" << F (rw.R)
                      << " c_lin=" << F (rw.c_lin)
                      << " c_ene=" << F (rw.c_ene)
                      << " c_del=" << F (rw.c_del, 6)
                      << " c_nei=" << F (rw.c_nei, 6) << "\n";
            if (!std::isfinite (q) || !std::isfinite (rw.R))
              {
                anyNanQ = true;
              }
          }
        const int nh = proto.nextHop (i);
        std::cout << "        NextHop(s" << i << ")=" << nh << "\n";
      }
    const int nh3 = proto.nextHop (3);
    const auto rw30 = proto.reward (3, 0);
    const auto rw31 = proto.reward (3, 1);
    const double q30 = proto.qValue (3, 0);
    const double q31 = proto.qValue (3, 1);
    const bool finiteChoice = std::isfinite (q30) && std::isfinite (q31)
                              && std::isfinite (rw30.R) && std::isfinite (rw31.R)
                              && q30 != q31;
    const bool ok = surfaceEmpty && !anyEmptyDeep && !anyNanQ && nh3 >= 0 && finiteChoice;
    Report ("smoke. stationary field: finite comparable Q(s3,a0) vs Q(s3,a1), defined NextHop",
            ok,
            "I(s0) empty=" + std::string (surfaceEmpty ? "true" : "false")
                + " any-empty-deep=" + std::string (anyEmptyDeep ? "true" : "false")
                + " any-nonfinite-Q=" + std::string (anyNanQ ? "true" : "false")
                + "\n        R(s3,a0)=" + F (rw30.R) + " Q(s3,a0)=" + F (q30)
                + " c_lin=" + F (rw30.c_lin)
                + "\n        R(s3,a1)=" + F (rw31.R) + " Q(s3,a1)=" + F (q31)
                + " c_lin=" + F (rw31.c_lin)
                + "\n        NextHop(s3)=" + std::to_string (nh3)
                + " (argmax of finite Q, not an inf tie)");

    // ------------------------------------------------------------------
    // DA-HELLO Part 2 tests (same process, same January gate)
    // ------------------------------------------------------------------
    std::cout << "\n--- 5. DA-HELLO identity: beta_1=beta_2=0, epsilon_0 huge ---\n";

    dahello::DahelloConfig ecfg;
    ecfg.eIni = 100.0;
    ecfg.beta_1 = 0.0;
    ecfg.beta_2 = 0.0;
    ecfg.epsilon_0 = 1.0e12; // trigger never fires
    ecfg.lambda = 0.0;
    std::vector<LqarpNode> enodes (4);
    enodes[0] = {0, 0.0, 0.0, 10.0, 100.0};
    enodes[1] = {1, 2000.0, 0.0, 40.0, 90.0};
    enodes[2] = {2, 3500.0, 0.0, 70.0, 70.0};
    enodes[3] = {3, 4800.0, 0.0, 100.0, 50.0};
    dahello::DahelloProtocol dahelloProto (janGate, ecfg, enodes);
    bool anyCandidate = false;
    bool triggerSilent = dahelloProto.eventHelloCount () == 0;
    for (uint32_t i = 0; i < 4; ++i)
      {
        if (!dahelloProto.candidateSet (i).empty ())
          {
            anyCandidate = true;
          }
        for (uint32_t j = 0; j < 4; ++j)
          {
            if (i != j && dahelloProto.ekf (i, j).broadcast_k != 0)
              {
                triggerSilent = false;
              }
          }
      }
    Report ("5pre. trigger never fires: bootstrap still fills I without Broadcast_k",
            anyCandidate && triggerSilent && dahelloProto.bootstrapHelloCount () > 0
                && dahelloProto.eventHelloCount () == 0,
            std::string ("any_I=") + (anyCandidate ? "yes" : "no")
                + " bootstrap_hellos=" + std::to_string (dahelloProto.bootstrapHelloCount ())
                + " event_hellos=" + std::to_string (dahelloProto.eventHelloCount ()));
    for (uint32_t i = 0; i < 4; ++i)
      {
        for (uint32_t j = 0; j < 4; ++j)
          {
            if (i != j)
              {
                dahelloProto.setNeighborVelocityZoh (i, j, 0.0, 0.0, 0.0);
              }
          }
      }
    for (uint32_t i = 0; i < 4; ++i)
      {
        dahelloProto.learnFrom (i);
      }

    bool identityOk = true;
    std::ostringstream idDetail;
    idDetail << std::scientific << std::setprecision (12);
    for (uint32_t i = 0; i < 4; ++i)
      {
        const auto Il = proto.candidateSet (i);
        const auto Ie = dahelloProto.candidateSet (i);
        const int nhl = proto.nextHop (i);
        const int nhe = dahelloProto.nextHop (i);
        idDetail << "        s" << i << " NextHop LQARP=" << nhl
                 << " DA-HELLO=" << nhe
                 << " |I_LQARP|=" << Il.size ()
                 << " |I_DAHELLO|=" << Ie.size () << "\n";
        if (nhl != nhe || Il.size () != Ie.size ())
          {
            identityOk = false;
          }
        for (uint32_t j : Il)
          {
            const double ql = proto.qValue (i, j);
            const double qe = dahelloProto.qValue (i, j);
            const auto rl = proto.reward (i, j);
            const auto re = dahelloProto.reward (i, j);
            idDetail << "        Q(" << i << "," << j << ") LQARP=" << F (ql)
                     << " DA-HELLO=" << F (qe)
                     << "  R LQARP=" << F (rl.R)
                     << " DA-HELLO=" << F (re.R)
                     << "  c_lin=" << F (rl.c_lin)
                     << " c_pred=" << F (re.c_pred)
                     << "  c_del=" << F (rl.c_del, 6)
                     << " c_drift=" << F (re.c_drift, 6) << "\n";
            if (!RelClose (ql, qe, 1e-12, 1e-9) || !RelClose (rl.R, re.R, 1e-12, 1e-9)
                || !RelClose (rl.c_lin, re.c_pred, 1e-12, 1e-12)
                || !RelClose (rl.c_del, re.c_drift, 1e-12, 1e-12))
              {
                identityOk = false;
              }
          }
      }
    Report ("5. DA-HELLO with beta_1=beta_2=0 and trigger off matches LQARP Q and NextHop",
            identityOk,
            idDetail.str ());

    std::cout << "\n--- 6. v_rel = 0 and c_drift with beta_2>0 ---\n";
    {
      dahello::DahelloConfig vcfg;
      vcfg.eIni = 100.0;
      vcfg.rRangeM = 1000.0;
      vcfg.beta_1 = 0.0;
      vcfg.beta_2 = 2.0;
      vcfg.epsilon_0 = 1.0e12;
      std::vector<LqarpNode> vnodes (2);
      vnodes[0] = {0, 0.0, 0.0, 100.0, 80.0, 0.0, 0.0, 0.0};
      vnodes[1] = {1, 100.0, 0.0, 40.0, 80.0, 0.0, 0.0, 0.0};
      dahello::DahelloProtocol vp (janGate, vcfg, vnodes);
      dahello::DahelloProtocol silent (janGate, vcfg, vnodes);
      silent.setBootstrapSuppressed (1, true);
      const double c_del = LqarpProtocol::cDel (80.0, 100.0, 100.0, 40.0);
      const double unheard = silent.cDrift (0, 1);
      const bool unheardOk = !silent.zohHeard (0, 1) && Close (unheard, 0.0, 1e-12);
      const double vr = vp.vRel (0, 1);
      const double sg = dahello::DahelloProtocol::sigmoid (0.0);
      const double expectedDrift = c_del * (1.0 + 2.0 * sg);
      const double actualDrift = vp.cDrift (0, 1);
      const bool okV = unheardOk && Close (vr, 0.0, 1e-15) && Close (sg, 0.5, 1e-15)
                       && Close (c_del, 48.0, 1e-12)
                       && Close (actualDrift, expectedDrift, 1e-12)
                       && Close (actualDrift, 48.0 * 2.0, 1e-12)
                       && vp.zohHeard (0, 1);
      Report ("6a. suppressed bootstrap => c_drift=0; bootstrap copies vx=0 so v_rel=0",
              okV,
              "unheard c_drift=" + F (unheard, 6) + " c_del=" + F (c_del, 6)
                  + " after ZOH v_rel=" + F (vr)
                  + " sigmoid(0)=" + F (sg, 6)
                  + " c_drift=" + F (actualDrift, 6)
                  + " expected=" + F (expectedDrift, 6));
    }

    {
      dahello::DahelloConfig vcfg;
      vcfg.eIni = 100.0;
      vcfg.rRangeM = 1000.0;
      vcfg.beta_2 = 2.0;
      vcfg.epsilon_0 = 1.0e12;
      std::vector<LqarpNode> vnodes (2);
      // p_ij = p_i - p_j = (-100, 0, 60), ||p||=116.619...
      // Give i a velocity along +x so closing/opening is nonzero.
      vnodes[0] = {0, 0.0, 0.0, 100.0, 80.0, 1.0, 0.0, 0.0};
      vnodes[1] = {1, 100.0, 0.0, 40.0, 80.0, 0.0, 0.0, 0.0};
      dahello::DahelloProtocol vp (janGate, vcfg, vnodes);
      dahello::DahelloProtocol silent (janGate, vcfg, vnodes);
      silent.setBootstrapSuppressed (1, true);
      const double c_del = 48.0;
      const double unheard = silent.cDrift (0, 1);
      // Bootstrap copies neighbor vx=0. v_rel is then the live own
      // velocity against that held zero, not a pre-discovery phantom.
      const double vr = vp.vRel (0, 1);
      const double nrm = ChannelGate::distance3D (0, 0, 100, 100, 0, 40);
      const double expVr = (1.0 * (-100.0) + 0 + 0) / nrm; // pdot_ij · p_ij / ||p||
      const double expectedDrift = c_del * (1.0 + 2.0 * dahello::DahelloProtocol::sigmoid (-vr));
      const double actualDrift = vp.cDrift (0, 1);
      Report ("6b. suppressed bootstrap => c_drift=0; bootstrap copies vx=0 into v_rel",
              Close (unheard, 0.0, 1e-12)
                  && !silent.zohHeard (0, 1)
                  && Close (vr, expVr, 1e-12) && Close (actualDrift, expectedDrift, 1e-12)
                  && vp.zohHeard (0, 1),
              "unheard c_drift=" + F (unheard, 6)
                  + " v_rel actual=" + F (vr) + " expected=" + F (expVr)
                  + "\n        c_drift actual=" + F (actualDrift, 6)
                  + " expected=" + F (expectedDrift, 6)
                  + " sigmoid(-v_rel)=" + F (dahello::DahelloProtocol::sigmoid (-vr), 6));
    }

    {
      dahello::DahelloConfig vcfg;
      vcfg.eIni = 100.0;
      vcfg.rRangeM = 1000.0;
      vcfg.beta_2 = 1.0;
      vcfg.epsilon_0 = 1.0e12;
      std::vector<LqarpNode> vnodes (2);
      vnodes[0] = {0, 0.0, 0.0, 100.0, 80.0, 2.0, 0.0, 0.0};
      vnodes[1] = {1, 100.0, 0.0, 40.0, 80.0, 2.0, 0.0, 0.0};
      dahello::DahelloProtocol vp (janGate, vcfg, vnodes);
      dahello::DahelloProtocol silent (janGate, vcfg, vnodes);
      silent.setBootstrapSuppressed (1, true);
      const double c_del = LqarpProtocol::cDel (80.0, 100.0, 100.0, 40.0);
      const bool unheardFlag = !silent.zohHeard (0, 1);
      const double vrRaw = silent.vRel (0, 1); // own 2 minus held 0
      const double cd = silent.cDrift (0, 1);
      vp.cDrift (0, 1); // bootstrap copies neighbor vx=2
      const double vrHeard = vp.vRel (0, 1);
      const double cdHeard = vp.cDrift (0, 1);
      const double expectedHeard = c_del * (1.0 + 1.0 * dahello::DahelloProtocol::sigmoid (0.0));
      Report ("6c. suppressed bootstrap keeps phantom v_rel; bootstrap copies vx so v_rel=0",
              unheardFlag
                  && Close (cd, 0.0, 1e-12)
                  && std::fabs (vrRaw) > 0.5
                  && Close (vrHeard, 0.0, 1e-12)
                  && Close (cdHeard, expectedHeard, 1e-12)
                  && vp.zohHeard (0, 1),
              "unheard |v_rel|=" + F (std::fabs (vrRaw), 4) + " c_drift=" + F (cd, 6)
                  + " (held c_del after ZOH " + F (c_del, 6) + ")"
                  + " after ZOH v_rel=" + F (vrHeard, 4)
                  + " c_drift=" + F (cdHeard, 6));
    }

    std::cout << "\n--- 7. Persistence-gated trigger and N_stab ---\n";
    {
      dahello::DahelloConfig tcfg;
      tcfg.eIni = 100.0;
      tcfg.rRangeM = 1000.0;
      tcfg.W = 3;
      tcfg.rho = 1.0; // every sample in the window must exceed
      tcfg.epsilon_0 = 1.0;
      tcfg.beta_1 = 0.0;
      tcfg.beta_2 = 0.0;
      std::vector<LqarpNode> tnodes (3);
      tnodes[0] = {0, 0.0, 0.0, 100.0, 100.0};
      tnodes[1] = {1, 100.0, 0.0, 60.0, 90.0};
      tnodes[2] = {2, 200.0, 0.0, 20.0, 70.0};
      dahello::DahelloProtocol tp (janGate, tcfg, tnodes);
      tp.setNeighborVelocityZoh (0, 1, 0.0, 0.0, 0.0);
      tp.setNeighborVelocityZoh (0, 2, 0.0, 0.0, 0.0);

      const auto I_before = tp.candidateSet (0);
      // Three consecutive |e|>=eps samples for pair (0,1).
      for (int n = 0; n < 3; ++n)
        {
          tp.forceInnovation (0, 1, 5.0, 1.0); // |5| >= 1
          tp.pushExceedFlag (0, 1, true);
        }
      tp.recomputeBroadcast (0, 1);
      const auto I_after = tp.candidateSet (0);
      const int bc = tp.ekf (0, 1).broadcast_k;
      const bool excluded = !Contains (I_after, 1) && Contains (I_after, 2)
                            && Contains (I_before, 1) && Contains (I_before, 2)
                            && bc == 1
                            && tp.inNStab (0, 1) == false
                            && tp.inNRange (0, 1) && tp.inNSnr (0, 1) && tp.inNDepth (0, 1);
      Report ("7. |e_k|>=epsilon_k over W with rho=1 fires Broadcast_k=1 and N_stab excludes s1",
              excluded,
              "Broadcast_k=" + std::to_string (bc)
                  + " inNStab(0,1)=" + std::string (tp.inNStab (0, 1) ? "true" : "false")
                  + " e_k=" + F (tp.ekf (0, 1).e_k, 4)
                  + " epsilon_k=" + F (tp.ekf (0, 1).epsilon_k, 4)
                  + "\n        I_DAHELLO(s0) before={"
                  + (Contains (I_before, 1) ? "1," : "") + "2?} after excludes 1="
                  + std::string (Contains (I_after, 1) ? "no" : "yes")
                  + " still has 2=" + std::string (Contains (I_after, 2) ? "yes" : "no")
                  + "\n        range/SNR/depth still pass, so exclusion is N_stab only");
    }
  }

  // ------------------------------------------------------------------
  // 8–9. RLOR-inspired baseline (SPEC Part 3, documented simplification)
  // ------------------------------------------------------------------
  std::cout << "\n--- 8. RLOR-inspired candidate set vs LQARP N_depth ---\n";

  {
    // s0 at depth 80 m. s1 is deeper (120 m) with high SNR at 200 m —
    // LQARP drops it on N_depth; RLOR keeps it (opportunistic).
    // s2 is shallower at 5100 m, January SNR just above 8.68 dB —
    // both protocols keep it. r_range = January R* = 5125 m.
    LqarpConfig cfg;
    cfg.rRangeM = 5125.0;
    cfg.eIni = 100.0;
    std::vector<LqarpNode> nodes (3);
    nodes[0] = {0, 0.0, 0.0, 80.0, 100.0};
    nodes[1] = {1, 200.0, 0.0, 120.0, 100.0};
    nodes[2] = {2, 5100.0, 0.0, 50.0, 100.0};
    LqarpProtocol lq (janGate, cfg, nodes);
    dahello::RlorInspiredProtocol rl (janGate, cfg, nodes);

    const auto Il = lq.candidateSet (0);
    const auto Ir = rl.candidateSet (0);
    const auto s01 = rl.linkSample (0, 1);
    const auto s02 = rl.linkSample (0, 2);
    const bool depthDrop = lq.inNDepth (0, 1) == false
                           && lq.inNRange (0, 1) && lq.inNSnr (0, 1)
                           && !Contains (Il, 1) && Contains (Il, 2);
    const bool rlorKeeps = rl.inNRange (0, 1) && rl.inNSnr (0, 1)
                           && Contains (Ir, 1) && Contains (Ir, 2)
                           && Ir.size () == 2 && Il.size () == 1;
    Report ("8. I_RLOR = N_range ∩ N_SNR (no N_depth): keeps deeper s1 that LQARP drops",
            depthDrop && rlorKeeps,
            "snr(s0,s1)=" + F (s01.snr_db, 8)
                + " snr(s0,s2)=" + F (s02.snr_db, 8)
                + " r_range=5125"
                + "\n        LQARP inNDepth(0,1)=" + std::string (lq.inNDepth (0, 1) ? "true" : "false")
                + " |I_LQARP(s0)|=" + std::to_string (Il.size ())
                + " contains 1=" + std::string (Contains (Il, 1) ? "yes" : "no")
                + " contains 2=" + std::string (Contains (Il, 2) ? "yes" : "no")
                + "\n        |I_RLOR(s0)|=" + std::to_string (Ir.size ())
                + " contains 1=" + std::string (Contains (Ir, 1) ? "yes" : "no")
                + " contains 2=" + std::string (Contains (Ir, 2) ? "yes" : "no"));
  }

  std::cout << "\n--- 9. RLOR-inspired next-hop = argmax P_succ ---\n";

  {
    LqarpConfig cfg;
    cfg.rRangeM = 5125.0;
    std::vector<LqarpNode> nodes (3);
    nodes[0] = {0, 0.0, 0.0, 80.0, 100.0};
    nodes[1] = {1, 200.0, 0.0, 120.0, 100.0};  // high SNR → P_succ at BER floor
    nodes[2] = {2, 5100.0, 0.0, 50.0, 100.0};  // SNR ~8.89 dB → lower P_succ
    dahello::RlorInspiredProtocol rl (janGate, cfg, nodes);
    const double p01 = rl.pSucc (0, 1);
    const double p02 = rl.pSucc (0, 2);
    const double r01 = rl.reward (0, 1);
    const int nh = rl.nextHop (0);
    const bool ok = p01 > p02
                    && Close (r01, p01, 0.0)
                    && nh == 1
                    && Close (p01, dahello::pSucc (rl.linkSample (0, 1).snr_db, 400, 2), 1e-18);
    Report ("9. NextHop is the highest-P_succ candidate (greedy; no Q / TD)",
            ok,
            "P_succ(s0,s1)=" + F (p01)
                + " P_succ(s0,s2)=" + F (p02)
                + " R(s0,s1)=P_succ=" + F (r01)
                + " NextHop(s0)=" + std::to_string (nh)
                + " expected=1 (s1, not the shallower s2)");
  }

  {
    LqarpConfig cfg;
    cfg.rRangeM = 6000.0;
    std::vector<LqarpNode> nodes (3);
    nodes[0] = {0, 0.0, 0.0, 80.0, 100.0};
    nodes[1] = {1, 5500.0, 0.0, 50.0, 100.0}; // January SNR 5.52 dB, fails N_SNR
    nodes[2] = {2, 200.0, 0.0, 120.0, 100.0}; // deeper, high SNR, RLOR-only hop
    dahello::RlorInspiredProtocol rl (janGate, cfg, nodes);
    LqarpProtocol lq (janGate, cfg, nodes);
    const auto Ir = rl.candidateSet (0);
    const auto Il = lq.candidateSet (0);
    const auto s01 = rl.linkSample (0, 1);
    const bool ok = !rl.inNRange (0, 1) && !rl.inNSnr (0, 1)
                    && !Contains (Ir, 1) && Contains (Ir, 2)
                    && Il.empty ()
                    && rl.nextHop (0) == 2
                    && lq.nextHop (0) == -1;
    Report ("9b. N_SNR still applied: low-SNR s1 dropped; LQARP voids on N_depth",
            ok,
            "snr(s0,s1)=" + F (s01.snr_db, 8)
                + " inNSnr_RLOR=" + std::string (rl.inNSnr (0, 1) ? "true" : "false")
                + " |I_RLOR|=" + std::to_string (Ir.size ())
                + " |I_LQARP|=" + std::to_string (Il.size ())
                + " NextHop_RLOR=" + std::to_string (rl.nextHop (0))
                + " NextHop_LQARP=" + std::to_string (lq.nextHop (0)));
  }

  // ------------------------------------------------------------------
  // 14. Fixed-periodic DA-HELLO ablation (SPEC Part 4)
  // ------------------------------------------------------------------
  std::cout << "\n--- 14. Fixed-periodic ablation: reward/Q identical, timing differs ---\n";

  {
    // Same beta_1, beta_2 as each other (test values, not a chosen sweep).
    // epsilon_0 / W_mp / W / rho / lambda are the compiled first-pass
    // defaults (2.0 / 3 / 5 / 0.6 / 1.0); this test does not override them.
    // learnFrom with no ticks: Broadcast_k timing cannot change the
    // reward/Q math (e_k=0 so N_stab passes even if periodic fired at t=0).
    dahello::DahelloConfig ev;
    ev.eIni = 100.0;
    ev.beta_1 = 0.5;
    ev.beta_2 = 0.5;
    ev.broadcastMode = dahello::BroadcastMode::EventTriggered;
    dahello::DahelloConfig per = ev;
    per.broadcastMode = dahello::BroadcastMode::FixedPeriodic;
    per.T_ctrl = 5.0;
    std::vector<LqarpNode> nodes (4);
    nodes[0] = {0, 0.0, 0.0, 10.0, 100.0};
    nodes[1] = {1, 2000.0, 0.0, 40.0, 90.0};
    nodes[2] = {2, 3500.0, 0.0, 70.0, 70.0};
    nodes[3] = {3, 4800.0, 0.0, 100.0, 50.0};
    dahello::DahelloProtocol eventP (janGate, ev, nodes);
    dahello::DahelloProtocol periodP (janGate, per, nodes);

    bool heardSplitOk = true;
    std::ostringstream md;
    md << std::scientific << std::setprecision (12);
    md << "beta_1=" << ev.beta_1 << " beta_2=" << ev.beta_2
       << " (same on both; test values, PENDING sweep)\n";
    md << "        periodic Broadcast_k(s3,s0) at t=0="
       << periodP.ekf (3, 0).broadcast_k
       << " event Broadcast_k=" << eventP.ekf (3, 0).broadcast_k
       << " (N_stab still passes: e_k=0)\n";
    const bool ctorUnheard = !eventP.zohHeard (3, 0) && periodP.zohHeard (3, 0);
    md << "        ctor heard: event(3,0)=" << (eventP.zohHeard (3, 0) ? "1" : "0")
       << " period(3,0)=" << (periodP.zohHeard (3, 0) ? "1" : "0")
       << "  (periodic t=0 HELLO copies ZOH; event bootstraps on first query)\n";

    // Bootstrap, not Broadcast_k, is what first fills the event hold.
    // After that query, c_drift matches periodic (both velocities are 0)
    // and broadcast_k stays 0.
    for (uint32_t i = 0; i < 4; ++i)
      {
        for (uint32_t j = 0; j < 4; ++j)
          {
            if (i == j)
              {
                continue;
              }
            const double c_del = periodP.reward (i, j).c_del;
            const double cdE = eventP.cDrift (i, j);
            const double cdP = periodP.cDrift (i, j);
            const double vrP = periodP.vRel (i, j);
            const double expP = c_del * (1.0 + ev.beta_2 * dahello::DahelloProtocol::sigmoid (-vrP));
            if (!eventP.zohHeard (i, j) || eventP.ekf (i, j).broadcast_k != 0
                || !periodP.zohHeard (i, j)
                || !Close (cdE, cdP, 1e-12) || !Close (cdP, expP, 1e-12)
                || !Close (vrP, 0.0, 1e-12))
              {
                heardSplitOk = false;
              }
          }
      }
    if (!ctorUnheard || eventP.bootstrapHelloCount () == 0
        || eventP.eventHelloCount () != 0)
      {
        heardSplitOk = false;
      }

    // Same kinematic information: copy true (zero) neighbor velocity onto
    // the event protocol, then rewards/Q must match. Broadcast_k timing
    // still differs.
    for (uint32_t i = 0; i < 4; ++i)
      {
        for (uint32_t j = 0; j < 4; ++j)
          {
            if (i == j)
              {
                continue;
              }
            eventP.setNeighborVelocityZoh (i, j, nodes[j].vx, nodes[j].vy, nodes[j].vz);
          }
      }
    for (uint32_t i = 0; i < 4; ++i)
      {
        eventP.learnFrom (i);
        periodP.learnFrom (i);
      }

    bool mathOk = true;
    for (uint32_t i = 0; i < 4; ++i)
      {
        const auto Ie = eventP.candidateSet (i);
        const auto Ip = periodP.candidateSet (i);
        if (Ie.size () != Ip.size () || eventP.nextHop (i) != periodP.nextHop (i))
          {
            mathOk = false;
          }
        for (uint32_t j : Ie)
          {
            const auto re = eventP.reward (i, j);
            const auto rp = periodP.reward (i, j);
            const double qe = eventP.qValue (i, j);
            const double qp = periodP.qValue (i, j);
            md << "        (" << i << "," << j << ") R_event=" << F (re.R)
               << " R_period=" << F (rp.R)
               << " Q_event=" << F (qe)
               << " Q_period=" << F (qp)
               << " c_pred=" << F (re.c_pred)
               << " c_drift_e=" << F (re.c_drift, 6)
               << " c_drift_p=" << F (rp.c_drift, 6) << "\n";
            if (!RelClose (re.R, rp.R, 1e-12, 1e-9)
                || !RelClose (qe, qp, 1e-12, 1e-9)
                || !RelClose (re.c_pred, rp.c_pred, 1e-12, 1e-12)
                || !RelClose (re.c_drift, rp.c_drift, 1e-12, 1e-12)
                || !RelClose (re.c_ene, rp.c_ene, 1e-12, 1e-12)
                || !RelClose (re.c_nei, rp.c_nei, 1e-12, 1e-12))
              {
                mathOk = false;
              }
          }
      }
    Report ("14a. bootstrap fills event holds with Broadcast_k still 0; reward/Q then match periodic",
            heardSplitOk && mathOk,
            md.str ());

    // 20 s, dt=1 s, T_ctrl=5 s. Periodic fires at t=0,5,10,15,20
    // → 5 rounds × 4 nodes = 20 packets. Event-triggered on a
    // stationary field should not persist-trigger (adaptive, ~0).
    const double dt = 1.0;
    const double Tsim = 20.0;
    const uint64_t nFireExpected = 5; // 0,5,10,15,20
    const uint64_t nNodes = 4;
    const uint64_t periodicExpected = nFireExpected * nNodes;
    uint64_t countAt4 = 0;
    uint64_t countAt5 = 0;
    for (int n = 0; n < static_cast<int> (Tsim / dt + 0.5); ++n)
      {
        eventP.tick (dt);
        periodP.tick (dt);
        if (n + 1 == 4)
          {
            countAt4 = periodP.controlTxCount ();
          }
        if (n + 1 == 5)
          {
            countAt5 = periodP.controlTxCount ();
          }
      }
    const uint64_t evCount = eventP.eventHelloCount ();
    const uint64_t perCount = periodP.eventHelloCount ();
    const uint64_t ctorCount = nNodes; // t=0
    const bool scheduleOk = countAt4 == ctorCount
                            && countAt5 == 2 * nNodes
                            && perCount == periodicExpected
                            && Close (periodP.simTime (), Tsim, 1e-12)
                            && Close (eventP.simTime (), Tsim, 1e-12);
    const bool adaptiveOk = evCount != perCount && evCount < perCount;
    Report ("14b. 20 s control-packet counts: periodic on T_ctrl grid, event adaptive",
            scheduleOk && adaptiveOk,
            "T_ctrl=5 dt=1 T_sim=20 nNodes=4"
                + std::string (" epsilon_0=") + F (ev.epsilon_0, 4)
                + " W_mp=" + std::to_string (ev.W_mp)
                + " W=" + std::to_string (ev.W)
                + std::string ("\n        periodic count=") + std::to_string (perCount)
                + " expected=" + std::to_string (periodicExpected)
                + " (5 fires × 4 nodes)"
                + "\n        periodic after t=4 s (between fires)=" + std::to_string (countAt4)
                + " expected=" + std::to_string (ctorCount)
                + "\n        periodic after t=5 s=" + std::to_string (countAt5)
                + " expected=" + std::to_string (2 * nNodes)
                + "\n        event-triggered count=" + std::to_string (evCount)
                + " (stationary ⇒ adaptive, not the fixed 20)"
                + "\n        simTime period=" + F (periodP.simTime (), 4)
                + " event=" + F (eventP.simTime (), 4));

    // SPEC 2.4: Broadcast_k=1 held for several ticks is one HELLO, at
    // the 0→1 rising edge, not one control packet per persisted tick.
    {
      dahello::DahelloConfig tcfg;
      tcfg.eIni = 100.0;
      tcfg.rRangeM = 1000.0;
      tcfg.broadcastMode = dahello::BroadcastMode::EventTriggered;
      std::vector<LqarpNode> tnodes (2);
      tnodes[0] = {0, 0.0, 0.0, 100.0, 100.0};
      tnodes[1] = {1, 100.0, 0.0, 60.0, 90.0};
      dahello::DahelloProtocol tp (janGate, tcfg, tnodes);

      tp.setBroadcastK (0, 1, 1);
      tp.flushEventControlTx ();
      const uint64_t afterRise = tp.controlTxCount ();
      tp.flushEventControlTx ();
      tp.flushEventControlTx ();
      const uint64_t afterHold = tp.controlTxCount ();
      tp.setBroadcastK (0, 1, 0);
      tp.flushEventControlTx ();
      const uint64_t afterFall = tp.controlTxCount ();
      tp.setBroadcastK (0, 1, 1);
      tp.flushEventControlTx ();
      const uint64_t afterSecond = tp.controlTxCount ();
      Report ("14c. persist Broadcast_k=1 several ticks: one ctrl TX per rising edge",
              afterRise == 1 && afterHold == 1 && afterFall == 1 && afterSecond == 2,
              "after 0→1=" + std::to_string (afterRise)
                  + " after two held ticks=" + std::to_string (afterHold)
                  + " after 1→0=" + std::to_string (afterFall)
                  + " after second 0→1=" + std::to_string (afterSecond)
                  + " expected 1,1,1,2");
    }
  }

  std::cout << "\n--- 21. SPEC 1.5 detour (documented approximation) ---\n";

  {
    FixedSnrGate fake (20.0, 250.0);
    LqarpConfig cfg;
    cfg.rRangeM = 250.0;
    cfg.eIni = 100.0;
    std::vector<LqarpNode> nodes (3);
    nodes[0] = {0, 0.0, 0.0, 100.0, 100.0};
    nodes[1] = {1, 200.0, 0.0, 150.0, 100.0}; // deeper: not in I(s0)
    nodes[2] = {2, 400.0, 0.0, 20.0, 100.0};  // sink, out of s0 range
    LqarpProtocol proto (fake, cfg, nodes);
    const double d01 = ChannelGate::distance3D (0, 0, 100, 200, 0, 150);
    const double d02 = ChannelGate::distance3D (0, 0, 100, 400, 0, 20);
    const double d12 = ChannelGate::distance3D (200, 0, 150, 400, 0, 20);
    const auto I0 = proto.candidateSet (0);
    const auto D0 = proto.detourSet (0);
    const std::vector<uint32_t> vis = {0};
    const auto T = proto.detourTargets (0, 2, vis);
    proto.learnFrom (0, T);
    const int nh = proto.nextHop (0, T);
    const bool ok = I0.empty () && Contains (D0, 1) && !Contains (D0, 2)
                    && T.size () == 1 && Contains (T, 1) && nh == 1
                    && d01 < 250.0 && d02 > 250.0 && d12 < 250.0
                    && proto.candidateSet (1).size () > 0;
    Report ("21a. empty I(s0) but s1 has valid I → DETOUR to s1, not VOID",
            ok,
            "d01=" + F (d01, 4) + " d02=" + F (d02, 4) + " d12=" + F (d12, 4)
                + " |I(s0)|=" + std::to_string (I0.size ())
                + " D(s0) has 1=" + std::string (Contains (D0, 1) ? "yes" : "no")
                + " |T|=" + std::to_string (T.size ())
                + " nextHopDetour=" + std::to_string (nh)
                + " |I(s1)|=" + std::to_string (proto.candidateSet (1).size ()));

    dahello::DahelloConfig ecfg;
    ecfg.rRangeM = 250.0;
    ecfg.eIni = 100.0;
    ecfg.beta_1 = 0.0;
    ecfg.beta_2 = 0.0;
    ecfg.epsilon_0 = 1.0e12;
    dahello::DahelloProtocol blocked (fake, ecfg, nodes);
    blocked.setBootstrapSuppressed (1, true);
    const auto TeBlocked = blocked.detourTargets (0, 2, vis);
    const bool neverBoot = TeBlocked.empty () && blocked.candidateSet (0).empty ()
                           && !blocked.zohHeard (0, 1);
    dahello::DahelloProtocol ep (fake, ecfg, nodes);
    const auto Te = ep.detourTargets (0, 2, vis);
    ep.learnFrom (0, Te);
    const int nhe = ep.nextHop (0, Te);
    Report ("21a-dahello. suppressed bootstrap => empty detour; bootstrap matches LQARP",
            neverBoot && ep.candidateSet (0).empty () && nhe == 1 && Te.size () == 1
                && ep.zohHeard (0, 1) && ep.ekf (0, 1).broadcast_k == 0,
            "blocked |T|=" + std::to_string (TeBlocked.size ())
                + " nextHop_DAHELLO=" + std::to_string (nhe)
                + " |T|=" + std::to_string (Te.size ()));
  }

  {
    FixedSnrGate fake (20.0, 250.0);
    LqarpConfig cfg;
    cfg.rRangeM = 250.0;
    cfg.eIni = 100.0;
    std::vector<LqarpNode> nodes (2);
    nodes[0] = {0, 0.0, 0.0, 100.0, 100.0};
    nodes[1] = {1, 200.0, 0.0, 150.0, 100.0}; // deeper dead end, not the sink
    LqarpProtocol proto (fake, cfg, nodes);
    const std::vector<uint32_t> vis = {0};
    const auto T = proto.detourTargets (0, 99, vis); // sink id 99 absent
    const bool ok = proto.candidateSet (0).empty ()
                    && proto.detourSet (0).size () == 1
                    && T.empty ()
                    && proto.nextHop (0, T) == -1;
    Report ("21b. only reachable node is a dead end (not sink) → VOID, no search",
            ok,
            "|I(s0)|=" + std::to_string (proto.candidateSet (0).size ())
                + " |D(s0)|=" + std::to_string (proto.detourSet (0).size ())
                + " |T|=" + std::to_string (T.size ())
                + " nextHop=" + std::to_string (proto.nextHop (0, T)));
  }

  {
    FixedSnrGate fake (20.0, 190.0);
    LqarpConfig cfg;
    cfg.rRangeM = 190.0;
    cfg.eIni = 100.0;
    std::vector<LqarpNode> nodes (4);
    nodes[0] = {0, 0.0, 0.0, 100.0, 50.0};
    nodes[1] = {1, 50.0, 0.0, 150.0, 100.0}; // visited, high energy
    nodes[2] = {2, 100.0, 0.0, 150.0, 10.0}; // unvisited, low energy
    nodes[3] = {3, 220.0, 0.0, 10.0, 100.0}; // sink, in range of s2 not s0
    LqarpProtocol proto (fake, cfg, nodes);
    const std::vector<uint32_t> vis = {0, 1};
    const auto T = proto.detourTargets (0, 3, vis);
    proto.learnFrom (0, T);
    const int nh = proto.nextHop (0, T);
    const auto rw1 = proto.reward (0, 1);
    const auto rw2 = proto.reward (0, 2);
    const bool ok = proto.candidateSet (0).empty ()
                    && !Contains (T, 1) && Contains (T, 2) && nh == 2
                    && rw1.R != rw2.R; // s1 would look different; must still be excluded
    Report ("21c. loop prevention: visited s1 excluded even if reward differs from s2",
            ok,
            "|T|=" + std::to_string (T.size ())
                + " contains visited 1=" + std::string (Contains (T, 1) ? "yes" : "no")
                + " nextHop=" + std::to_string (nh)
                + " R(s0,s1)=" + F (rw1.R, 6) + " R(s0,s2)=" + F (rw2.R, 6));
  }

  {
    FixedSnrGate fake (20.0, 180.0);
    LqarpConfig cfg;
    cfg.rRangeM = 180.0;
    cfg.eIni = 100.0;
    // Chain 0-1-2-3-4-sink(5). Adjacent ~100 m, r_range=180 so skip-one
    // is out. Depths increase along 0..4 so every hop except 4→sink is
    // a depth-violating detour. Cap=3 ⇒ VOID at node 3, never reach 4.
    std::vector<LqarpNode> nodes (6);
    nodes[0] = {0, 0.0, 0.0, 100.0, 100.0};
    nodes[1] = {1, 100.0, 0.0, 110.0, 100.0};
    nodes[2] = {2, 200.0, 0.0, 120.0, 100.0};
    nodes[3] = {3, 300.0, 0.0, 130.0, 100.0};
    nodes[4] = {4, 400.0, 0.0, 140.0, 100.0};
    nodes[5] = {5, 500.0, 0.0, 10.0, 100.0};
    LqarpProtocol proto (fake, cfg, nodes);
    uint32_t at = 0;
    std::vector<uint32_t> vis = {0};
    uint32_t detours = 0;
    int lastNh = -1;
    std::string path = "0";
    bool voided = false;
    for (int step = 0; step < 8; ++step)
      {
        std::vector<uint32_t> I;
        for (uint32_t j : proto.candidateSet (at))
          {
            if (!LqarpProtocol::containsId (vis, j))
              {
                I.push_back (j);
              }
          }
        int nh = -1;
        bool usedDetour = false;
        if (!I.empty ())
          {
            proto.learnFrom (at, I);
            nh = proto.nextHop (at, I);
          }
        else if (detours >= dahello::kMaxDetourHops)
          {
            voided = true;
            break;
          }
        else
          {
            const auto T = proto.detourTargets (at, 5, vis);
            if (T.empty ())
              {
                voided = true;
                break;
              }
            proto.learnFrom (at, T);
            nh = proto.nextHop (at, T);
            usedDetour = true;
          }
        if (nh < 0)
          {
            voided = true;
            break;
          }
        lastNh = nh;
        if (usedDetour)
          {
            ++detours;
          }
        at = static_cast<uint32_t> (nh);
        vis.push_back (at);
        path += "->" + std::to_string (at);
        if (at == 5)
          {
            break;
          }
      }
    const bool ok = voided && detours == dahello::kMaxDetourHops && at == 3
                    && lastNh == 3 && !LqarpProtocol::containsId (vis, 4);
    Report ("21d. detour cap=3: chain needing 4+ detours VOIDs after 3rd detour hop",
            ok,
            "path=" + path + " detours=" + std::to_string (detours)
                + " cap=" + std::to_string (dahello::kMaxDetourHops)
                + " voided=" + std::string (voided ? "true" : "false")
                + " stopped_at=" + std::to_string (at)
                + " (must not reach node 4 or sink 5)");
  }

  std::cout << "\n--- 22. Scenario III eddy mean flow (kinematics only) ---\n";
  {
    dahello::EddyConfig e;
    e.cx = 3000.0;
    e.cy = 3000.0;
    e.V = 2.0;
    e.R = 2000.0;
    double mx = 1, my = 1, mz = 1;
    dahello::eddyMeanFlow (e.cx, e.cy, e, mx, my, mz);
    Report ("22a. eddy at the centre is the zero vector (no singularity)",
            Close (mx, 0.0, 1e-15) && Close (my, 0.0, 1e-15) && Close (mz, 0.0, 1e-15),
            "v=(" + F (mx, 4) + "," + F (my, 4) + "," + F (mz, 4) + ")");

    dahello::eddyMeanFlow (e.cx + e.R, e.cy, e, mx, my, mz);
    const double speedCore = std::sqrt (mx * mx + my * my);
    Report ("22b. east of centre at r=R: |v|=V, CCW so +y",
            Close (speedCore, e.V, 1e-12) && Close (mx, 0.0, 1e-12)
                && Close (my, e.V, 1e-12) && Close (mz, 0.0, 1e-15),
            "v=(" + F (mx, 6) + "," + F (my, 6) + "," + F (mz, 6) + ")");

    dahello::eddyMeanFlow (e.cx + 2.0 * e.R, e.cy, e, mx, my, mz);
    const double speedOut = std::sqrt (mx * mx + my * my);
    Report ("22c. east of centre at r=2R: |v| capped at V (Rankine outer)",
            Close (speedOut, e.V, 1e-12) && Close (mz, 0.0, 1e-15),
            "|v|=" + F (speedOut, 6) + " V=" + F (e.V, 6));

    dahello::eddyMeanFlow (e.cx, e.cy + e.R, e, mx, my, mz);
    Report ("22d. north of centre at r=R: CCW so -x",
            Close (mx, -e.V, 1e-12) && Close (my, 0.0, 1e-12),
            "v=(" + F (mx, 6) + "," + F (my, 6) + ")");

    double ax, ay, az, bx, by, bz;
    dahello::eddyMeanFlow (2000.0, 3000.0, e, ax, ay, az);
    dahello::eddyMeanFlow (4000.0, 3000.0, e, bx, by, bz);
    const double vrel = std::sqrt ((ax - bx) * (ax - bx) + (ay - by) * (ay - by));
    Report ("22e. two AUVs on opposite sides have nonzero mean v_rel",
            vrel > 1.0 && Close (az, 0.0, 1e-15) && Close (bz, 0.0, 1e-15),
            "|v_rel|=" + F (vrel, 6) + " (Scenario II this is identically 0)");

    dahello::GaussMarkovConfig gcfg;
    gcfg.zeta = 0.95;
    gcfg.dt = 1.0;
    gcfg.mu_x = 2.0;
    gcfg.mu_y = 0.0;
    gcfg.mu_z = 0.0;
    gcfg.sigma_x = gcfg.sigma_y = gcfg.sigma_z = 0.0;
    dahello::GaussMarkovState st;
    st.vx = 2.0;
    std::mt19937 rng (1);
    dahello::gaussMarkovStep (st, gcfg, rng);
    Report ("22f. sigma=0 and v=mu: Gauss-Markov holds the mean (no wander)",
            Close (st.vx, 2.0, 1e-12) && Close (st.vy, 0.0, 1e-12)
                && Close (gcfg.sigma_z, 0.0, 1e-15),
            "vx=" + F (st.vx, 6) + " compiled sigma_z=" + F (gcfg.sigma_z, 4));

    // Solid-body core: v = ω × r ⇒ (v_b − v_a) ⊥ (p_b − p_a).
    double vRad = 1, vTan = 0, vRel = 0;
    double avx, avy, avz, bvx, bvy, bvz;
    dahello::eddyMeanFlow (2500.0, 3000.0, e, avx, avy, avz);
    dahello::eddyMeanFlow (3500.0, 3000.0, e, bvx, bvy, bvz);
    dahello::vRelRadialTangential (2500.0, 3000.0, 50.0, avx, avy, avz,
                                 3500.0, 3000.0, 50.0, bvx, bvy, bvz,
                                 vRad, vTan, vRel);
    Report ("22g. two core points: mean-flow v_rel is purely tangential (v·r_ij=0)",
            Close (vRad, 0.0, 1e-12) && vTan > 0.5 && Close (vRel, vTan, 1e-12)
                && Close (avz, 0.0, 1e-15) && Close (bvz, 0.0, 1e-15),
            "|v_rad|=" + F (vRad, 8) + " |v_tan|=" + F (vTan, 6)
                + " |v_rel|=" + F (vRel, 6));

    dahello::vRelRadialTangential (0.0, 0.0, 0.0, 0.0, 0.0, 0.0,
                                 10.0, 0.0, 0.0, 3.0, 4.0, 0.0,
                                 vRad, vTan, vRel);
    Report ("22h. vRelRadialTangential: LOS (3,0) plus perp (0,4)",
            Close (vRad, 3.0, 1e-12) && Close (vTan, 4.0, 1e-12)
                && Close (vRel, 5.0, 1e-12),
            "|v_rad|=" + F (vRad, 6) + " |v_tan|=" + F (vTan, 6)
                + " |v_rel|=" + F (vRel, 6));

    const double closing = dahello::signedRangeRate (0, 0, 0, 1, 0, 0,
                                                   10, 0, 0, -1, 0, 0);
    const double opening = dahello::signedRangeRate (0, 0, 0, -1, 0, 0,
                                                   10, 0, 0, 1, 0, 0);
    Report ("22i. signedRangeRate: approaching is negative (SPEC 2.1 closing)",
            Close (closing, -2.0, 1e-12) && Close (opening, 2.0, 1e-12),
            "closing=" + F (closing, 6) + " opening=" + F (opening, 6));
  }

  std::cout << "\n--- 23. Suppressed HELLO / discovery: candidate set diverges ---\n";
  {
    FixedSnrGate gate (30.0, 400.0);
    dahello::DahelloConfig cfg;
    cfg.rRangeM = 400.0;
    cfg.eIni = 100.0;
    cfg.epsilon_0 = 1.0e12;
    cfg.lambda = 0.0;
    cfg.beta_1 = 0.0;
    cfg.beta_2 = 0.0;
    cfg.broadcastMode = dahello::BroadcastMode::FixedPeriodic;
    cfg.T_ctrl = 5.0;
    cfg.dt = 1.0;
    std::vector<LqarpNode> nodes (2);
    nodes[0] = {0, 0.0, 0.0, 100.0, 100.0};
    nodes[1] = {1, 100.0, 0.0, 40.0, 90.0};
    dahello::DahelloProtocol baseline (gate, cfg, nodes);
    dahello::DahelloProtocol suppressed (gate, cfg, nodes);
    // t=0 periodic HELLO has already copied node 1. Further copies
    // from node 1 are dropped on the suppressed protocol only.
    suppressed.setHelloSuppressed (1, true);

    std::ostringstream trace;
    trace << std::fixed << std::setprecision (1);
    trace << "t live_x live_in_range baseline_has_1 suppressed_has_1\n";
    bool matchedUntilHello = true;
    bool divergedAfter = false;
    auto has1 = [] (const std::vector<uint32_t> &I) {
      return std::find (I.begin (), I.end (), 1u) != I.end ();
    };
    const bool t0base = has1 (baseline.candidateSet (0));
    const bool t0sup = has1 (suppressed.candidateSet (0));
    trace << "0 100.0 1 " << (t0base ? 1 : 0) << " " << (t0sup ? 1 : 0) << "\n";
    if (!t0base || !t0sup)
      {
        matchedUntilHello = false;
      }
    for (int step = 1; step <= 8; ++step)
      {
        const double x = 100.0 + 80.0 * static_cast<double> (step);
        baseline.setKinematics (1, x, 0.0, 40.0, 1.0, 0.0, 0.0);
        suppressed.setKinematics (1, x, 0.0, 40.0, 1.0, 0.0, 0.0);
        baseline.tick (1.0);
        suppressed.tick (1.0);
        const double d = dahello::ChannelGate::distance3D (0.0, 0.0, 100.0, x, 0.0, 40.0);
        const bool liveIn = d < 400.0;
        const bool bHas = has1 (baseline.candidateSet (0));
        const bool sHas = has1 (suppressed.candidateSet (0));
        trace << step << " " << x << " " << (liveIn ? 1 : 0) << " "
              << (bHas ? 1 : 0) << " " << (sHas ? 1 : 0) << "\n";
        // Next periodic ingest is at t=5 s. Before that both holds
        // are the t=0 pose, so both still list node 1.
        if (step < 5 && (!bHas || !sHas))
          {
            matchedUntilHello = false;
          }
        if (step >= 5 && bHas != sHas)
          {
            divergedAfter = true;
          }
      }
    const bool baseDropped = !has1 (baseline.candidateSet (0));
    const bool supKept = has1 (suppressed.candidateSet (0));
    Report ("23a. suppressing node 1's later HELLOs leaves I(s0) stale while the baseline drops it",
            matchedUntilHello && divergedAfter && baseDropped && supKept,
            trace.str ());

    LqarpConfig lcfg;
    lcfg.rRangeM = 400.0;
    lcfg.eIni = 100.0;
    std::vector<LqarpNode> ln (2);
    ln[0] = {0, 0.0, 0.0, 100.0, 100.0};
    ln[1] = {1, 100.0, 0.0, 40.0, 90.0};
    LqarpProtocol discovered (gate, lcfg, ln);
    LqarpProtocol never (gate, lcfg, ln);
    never.setDiscoverySuppressed (1, true);
    const bool d0 = has1 (discovered.candidateSet (0));
    const bool n0 = has1 (never.candidateSet (0));
    discovered.setKinematics (1, 900.0, 0.0, 40.0, 0.0, 0.0, 0.0);
    never.setKinematics (1, 900.0, 0.0, 40.0, 0.0, 0.0, 0.0);
    const bool d1 = has1 (discovered.candidateSet (0));
    const bool n1 = has1 (never.candidateSet (0));
    const double liveD = dahello::ChannelGate::distance3D (0, 0, 100, 900, 0, 40);
    Report ("23b. LQARP freezes the first discovery; a suppressed neighbor never enters I",
            d0 && !n0 && d1 && !n1 && liveD > 400.0,
            "at contact discovered_has_1=" + std::string (d0 ? "yes" : "no")
                + " suppressed_has_1=" + std::string (n0 ? "yes" : "no")
                + "\n        after x=900 (live d=" + F (liveD, 1)
                + ") discovered_has_1=" + std::string (d1 ? "yes" : "no")
                + " suppressed_has_1=" + std::string (n1 ? "yes" : "no"));

    // Event-triggered: the hold is written only inside updatePair when
    // broadcast_k == 1. W=1, rho=1 so one exceed fires on that tick.
    // SNR still clears 8.68 dB at x=800, so a drop is the held range.
    RangeSlopeGate slope (50.0, 0.04, 400.0);
    dahello::DahelloConfig ecfg;
    ecfg.rRangeM = 400.0;
    ecfg.eIni = 100.0;
    ecfg.beta_1 = 0.0;
    ecfg.beta_2 = 0.0;
    ecfg.epsilon_0 = 2.0;
    ecfg.lambda = 1.0;
    ecfg.W = 1;
    ecfg.rho = 1.0;
    ecfg.broadcastMode = dahello::BroadcastMode::EventTriggered;
    std::vector<LqarpNode> en (2);
    en[0] = {0, 0.0, 0.0, 100.0, 100.0};
    en[1] = {1, 100.0, 0.0, 40.0, 90.0};
    dahello::DahelloProtocol eBase (slope, ecfg, en);
    dahello::DahelloProtocol eSup (slope, ecfg, en);
    eBase.setNeighborVelocityZoh (0, 1, 0.0, 0.0, 0.0);
    eSup.setNeighborVelocityZoh (0, 1, 0.0, 0.0, 0.0);
    const bool armed = has1 (eBase.candidateSet (0)) && has1 (eSup.candidateSet (0))
                       && Close (eBase.ekf (0, 1).x_j_zoh, 100.0, 1e-9)
                       && Close (eSup.ekf (0, 1).x_j_zoh, 100.0, 1e-9);
    eSup.setHelloSuppressed (1, true);
    eBase.setKinematics (1, 800.0, 0.0, 40.0, 0.0, 0.0, 0.0);
    eSup.setKinematics (1, 800.0, 0.0, 40.0, 0.0, 0.0, 0.0);
    std::ostringstream etrace;
    etrace << std::fixed << std::setprecision (1);
    etrace << "tick bc_k e_tr x_base x_sup has_base has_sup\n";
    bool sawFire = false;
    // Received-only z_k: node 0 only measures node 1 when it demodulates
    // a packet from node 1. Model node 1 sending one packet per tick that
    // node 0 decodes (e.g. data traffic); HELLO suppression is separate.
    bool rxBoth = true;
    for (int step = 1; step <= 12; ++step)
      {
        rxBoth = eBase.noteReception (0, 1) && rxBoth;
        rxBoth = eSup.noteReception (0, 1) && rxBoth;
        eBase.tick (1.0);
        eSup.tick (1.0);
        const auto &eb = eBase.ekf (0, 1);
        const auto &es = eSup.ekf (0, 1);
        const bool bHas = has1 (eBase.candidateSet (0));
        const bool sHas = has1 (eSup.candidateSet (0));
        if (eb.broadcast_k == 1)
          {
            sawFire = true;
          }
        etrace << step << " " << eb.broadcast_k << " " << eb.eTrigger
               << " " << eb.x_j_zoh << " " << es.x_j_zoh
               << " " << (bHas ? 1 : 0) << " " << (sHas ? 1 : 0) << "\n";
      }
    const bool baseFar = eBase.ekf (0, 1).x_j_zoh > 400.0;
    const bool supNear = Close (eSup.ekf (0, 1).x_j_zoh, 100.0, 1e-6);
    // The open-loop forecast keeps |e_trigger| large, so Broadcast_k
    // stays 1 and N_stab closes both sets. Drop the flag after the
    // write; the held range is then what splits I(s0).
    eBase.setBroadcastK (0, 1, 0);
    eSup.setBroadcastK (0, 1, 0);
    const bool baseDroppedE = !has1 (eBase.candidateSet (0));
    const bool supKeptE = has1 (eSup.candidateSet (0));
    const double snrFar = slope.queryLink (0.0, 0.0, 100.0, 800.0, 0.0, 40.0).snr_db;
    Report ("23c. event-triggered: broadcast_k writes the new pose; a suppressed node keeps x=100",
            armed && sawFire && baseFar && supNear && baseDroppedE && supKeptE
                && snrFar > 8.68 && rxBoth,
            "armed=" + std::string (armed ? "yes" : "no")
                + " rx_from_1_each_tick=" + std::string (rxBoth ? "yes" : "no")
                + " saw Broadcast_k=1=" + std::string (sawFire ? "yes" : "no")
                + " x_base=" + F (eBase.ekf (0, 1).x_j_zoh, 1)
                + " x_sup=" + F (eSup.ekf (0, 1).x_j_zoh, 1)
                + " I_base_has_1=" + std::string (baseDroppedE ? "no" : "yes")
                + " I_sup_has_1=" + std::string (supKeptE ? "yes" : "no")
                + " snr(800m)=" + F (snrFar, 2)
                + "\n        " + etrace.str ());

    // 23c'. Received-only z_k: with no packet from node 1 there is no
    // measurement, so the trigger cannot fire and the hold is not written,
    // however far node 1 has actually moved. Both tracks only predict.
    {
      dahello::DahelloProtocol eSilent (slope, ecfg, en);
      eSilent.setNeighborVelocityZoh (0, 1, 0.0, 0.0, 0.0);
      const bool armedS = has1 (eSilent.candidateSet (0))
                          && Close (eSilent.ekf (0, 1).x_j_zoh, 100.0, 1e-9);
      const uint64_t rx0 = eSilent.receptionCount ();
      const uint64_t upd0 = eSilent.measuredUpdates ();
      eSilent.setKinematics (1, 800.0, 0.0, 40.0, 0.0, 0.0, 0.0);
      bool firedS = false;
      for (int step = 1; step <= 12; ++step)
        {
          eSilent.tick (1.0);
          if (eSilent.ekf (0, 1).broadcast_k == 1)
            {
              firedS = true;
            }
        }
      const bool noRx = eSilent.receptionCount () == rx0;
      const bool noUpd = eSilent.measuredUpdates () == upd0;
      const bool holdKept = Close (eSilent.ekf (0, 1).x_j_zoh, 100.0, 1e-9);
      const bool eZero = eSilent.ekf (0, 1).eTrigger == 0.0;
      Report ("23c'. received-only z_k: no packet from node 1 means no z_k, no fire, no pose write",
              armedS && !firedS && noRx && noUpd && holdKept && eZero,
              "armed=" + std::string (armedS ? "yes" : "no")
                  + " fired=" + std::string (firedS ? "yes" : "no")
                  + " receptions=" + std::to_string (eSilent.receptionCount () - rx0)
                  + " z_updates=" + std::to_string (eSilent.measuredUpdates () - upd0)
                  + " x_j_zoh=" + F (eSilent.ekf (0, 1).x_j_zoh, 1)
                  + " e_trigger=" + F (eSilent.ekf (0, 1).eTrigger, 2));
    }

    // 23e. Step 5: the energy beacon's 32-bit quantizer must be finite.
    // (1u << 32) was undefined behaviour -> levels 0 -> NaN residual.
    {
      dahello::DahelloConfig qc = ecfg;
      qc.energyRefreshS = 5.0;
      qc.W = 5;
      qc.rho = 0.6;
      bool allOk = true;
      std::string detail;
      for (int bits : {8, 16, 31, 32})
        {
          qc.energyQuantBits = bits;
          dahello::DahelloProtocol pq (slope, qc, en);
          (void) pq.candidateSet (0); // bootstrap 0<-1
          pq.setEnergy (1, 43.215);
          for (int k = 0; k < 5; ++k)
            {
              pq.tick (1.0);
            }
          const double held = pq.ekf (0, 1).energy_j_zoh;
          const double tol = ecfg.eIni / (std::ldexp (1.0, bits) - 1.0);
          const bool ok = std::isfinite (held) && std::fabs (held - 43.215) <= tol;
          allOk = allOk && ok;
          detail += " bits=" + std::to_string (bits) + ":held=" + F (held, 6)
                    + (ok ? "" : "(BAD)");
        }
      Report ("23e. energy beacon quantizer is finite and within one step at 8/16/31/32 bits",
              allOk, detail);
    }

    // 23f. Step 5: a Fixed-Periodic HELLO writes i's hold for j only if i
    // demodulates it. Node 2 is far outside the SNR gate of node 0.
    {
      dahello::DahelloConfig pc = ecfg;
      pc.broadcastMode = dahello::BroadcastMode::FixedPeriodic;
      std::vector<LqarpNode> pn (3);
      pn[0] = {0, 0.0, 0.0, 100.0, 100.0};
      pn[1] = {1, 100.0, 0.0, 40.0, 90.0};
      pn[2] = {2, 5000.0, 0.0, 40.0, 90.0};
      dahello::DahelloProtocol gated (slope, pc, pn);
      pc.helloRxGate = false;
      dahello::DahelloProtocol flood (slope, pc, pn);
      const double snr2 = slope.queryLink (5000.0, 0.0, 40.0, 0.0, 0.0, 100.0).snr_db;
      const bool near = gated.zohHeard (0, 1);
      const bool farGated = gated.zohHeard (0, 2);
      const bool farFlood = flood.zohHeard (0, 2);
      Report ("23f. periodic HELLO is reception-gated: in-range neighbour written, out-of-SNR node not",
              near && !farGated && farFlood && !(snr2 > 8.68),
              "near_heard=" + std::string (near ? "yes" : "no")
                  + " far_heard_gated=" + std::string (farGated ? "yes" : "no")
                  + " far_heard_ungated=" + std::string (farFlood ? "yes" : "no")
                  + " snr(5000m)=" + F (snr2, 1));
    }

    // 23g. Step 7: a pair admitted on its held pose but whose LIVE sample is
    // outside the Bellhop envelope must leave the candidate set, because the
    // reward reads that live sample and c_lin is non-finite there (which the
    // reward turns into R = +inf, the most attractive score available).
    {
      EnvelopeGate env (50.0, 0.04, 400.0, 6000.0);
      LqarpConfig ec2;
      ec2.rRangeM = 400.0;
      ec2.eIni = 100.0;
      std::vector<LqarpNode> n2 (2);
      n2[0] = {0, 0.0, 0.0, 100.0, 100.0};
      n2[1] = {1, 100.0, 0.0, 40.0, 90.0};

      // The live sample out there really is non-finite in c_lin: that is the
      // pathology the guard removes, asserted rather than assumed.
      const dahello::ChannelSample far = env.queryLink (0.0, 0.0, 100.0, 9000.0, 0.0, 40.0);
      const double cLinFar = dahello::cLin (far.snr_db, 400, 2);
      const bool pathology = !far.valid && !std::isfinite (cLinFar);

      LqarpProtocol lq (env, ec2, n2);
      const bool lqIn = Contains (lq.candidateSet (0), 1);   // bootstrap at x=100
      lq.setKinematics (1, 9000.0, 0.0, 40.0, 0.0, 0.0, 0.0);
      const bool lqHeldStillPasses = lq.inNRange (0, 1);     // held pose unchanged
      const bool lqOut = !Contains (lq.candidateSet (0), 1);
      lq.learnFrom (0);
      const bool lqQFinite = std::isfinite (lq.qValue (0, 1));

      dahello::DahelloConfig ec3;
      static_cast<LqarpConfig &> (ec3) = ec2;
      ec3.beta_1 = 0.0;
      ec3.beta_2 = 0.0;
      dahello::DahelloProtocol el (env, ec3, n2);
      const bool elIn = Contains (el.candidateSet (0), 1);
      el.setKinematics (1, 9000.0, 0.0, 40.0, 0.0, 0.0, 0.0);
      const bool elHeldStillPasses = el.inNRange (0, 1);
      const bool elOut = !Contains (el.candidateSet (0), 1);
      el.learnFrom (0);
      const bool elQFinite = std::isfinite (el.qValue (0, 1));

      Report ("23g. live out-of-envelope pair leaves the candidate set (LQARP and DA-HELLO); Q stays finite",
              pathology && lqIn && lqHeldStillPasses && lqOut && lqQFinite
                  && elIn && elHeldStillPasses && elOut && elQFinite,
              "live_sample_valid=" + std::string (far.valid ? "yes" : "no")
                  + " c_lin(live)=" + F (cLinFar, 3)
                  + " | LQARP in_set_before=" + std::string (lqIn ? "yes" : "no")
                  + " held_still_in_range=" + std::string (lqHeldStillPasses ? "yes" : "no")
                  + " dropped_after=" + std::string (lqOut ? "yes" : "no")
                  + " Q_finite=" + std::string (lqQFinite ? "yes" : "no")
                  + " | DA-HELLO in_set_before=" + std::string (elIn ? "yes" : "no")
                  + " held_still_in_range=" + std::string (elHeldStillPasses ? "yes" : "no")
                  + " dropped_after=" + std::string (elOut ? "yes" : "no")
                  + " Q_finite=" + std::string (elQFinite ? "yes" : "no"));
    }

    // Bootstrap itself suppressed: the pair is never a candidate, which
    // is not the same as 23c (bootstrap or HELLO happened, then went stale).
    dahello::DahelloProtocol fresh (slope, ecfg, en);
    dahello::DahelloProtocol noBoot (slope, ecfg, en);
    noBoot.setBootstrapSuppressed (1, true);
    const bool freshHas = has1 (fresh.candidateSet (0));
    const bool noHas = has1 (noBoot.candidateSet (0));
    fresh.setKinematics (1, 800.0, 0.0, 40.0, 0.0, 0.0, 0.0);
    noBoot.setKinematics (1, 800.0, 0.0, 40.0, 0.0, 0.0, 0.0);
    const bool freshStale = has1 (fresh.candidateSet (0))
                            && Close (fresh.ekf (0, 1).x_j_zoh, 100.0, 1e-6)
                            && fresh.ekf (0, 1).broadcast_k == 0;
    const bool neverAdmitted = !has1 (noBoot.candidateSet (0)) && !noBoot.zohHeard (0, 1)
                               && noBoot.bootstrapHelloCount () == 0
                               && noBoot.eventHelloCount () == 0;
    Report ("23d. suppressing bootstrap itself keeps the pair out of I; a bootstrap then goes stale",
            freshHas && !noHas && fresh.zohHeard (0, 1) && fresh.bootstrapHelloCount () >= 1
                && freshStale && neverAdmitted,
            "at contact fresh_has_1=" + std::string (freshHas ? "yes" : "no")
                + " suppressed_has_1=" + std::string (noHas ? "yes" : "no")
                + " bootstrap_hellos=" + std::to_string (fresh.bootstrapHelloCount ())
                + "\n        after x=800 fresh_has_1="
                + std::string (freshStale ? "yes" : "no")
                + " x_fresh=" + F (fresh.ekf (0, 1).x_j_zoh, 1)
                + " suppressed_heard="
                + std::string (noBoot.zohHeard (0, 1) ? "yes" : "no"));
  }

  std::cout << "\nSummary: PASS=" << g_pass << " FAIL=" << g_fail << "\n";
  return (g_fail == 0) ? 0 : 1;
}

} // namespace

#endif // DAHELLO_LQARP_UNIT_CHECK_COMMON_H
