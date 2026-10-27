// p9: unit check for comparators.h (B6 / B8 / B9 decision logic). Pure; exit 0 iff all pass.
#include "comparators.h"
#include <cstdio>
static int pass = 0, fail = 0;
#define CHECK(name, cond) do { if (cond) { ++pass; } else { ++fail; std::printf ("FAIL %s\n", name); } } while (0)
int main ()
{
  using namespace cmp;
  const double inf = std::numeric_limits<double>::infinity ();
  // ---- B9 / B8 first-contact reply
  {
    FcState s;
    CHECK ("fc: fresh sender -> no reply", std::isnan (FcOnDecode (s, true, 10.0, 0.5, 2.0, 5.0)) && !s.pending);
    const double t = FcOnDecode (s, false, 10.0, 0.5, 2.0, 5.0);
    CHECK ("fc: unknown sender -> reply at now+u*J", t == 11.0 && s.pending);
    const uint64_t tok = s.token;
    CHECK ("fc: aggregation (second newcomer while pending -> none)", std::isnan (FcOnDecode (s, false, 10.5, 0.1, 2.0, 5.0)));
    CHECK ("fc: fires with matching token", FcFire (s, tok, 11.0) && !s.pending && s.lastTx == 11.0);
    CHECK ("fc: stale token does not fire twice", !FcFire (s, tok, 11.0));
    const double t2 = FcOnDecode (s, false, 12.0, 0.0, 2.0, 5.0);
    CHECK ("fc: rate limit -> not before lastTx+5", t2 == 16.0);
    const uint64_t tok2 = s.token;
    CHECK ("fc: piggyback cancels", FcCancel (s) && !s.pending);
    CHECK ("fc: cancelled event does not fire", !FcFire (s, tok2, 16.0));
    CHECK ("fc: cancel with nothing pending is a no-op", !FcCancel (s));
    const double t3 = FcOnDecode (s, false, 30.0, 1.0, 2.0, 5.0);
    CHECK ("fc: jitter upper bound J=2", t3 == 32.0);
  }
  // ---- p10 never-heard-only: the caller passes "ever heard" (MarvRecord::heard, never
  // cleared), so a sender whose record expired (age > T_nb) is still "known" -> no reply.
  {
    FcState s;
    const bool everHeardExpired = true;  // heard once, now older than T_nb
    CHECK ("fc10: expired-but-heard sender -> no reply", std::isnan (FcOnDecode (s, everHeardExpired, 500.0, 0.3, 2.0, 5.0)) && !s.pending);
    const bool neverHeard = false;
    CHECK ("fc10: never-heard sender -> reply", FcOnDecode (s, neverHeard, 500.0, 0.25, 2.0, 5.0) == 500.5);
  }
  // ---- B6 distance trigger
  {
    CHECK ("b6: still -> none", B6Trigger (0, 0, 100, 0, 0, 100, 0, 10, 250, 50, 300) == 0);
    CHECK ("b6: moved 251 m -> dist", B6Trigger (251, 0, 100, 0, 0, 100, 0, 10, 250, 50, 300) == 1);
    CHECK ("b6: moved exactly 250 m -> none", B6Trigger (250, 0, 100, 0, 0, 100, 0, 10, 250, 50, 300) == 0);
    CHECK ("b6: depth 60 m (3-D 60 < d_th) -> depth", B6Trigger (0, 0, 160, 0, 0, 100, 0, 10, 250, 50, 300) == 2);
    CHECK ("b6: T_max elapsed -> tmax", B6Trigger (0, 0, 100, 0, 0, 100, 0, 300, 250, 50, 300) == 3);
    CHECK ("b6: T_max not yet -> none", B6Trigger (0, 0, 100, 0, 0, 100, 0, 299.9, 250, 50, 300) == 0);
    CHECK ("b6: NaN own pose -> guard", B6Trigger (std::nan (""), 0, 100, 0, 0, 100, 0, 10, 250, 50, 300) == -1);
    CHECK ("b6: 1e9 pose -> guard", B6Trigger (1e9, 0, 100, 0, 0, 100, 0, 10, 250, 50, 300) == -1);
    CHECK ("b6: inf anchor -> guard", B6Trigger (0, 0, 100, inf, 0, 100, 0, 10, 250, 50, 300) == -1);
  }
  // ---- B8 refresh mapping
  {
    CHECK ("b8: 0 -> inf", std::isinf (B8RefreshS (0.0)));
    CHECK ("b8: 60 ok", B8RefreshS (60.0) == 60.0);
    CHECK ("b8: 120 ok", B8RefreshS (120.0) == 120.0);
    CHECK ("b8: 300 ok", B8RefreshS (300.0) == 300.0);
    CHECK ("b8: 30 invalid", std::isnan (B8RefreshS (30.0)));
    CHECK ("b8: 301 invalid", std::isnan (B8RefreshS (301.0)));
  }
  // ---- p12 Fix B tally
  {
    OctrlTally t;
    t.add (2, 400, 3.0, 1800.0);      // join
    t.add (7, 400, 100.0, 1800.0);    // fc reply
    t.add (6, 400, 1800.0, 1800.0);   // sink beacon at exactly T_sim: counted
    t.add (3, 400, 1800.5, 1800.0);   // host-state in drain: not counted
    t.add (12, 64, 10.0, 1800.0);     // out-of-range cause clamps to 9, actual size used
    CHECK ("octrlB: frames counted on [0,T_sim]", t.totalFrames () == 4);
    CHECK ("octrlB: drain excluded", t.frames[3] == 0);
    CHECK ("octrlB: cause clamp + actual size", t.frames[9] == 1 && t.bytes[9] == 8.0);
    CHECK ("octrlB: bytes", t.totalBytes () == 3 * 50.0 + 8.0);
    CHECK ("octrlB: O_ctrl*T == bytes (rel 1e-12)", std::fabs (t.oCtrl (1800.0) * 1800.0 - 158.0) <= 1e-12 * 158.0);
  }
  // ---- p13 B2 Trickle (RFC 6206)
  {
    TrickleState s;
    TrickleInit (s, 5.0, 4, 1, 0.0, 0.0);
    CHECK ("tr: Imax = Imin*2^d", s.Imax == 80.0 && s.I == 5.0);
    CHECK ("tr: t = start + I/2 at u=0", s.t == 2.5);
    CHECK ("tr: no tx before t", !TrickleStep (s, 2.0, 0.0));
    CHECK ("tr: tx at t when c<k", TrickleStep (s, 2.5, 0.0));
    CHECK ("tr: interval end doubles I", !TrickleStep (s, 5.0, 0.0) && s.I == 10.0 && s.start == 5.0);
    TrickleConsistent (s);
    CHECK ("tr: suppressed when c>=k", !TrickleStep (s, 10.0, 0.0));
    for (int i = 0; i < 10; ++i) { TrickleStep (s, s.start + s.I, 0.0); }
    CHECK ("tr: I capped at Imax", s.I == 80.0);
    CHECK ("tr: inconsistency resets to Imin", TrickleInconsistent (s, 1000.0, 0.0) && s.I == 5.0 && s.start == 1000.0);
    CHECK ("tr: inconsistency at Imin is no-op", !TrickleInconsistent (s, 1001.0, 0.0));
    TrickleState q; TrickleInit (q, 2.0, 8, 0, 0.0, 0.99);
    for (int i = 0; i < 9; ++i) { TrickleConsistent (q); }
    CHECK ("tr: k=inf always transmits", TrickleStep (q, 2.0, 0.0));
    CHECK ("tr: Imax d=8", q.Imax == 512.0);
  }
  // ---- p13 B3 TAP
  {
    CHECK ("tap: r = r_opt keeps f", std::fabs (TapUpdate (0.1, 4, 100, 0.04, 1.0 / 300, 0.2) - 0.1) < 1e-15);
    CHECK ("tap: r = 2 r_opt doubles f", std::fabs (TapUpdate (0.05, 8, 100, 0.04, 1.0 / 300, 0.2) - 0.1) < 1e-15);
    CHECK ("tap: clamp max 1/5", TapUpdate (0.1, 50, 100, 0.04, 1.0 / 300, 0.2) == 0.2);
    CHECK ("tap: empty table -> f_min", TapUpdate (0.1, 3, 0, 0.04, 1.0 / 300, 0.2) == 1.0 / 300);
    CHECK ("tap: NaN f -> NaN (guard)", std::isnan (TapUpdate (std::nan (""), 1, 10, 0.04, 1.0 / 300, 0.2)));
  }
  // ---- p13 B4 SpeedOpt
  {
    CHECK ("spd: S=0 -> f_min (T_max 300 s)", SpeedOptF (0.0, 0.1, 1200, 1.0 / 300, 0.2) == 1.0 / 300);
    CHECK ("spd: f = 2S/(aR)", std::fabs (SpeedOptF (0.6, 0.1, 1200, 1.0 / 300, 0.2) - 0.01) < 1e-15);
    CHECK ("spd: clamp max", SpeedOptF (100.0, 0.05, 1200, 1.0 / 300, 0.2) == 0.2);
    CHECK ("spd: NaN S -> guard", std::isnan (SpeedOptF (std::nan (""), 0.1, 1200, 1.0 / 300, 0.2)));
    CHECK ("spd: 1e9 S -> guard", std::isnan (SpeedOptF (1e9, 0.1, 1200, 1.0 / 300, 0.2)));
  }
  std::printf ("COMPARATORS_UNIT %d/%d %s\n", pass, pass + fail, fail ? "FAIL" : "PASS");
  return fail ? 1 : 0;
}
