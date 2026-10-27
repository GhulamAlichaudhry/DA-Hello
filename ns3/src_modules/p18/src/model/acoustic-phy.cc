#include "acoustic-phy.h"

#include <algorithm>
#include <stdexcept>

namespace dahello {

double
thorpDbPerKm (double f)
{
  const double f2 = f * f;
  return 0.11 * f2 / (1 + f2) + 44 * f2 / (4100 + f2) + 2.75e-4 * f2 + 0.003;
}

double
wenzPsdDb (double f, double s, double w)
{
  const double nt = 17 - 30 * std::log10 (f);
  const double ns = 40 + 20 * (s - 0.5) + 26 * std::log10 (f) - 60 * std::log10 (f + 0.03);
  const double nw = 50 + 7.5 * std::sqrt (w) + 20 * std::log10 (f) - 40 * std::log10 (f + 0.4);
  const double nth = -15 + 20 * std::log10 (f);
  return 10 * std::log10 (std::pow (10, nt / 10) + std::pow (10, ns / 10) + std::pow (10, nw / 10)
                          + std::pow (10, nth / 10));
}

double
wenzBandNlDb (double fc, double b, double s, double w, int n)
{
  double acc = 0;
  const double df = b / (n - 1);
  for (int i = 0; i < n; ++i)
    {
      const double f = fc - b / 2 + i * df;
      acc += (i == 0 || i == n - 1 ? 0.5 : 1.0) * std::pow (10, wenzPsdDb (f, s, w) / 10);
    }
  return 10 * std::log10 (acc * df * 1000.0);
}

AcousticPhy::AcousticPhy (const AcousticPhyConfig &cfg)
    : c_ (cfg)
{
  alpha_ = thorpDbPerKm (c_.fcKhz);
  if (std::isnan (c_.nl))
    {
      c_.nl = wenzBandNlDb (c_.fcKhz, c_.bKhz, c_.shipping, c_.windMps);
    }
  if (c_.per == "bfsk")
    {
      const double L = c_.refBits;
      const double ber = 1 - std::pow (1 - c_.perTarget, 1 / L);
      const double gb = -2 * std::log (2 * ber);
      snrReq_ = 10 * std::log10 (gb * c_.rbBps / (c_.bKhz * 1000));
    }
  else if (c_.per == "thr")
    {
      snrReq_ = c_.thrDb;
    }
  else
    {
      throw std::invalid_argument ("AcousticPhy: per must be bfsk|thr");
    }
  if (std::isnan (c_.sl))
    {
      c_.sl = (c_.chan == "formula") ? tlFormulaDb (c_.ropM) + c_.nl + snrReq_ - c_.di : 175.0;
    }
}

double
AcousticPhy::tlFormulaDb (double d3) const
{
  const double d = std::max (d3, 1.0);
  // OPT-3: thorpDbPerKm(fc) is a pure function of the frozen fc; cached at construction (same bits)
  return 10 * c_.k * std::log10 (d) + alpha_ * d / 1000.0;
}

double
AcousticPhy::psucc (double sinrLin, int bits) const
{
  if (c_.per == "thr")
    {
      return 10 * std::log10 (std::max (sinrLin, 1e-30)) >= c_.thrDb ? 1.0 : 0.0;
    }
  const double gb = sinrLin * (c_.bKhz * 1000) / c_.rbBps;
  const double ber = 0.5 * std::exp (-gb / 2);
  return std::pow (1 - ber, bits);
}

double
AcousticPhy::formulaRangeForSnr (double snrDb) const
{
  double lo = 1, hi = 50000;
  for (int k = 0; k < 60; ++k)
    {
      const double m = 0.5 * (lo + hi);
      if (formulaSnrDb (m) >= snrDb) lo = m; else hi = m;
    }
  return lo;
}

PowerProfile
powerProfile (const std::string &n)
{
  if (n == "s2c") return {"s2c", 8.0, 1.3, 0.285, 0.0025};
  if (n == "s2c-tx28") return {"s2c-tx28", 2.8, 1.3, 0.285, 0.0025};
  if (n == "s2c-wake" || n == "s2cwake") return {"s2c-wake", 8.0, 1.3, 0.005, 0.0025};
  if (n == "whoi2") return {"whoi2", 10.0, 2.3, 0.08, 0.000165};
  if (n == "ns3mm") return {"ns3mm", 50.0, 0.158, 0.158, 0.0058};
  if (n == "legacy") return {"legacy", 5.26, 0.1, 0.0, 0.0};
  throw std::invalid_argument ("unknown power profile " + n);
}

} // namespace dahello
