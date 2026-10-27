// rlor-inspired-protocol.cc
//
// Simplified RLOR-inspired baseline. See the header limitation block.

#include "rlor-inspired-protocol.h"

#include <stdexcept>

namespace dahello {

RlorInspiredProtocol::RlorInspiredProtocol(ChannelGate& gate,
                                           LqarpConfig cfg,
                                           std::vector<LqarpNode> nodes)
    : gate_(&gate),
      cfg_(cfg),
      nodes_(std::move(nodes)),
      rRangeM_(cfg.rRangeM)
{
    if (nodes_.empty()) {
        throw std::runtime_error("RlorInspiredProtocol: empty node list");
    }
    for (size_t i = 0; i < nodes_.size(); ++i) {
        nodes_[i].id = static_cast<uint32_t>(i);
    }
    if (!std::isfinite(rRangeM_)) {
        rRangeM_ = gate_->rangeAtSnrThreshold();
    }
    const size_t n = nodes_.size();
    frozen_.assign(n, std::vector<FrozenNeighbor>(n));
    discoverySuppressed_.assign(n, 0);
}

void
RlorInspiredProtocol::checkIndex(uint32_t i) const
{
    if (i >= nodes_.size()) {
        throw std::out_of_range("RlorInspiredProtocol: node index out of range");
    }
}

ChannelSample
RlorInspiredProtocol::linkSample(uint32_t i, uint32_t j) const
{
    checkIndex(i);
    checkIndex(j);
    const LqarpNode& a = nodes_[i];
    const LqarpNode& b = nodes_[j];
    return gate_->queryLink(a.x, a.y, a.queryDepthM(),
                            b.x, b.y, b.queryDepthM());
}

bool
RlorInspiredProtocol::liveInRange(uint32_t i, uint32_t j) const
{
    if (i == j) {
        return false;
    }
    const LqarpNode& a = nodes_[i];
    const LqarpNode& b = nodes_[j];
    return ChannelGate::distance3D(a.x, a.y, a.z, b.x, b.y, b.z) < rRangeM_;
}

bool
RlorInspiredProtocol::liveInSnr(uint32_t i, uint32_t j) const
{
    if (i == j) {
        return false;
    }
    return !gate_->sampleFailsCandidateSnr(linkSample(i, j));
}

void
RlorInspiredProtocol::noteDiscovery(uint32_t i, uint32_t j) const
{
    if (i == j || discoverySuppressed_[j]) {
        return;
    }
    FrozenNeighbor& h = frozen_[i][j];
    if (h.heard) {
        return;
    }
    // First live contact under I_RLOR = N_range ∩ N_SNR. This protocol
    // has no N_depth, so a deeper neighbor is stored. Not refreshed.
    if (!(liveInRange(i, j) && liveInSnr(i, j))) {
        return;
    }
    const LqarpNode& b = nodes_[j];
    h.x_j_zoh = b.x;
    h.y_j_zoh = b.y;
    h.z_j_zoh = b.z;
    h.energy_j_zoh = b.energy;
    h.query_z_j_zoh = b.queryDepthM();
    h.heard = true;
}

ChannelSample
RlorInspiredProtocol::heldLinkSample(uint32_t i, uint32_t j) const
{
    const FrozenNeighbor& h = frozen_[i][j];
    const LqarpNode& a = nodes_[i];
    return gate_->queryLink(a.x, a.y, a.queryDepthM(),
                            h.x_j_zoh, h.y_j_zoh, h.query_z_j_zoh);
}

bool
RlorInspiredProtocol::inNRange(uint32_t i, uint32_t j) const
{
    checkIndex(i);
    checkIndex(j);
    if (i == j) {
        return false;
    }
    noteDiscovery(i, j);
    const FrozenNeighbor& h = frozen_[i][j];
    if (!h.heard) {
        return false;
    }
    const LqarpNode& a = nodes_[i];
    return ChannelGate::distance3D(a.x, a.y, a.z,
                                   h.x_j_zoh, h.y_j_zoh, h.z_j_zoh) < rRangeM_;
}

bool
RlorInspiredProtocol::inNSnr(uint32_t i, uint32_t j) const
{
    checkIndex(i);
    checkIndex(j);
    if (i == j) {
        return false;
    }
    noteDiscovery(i, j);
    const FrozenNeighbor& h = frozen_[i][j];
    if (!h.heard) {
        return false;
    }
    return !gate_->sampleFailsCandidateSnr(heldLinkSample(i, j));
}

std::vector<uint32_t>
RlorInspiredProtocol::candidateSet(uint32_t i) const
{
    checkIndex(i);
    std::vector<uint32_t> I;
    for (uint32_t j = 0; j < nodes_.size(); ++j) {
        if (inNRange(i, j) && inNSnr(i, j)) {
            I.push_back(j);
        }
    }
    return I;
}

double
RlorInspiredProtocol::pSucc(uint32_t i, uint32_t j) const
{
    checkIndex(i);
    checkIndex(j);
    noteDiscovery(i, j);
    if (!frozen_[i][j].heard) {
        return 0.0;
    }
    const ChannelSample s = heldLinkSample(i, j);
    return dahello::pSucc(s.snr_db, cfg_.packetBits, cfg_.constellationM);
}

int
RlorInspiredProtocol::nextHop(uint32_t i) const
{
    return nextHop(i, candidateSet(i));
}

int
RlorInspiredProtocol::nextHop(uint32_t i, const std::vector<uint32_t>& actions) const
{
    if (actions.empty()) {
        return -1;
    }
    uint32_t best = actions.front();
    double bestP = pSucc(i, best);
    for (uint32_t j : actions) {
        const double p = pSucc(i, j);
        if (p > bestP || (p == bestP && j < best)) {
            bestP = p;
            best = j;
        }
    }
    return static_cast<int>(best);
}

void
RlorInspiredProtocol::setEnergy(uint32_t i, double joules)
{
    checkIndex(i);
    nodes_[i].energy = joules;
}

double
RlorInspiredProtocol::energy(uint32_t i) const
{
    checkIndex(i);
    return nodes_[i].energy;
}

void
RlorInspiredProtocol::setKinematics(uint32_t i, double x, double y, double z,
                                    double vx, double vy, double vz)
{
    checkIndex(i);
    nodes_[i].x = x;
    nodes_[i].y = y;
    nodes_[i].z = z;
    nodes_[i].vx = vx;
    nodes_[i].vy = vy;
    nodes_[i].vz = vz;
}

void
RlorInspiredProtocol::setDiscoverySuppressed(uint32_t j, bool suppressed)
{
    checkIndex(j);
    discoverySuppressed_[j] = suppressed ? 1 : 0;
}

} // namespace dahello
