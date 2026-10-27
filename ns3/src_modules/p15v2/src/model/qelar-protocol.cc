// qelar-protocol.cc

#include "qelar-protocol.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace dahello {

QelarProtocol::QelarProtocol(ChannelGate& gate,
                             QelarConfig cfg,
                             std::vector<LqarpNode> nodes)
    : gate_(&gate),
      cfg_(cfg),
      nodes_(std::move(nodes)),
      rRangeM_(cfg.rRangeM)
{
    if (nodes_.empty()) {
        throw std::runtime_error("QelarProtocol: empty node list");
    }
    for (size_t i = 0; i < nodes_.size(); ++i) {
        nodes_[i].id = static_cast<uint32_t>(i);
    }
    if (!(rRangeM_ > 0.0)) {
        rRangeM_ = gate_->rangeAtSnrThreshold();
    }
    const size_t n = nodes_.size();
    Q_.assign(n, std::vector<double>(n, 0.0));
    qWritten_.assign(n, std::vector<char>(n, 0));
    heardE_.assign(n, std::vector<double>(n, cfg_.eIni));
    heardGroup_.assign(n, std::vector<double>(n, cfg_.eIni));
    heard_.assign(n, std::vector<char>(n, 0));
    tHeard_.assign(n, std::vector<double>(n, -1.0));
    heldV_.assign(n, std::vector<double>(n, std::numeric_limits<double>::quiet_NaN()));
}

bool
QelarProtocol::fresh(uint32_t i, uint32_t j) const
{
    return i != j && hc_.fresh(heard_[i][j] != 0, tHeard_[i][j]);
}

void
QelarProtocol::hearFrame(uint32_t rx, uint32_t tx, double t, const HeardFrame& f)
{
    checkIndex(rx);
    checkIndex(tx);
    if (rx == tx) {
        return;
    }
    if (std::isfinite(f.energy)) {
        heardE_[rx][tx] = f.energy;
    }
    if (std::isfinite(f.groupAvg)) {
        heardGroup_[rx][tx] = f.groupAvg;
    }
    if (std::isfinite(f.V)) {
        heldV_[rx][tx] = f.V;
    }
    heard_[rx][tx] = 1;
    tHeard_[rx][tx] = t;
}

double
QelarProtocol::ownV(uint32_t s) const
{
    checkIndex(s);
    double v = 0.0;
    bool any = false;
    for (uint32_t a = 0; a < nodes_.size(); ++a) {
        if (a == s || !qWritten_[s][a]) {
            continue;
        }
        if (!any || Q_[s][a] > v) {
            v = Q_[s][a];
            any = true;
        }
    }
    return any ? v : 0.0;
}

double
QelarProtocol::ownGroupAverage(uint32_t s) const
{
    checkIndex(s);
    return groupAverage(s);
}

void
QelarProtocol::checkIndex(uint32_t i) const
{
    if (i >= nodes_.size()) {
        throw std::out_of_range("QelarProtocol: node index out of range");
    }
}

void
QelarProtocol::setSink(uint32_t sinkId)
{
    checkIndex(sinkId);
    sinkId_ = sinkId;
    sinkSet_ = true;
}

ChannelSample
QelarProtocol::linkSample(uint32_t i, uint32_t j) const
{
    checkIndex(i);
    checkIndex(j);
    if (hc_.on && !observing()) {
        oracleLeak("QelarProtocol::linkSample", i, j);
    }
    const LqarpNode& a = nodes_[i];
    const LqarpNode& b = nodes_[j];
    return gate_->queryLink(a.x, a.y, a.queryDepthM(),
                            b.x, b.y, b.queryDepthM());
}

bool
QelarProtocol::inRange(uint32_t i, uint32_t j) const
{
    if (i == j) {
        return false;
    }
    if (hc_.on && !observing()) {
        oracleLeak("QelarProtocol::inRange", i, j);
    }
    const LqarpNode& a = nodes_[i];
    const LqarpNode& b = nodes_[j];
    return ChannelGate::distance3D(a.x, a.y, a.z, b.x, b.y, b.z) < rRangeM_;
}

std::vector<uint32_t>
QelarProtocol::candidateSet(uint32_t i) const
{
    checkIndex(i);
    std::vector<uint32_t> out;
    for (uint32_t j = 0; j < nodes_.size(); ++j) {
        if (hc_.on ? fresh(i, j) : inRange(i, j)) { // heard: candidates = fresh records
            out.push_back(j);
        }
    }
    return out;
}

double
QelarProtocol::groupAverage(uint32_t n) const
{
    double sum = nodes_[n].energy;
    uint32_t count = 1;
    for (uint32_t j = 0; j < nodes_.size(); ++j) {
        if (hc_.on ? !fresh(n, j) : !inRange(n, j)) { // heard: over fresh records
            continue;
        }
        sum += heard_[n][j] ? heardE_[n][j] : cfg_.eIni;
        ++count;
    }
    return sum / static_cast<double>(count);
}

void
QelarProtocol::noteHeader(uint32_t rx, uint32_t tx)
{
    checkIndex(rx);
    checkIndex(tx);
    if (rx == tx) {
        return;
    }
    if (hc_.on) {
        oracleLeak("QelarProtocol::noteHeader(true E/G of tx)", rx, tx); // heard: hearFrame
    }
    heardE_[rx][tx] = nodes_[tx].energy;
    heardGroup_[rx][tx] = groupAverage(tx);
    heard_[rx][tx] = 1;
}

void
QelarProtocol::setHeardEnergy(uint32_t i, uint32_t j, double joules)
{
    checkIndex(i);
    checkIndex(j);
    if (i == j) {
        return;
    }
    heardE_[i][j] = joules;
    heard_[i][j] = 1;
}

QelarReward
QelarProtocol::reward(uint32_t i, uint32_t j) const
{
    checkIndex(i);
    checkIndex(j);
    QelarReward rw;
    const double eIni = (cfg_.eIni > 0.0) ? cfg_.eIni : 1.0;
    const double eNext = heard_[i][j] ? heardE_[i][j] : cfg_.eIni;
    const double gNext = heard_[i][j] ? heardGroup_[i][j] : cfg_.eIni;
    rw.cSender = 1.0 - nodes_[i].energy / eIni;
    rw.cNext = 1.0 - eNext / eIni;
    const double twoOverPi = 2.0 / std::acos(-1.0);
    rw.dSender = twoOverPi * std::atan(nodes_[i].energy - groupAverage(i));
    rw.dNext = twoOverPi * std::atan(eNext - gNext);
    rw.R = -cfg_.g
           - cfg_.alpha1 * (rw.cSender + rw.cNext)
           + cfg_.alpha2 * (rw.dSender + rw.dNext);
    return rw;
}

double
QelarProtocol::valueOf(uint32_t j) const
{
    if (sinkSet_ && j == sinkId_) {
        return 0.0;
    }
    if (hc_.on) {
        oracleLeak("QelarProtocol::valueOf(j's Q row)", j, j); // heard: held V
    }
    // Unwritten entries stay 0, which is the paper's initial value, but
    // a written reward is negative. Max must ignore entries never computed,
    // or the zeros would always beat a real Q.
    double v = 0.0;
    bool any = false;
    for (uint32_t a = 0; a < nodes_.size(); ++a) {
        if (a == j || !qWritten_[j][a]) {
            continue;
        }
        if (!any || Q_[j][a] > v) {
            v = Q_[j][a];
            any = true;
        }
    }
    return any ? v : 0.0;
}

double
QelarProtocol::qValue(uint32_t i, uint32_t j) const
{
    checkIndex(i);
    checkIndex(j);
    return Q_[i][j];
}

void
QelarProtocol::learnFrom(uint32_t i, const std::vector<uint32_t>& actions)
{
    checkIndex(i);
    for (uint32_t j : actions) {
        checkIndex(j);
        if (j == i) {
            continue;
        }
        const double R = reward(i, j).R;
        double vj;
        if (hc_.on) {
            // held V of j's last decoded header (sink: 0, terminal); none held -> 0 (paper init)
            vj = (sinkSet_ && j == sinkId_) ? 0.0
                 : (std::isfinite(heldV_[i][j]) ? heldV_[i][j] : 0.0);
        } else {
            vj = valueOf(j);
        }
        Q_[i][j] = R + cfg_.gamma * vj;
        if (hc_.on) {
            assertFinite(Q_[i][j], "QelarQ", i, j);
        }
        qWritten_[i][j] = 1;
    }
}

int
QelarProtocol::nextHop(uint32_t i, const std::vector<uint32_t>& actions) const
{
    checkIndex(i);
    int best = -1;
    double bestQ = 0.0;
    for (uint32_t j : actions) {
        if (j == i || j >= nodes_.size()) {
            continue;
        }
        const double q = Q_[i][j];
        if (best < 0 || q > bestQ || (q == bestQ && static_cast<int>(j) < best)) {
            best = static_cast<int>(j);
            bestQ = q;
        }
    }
    return best;
}

void
QelarProtocol::setEnergy(uint32_t i, double joules)
{
    checkIndex(i);
    nodes_[i].energy = joules;
}

double
QelarProtocol::energy(uint32_t i) const
{
    checkIndex(i);
    return nodes_[i].energy;
}

void
QelarProtocol::setKinematics(uint32_t i, double x, double y, double z,
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

} // namespace dahello
