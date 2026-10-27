// acoustic-energy.h
//
// First-pass per-transmission energy for Section 4.4 E_bit.
// NOTHING in this repository previously deducted energy on a data
// hop: LqarpNode.energy was a static state variable for c_ene/c_del,
// E_ini=10000 J was a scale constant, and phase1's "10 W * airtime"
// helper is a different protocol and is not used here.
//
// Radiated acoustic power from source level (Urick):
//   P_ac = 10^((SL - 170.8)/10)   W
//   SL   = 175 dB re 1 μPa @ 1 m   (LQARP Table II, not phase1's 185)
//   170.8 dB re 1 μPa @ 1 m ≡ 1 W radiated in seawater
// Electrical transmit power assumes a projector efficiency eta:
//   P_tx = P_ac / eta
// Receive electronics only on a successful decode (no idle listen):
//   P_rx = 0.1 W
// Airtime T = l / R_b with R_b = 1000 bit/s, l = 400 bits.
//
//   E_tx = P_tx * T     deducted at the sender for every attempted TX
//                       (including SNR-reception failures; not VOID)
//   E_rx = P_rx * T     deducted at the receiver only on receptionOk
//
// Idle listening is NOT modelled (it would dominate T_sim=1600 s and
// hide hop-count differences in E_bit). Flag when comparing papers
// that include a listen duty cycle.

#ifndef DAHELLO_ACOUSTIC_ENERGY_H
#define DAHELLO_ACOUSTIC_ENERGY_H

#include <cmath>
#include <cstdint>

namespace dahello {

struct AcousticEnergyModel {
    double sourceLevelDb = 175.0;
    double slRefDbPerWatt = 170.8;
    double projectorEta = 0.5;
    double pRxWatt = 0.1;
    double bitRateBps = 1000.0;
    uint32_t packetBits = 400;
    double soundSpeedMps = 1500.0;

    double pAcousticWatt() const
    {
        return std::pow(10.0, (sourceLevelDb - slRefDbPerWatt) / 10.0);
    }

    double pTxWatt() const
    {
        const double eta = (projectorEta > 0.0) ? projectorEta : 1.0;
        return pAcousticWatt() / eta;
    }

    double airTimeS() const
    {
        return static_cast<double>(packetBits) / bitRateBps;
    }

    double eTxJ() const { return pTxWatt() * airTimeS(); }
    double eRxJ() const { return pRxWatt * airTimeS(); }

    double txDelayS(double distM) const
    {
        return airTimeS() + distM / soundSpeedMps;
    }
};

} // namespace dahello

#endif // DAHELLO_ACOUSTIC_ENERGY_H
