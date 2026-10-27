// qelar-protocol.h
//
// QELAR host (Hu and Fei, IEEE TMC vol. 9, no. 6, June 2010).
// Model-based update, Eq. (7): Q(n, m) = R_success(n, m) + gamma * V(m).
// No learning rate. Failure reward Eq. (13) is not applied: this binary
// is collision-free, so P_success = 1 and Eq. (14) reduces to Eq. (10).
// gamma = 0.5 is the worked example in Section 2.4, signed as a choice,
// not a named experimental default. Section 4.2 uses g = 1, alpha1 = 0.5,
// alpha2 = 0.05.
//
// Candidate set is acoustic range only (no depth filter, no SNR cut).
// Ties in argmax Q take the smallest id (the paper's example draws
// uniformly; the harness needs a reproducible rule).
//
// Neighbor residual and group-average energy are the header fields in
// Section 3.4. They update here only when a data packet from that
// neighbor is demodulated. Until then both stay at E_ini.

#ifndef DAHELLO_QELAR_PROTOCOL_H
#define DAHELLO_QELAR_PROTOCOL_H

#include "lqarp-protocol.h"
#include "oracle-guard.h"

#include <cstdint>
#include <vector>

namespace dahello {

struct QelarConfig {
    double g = 1.0;
    double alpha1 = 0.5;
    double alpha2 = 0.05;
    double beta1 = 0.5;   // A2.9 fidelity=1: Eq. (13) failure weights (paper)
    double beta2 = 0.05;
    double gamma = 0.5;
    double eIni = kDefaultEiniJ;
    double rRangeM = 0.0;
    double snrThresholdDb = 8.68; // A2.9 fidelity=1: candidate SNR cut
    bool hostFidelity = false;    // A2.9 --hostFidelity; default 0 = p15v2
};

struct QelarReward {
    double cSender = 0.0;
    double cNext = 0.0;
    double dSender = 0.0;
    double dNext = 0.0;
    double R = 0.0;
};

class QelarProtocol {
public:
    QelarProtocol(ChannelGate& gate,
                  QelarConfig cfg,
                  std::vector<LqarpNode> nodes);

    const QelarConfig& config() const { return cfg_; }
    const std::vector<LqarpNode>& nodes() const { return nodes_; }
    double rRangeM() const { return rRangeM_; }

    void setSink(uint32_t sinkId);

    // Acoustic range / fresh heard. Fidelity=1 adds record SNR >= threshold.
    // No N_depth. Previous-hop revisit is allowed by the Forward seam when fidelity=1.
    std::vector<uint32_t> candidateSet(uint32_t i) const;

    QelarReward reward(uint32_t i, uint32_t j) const;
    double qValue(uint32_t i, uint32_t j) const;

    // Direct model-based write for each action. Does not touch other rows.
    void learnFrom(uint32_t i, const std::vector<uint32_t>& actions);
    int nextHop(uint32_t i, const std::vector<uint32_t>& actions) const;

    ChannelSample linkSample(uint32_t i, uint32_t j) const;

    void setEnergy(uint32_t i, double joules);
    double energy(uint32_t i) const;
    void setKinematics(uint32_t i, double x, double y, double z,
                       double vx, double vy, double vz);

    // Data header from tx demodulated at rx.
    void noteHeader(uint32_t rx, uint32_t tx);

    // Energy-beacon overwrite of the residual header at i for neighbor j.
    void setHeardEnergy(uint32_t i, uint32_t j, double joules);

    // ---- Port A heard mode (amend1 A5 + v3 correction: V in the data header)
    HeardCtl& heard() { return hc_; }
    const HeardCtl& heard() const { return hc_; }
    // Held write from a decoded frame (residual energy, group average, V).
    void hearFrame(uint32_t rx, uint32_t tx, double t, const HeardFrame& f);
    bool fresh(uint32_t i, uint32_t j) const;
    // A2.11: ever held a record of j (never cleared; for first-contact known-sender test).
    bool everHeard(uint32_t i, uint32_t j) const;
    // A2.11 Gate 2: count of nodes with a heard record at i (any age).
    uint32_t heardCount(uint32_t i) const;
    // Sender's own advertised values at its transmission start (I1).
    double ownV(uint32_t s) const;          // max over written Q(s, .) (legacy valueOf(s))
    double ownGroupAverage(uint32_t s) const;
    std::vector<LqarpNode>& nodesMut() { return nodes_; }

private:
    ChannelGate* gate_;
    QelarConfig cfg_;
    std::vector<LqarpNode> nodes_;
    double rRangeM_;
    uint32_t sinkId_ = 0;
    bool sinkSet_ = false;
    std::vector<std::vector<double>> Q_;
    std::vector<std::vector<char>> qWritten_;
    std::vector<std::vector<double>> heardE_;
    std::vector<std::vector<double>> heardGroup_;
    std::vector<std::vector<char>> heard_;
    HeardCtl hc_;
    std::vector<std::vector<double>> tHeard_;
    std::vector<std::vector<double>> heldV_;
    std::vector<std::vector<double>> heldSnr_; // A2.9 fidelity=1

    void checkIndex(uint32_t i) const;
    bool inRange(uint32_t i, uint32_t j) const;
    double groupAverage(uint32_t n) const;
    double valueOf(uint32_t j) const;
};

} // namespace dahello

#endif
