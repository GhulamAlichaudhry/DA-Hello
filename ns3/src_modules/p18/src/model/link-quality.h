// link-quality.h
//
// Shared OFDM BER / P_succ / c_lin used by LQARP and DA-HELLO.
// SPEC.md §1.3 and the BER-floor addendum. One implementation so
// the floor cannot drift between protocol variants.

#ifndef DAHELLO_LINK_QUALITY_H
#define DAHELLO_LINK_QUALITY_H

#include <cstdint>

namespace dahello {

// Residual BER floor: p_e_effective = max(p_e_computed, kBerFloor).
// Models receiver noise/quantization not captured by ideal AWGN.
// SPEC.md §1.3 addendum. Do not duplicate this constant elsewhere.
constexpr double kBerFloor = 1e-9;

// Section 4.1: initial energy E_ini (J), same for all nodes.
constexpr double kDefaultEiniJ = 10000.0;

// Raw OFDM BER (SPEC 1.3). May underflow to 0 at high SNR.
double ofdmBitErrorRate(double snrDb, int M);

// p_e_effective = max(p_e_computed, kBerFloor). If the raw BER is
// already 1 (invalid/unusable SNR), it stays 1.
double effectiveBer(double snrDb, int M);

// P_succ = (1 - p_e_effective)^l
double pSucc(double snrDb, uint32_t l, int M);

// c_lin = ln(1 / P_succ) = -l * ln(1 - p_e_effective). Natural log.
// With the floor, c_lin is at least -l * ln(1 - kBerFloor) and finite
// whenever p_e_effective < 1.
double cLin(double snrDb, uint32_t l, int M);

// High-SNR plateau of c_lin: ln(1 / (1-kBerFloor)^l).
double cLinAtBerFloor(uint32_t l);

} // namespace dahello

#endif // DAHELLO_LINK_QUALITY_H
