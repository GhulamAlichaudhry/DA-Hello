// Port A (2026-10-12): heard-mode oracle guard (amendment amend1, "Runtime enforcement").
// Any legacy oracle function reached by a host or policy in heard mode ends the run with
// exit code 3 and "ORACLE_LEAK <function> <i> <j>". Positive controls (--leakTest=k) re-enable
// a single legacy oracle code path; the guard below is what must catch it.
#ifndef DAHELLO_ORACLE_GUARD_H
#define DAHELLO_ORACLE_GUARD_H

#include <limits>
#include <cstdint>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace dahello {

[[noreturn]] inline void
oracleLeak (const char *fn, uint32_t i, uint32_t j)
{
  std::fprintf (stderr, "ORACLE_LEAK %s %u %u\n", fn, i, j);
  std::fflush (stderr);
  std::exit (3);
}

// Finiteness assert for values a host or policy writes (Q, reward, score, timer).
// Under poisoning, any value derived from a peer's true state becomes NaN/±1e9 and,
// for NaN, is caught here.
inline void
assertFinite (double v, const char *what, uint32_t i, uint32_t j)
{
  if (!std::isfinite (v))
    {
      std::fprintf (stderr, "ORACLE_LEAK nonfinite:%s %u %u\n", what, i, j);
      std::fflush (stderr);
      std::exit (3);
    }
}

// Observer scope: metrics/attribution code (VOID_stale, stale-hop counters, traces)
// may read ground truth; it must not feed any decision. Inside an ObserverScope the
// guarded host functions do not abort. Decision code never opens one.
inline int &observerDepth () { static int d = 0; return d; }
struct ObserverScope
{
  ObserverScope () { ++observerDepth (); }
  ~ObserverScope () { --observerDepth (); }
};
inline bool observing () { return observerDepth () > 0; }

// I2 fields of one decoded frame, as written by the sender at its transmission start.
// NaN = field not carried by this frame kind / host.
struct HeardFrame
{
  uint8_t kind = 0;          // dahello::kFrame*
  bool poseCarried = false;  // HELLO / beacon / init (pose + velocity)
  double x = std::numeric_limits<double>::quiet_NaN (), y = x, z = x, vx = x, vy = x, vz = x;
  double energy = x, groupAvg = x, number = x, V = x, snrDb = x;
};

// Heard-mode control shared by the host protocols (MARV, QELAR, SAQR).
struct HeardCtl
{
  bool on = false;          // --disc=heard
  double tNbS = 3600.0;     // freshness T_nb (s); CLI --tNb, same for all arms of a cell
  double (*clock) () = nullptr; // true simulation clock (s), injected by the scenario
  int leakPlant = 0;        // --leakTest=k (test only): re-enable one legacy oracle path
  double now () const { return clock ? clock () : 0.0; }
  bool fresh (bool heard, double tLast) const
  {
    return heard && std::isfinite (tLast) && tLast >= 0.0 && (now () - tLast) <= tNbS;
  }
};

} // namespace dahello

#endif
