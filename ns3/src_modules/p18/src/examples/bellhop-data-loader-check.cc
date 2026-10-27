/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Standalone validation of BellhopDataLoader against the Step G CSVs.
 * Not a full Aqua-Sim scenario — constructor load + query() only.
 */

#include "ns3/core-module.h"
#include "ns3/bellhop-data-loader.h"

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

using dahello::BellhopDataLoader;
using dahello::ChannelSample;
using dahello::Season;

namespace {

int g_pass = 0;
int g_fail = 0;

void
Report (const std::string &name, bool ok, const std::string &detail)
{
  if (ok)
    {
      ++g_pass;
      std::cout << "PASS  " << name << "  " << detail << "\n";
    }
  else
    {
      ++g_fail;
      std::cout << "FAIL  " << name << "  " << detail << "\n";
    }
}

bool
Close (double a, double b, double tol)
{
  if (std::isnan (a) || std::isnan (b))
    {
      return false;
    }
  return std::abs (a - b) <= tol;
}

std::string
SampleStr (const ChannelSample &s)
{
  std::ostringstream oss;
  oss << std::fixed << std::setprecision (8)
      << "valid=" << (s.valid ? "true" : "false")
      << " nearPR=" << (s.nearPressureRelease ? "true" : "false")
      << " TL=" << s.tl_db << " SNR=" << s.snr_db;
  return oss.str ();
}

} // namespace

int
main (int argc, char *argv[])
{
  std::string dataDir = "${WORKSPACE}/data/channel_db";
  ns3::CommandLine cmd (__FILE__);
  cmd.AddValue ("dataDir", "Directory containing TL/SNR extended CSVs", dataDir);
  cmd.Parse (argc, argv);

  const std::string janTl = dataDir + "/TL_grid_extended_january.csv";
  const std::string janSnr = dataDir + "/SNR_grid_extended_january.csv";
  const std::string julTl = dataDir + "/TL_grid_extended_july.csv";
  const std::string julSnr = dataDir + "/SNR_grid_extended_july.csv";

  std::cout << std::fixed << std::setprecision (8);
  std::cout << "dataDir=" << dataDir << "\n";

  auto t0 = std::chrono::steady_clock::now ();
  BellhopDataLoader jan (janTl, janSnr, Season::January);
  auto t1 = std::chrono::steady_clock::now ();
  const double janMs =
      std::chrono::duration<double, std::milli> (t1 - t0).count ();
  std::cout << "January load time: " << janMs << " ms  "
            << "maxRange=" << jan.maxRangeM () << " m  "
            << "maxDepth=" << jan.maxDepthM () << " m  "
            << "season="
            << (jan.getSeason () == Season::January ? "January" : "other")
            << "\n";

  t0 = std::chrono::steady_clock::now ();
  BellhopDataLoader jul (julTl, julSnr, Season::July);
  t1 = std::chrono::steady_clock::now ();
  const double julMs =
      std::chrono::duration<double, std::milli> (t1 - t0).count ();
  std::cout << "July load time: " << julMs << " ms\n";
  std::cout << "Design: grids are loaded once in the constructor; "
               "query() only interpolates.\n\n";

  // 1. Known January focus cell.
  {
    ChannelSample s = jan.query (1000.0, 50.0);
    const bool ok = s.valid && Close (s.tl_db, 58.87, 0.02)
                    && Close (s.snr_db, 50.35, 0.02);
    Report ("query(1000,50) January vs 58.87 / 50.35", ok, SampleStr (s));
  }

  // 2. January 4000 m / 50 m — report, vicinity of ~18 dB SNR.
  {
    ChannelSample s = jan.query (4000.0, 50.0);
    const bool ok = s.valid && !std::isnan (s.tl_db) && !std::isnan (s.snr_db)
                    && s.snr_db > 10.0 && s.snr_db < 30.0;
    Report ("query(4000,50) January in 4000-5000 m band vicinity",
            ok,
            SampleStr (s) + "  (band median SNR was ~17.9 dB)");
  }

  // 3. range=0 singularity: valid=false always coincides with a NaN payload.
  {
    ChannelSample s = jan.query (0.0, 50.0);
    Report ("query(0,50) valid=false and NaN payload (range=0 singularity)",
            !s.valid && std::isnan (s.tl_db) && std::isnan (s.snr_db),
            SampleStr (s));
  }

  // 4. depth=0 pressure-release surface: CSV is finite, so valid stays true.
  //    nearPressureRelease flags the physically degenerate z=0 row.
  {
    ChannelSample s = jan.query (2500.0, 0.0);
    const bool ok = s.valid && s.nearPressureRelease
                    && !std::isnan (s.tl_db) && !std::isnan (s.snr_db);
    Report ("query(2500,0) valid=true nearPressureRelease=true",
            ok,
            SampleStr (s));
  }

  // 4b. Depth just above 0 but still inside the 5 m half-step: bilinear
  //     still uses the depth=0 corner, so nearPressureRelease remains true.
  {
    ChannelSample s = jan.query (2500.0, 3.0);
    const bool ok = s.valid && s.nearPressureRelease
                    && !std::isnan (s.tl_db) && !std::isnan (s.snr_db);
    Report ("query(2500,3) nearPressureRelease=true (within 5m half-step)",
            ok,
            SampleStr (s));
  }

  // 4c. Just outside the 5 m half-step: not flagged, even though bilinear
  //     between z=0 and z=10 still mixes a surface corner.
  {
    ChannelSample s = jan.query (2500.0, 8.0);
    const bool ok = s.valid && !s.nearPressureRelease
                    && !std::isnan (s.tl_db) && !std::isnan (s.snr_db);
    Report ("query(2500,8) nearPressureRelease=false (outside 5m half-step)",
            ok,
            SampleStr (s));
  }

  // 5. Grid edge.
  {
    ChannelSample s = jan.query (6000.0, 500.0);
    Report ("query(6000,500) valid edge of grid",
            s.valid && !std::isnan (s.tl_db) && !std::isnan (s.snr_db),
            SampleStr (s));
  }

  // 6. Outside envelope: valid=false AND NaN payload (not a clamped interpolant).
  {
    ChannelSample s = jan.query (6500.0, 250.0);
    const bool ok = !s.valid && std::isnan (s.tl_db) && std::isnan (s.snr_db)
                    && !s.nearPressureRelease;
    Report ("query(6500,250) valid=false and NaN payload (range > 6000)",
            ok,
            SampleStr (s));
  }

  // 7. July crossing cell.
  {
    ChannelSample s = jul.query (4525.0, 50.0);
    const bool ok = s.valid && Close (s.snr_db, 8.03, 0.02);
    Report ("query(4525,50) July SNR ~ 8.03 dB", ok, SampleStr (s));
  }

  // 8. Exact grid node matches CSV to printed precision.
  {
    ChannelSample s = jan.query (1000.0, 50.0);
    const bool ok = s.valid && Close (s.tl_db, 58.87307234, 5e-8)
                    && Close (s.snr_db, 50.35195475, 5e-8);
    Report ("exact grid node (1000,50) matches CSV bits", ok, SampleStr (s));
  }

  // 9. Midpoint in range at exact depth 50 m → average of 1000 and 1025.
  {
    const double tl0 = 58.87307234;
    const double tl1 = 59.21823010;
    const double sn0 = 50.35195475;
    const double sn1 = 50.00679699;
    const double tlAvg = 0.5 * (tl0 + tl1);
    const double snAvg = 0.5 * (sn0 + sn1);
    ChannelSample s = jan.query (1012.5, 50.0);
    const bool ok = s.valid && Close (s.tl_db, tlAvg, 1e-8)
                    && Close (s.snr_db, snAvg, 1e-8);
    std::ostringstream d;
    d << SampleStr (s) << "  expected TL=" << std::setprecision (8) << tlAvg
      << " SNR=" << snAvg;
    Report ("midpoint range 1012.5 m is arithmetic average", ok, d.str ());
  }

  std::cout << "\nSummary: PASS=" << g_pass << " FAIL=" << g_fail << "\n";
  return (g_fail == 0) ? 0 : 1;
}
