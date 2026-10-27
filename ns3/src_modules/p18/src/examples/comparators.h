// p9 (2026-10-16): pure decision logic of the missing comparators B6, B8, B9
// (strengthening_plan.md §3 table rows B6/B8, §2.4; strengthening_prereg.md:59, :176, :217-219).
// No ns-3, no simulator state: inputs are the actor's OWN pose/timers and its OWN held-table
// freshness bit only. The scenario wires these into the medium (scenario-porta.inc).
#ifndef DAHELLO_COMPARATORS_H
#define DAHELLO_COMPARATORS_H
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace cmp {

// ---- Reactive first-contact reply (plan §2.4; prereg :59 "jitter U[0, 2 s], at most one
// per 5 s per node, cancelled by piggyback"). Shared by B8 and B9.
struct FcState
{
  bool pending = false;
  uint64_t token = 0;      // bumps on every schedule/cancel; a fired event must match it
  double lastTx = -1e18;   // time of the last reply actually released (rate limit)
};
// r decoded a frame from s. senderFresh = r already holds a record of s (p10: never-heard-only;
// the caller passes MarvRecord::heard, which is never cleared by T_nb expiry).
// Returns the release time, or NaN when no reply is scheduled (known sender, or one is
// already pending: aggregation, one reply serves every newcomer heard before it fires).
inline double
FcOnDecode (FcState &st, bool senderFresh, double now, double u01, double jitterS, double gapS)
{
  if (senderFresh || st.pending)
    {
      return std::numeric_limits<double>::quiet_NaN ();
    }
  double t = now + u01 * jitterS;
  t = std::max (t, st.lastTx + gapS); // rate limit: at most one per gapS per node
  st.pending = true;
  ++st.token;
  return t;
}
// The scheduled event with token tok fires at now. True = release the HELLO.
inline bool
FcFire (FcState &st, uint64_t tok, double now)
{
  if (!st.pending || tok != st.token)
    {
      return false;
    }
  st.pending = false;
  st.lastTx = now;
  return true;
}
// The node starts transmitting any frame that carries its own state (piggyback).
inline bool
FcCancel (FcState &st)
{
  if (!st.pending)
    {
      return false;
    }
  st.pending = false;
  ++st.token;
  return true;
}

// ---- B6 distance/kinematic trigger (plan §3 row B6: "send when moved > d_th or depth
// changed > z_th since the last HELLO, or after T_max"). Own pose vs own pose at the
// last HELLO this node sent. Returns 0 none, 1 moved > d_th (3-D), 2 |dz| > z_th,
// 3 T_max elapsed, -1 non-finite or implausible (|coord| > 1e7) input = guard.
inline int
B6Trigger (double x, double y, double z, double ax, double ay, double az, double tLast,
           double now, double dTh, double zTh, double tMax)
{
  const double v[6] = {x, y, z, ax, ay, az};
  for (double c : v)
    {
      if (!std::isfinite (c) || std::fabs (c) > 1e7)
        {
          return -1;
        }
    }
  const double dx = x - ax, dy = y - ay, dz = z - az;
  if (std::sqrt (dx * dx + dy * dy + dz * dz) > dTh)
    {
      return 1;
    }
  if (std::fabs (dz) > zTh)
    {
      return 2;
    }
  if (now - tLast >= tMax)
    {
      return 3;
    }
  return 0;
}

// ---- B8 Naive-B refresh T_r (prereg :218 "B7/B8 T_r ∈ {60, 120, 300, (∞ for B8)}").
// CLI value 0 means ∞. Valid: 0 or [60, 300]. Returns the period, or NaN if invalid.
inline double
B8RefreshS (double cli)
{
  if (cli == 0.0)
    {
      return std::numeric_limits<double>::infinity ();
    }
  if (cli >= 60.0 && cli <= 300.0)
    {
      return cli;
    }
  return std::numeric_limits<double>::quiet_NaN ();
}

// ---- p12 Fix B (output only): heard-mode O_ctrl from the medium. Every HELLO / beacon
// frame whose transmission STARTED on [0, T_sim] contributes its actual size (bits / 8).
struct OctrlTally
{
  uint64_t frames[10] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0}; // by cause (0..9, clamped)
  double bytes[10] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
  void add (int cause, int bits, double txStart, double tSim)
  {
    if (!(txStart <= tSim + 1e-9))
      {
        return;
      }
    const int c = std::min (std::max (cause, 0), 9);
    ++frames[c];
    bytes[c] += static_cast<double> (bits) / 8.0;
  }
  uint64_t totalFrames () const { uint64_t n = 0; for (uint64_t f : frames) n += f; return n; }
  double totalBytes () const { double b = 0; for (double x : bytes) b += x; return b; }
  double oCtrl (double tSim) const { return totalBytes () / tSim; }
};


// ---- p13 (2026-10-16): B2 Trickle (RFC 6206), B3 TAP, B4 SpeedOpt, P refresh
// (strengthening_plan.md §3 rows B2/B2*/B3/B4; prereg :213-215, :220; decisions_20261016_final.md D2).
// Pure logic; inputs are the actor's OWN timers/state and OWN held-table counts only.
struct TrickleState
{
  double I = 0, Imin = 5, Imax = 80, t = 0, start = 0;
  int k = 5;        // redundancy constant; k <= 0 means infinity (always transmit)
  int c = 0;
  bool sent = false;
  bool init = false;
};
// RFC 6206 §4.2 steps 1-2: begin an interval of length I at `now`; t ~ U[I/2, I).
inline void
TrickleBegin (TrickleState &st, double now, double u01)
{
  st.start = now;
  st.c = 0;
  st.sent = false;
  st.t = now + st.I * (0.5 + 0.5 * u01);
}
inline void
TrickleInit (TrickleState &st, double imin, int doublings, int k, double now, double u01)
{
  st.Imin = imin;
  st.Imax = imin * std::ldexp (1.0, doublings);
  st.k = k;
  st.I = imin; // RFC: I in [Imin, Imax]; we start at Imin (fast initial convergence)
  st.init = true;
  TrickleBegin (st, now, u01);
}
// §4.2 step 3: consistent transmission heard -> c++.
inline void TrickleConsistent (TrickleState &st) { ++st.c; }
// §4.2 step 6: inconsistency -> if I > Imin, reset I = Imin and begin a new interval.
// Returns true if the timer was reset.
inline bool
TrickleInconsistent (TrickleState &st, double now, double u01)
{
  if (st.I > st.Imin)
    {
      st.I = st.Imin;
      TrickleBegin (st, now, u01);
      return true;
    }
  return false;
}
// Advance at `now`. Returns true if the node transmits now (§4.2 step 4), and handles
// interval expiry (§4.2 step 5: I <- min(2I, Imax), new interval).
inline bool
TrickleStep (TrickleState &st, double now, double u01)
{
  bool tx = false;
  if (!st.sent && now >= st.t)
    {
      st.sent = true;
      tx = (st.k <= 0) || (st.c < st.k);
    }
  if (now >= st.start + st.I)
    {
      st.I = std::min (2.0 * st.I, st.Imax);
      TrickleBegin (st, now, u01);
    }
  return tx;
}
// B3 TAP (Ingelrest, Mitton, Simplot-Ryl 2007): f <- f * r / r_opt, clamped to
// [fMin, fMax]; r = new held ids since the last own HELLO / current table size (0 if empty).
inline double
TapUpdate (double f, double newIds, double tableSize, double rOpt, double fMin, double fMax)
{
  const double r = (tableSize > 0.0) ? newIds / tableSize : 0.0;
  double g = f * r / rOpt;
  if (!std::isfinite (g))
    {
      return std::numeric_limits<double>::quiet_NaN ();
    }
  return std::min (std::max (g, fMin), fMax);
}
// B4 SpeedOpt (Troel 2004 = Eq. 1 in Ingelrest 2007): f = 2 S / (a R), clamped; S = mean
// relative speed |v_i - v_j| over held records. Returns NaN on non-finite input (guard).
inline double
SpeedOptF (double S, double a, double R, double fMin, double fMax)
{
  if (!std::isfinite (S) || S < 0.0 || std::fabs (S) > 1e4)
    {
      return std::numeric_limits<double>::quiet_NaN ();
    }
  return std::min (std::max (2.0 * S / (a * R), fMin), fMax);
}

} // namespace cmp
#endif
