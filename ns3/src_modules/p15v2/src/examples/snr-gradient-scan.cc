// Standalone Step 39: |dSNR/drange| over the January and July grids.
// No ns-3. Same finite difference as Step 36: (SNR(r+10)-SNR(r))/10.

#include "bellhop-data-loader.h"

#include <algorithm>
#include <cstdint>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

using dahello::BellhopDataLoader;
using dahello::ChannelSample;
using dahello::Season;

namespace {

struct GradStats
{
  std::vector<double> absG;
  uint64_t n = 0;
  uint64_t nValid = 0;
  uint64_t nGt05 = 0;
  uint64_t nGt20 = 0;
  uint64_t nNearCross = 0;
  uint64_t nNearCrossGt05 = 0;
  uint64_t nNearCrossGt20 = 0;
  uint64_t nInband = 0; // r < rCross - 250
  uint64_t nInbandGt05 = 0;
  uint64_t nInbandGt20 = 0;
  double rCross = 0.0;

  void add (double g, double r, double rLo, double rHi)
  {
    ++n;
    if (!std::isfinite (g))
      {
        return;
      }
    const double a = std::fabs (g);
    absG.push_back (a);
    ++nValid;
    if (a > 0.5)
      {
        ++nGt05;
      }
    if (a > 2.0)
      {
        ++nGt20;
      }
    if (r >= rLo && r <= rHi)
      {
        ++nNearCross;
        if (a > 0.5)
          {
            ++nNearCrossGt05;
          }
        if (a > 2.0)
          {
            ++nNearCrossGt20;
          }
      }
    if (r < rLo)
      {
        ++nInband;
        if (a > 0.5)
          {
            ++nInbandGt05;
          }
        if (a > 2.0)
          {
            ++nInbandGt20;
          }
      }
  }

  static double pct (const std::vector<double> &v, double p)
  {
    if (v.empty ())
      {
        return std::numeric_limits<double>::quiet_NaN ();
      }
    const size_t i = std::min (v.size () - 1,
                               static_cast<size_t> (std::floor (p * (v.size () - 1.0))));
    return v[i];
  }

  void finish () { std::sort (absG.begin (), absG.end ()); }

  void dump (std::ostream &os, const char *name) const
  {
    os << std::scientific << std::setprecision (6);
    os << name << " n_query=" << n << " n_finite=" << nValid << "\n";
    if (absG.empty ())
      {
        os << "  empty\n";
        return;
      }
    os << "  |dSNR/dr| min=" << absG.front ()
       << " p25=" << pct (absG, 0.25)
       << " p50=" << pct (absG, 0.50)
       << " p75=" << pct (absG, 0.75)
       << " p90=" << pct (absG, 0.90)
       << " p99=" << pct (absG, 0.99)
       << " max=" << absG.back () << " dB/m\n"
       << "  frac>|0.5|=" << static_cast<double> (nGt05) / nValid
       << " n=" << nGt05
       << "  frac>|2.0|=" << static_cast<double> (nGt20) / nValid
       << " n=" << nGt20 << "\n"
       << "  near_cross[" << rCross - 250.0 << "," << rCross + 250.0 << "] n=" << nNearCross
       << " frac>|0.5|="
       << (nNearCross ? static_cast<double> (nNearCrossGt05) / nNearCross : 0.0)
       << " frac>|2.0|="
       << (nNearCross ? static_cast<double> (nNearCrossGt20) / nNearCross : 0.0) << "\n"
       << "  inband_r<" << (rCross - 250.0) << " n=" << nInband
       << " frac>|0.5|="
       << (nInband ? static_cast<double> (nInbandGt05) / nInband : 0.0)
       << " frac>|2.0|="
       << (nInband ? static_cast<double> (nInbandGt20) / nInband : 0.0) << "\n";
  }
};

double
DsnrDr (const BellhopDataLoader &L, double r, double z)
{
  const ChannelSample a = L.query (r, z);
  const double r2 = std::min (L.maxRangeM (), r + 10.0);
  const double r1 = (r2 - r < 5.0) ? std::max (0.0, r - 10.0) : r;
  const double rHi = (r2 - r < 5.0) ? r : r2;
  const ChannelSample b = L.query ((r2 - r < 5.0) ? r1 : r2, z);
  if (!a.valid || !b.valid || !std::isfinite (a.snr_db) || !std::isfinite (b.snr_db))
    {
      return std::numeric_limits<double>::quiet_NaN ();
    }
  const double denom = (r2 - r < 5.0) ? (r - r1) : (rHi - r1);
  if (denom <= 0.0)
    {
      return std::numeric_limits<double>::quiet_NaN ();
    }
  return (b.snr_db - a.snr_db) / denom;
}

void
Scan (const BellhopDataLoader &L, const char *month, double rCross, std::ostream &os)
{
  const double rLo = rCross - 250.0;
  const double rHi = rCross + 250.0;

  GradStats native;
  native.rCross = rCross;
  const auto &R = L.rangeAxis ();
  const auto &Z = L.depthAxis ();
  for (double r : R)
    {
      if (r < 25.0)
        {
          continue; // range=0 NaN row
        }
      for (double z : Z)
        {
          if (z < 5.0)
            {
              continue; // pressure-release surface
            }
          native.add (DsnrDr (L, r, z), r, rLo, rHi);
        }
    }
  native.finish ();

  GradStats dense;
  dense.rCross = rCross;
  for (double r = 50.0; r <= 6000.0 + 1e-9; r += 50.0)
    {
      for (double z = 20.0; z <= 500.0 + 1e-9; z += 20.0)
        {
          dense.add (DsnrDr (L, r, z), r, rLo, rHi);
        }
    }
  dense.finish ();

  os << "=== " << month << " r_range(50m)≈" << rCross << " m ===\n";
  native.dump (os, "NATIVE_GRID range=25m depth=10m (skip r<25, z<5)");
  dense.dump (os, "DENSE_SAMPLE range=50m depth=20m");

  // Range-binned median |dSNR/dr| on the dense sample (shows fringe vs crossing).
  const double edges[] = {0, 1000, 2000, 3000, 4000, 4500, 5000, 5500, 6000};
  os << "  DENSE range-bin |dSNR/dr| p50 / frac>|0.5| / frac>|2.0|\n";
  for (int b = 0; b < 8; ++b)
    {
      std::vector<double> v;
      uint64_t n05 = 0;
      uint64_t n20 = 0;
      for (double r = 50.0; r <= 6000.0 + 1e-9; r += 50.0)
        {
          if (r < edges[b] || r >= edges[b + 1])
            {
              continue;
            }
          for (double z = 20.0; z <= 500.0 + 1e-9; z += 20.0)
            {
              const double g = DsnrDr (L, r, z);
              if (!std::isfinite (g))
                {
                  continue;
                }
              const double a = std::fabs (g);
              v.push_back (a);
              if (a > 0.5)
                {
                  ++n05;
                }
              if (a > 2.0)
                {
                  ++n20;
                }
            }
        }
      std::sort (v.begin (), v.end ());
      os << "    [" << edges[b] << "," << edges[b + 1] << ") n=" << v.size ();
      if (!v.empty ())
        {
          os << " p50=" << GradStats::pct (v, 0.50)
             << " p90=" << GradStats::pct (v, 0.90)
             << " frac>|0.5|=" << static_cast<double> (n05) / v.size ()
             << " frac>|2.0|=" << static_cast<double> (n20) / v.size ();
        }
      os << "\n";
    }
}

} // namespace

int
main (int argc, char **argv)
{
  const std::string dataDir = argc > 1 ? argv[1] : "${WORKSPACE}/data/channel_db";
  BellhopDataLoader jan (dataDir + "/TL_grid_extended_january.csv",
                         dataDir + "/SNR_grid_extended_january.csv",
                         Season::January);
  BellhopDataLoader jul (dataDir + "/TL_grid_extended_july.csv",
                         dataDir + "/SNR_grid_extended_july.csv",
                         Season::July);
  std::cout << "dataDir=" << dataDir << "\n"
            << "finite-diff = (SNR(r+10)-SNR(r))/10 dB/m, same as Step 36\n"
            << "pair(1,2) operating point was 0.0603 dB/m at r=2705 m, z=203 m\n\n";
  Scan (jan, "JANUARY", 5125.0, std::cout);
  std::cout << "\n";
  Scan (jul, "JULY", 4525.0, std::cout);
  return 0;
}
