// marv-protocol.cc

#include "marv-protocol.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace dahello {

MarvProtocol::MarvProtocol(ChannelGate& gate, MarvConfig cfg, std::vector<LqarpNode> nodes)
    : gate_(&gate),
      cfg_(cfg),
      nodes_(std::move(nodes)),
      rRangeM_(cfg.rRangeM)
{
    if (nodes_.empty()) {
        throw std::runtime_error("MarvProtocol: empty node list");
    }
    for (size_t i = 0; i < nodes_.size(); ++i) {
        nodes_[i].id = static_cast<uint32_t>(i);
    }
    if (!(rRangeM_ > 0.0)) {
        rRangeM_ = gate_->rangeAtSnrThreshold();
    }
    const size_t n = nodes_.size();
    Q_.assign(n, std::vector<double>(n, cfg_.qInit));
    n_.assign(n, std::vector<uint64_t>(n, 0));
    m_.assign(n, std::vector<uint64_t>(n, 0));
    ownV_.assign(n, 0.0);
    ownVoid_.assign(n, 0);
    MarvRecord r0;
    r0.energy = cfg_.eIni;
    rec_.assign(n, std::vector<MarvRecord>(n, r0));
}

void
MarvProtocol::checkIndex(uint32_t i) const
{
    if (i >= nodes_.size()) {
        throw std::out_of_range("MarvProtocol: node index out of range");
    }
}

void
MarvProtocol::setSink(uint32_t sinkId)
{
    checkIndex(sinkId);
    sinkId_ = sinkId;
    ownV_[sinkId] = 0.0;
    ownVoid_[sinkId] = 0;
}

bool
MarvProtocol::fresh(uint32_t i, uint32_t j) const
{
    return i != j && heard_.fresh(rec_[i][j].heard, rec_[i][j].t);
}

double
MarvProtocol::heldZ(uint32_t i, uint32_t j) const
{
    if (j == sinkId_) {
        return nodes_[sinkId_].z; // I5: fixed, known sink deployment
    }
    const MarvRecord& r = rec_[i][j];
    return (r.tZ >= 0.0) ? r.z : std::numeric_limits<double>::quiet_NaN();
}

void
MarvProtocol::hearZ(uint32_t k, uint32_t j, double z, double t)
{
    if (!std::isfinite(z)) {
        return;
    }
    MarvRecord& r = rec_[k][j];
    r.z = z;
    r.tZ = t;
}

bool
MarvProtocol::inRange(uint32_t i, uint32_t j) const
{
    if (i == j) {
        return false;
    }
    if (heard_.on && !observing()) {
        oracleLeak("MarvProtocol::inRange", i, j); // true 3-D range test
    }
    const LqarpNode& a = nodes_[i];
    const LqarpNode& b = nodes_[j];
    return ChannelGate::distance3D(a.x, a.y, a.z, b.x, b.y, b.z) < rRangeM_;
}

std::vector<uint32_t>
MarvProtocol::rangeSet(uint32_t i) const
{
    checkIndex(i);
    std::vector<uint32_t> out;
    for (uint32_t j = 0; j < nodes_.size(); ++j) {
        // heard: "neighbour" = decoded within T_nb (amend1 A5, MARV has no range test)
        if (heard_.on ? (heard_.leakPlant == 3 ? inRange(i, j) : fresh(i, j)) : inRange(i, j)) {
            out.push_back(j);
        }
    }
    return out;
}

bool
MarvProtocol::qualifies(uint32_t i, uint32_t j) const
{
    if (i == j) {
        return false;
    }
    if (heard_.on && heard_.leakPlant != 1) {
        const double zj = heldZ(i, j);
        if (!(zj < nodes_[i].z)) { // NaN (no held depth) does not qualify
            return false;
        }
    } else {
        if (heard_.on && j != sinkId_) {
            oracleLeak("MarvProtocol::qualifies(true z_j)", i, j); // planted k=1 lands here
        }
        if (!(nodes_[j].z < nodes_[i].z)) {
            return false;
        }
    }
    if (j != sinkId_ && rec_[i][j].voidFlag) {
        return false;
    }
    return true;
}

double
MarvProtocol::psat(uint32_t i, uint32_t j) const
{
    if (n_[i][j] == 0) {
        return cfg_.psatInit;
    }
    return static_cast<double>(m_[i][j]) / static_cast<double>(n_[i][j]);
}

double
MarvProtocol::eCost(double eRes) const
{
    return 1.0 - eRes / cfg_.eIni;
}

MarvReward
MarvProtocol::reward(uint32_t i, uint32_t j) const
{
    MarvReward rw;
    const double wE = 1.0 - cfg_.omega - cfg_.voi;
    const double wD = cfg_.omega + cfg_.voi;
    rw.Ei = eCost(nodes_[i].energy);
    rw.Ej = eCost(rec_[i][j].energy);
    if (heard_.on) {
        rw.D = 1.0 - (nodes_[i].z - heldZ(i, j)) / rRangeM_;
        assertFinite(rw.D, "MarvReward.D", i, j);
    } else {
        rw.D = 1.0 - (nodes_[i].z - nodes_[j].z) / rRangeM_;
    }
    rw.C = 0.0; // D3: cg = 0 for every node -> C = 0/(0+1)
    rw.Rij = -cfg_.gc - wE * (rw.Ei + rw.Ej) - wD * rw.D + cfg_.delta * rw.C;
    // D10: self-transition, D(i,i) = 1, E(j) = E(i), C = 0.
    rw.Rii = -cfg_.gc - wE * (2.0 * rw.Ei) - wD * 1.0 + 0.0;
    rw.psat = psat(i, j);
    rw.r = rw.psat * rw.Rij + (1.0 - rw.psat) * rw.Rii;
    return rw;
}

double
MarvProtocol::qValue(uint32_t i, uint32_t j) const
{
    return Q_[i][j];
}

double
MarvProtocol::advertisedV(uint32_t j) const
{
    return (j == sinkId_) ? 0.0 : ownV_[j];
}

bool
MarvProtocol::advertisedVoid(uint32_t j) const
{
    return (j == sinkId_) ? false : (ownVoid_[j] != 0);
}

std::vector<uint32_t>
MarvProtocol::updateAndRank(uint32_t i, const std::vector<uint32_t>& U)
{
    checkIndex(i);
    if (i == sinkId_) {
        return {};
    }
    if (U.empty()) {
        if (!ownVoid_[i]) {
            ++voidSets_;
        }
        ownVoid_[i] = 1;
        return {};
    }
    auto vOf = [&](uint32_t s) { return (s == sinkId_) ? 0.0 : rec_[i][s].V; };
    double sumAll = 0.0;
    for (uint32_t s : U) {
        sumAll += vOf(s);
    }
    for (uint32_t j : U) {
        const MarvReward rw = reward(i, j);
        const double vj = vOf(j);
        double sumOther = 0.0;
        for (uint32_t s : U) {
            if (s != j) {
                sumOther += vOf(s);
            }
        }
        const double target = rw.r + cfg_.gamma * cfg_.lambda1 * vj
                              + cfg_.gamma * cfg_.lambda2 * sumOther;
        Q_[i][j] = (1.0 - cfg_.alpha) * Q_[i][j] + cfg_.alpha * target;
        if (heard_.on) {
            assertFinite(Q_[i][j], "MarvQ", i, j);
        }
        ++qUpdates_;
    }
    (void)sumAll;
    std::vector<uint32_t> list = U;
    std::stable_sort(list.begin(), list.end(), [&](uint32_t a, uint32_t b) {
        if (Q_[i][a] != Q_[i][b]) {
            return Q_[i][a] > Q_[i][b];
        }
        return a < b;
    });
    ownV_[i] = Q_[i][list.front()];
    ownVoid_[i] = 0;
    return list;
}

void
MarvProtocol::hear(uint32_t k, uint32_t j, double V, bool voidFlag, double energy, double t)
{
    MarvRecord& r = rec_[k][j];
    r.V = V;
    r.voidFlag = voidFlag;
    r.energy = energy;
    r.t = t;
    r.heard = true;
}

void
MarvProtocol::hearEnergy(uint32_t k, uint32_t j, double energy, double t)
{
    MarvRecord& r = rec_[k][j];
    r.energy = energy;
    r.t = t;
}

const MarvRecord&
MarvProtocol::record(uint32_t k, uint32_t j) const
{
    return rec_[k][j];
}

void
MarvProtocol::noteSent(uint32_t i, const std::vector<uint32_t>& list)
{
    if (list.empty()) {
        return;
    }
    if (cfg_.psatScope == MarvPsatScope::Top) {
        ++n_[i][list.front()];
    } else {
        for (uint32_t j : list) {
            ++n_[i][j];
        }
    }
}

void
MarvProtocol::noteForwardOverheard(uint32_t i, uint32_t j, uint32_t topOfList)
{
    if (cfg_.psatScope == MarvPsatScope::Top && j != topOfList) {
        return;
    }
    if (m_[i][j] < n_[i][j]) {
        ++m_[i][j];
    }
}

double
MarvProtocol::tMaxS() const
{
    return 2.0 * rRangeM_ / cfg_.soundSpeedMps; // Eq. 14
}

double
MarvProtocol::holdingTimeS(uint32_t n) const
{
    if (n <= 1) {
        return 0.0;
    }
    return cfg_.k * tMaxS() / (cfg_.h * cfg_.voi) * static_cast<double>(n - 1); // Eq. 13
}

ChannelSample
MarvProtocol::linkSample(uint32_t i, uint32_t j) const
{
    checkIndex(i);
    checkIndex(j);
    if (heard_.on && !observing()) {
        oracleLeak("MarvProtocol::linkSample", i, j); // peer linkSample
    }
    const LqarpNode& a = nodes_[i];
    const LqarpNode& b = nodes_[j];
    return gate_->queryLink(a.x, a.y, a.queryDepthM(), b.x, b.y, b.queryDepthM());
}

void
MarvProtocol::setEnergy(uint32_t i, double joules)
{
    checkIndex(i);
    nodes_[i].energy = joules;
}

double
MarvProtocol::energy(uint32_t i) const
{
    checkIndex(i);
    return nodes_[i].energy;
}

void
MarvProtocol::setKinematics(uint32_t i, double x, double y, double z,
                            double vx, double vy, double vz)
{
    checkIndex(i);
    LqarpNode& n = nodes_[i];
    n.x = x;
    n.y = y;
    n.z = z;
    n.vx = vx;
    n.vy = vy;
    n.vz = vz;
}

} // namespace dahello
