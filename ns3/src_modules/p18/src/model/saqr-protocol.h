// saqr-protocol.h
//
// SAQR host (Zhang, Chen, and Xie, Computer Networks 257 (2025) 110986).
// Service A weights, Section 7.2: rho=0.7, sigma=0.1, tau=0.1, phi=0.1.
// Reward Eq. (15) is a cost. Next hop is the minimum Q. Update is the
// Eq. (11) average with the min substituted for the max, alpha=0.1
// (Table 2), gamma=0.8 (Section 7.4, the runs that fix the other knob).
// epsilon starts at 0.5 and is fixed at 0.01 after 20 forwarding
// decisions at that node (page 14) when hostFidelity=0.
// A2.9 hostFidelity=1: epsilon 0.5 during warmS (900 s) then 0.01
// (paper "round" has no meaning in our time base; labelled placeholder).
//
// Two signed implementation choices, not published formulas:
//   the star in Eq. (15) is min-max over the actions of this decision,
//   and a one-candidate set normalizes every term to 0;
//   delay is propagation plus airtime (holding time omitted, no MAC queue).
// Path loss stays on the Bellhop gate. This class does not copy SAQR's
// own scenario.

#ifndef DAHELLO_SAQR_PROTOCOL_H
#define DAHELLO_SAQR_PROTOCOL_H

#include "lqarp-protocol.h"
#include "oracle-guard.h"

#include <cstdint>
#include <random>
#include <vector>

namespace dahello {

struct SaqrConfig {
    double rho = 0.7;
    double sigma = 0.1;
    double tau = 0.1;
    double phi = 0.1;
    double alpha = 0.1;
    double gamma = 0.8;
    double epsilon0 = 0.5;
    double epsilonLate = 0.01;
    uint32_t exploreDecisions = 20; // fidelity=0: switch after N decisions
    double warmS = 900.0;          // A2.9 fidelity=1: epsilon switch by warm time
    double holdK = 3.2;            // A2.9: paper k at R_op 1200 m (holding not applied)
    double qMinInit = 1.0;         // A2.9: paper Q_min initial (holding not applied)
    double eIni = kDefaultEiniJ;
    double rRangeM = 0.0;
    double soundSpeedMps = 1500.0;
    double bitRateBps = 1000.0;
    uint32_t packetBits = 400;
    uint32_t rngSeed = 1;
    bool hostFidelity = false;     // A2.9 --hostFidelity
};

struct SaqrReward {
    double plr = 0.0;
    double delay = 0.0;
    double er = 0.0;
    double alm = 0.0;
    double plrStar = 0.0;
    double delayStar = 0.0;
    double erStar = 0.0;
    double almStar = 0.0;
    double R = 0.0;
};

class SaqrProtocol {
public:
    SaqrProtocol(ChannelGate& gate,
                 SaqrConfig cfg,
                 std::vector<LqarpNode> nodes);

    const SaqrConfig& config() const { return cfg_; }
    const std::vector<LqarpNode>& nodes() const { return nodes_; }
    double rRangeM() const { return rRangeM_; }

    void setSink(uint32_t sinkId);

    // SNo = acoustic range. SC = closer to the sink AND neighbor-count
    // at least the mean (Eqs. 12-14). No depth filter, no SNR cut.
    std::vector<uint32_t> candidateSet(uint32_t i) const;

    double qValue(uint32_t i, uint32_t j) const;
    SaqrReward reward(uint32_t i, uint32_t j) const;

    void learnFrom(uint32_t i, const std::vector<uint32_t>& actions);
    int nextHop(uint32_t i, const std::vector<uint32_t>& actions) const;

    ChannelSample linkSample(uint32_t i, uint32_t j) const;
    void setEnergy(uint32_t i, double joules);
    double energy(uint32_t i) const;
    void setKinematics(uint32_t i, double x, double y, double z,
                       double vx, double vy, double vz);

    void noteHeader(uint32_t rx, uint32_t tx);
    void setHeardEnergy(uint32_t i, uint32_t j, double joules);
    void noteTxAttempt(uint32_t tx);
    void noteTxSuccess(uint32_t tx);

    // ---- Port A heard mode (amend1 A5 + v3 correction: V in the data header)
    HeardCtl& heard() { return hc_; }
    const HeardCtl& heard() const { return hc_; }
    void hearFrame(uint32_t rx, uint32_t tx, double t, const HeardFrame& f);
    bool fresh(uint32_t i, uint32_t j) const;
    double ownV(uint32_t s) const;          // min over written Q(s, .) (legacy minWrittenQ(s))
    uint32_t ownNumber(uint32_t s) const;   // number of fresh records of s
    // PLR from own ACK statistics (I6): unicast hops i->j sent / acknowledged.
    void noteOwnSend(uint32_t i, uint32_t j);
    void noteOwnAck(uint32_t i, uint32_t j);
    std::vector<LqarpNode>& nodesMut() { return nodes_; }

private:
    ChannelGate* gate_;
    SaqrConfig cfg_;
    std::vector<LqarpNode> nodes_;
    double rRangeM_;
    uint32_t sinkId_ = 0;
    bool sinkSet_ = false;
    std::vector<std::vector<double>> Q_;
    std::vector<std::vector<char>> qWritten_;
    std::vector<std::vector<double>> R_;
    std::vector<std::vector<double>> heardE_;
    std::vector<std::vector<char>> heard_;
    std::vector<uint64_t> nTotal_;
    std::vector<uint64_t> nTrans_;
    mutable std::vector<uint32_t> decisions_;
    mutable std::mt19937 rng_;
    HeardCtl hc_;
    struct Held {
        double t = -1.0, tPose = -1.0;
        bool pose = false;
        double x = 0, y = 0, z = 0, vx = 0, vy = 0, vz = 0;
        double number = std::numeric_limits<double>::quiet_NaN();
        double V = std::numeric_limits<double>::quiet_NaN();
    };
    std::vector<std::vector<Held>> held_;
    std::vector<std::vector<uint32_t>> sendIJ_, ackIJ_;
    bool heldPos(uint32_t i, uint32_t j, double& x, double& y, double& z) const;
    double plrOf(uint32_t i, uint32_t j) const;

    void checkIndex(uint32_t i) const;
    bool inRange(uint32_t i, uint32_t j) const;
    uint32_t neighborCount(uint32_t i) const;
    double distToSink(uint32_t i) const;
    double heardResidual(uint32_t i, uint32_t j) const;
    double rawPlr(uint32_t j) const;
    double rawDelay(uint32_t i, uint32_t j) const;
    double rawEr(uint32_t i, uint32_t j) const;
    double rawAlm(uint32_t i, uint32_t j) const;
    double minWrittenQ(uint32_t j) const;
    static double minMaxStar(double x, double lo, double hi, uint32_t n);
};

} // namespace dahello

#endif
