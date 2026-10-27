// tick()-scope oracle guard test (from bin_tt 8f578b7; merged on p5 with the NaN-path audit).
// Not a host or policy change. Wraps Pipeline::OnTick's proto->tick(dt) call.
//  --tickPoison=1 : in heard mode, while proto->tick runs, every protocol node copy except the
//                   sink pose (I5) is poisoned (nan|big, same pattern as PoisonScope). After tick,
//                   a guard audits every held scheduler record (zohHeard) and calls oracleLeak
//                   (exit 3) if a held pose/velocity is non-finite or implausible (|v| > 1e7 m).
//  --leakTest=5   : PLANTED LEAK inside the tick scope: for one held pair per tick, the TRUE
//                   peer pose (protocol copy as SyncPoses wrote it) is written into i's held pose
//                   of j via the scheduler's applyHeardHold, which feeds candidate sets and
//                   relative state (decisions). Implies tickPoison so the guard can see it.
// Default (both off): proto->tick(dt) exactly as in 83560909 (features-off identity).
#ifndef PORTA_TICKTEST_INC
#define PORTA_TICKTEST_INC
#include <cstdio>
namespace tt {
inline int tickPoison = 0;
inline int plant = 0;
inline uint64_t scopes = 0, audited = 0, plants = 0;
struct Report
{
  ~Report ()
  {
    if (tickPoison || plant)
      std::printf ("TICKTEST tickPoison=%d plant=%d scopes=%llu audited_held=%llu plant_writes=%llu\n",
                   tickPoison, plant, (unsigned long long) scopes, (unsigned long long) audited,
                   (unsigned long long) plants);
  }
};
inline Report report;

template <class Pipe>
void
GuardedTick (Pipe &p)
{
  auto *proto = p.proto.get ();
  const bool guard = (tickPoison || plant) && p.discMode == "heard" && p.poisonMode != "off";
  if (!guard)
    {
      proto->tick (p.tickDt);
      return;
    }
  ++scopes;
  const bool big = (p.poisonMode == "big");
  const uint32_t seed = p.poisonSeed;
  const uint32_t sink = p.sinkId;
  std::vector<std::vector<dahello::LqarpNode>> saved;
  proto->forEachNodeCopy ([&] (std::vector<dahello::LqarpNode> &v) {
    saved.push_back (v);
    for (uint32_t k = 0; k < v.size (); ++k)
      {
        double val = std::numeric_limits<double>::quiet_NaN ();
        if (big)
          {
            const uint32_t h = (k * 2654435761u) ^ (seed * 40503u);
            val = (h & 1u) ? 1.0e9 : -1.0e9;
          }
        v[k].energy = val;
        if (k == sink)
          continue;
        v[k].x = val; v[k].y = val; v[k].z = val;
        v[k].vx = val; v[k].vy = val; v[k].vz = val;
      }
  });
  auto *sch = const_cast<dahello::DahelloProtocol *> (proto->scheduler ());
  // p5: NaN-path audit for held writes inside the poisoned scope (nan poison values are
  // otherwise dropped by applyHeardHold's isfinite filter and never reach the audit below).
  if (sch)
    sch->setHoldWriteAudit (true);
  proto->tick (p.tickDt);
  if (plant && sch)
    {
      const auto &nd = sch->nodes ();
      const uint32_t n = static_cast<uint32_t> (nd.size ());
      const double now = ns3::Simulator::Now ().GetSeconds ();
      bool done = false;
      for (uint32_t i = 1; i < n && !done; ++i)
        for (uint32_t j = 1; j < n && !done; ++j)
          if (i != j && sch->ekfMeta (i, j).zohHeard)
            {
              const double nan = std::numeric_limits<double>::quiet_NaN ();
              // LEAK: true peer pose from the protocol copy -> held pose (decision input)
              sch->applyHeardHold (i, j, sch->ekfMeta (i, j).lastZohTime, nd[j].x, nd[j].y, nd[j].z,
                                   nd[j].vx, nd[j].vy, nd[j].vz, nan, nan, nan, nan);
              ++plants;
              done = true;
              (void) now;
            }
    }
  if (sch)
    sch->setHoldWriteAudit (false);
  size_t c = 0;
  proto->forEachNodeCopy ([&] (std::vector<dahello::LqarpNode> &v) { v = saved[c++]; });
  if (sch)
    {
      const uint32_t n = static_cast<uint32_t> (sch->nodes ().size ());
      for (uint32_t i = 0; i < n; ++i)
        for (uint32_t j = 0; j < n; ++j)
          {
            if (i == j)
              continue;
            const auto &s = sch->ekfMeta (i, j);
            if (!s.zohHeard)
              continue;
            ++audited;
            const double f[6] = {s.x_j_zoh, s.y_j_zoh, s.z_j_zoh, s.vx_j_zoh, s.vy_j_zoh, s.vz_j_zoh};
            for (double x : f)
              if (!std::isfinite (x) || std::fabs (x) > 1.0e7)
                dahello::oracleLeak ("tick:heldPose", i, j);
          }
    }
}
} // namespace tt
#endif
