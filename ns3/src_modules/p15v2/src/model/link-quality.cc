// link-quality.cc
//
// Single BER / c_lin implementation for every protocol variant.

#include "link-quality.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace dahello {

double
ofdmBitErrorRate(double snrDb, int M)
{
    if (!std::isfinite(snrDb) || M < 2) {
        return 1.0;
    }
    const double snrLin = std::pow(10.0, snrDb / 10.0);
    const double sqrtM = std::sqrt(static_cast<double>(M));
    const double log2sqrtM = std::log2(sqrtM);
    const double log2M = std::log2(static_cast<double>(M));
    const double coeff1 = (sqrtM - 1.0) / (sqrtM * log2sqrtM);
    const double coeff2 = (sqrtM - 2.0) / (sqrtM * log2sqrtM);
    const double inner = 3.0 * snrLin * log2M / (2.0 * (M - 1));
    const double t = std::sqrt(std::max(inner, 0.0));
    double p_e = coeff1 * std::erfc(t) + coeff2 * std::erfc(3.0 * t);
    if (p_e < 0.0) {
        p_e = 0.0;
    }
    if (p_e > 1.0) {
        p_e = 1.0;
    }
    return p_e;
}

double
effectiveBer(double snrDb, int M)
{
    const double p_e = ofdmBitErrorRate(snrDb, M);
    if (p_e >= 1.0) {
        return 1.0;
    }
    return std::max(p_e, kBerFloor);
}

double
pSucc(double snrDb, uint32_t l, int M)
{
    const double p_e = effectiveBer(snrDb, M);
    return std::pow(1.0 - p_e, static_cast<double>(l));
}

double
cLin(double snrDb, uint32_t l, int M)
{
    const double p_e = effectiveBer(snrDb, M);
    if (p_e >= 1.0) {
        return std::numeric_limits<double>::infinity();
    }
    return -static_cast<double>(l) * std::log1p(-p_e);
}

double
cLinAtBerFloor(uint32_t l)
{
    return -static_cast<double>(l) * std::log1p(-kBerFloor);
}

} // namespace dahello
