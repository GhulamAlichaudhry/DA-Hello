// Port A (2026-10-12): acoustic PHY for the shared-medium extension of the
// ns-3.41 scenario. Formula channel (Thorp + practical spreading + Wenz),
// BFSK PER, thresholds, power profiles. Frozen parameters: see
// ${OUT}/strengthening_prereg.md section 1 (unchanged by Amendment 1).
// Reference: the void v2 kernel (src/v2/phy.h, commit 42e8692), re-written here.
// Used only when --mac != legacy; the legacy reception gate is untouched.
#ifndef DAHELLO_ACOUSTIC_PHY_H
#define DAHELLO_ACOUSTIC_PHY_H

#include <cmath>
#include <string>

namespace dahello {

double thorpDbPerKm (double fKhz);
double wenzPsdDb (double fKhz, double shipping, double windMps);
double wenzBandNlDb (double fcKhz, double bKhz, double shipping, double windMps, int n = 2001);

struct AcousticPhyConfig
{
  std::string chan = "formula"; // formula | bellhop (bellhop SNR comes from the legacy ChannelGate)
  double fcKhz = 24.0;
  double bKhz = 5.0;
  double rbBps = 5000.0;
  double k = 1.5;               // practical spreading
  double sl = NAN;              // NaN: calibrated so that SNR(rop) = snrReq (formula only)
  double di = 0.0;
  double ropM = 1200.0;
  double shipping = 0.2;
  double windMps = 2.0;
  double nl = NAN;              // NaN: Wenz band-integrated
  std::string per = "bfsk";     // bfsk | thr
  double perTarget = 0.1;       // bfsk: snrReq = noise-only PER target at refBits
  int refBits = 400;
  double thrDb = 8.68;          // thr: hard SINR threshold
  double detDelta = 3.0;        // lock threshold = snrReq - detDelta
  double csDelta = 3.0;         // carrier-sense threshold = snrReq - csDelta
  double floorDb = -10.0;       // arrivals below NL + floorDb are ignored
  double tPre = 0.05;           // preamble (s)
  double c = 1500.0;
};

class AcousticPhy
{
public:
  explicit AcousticPhy (const AcousticPhyConfig &cfg);
  const AcousticPhyConfig &config () const { return c_; }
  double snrReqDb () const { return snrReq_; }
  double lockDb () const { return snrReq_ - c_.detDelta; }
  double csDb () const { return snrReq_ - c_.csDelta; }
  double floorDb () const { return c_.floorDb; }
  double nlDb () const { return c_.nl; }
  double slDb () const { return c_.sl; }
  double tlFormulaDb (double d3) const;
  // noise-only SNR (dB) of a d3-metre link under the formula channel
  double formulaSnrDb (double d3) const { return c_.sl + c_.di - tlFormulaDb (d3) - c_.nl; }
  // success probability of a bits-long packet at linear SINR (noise-normalised)
  double psucc (double sinrLin, int bits) const;
  double txTime (int bits) const { return c_.tPre + bits / c_.rbBps; }
  // largest d with formulaSnrDb(d) >= snrDb (bisection), for ranging / Rop
  double formulaRangeForSnr (double snrDb) const;

private:
  AcousticPhyConfig c_;
  double snrReq_ = 0.0;
  double alpha_ = 0.0; // thorpDbPerKm(fcKhz), cached (OPT-3)
};

struct PowerProfile
{
  std::string name;
  double tx = 0, rx = 0, idle = 0, sleep = 0; // W
};
// Frozen table (prereg section 2): s2c | s2c-tx28 | s2c-wake | whoi2 | ns3mm | legacy
PowerProfile powerProfile (const std::string &name);

} // namespace dahello

#endif
