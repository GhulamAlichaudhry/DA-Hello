// Standalone (no ns-3) driver — same checks as bellhop-data-loader-check.cc.
#include "bellhop-data-loader.h"
#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

using dahello::BellhopDataLoader;
using dahello::ChannelSample;
using dahello::Season;

static int g_pass = 0;
static int g_fail = 0;

static void
Report (const std::string &name, bool ok, const std::string &detail)
{
  std::cout << (ok ? "PASS  " : "FAIL  ") << name << "  " << detail << "\n";
  (ok ? g_pass : g_fail)++;
}

static bool
Close (double a, double b, double tol)
{
  return !std::isnan (a) && !std::isnan (b) && std::abs (a - b) <= tol;
}

static std::string
SampleStr (const ChannelSample &s)
{
  std::ostringstream oss;
  oss << std::fixed << std::setprecision (8)
      << "valid=" << (s.valid ? "true" : "false")
      << " nearPR=" << (s.nearPressureRelease ? "true" : "false")
      << " TL=" << s.tl_db << " SNR=" << s.snr_db;
  return oss.str ();
}

int
main (int argc, char **argv)
{
  std::string dataDir = argc > 1 ? argv[1] : "${WORKSPACE}/data/channel_db";
  std::cout << std::fixed << std::setprecision (8);
  std::cout << "dataDir=" << dataDir << "\n";

  auto t0 = std::chrono::steady_clock::now ();
  BellhopDataLoader jan (dataDir + "/TL_grid_extended_january.csv",
                         dataDir + "/SNR_grid_extended_january.csv",
                         Season::January);
  auto t1 = std::chrono::steady_clock::now ();
  std::cout << "January load time: "
            << std::chrono::duration<double, std::milli> (t1 - t0).count ()
            << " ms  maxRange=" << jan.maxRangeM ()
            << " maxDepth=" << jan.maxDepthM () << "\n";

  t0 = std::chrono::steady_clock::now ();
  BellhopDataLoader jul (dataDir + "/TL_grid_extended_july.csv",
                         dataDir + "/SNR_grid_extended_july.csv",
                         Season::July);
  t1 = std::chrono::steady_clock::now ();
  std::cout << "July load time: "
            << std::chrono::duration<double, std::milli> (t1 - t0).count ()
            << " ms\n";
  std::cout << "Design: grids loaded once in constructor; query() interpolates only.\n\n";

  {
    ChannelSample s = jan.query (1000.0, 50.0);
    Report ("query(1000,50) January vs 58.87 / 50.35",
            s.valid && Close (s.tl_db, 58.87, 0.02) && Close (s.snr_db, 50.35, 0.02),
            SampleStr (s));
  }
  {
    ChannelSample s = jan.query (4000.0, 50.0);
    Report ("query(4000,50) January in 4000-5000 m band vicinity",
            s.valid && s.snr_db > 10.0 && s.snr_db < 30.0,
            SampleStr (s) + "  (band median SNR was ~17.9 dB)");
  }
  {
    ChannelSample s = jan.query (0.0, 50.0);
    Report ("query(0,50) valid=false and NaN payload (range=0 singularity)",
            !s.valid && std::isnan (s.tl_db) && std::isnan (s.snr_db),
            SampleStr (s));
  }
  {
    ChannelSample s = jan.query (2500.0, 0.0);
    Report ("query(2500,0) valid=true nearPressureRelease=true",
            s.valid && s.nearPressureRelease
                && !std::isnan (s.tl_db) && !std::isnan (s.snr_db),
            SampleStr (s));
  }
  {
    ChannelSample s = jan.query (2500.0, 3.0);
    Report ("query(2500,3) nearPressureRelease=true (within 5m half-step)",
            s.valid && s.nearPressureRelease
                && !std::isnan (s.tl_db) && !std::isnan (s.snr_db),
            SampleStr (s));
  }
  {
    ChannelSample s = jan.query (2500.0, 8.0);
    Report ("query(2500,8) nearPressureRelease=false (outside 5m half-step)",
            s.valid && !s.nearPressureRelease
                && !std::isnan (s.tl_db) && !std::isnan (s.snr_db),
            SampleStr (s));
  }
  {
    ChannelSample s = jan.query (6000.0, 500.0);
    Report ("query(6000,500) valid edge of grid",
            s.valid && !std::isnan (s.tl_db), SampleStr (s));
  }
  {
    ChannelSample s = jan.query (6500.0, 250.0);
    Report ("query(6500,250) valid=false and NaN payload (range > 6000)",
            !s.valid && std::isnan (s.tl_db) && std::isnan (s.snr_db)
                && !s.nearPressureRelease,
            SampleStr (s));
  }
  {
    ChannelSample s = jul.query (4525.0, 50.0);
    Report ("query(4525,50) July SNR ~ 8.03 dB",
            s.valid && Close (s.snr_db, 8.03, 0.02), SampleStr (s));
  }
  {
    ChannelSample s = jan.query (1000.0, 50.0);
    Report ("exact grid node (1000,50) matches CSV bits",
            s.valid && Close (s.tl_db, 58.87307234, 5e-8)
                && Close (s.snr_db, 50.35195475, 5e-8),
            SampleStr (s));
  }
  {
    const double tlAvg = 0.5 * (58.87307234 + 59.21823010);
    const double snAvg = 0.5 * (50.35195475 + 50.00679699);
    ChannelSample s = jan.query (1012.5, 50.0);
    std::ostringstream d;
    d << SampleStr (s) << "  expected TL=" << std::setprecision (8) << tlAvg
      << " SNR=" << snAvg;
    Report ("midpoint range 1012.5 m is arithmetic average",
            s.valid && Close (s.tl_db, tlAvg, 1e-8) && Close (s.snr_db, snAvg, 1e-8),
            d.str ());
  }

  std::cout << "\nSummary: PASS=" << g_pass << " FAIL=" << g_fail << "\n";
  return (g_fail == 0) ? 0 : 1;
}
