// rlor-inspired-protocol.h
//
// =============================================================================
// RLOR-INSPIRED BASELINE — DOCUMENTED SIMPLIFICATION, NOT A FAITHFUL
// REIMPLEMENTATION OF RLOR.
//
// LQARP's paper describes RLOR only briefly: "an opportunistic routing
// protocol featuring two reward mechanisms, designed to improve the
// data transmission rate in sparse UWSNs," and separately, "a
// distributed routing strategy in which the packet transmission
// success rate is used to guide next-hop selection." That is not
// enough detail to faithfully reproduce RLOR's actual algorithm.
//
// This class is therefore a simplified, explicitly labeled
// "RLOR-inspired" baseline for comparative experiments only. It must
// not be cited as an implementation of the original RLOR protocol.
// =============================================================================

#ifndef DAHELLO_RLOR_INSPIRED_PROTOCOL_H
#define DAHELLO_RLOR_INSPIRED_PROTOCOL_H

#include "lqarp-protocol.h"

#include <cstdint>
#include <vector>

namespace dahello {

// RLOR's I is already N_range ∩ N_SNR (no N_depth). SPEC 1.5's
// detour exists to escape depth-DAG dead ends; that failure mode
// does not apply here, so this class has no detour path. Do not add
// one.

class RlorInspiredProtocol {
public:
    RlorInspiredProtocol(ChannelGate& gate,
                         LqarpConfig cfg,
                         std::vector<LqarpNode> nodes);

    const LqarpConfig& config() const { return cfg_; }
    const std::vector<LqarpNode>& nodes() const { return nodes_; }
    double rRangeM() const { return rRangeM_; }

    // I_RLOR = N_range ∩ N_SNR   (NO N_depth — opportunistic, not
    // depth-progressive). SPEC.md Part 3.
    bool inNRange(uint32_t i, uint32_t j) const;
    bool inNSnr(uint32_t i, uint32_t j) const;
    std::vector<uint32_t> candidateSet(uint32_t i) const;

    // R(s_i, s_j) = P_succ(s_i, s_j) via the shared BER floor.
    double pSucc(uint32_t i, uint32_t j) const;
    double reward(uint32_t i, uint32_t j) const { return pSucc(i, j); }

    // Greedy opportunistic: argmax P_succ. No Q-learning / TD update.
    // Ties → smallest id. Returns -1 if I_RLOR is empty.
    int nextHop(uint32_t i) const;
    int nextHop(uint32_t i, const std::vector<uint32_t>& actions) const;

    ChannelSample linkSample(uint32_t i, uint32_t j) const;

    void setEnergy(uint32_t i, double joules);
    double energy(uint32_t i) const;
    void setKinematics(uint32_t i, double x, double y, double z,
                       double vx, double vy, double vz);
    void setDiscoverySuppressed(uint32_t j, bool suppressed);

private:
    // Same one-shot hold as LQARP. Discovery is live N_range ∩ N_SNR
    // (this protocol has no N_depth). The copy is never refreshed.
    struct FrozenNeighbor {
        double x_j_zoh = 0.0;
        double y_j_zoh = 0.0;
        double z_j_zoh = 0.0;
        double energy_j_zoh = 0.0;
        double query_z_j_zoh = 0.0;
        bool heard = false;
    };

    ChannelGate* gate_;
    LqarpConfig cfg_;
    std::vector<LqarpNode> nodes_;
    double rRangeM_;
    mutable std::vector<std::vector<FrozenNeighbor>> frozen_;
    std::vector<char> discoverySuppressed_;

    void checkIndex(uint32_t i) const;
    bool liveInRange(uint32_t i, uint32_t j) const;
    bool liveInSnr(uint32_t i, uint32_t j) const;
    void noteDiscovery(uint32_t i, uint32_t j) const;
    ChannelSample heldLinkSample(uint32_t i, uint32_t j) const;
};

} // namespace dahello

#endif // DAHELLO_RLOR_INSPIRED_PROTOCOL_H
