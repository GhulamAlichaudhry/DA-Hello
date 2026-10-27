// saqr-protocol.cc

#include "saqr-protocol.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace dahello {

SaqrProtocol::SaqrProtocol(ChannelGate& gate,
                           SaqrConfig cfg,
                           std::vector<LqarpNode> nodes)
    : gate_(&gate),
      cfg_(cfg),
      nodes_(std::move(nodes)),
      rRangeM_(cfg.rRangeM),
      rng_(cfg.rngSeed)
{
    if (nodes_.empty()) {
        throw std::runtime_error("SaqrProtocol: empty node list");
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
    R_.assign(n, std::vector<double>(n, 0.0));
    heardE_.assign(n, std::vector<double>(n, cfg_.eIni));
    heard_.assign(n, std::vector<char>(n, 0));
    nTotal_.assign(n, 0);
    nTrans_.assign(n, 0);
    decisions_.assign(n, 0);
    held_.assign(n, std::vector<Held>(n));
    sendIJ_.assign(n, std::vector<uint32_t>(n, 0));
    ackIJ_.assign(n, std::vector<uint32_t>(n, 0));
}

bool
SaqrProtocol::fresh(uint32_t i, uint32_t j) const
{
    return i != j && hc_.fresh(heard_[i][j] != 0, held_[i][j].t);
}

void
SaqrProtocol::hearFrame(uint32_t rx, uint32_t tx, double t, const HeardFrame& f)
{
    checkIndex(rx);
    checkIndex(tx);
    if (rx == tx) {
        return;
    }
    Held& h = held_[rx][tx];
    if (std::isfinite(f.energy)) {
        heardE_[rx][tx] = f.energy;
    }
    if (std::isfinite(f.number)) {
        h.number = f.number;
    }
    if (std::isfinite(f.V)) {
        h.V = f.V;
    }
    if (f.poseCarried && std::isfinite(f.x) && std::isfinite(f.y) && std::isfinite(f.z)) {
        h.pose = true;
        h.tPose = t;
        h.x = f.x; h.y = f.y; h.z = f.z;
        h.vx = std::isfinite(f.vx) ? f.vx : 0.0;
        h.vy = std::isfinite(f.vy) ? f.vy : 0.0;
        h.vz = std::isfinite(f.vz) ? f.vz : 0.0;
    }
    heard_[rx][tx] = 1;
    h.t = t;
}

bool
SaqrProtocol::heldPos(uint32_t i, uint32_t j, double& x, double& y, double& z) const
{
    if (sinkSet_ && j == sinkId_) {
        // I5: fixed, known sink deployment position
        x = nodes_[sinkId_].x; y = nodes_[sinkId_].y; z = nodes_[sinkId_].z;
        return true;
    }
    const Held& h = held_[i][j];
    if (!h.pose) {
        return false;
    }
    const double age = std::max(0.0, hc_.now() - h.tPose); // I4: held pose + held velocity x age
    x = h.x + h.vx * age;
    y = h.y + h.vy * age;
    z = h.z + h.vz * age;
    return true;
}

double
SaqrProtocol::plrOf(uint32_t i, uint32_t j) const
{
    if (!hc_.on) {
        return rawPlr(j);
    }
    const uint32_t s = sendIJ_[i][j];
    if (s == 0) {
        return 0.0; // as legacy with nTotal == 0
    }
    const uint32_t a = std::min(ackIJ_[i][j], s);
    return static_cast<double>(s - a) / static_cast<double>(s);
}

void
SaqrProtocol::noteOwnSend(uint32_t i, uint32_t j)
{
    checkIndex(i);
    checkIndex(j);
    ++sendIJ_[i][j];
}

void
SaqrProtocol::noteOwnAck(uint32_t i, uint32_t j)
{
    checkIndex(i);
    checkIndex(j);
    ++ackIJ_[i][j];
}

double
SaqrProtocol::ownV(uint32_t s) const
{
    checkIndex(s);
    if (sinkSet_ && s == sinkId_) {
        return 0.0;
    }
    double v = 0.0;
    bool any = false;
    for (uint32_t a = 0; a < nodes_.size(); ++a) {
        if (a == s || !qWritten_[s][a]) {
            continue;
        }
        if (!any || Q_[s][a] < v) {
            v = Q_[s][a];
            any = true;
        }
    }
    return any ? v : 0.0;
}

uint32_t
SaqrProtocol::ownNumber(uint32_t s) const
{
    checkIndex(s);
    uint32_t n = 0;
    for (uint32_t j = 0; j < nodes_.size(); ++j) {
        if (fresh(s, j)) {
            ++n;
        }
    }
    return n;
}

void
SaqrProtocol::checkIndex(uint32_t i) const
{
    if (i >= nodes_.size()) {
        throw std::out_of_range("SaqrProtocol: node index out of range");
    }
}

void
SaqrProtocol::setSink(uint32_t sinkId)
{
    checkIndex(sinkId);
    sinkId_ = sinkId;
    sinkSet_ = true;
}

ChannelSample
SaqrProtocol::linkSample(uint32_t i, uint32_t j) const
{
    checkIndex(i);
    checkIndex(j);
    if (hc_.on && !observing()) {
        oracleLeak("SaqrProtocol::linkSample", i, j);
    }
    const LqarpNode& a = nodes_[i];
    const LqarpNode& b = nodes_[j];
    return gate_->queryLink(a.x, a.y, a.queryDepthM(),
                            b.x, b.y, b.queryDepthM());
}

bool
SaqrProtocol::inRange(uint32_t i, uint32_t j) const
{
    if (i == j) {
        return false;
    }
    if (hc_.on && !observing()) {
        oracleLeak("SaqrProtocol::inRange", i, j);
    }
    const LqarpNode& a = nodes_[i];
    const LqarpNode& b = nodes_[j];
    return ChannelGate::distance3D(a.x, a.y, a.z, b.x, b.y, b.z) < rRangeM_;
}

uint32_t
SaqrProtocol::neighborCount(uint32_t i) const
{
    if (hc_.on && !observing()) {
        oracleLeak("SaqrProtocol::neighborCount(true)", i, i);
    }
    uint32_t n = 0;
    for (uint32_t j = 0; j < nodes_.size(); ++j) {
        if (inRange(i, j)) {
            ++n;
        }
    }
    return n;
}

double
SaqrProtocol::distToSink(uint32_t i) const
{
    if (!sinkSet_) {
        return 0.0;
    }
    const LqarpNode& a = nodes_[i];
    const LqarpNode& s = nodes_[sinkId_];
    return ChannelGate::distance3D(a.x, a.y, a.z, s.x, s.y, s.z);
}

std::vector<uint32_t>
SaqrProtocol::candidateSet(uint32_t i) const
{
    checkIndex(i);
    if (hc_.on && hc_.leakPlant != 4) {
        // heard (amend1 A5): fresh, closer (aged held pose vs I5 sink), popular (held number)
        std::vector<uint32_t> oneHop;
        for (uint32_t j = 0; j < nodes_.size(); ++j) {
            if (fresh(i, j)) {
                oneHop.push_back(j);
            }
        }
        if (oneHop.empty() || !sinkSet_) {
            return {};
        }
        const LqarpNode& a = nodes_[i];
        const LqarpNode& sk = nodes_[sinkId_];
        const double di = ChannelGate::distance3D(a.x, a.y, a.z, sk.x, sk.y, sk.z);
        double sumNum = 0.0;
        for (uint32_t k : oneHop) {
            const double nk = held_[i][k].number;
            sumNum += std::isfinite(nk) ? nk : 0.0;
        }
        const double meanNum = sumNum / static_cast<double>(oneHop.size());
        std::vector<uint32_t> out;
        for (uint32_t j : oneHop) {
            double x, y, z;
            if (!heldPos(i, j, x, y, z)) {
                continue; // no held pose or depth -> closeness undefined -> not a candidate
            }
            const bool closer = ChannelGate::distance3D(x, y, z, sk.x, sk.y, sk.z) < di;
            const double nj = held_[i][j].number;
            const bool popular = (std::isfinite(nj) ? nj : 0.0) >= meanNum;
            if (closer && popular) {
                out.push_back(j);
            }
        }
        return out;
    }
    if (hc_.on && hc_.leakPlant == 4) {
        // planted leak k=4: SAQR reads the true neighbour count (guard must abort)
        (void)neighborCount(i == 0 ? 1 : 0);
    }
    std::vector<uint32_t> oneHop;
    for (uint32_t j = 0; j < nodes_.size(); ++j) {
        if (inRange(i, j)) {
            oneHop.push_back(j);
        }
    }
    if (oneHop.empty() || !sinkSet_) {
        return {};
    }
    const double di = distToSink(i);
    double sumNum = 0.0;
    for (uint32_t k : oneHop) {
        sumNum += static_cast<double>(neighborCount(k));
    }
    const double meanNum = sumNum / static_cast<double>(oneHop.size());
    std::vector<uint32_t> out;
    for (uint32_t j : oneHop) {
        const bool closer = distToSink(j) < di;
        const bool popular = static_cast<double>(neighborCount(j)) >= meanNum;
        if (closer && popular) {
            out.push_back(j);
        }
    }
    return out;
}

double
SaqrProtocol::heardResidual(uint32_t i, uint32_t j) const
{
    return heard_[i][j] ? heardE_[i][j] : cfg_.eIni;
}

double
SaqrProtocol::rawPlr(uint32_t j) const
{
    if (hc_.on && !observing()) {
        oracleLeak("SaqrProtocol::rawPlr(j counters)", j, j);
    }
    if (nTotal_[j] == 0) {
        return 0.0;
    }
    const double lost = static_cast<double>(nTotal_[j] - nTrans_[j]);
    return lost / static_cast<double>(nTotal_[j]);
}

double
SaqrProtocol::rawDelay(uint32_t i, uint32_t j) const
{
    const LqarpNode& a = nodes_[i];
    double dist;
    if (hc_.on) {
        double x, y, z;
        if (heldPos(i, j, x, y, z)) {
            dist = ChannelGate::distance3D(a.x, a.y, a.z, x, y, z);
        } else {
            // amend1: "else SNR-inverted range". Unreachable here: a j without a held
            // pose is never a SAQR candidate (closeness undefined). Fail closed at R_op.
            dist = rRangeM_;
        }
    } else {
        const LqarpNode& b = nodes_[j];
        dist = ChannelGate::distance3D(a.x, a.y, a.z, b.x, b.y, b.z);
    }
    const double c = (cfg_.soundSpeedMps > 0.0) ? cfg_.soundSpeedMps : 1500.0;
    const double rate = (cfg_.bitRateBps > 0.0) ? cfg_.bitRateBps : 1000.0;
    return dist / c + static_cast<double>(cfg_.packetBits) / rate;
}

double
SaqrProtocol::rawEr(uint32_t i, uint32_t j) const
{
    // Cumulative transmit plus receive energy is E_ini - residual in this
    // harness (no idle listen). Eq. (18) divides that by residual.
    const double e = heardResidual(i, j);
    if (!(e > 0.0)) {
        return 1.0e9;
    }
    double used = cfg_.eIni - e;
    if (used < 0.0) {
        used = 0.0;
    }
    return used / e;
}

double
SaqrProtocol::rawAlm(uint32_t i, uint32_t j) const
{
    if (!sinkSet_ || i == sinkId_) {
        return 0.0;
    }
    const LqarpNode& a = nodes_[i];
    const LqarpNode& d = nodes_[sinkId_];
    double bx, by, bz;
    if (hc_.on) {
        if (!heldPos(i, j, bx, by, bz)) {
            return 1.0e9; // no held pose: no progress known
        }
    } else {
        bx = nodes_[j].x; by = nodes_[j].y; bz = nodes_[j].z;
    }
    const double ijx = bx - a.x;
    const double ijy = by - a.y;
    const double ijz = bz - a.z;
    const double idx = d.x - a.x;
    const double idy = d.y - a.y;
    const double idz = d.z - a.z;
    const double id = std::sqrt(idx * idx + idy * idy + idz * idz);
    const double ij = std::sqrt(ijx * ijx + ijy * ijy + ijz * ijz);
    if (!(id > 0.0)) {
        return 0.0;
    }
    const double progress = (ijx * idx + ijy * idy + ijz * idz) / id;
    if (!(progress > 1.0e-6)) {
        return 1.0e9;
    }
    return ij / progress;
}

double
SaqrProtocol::minMaxStar(double x, double lo, double hi, uint32_t n)
{
    if (n <= 1) {
        return 0.0;
    }
    const double span = hi - lo;
    if (!(span > 0.0)) {
        return 0.0;
    }
    return (x - lo) / span;
}

SaqrReward
SaqrProtocol::reward(uint32_t i, uint32_t j) const
{
    checkIndex(i);
    checkIndex(j);
    SaqrReward rw;
    rw.plr = plrOf(i, j);
    rw.delay = rawDelay(i, j);
    rw.er = rawEr(i, j);
    rw.alm = rawAlm(i, j);
    rw.R = R_[i][j];
    return rw;
}

double
SaqrProtocol::qValue(uint32_t i, uint32_t j) const
{
    checkIndex(i);
    checkIndex(j);
    return Q_[i][j];
}

double
SaqrProtocol::minWrittenQ(uint32_t j) const
{
    if (sinkSet_ && j == sinkId_) {
        return 0.0;
    }
    if (hc_.on) {
        oracleLeak("SaqrProtocol::minWrittenQ(j's Q row)", j, j);
    }
    double v = 0.0;
    bool any = false;
    for (uint32_t a = 0; a < nodes_.size(); ++a) {
        if (a == j || !qWritten_[j][a]) {
            continue;
        }
        if (!any || Q_[j][a] < v) {
            v = Q_[j][a];
            any = true;
        }
    }
    return any ? v : 0.0;
}

void
SaqrProtocol::learnFrom(uint32_t i, const std::vector<uint32_t>& actions)
{
    checkIndex(i);
    std::vector<uint32_t> acts;
    for (uint32_t j : actions) {
        if (j != i && j < nodes_.size()) {
            acts.push_back(j);
        }
    }
    if (acts.empty()) {
        return;
    }
    double loP = 0, hiP = 0, loD = 0, hiD = 0, loE = 0, hiE = 0, loA = 0, hiA = 0;
    bool first = true;
    for (uint32_t j : acts) {
        const double p = plrOf(i, j);
        const double d = rawDelay(i, j);
        const double e = rawEr(i, j);
        const double a = rawAlm(i, j);
        if (first) {
            loP = hiP = p;
            loD = hiD = d;
            loE = hiE = e;
            loA = hiA = a;
            first = false;
        } else {
            loP = std::min(loP, p);
            hiP = std::max(hiP, p);
            loD = std::min(loD, d);
            hiD = std::max(hiD, d);
            loE = std::min(loE, e);
            hiE = std::max(hiE, e);
            loA = std::min(loA, a);
            hiA = std::max(hiA, a);
        }
    }
    const uint32_t n = static_cast<uint32_t>(acts.size());
    for (uint32_t j : acts) {
        SaqrReward rw;
        rw.plr = plrOf(i, j);
        rw.delay = rawDelay(i, j);
        rw.er = rawEr(i, j);
        rw.alm = rawAlm(i, j);
        rw.plrStar = minMaxStar(rw.plr, loP, hiP, n);
        rw.delayStar = minMaxStar(rw.delay, loD, hiD, n);
        rw.erStar = minMaxStar(rw.er, loE, hiE, n);
        rw.almStar = minMaxStar(rw.alm, loA, hiA, n);
        rw.R = cfg_.rho * rw.plrStar + cfg_.sigma * rw.delayStar
               + cfg_.tau * rw.erStar + cfg_.phi * rw.almStar;
        const double old = qWritten_[i][j] ? Q_[i][j] : 0.0;
        double vj;
        if (hc_.on) {
            const double hv = held_[i][j].V;
            vj = (sinkSet_ && j == sinkId_) ? 0.0 : (std::isfinite(hv) ? hv : 0.0);
        } else {
            vj = minWrittenQ(j);
        }
        const double target = rw.R + cfg_.gamma * vj;
        Q_[i][j] = cfg_.alpha * target + (1.0 - cfg_.alpha) * old;
        if (hc_.on) {
            assertFinite(Q_[i][j], "SaqrQ", i, j);
        }
        R_[i][j] = rw.R;
        qWritten_[i][j] = 1;
    }
}

int
SaqrProtocol::nextHop(uint32_t i, const std::vector<uint32_t>& actions) const
{
    checkIndex(i);
    std::vector<uint32_t> acts;
    for (uint32_t j : actions) {
        if (j != i && j < nodes_.size()) {
            acts.push_back(j);
        }
    }
    if (acts.empty()) {
        return -1;
    }
    const double eps = (decisions_[i] >= cfg_.exploreDecisions) ? cfg_.epsilonLate
                                                                : cfg_.epsilon0;
    ++decisions_[i];
    std::uniform_real_distribution<double> unit(0.0, 1.0);
    if (unit(rng_) < eps) {
        std::uniform_int_distribution<size_t> pick(0, acts.size() - 1);
        return static_cast<int>(acts[pick(rng_)]);
    }
    int best = -1;
    double bestQ = 0.0;
    for (uint32_t j : acts) {
        const double q = Q_[i][j];
        if (best < 0 || q < bestQ || (q == bestQ && static_cast<int>(j) < best)) {
            best = static_cast<int>(j);
            bestQ = q;
        }
    }
    return best;
}

void
SaqrProtocol::setEnergy(uint32_t i, double joules)
{
    checkIndex(i);
    nodes_[i].energy = joules;
}

double
SaqrProtocol::energy(uint32_t i) const
{
    checkIndex(i);
    return nodes_[i].energy;
}

void
SaqrProtocol::setKinematics(uint32_t i, double x, double y, double z,
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
SaqrProtocol::noteHeader(uint32_t rx, uint32_t tx)
{
    checkIndex(rx);
    checkIndex(tx);
    if (rx == tx) {
        return;
    }
    if (hc_.on) {
        oracleLeak("SaqrProtocol::noteHeader(true E of tx)", rx, tx);
    }
    heardE_[rx][tx] = nodes_[tx].energy;
    heard_[rx][tx] = 1;
}

void
SaqrProtocol::setHeardEnergy(uint32_t i, uint32_t j, double joules)
{
    checkIndex(i);
    checkIndex(j);
    if (i == j) {
        return;
    }
    heardE_[i][j] = joules;
    heard_[i][j] = 1;
}

void
SaqrProtocol::noteTxAttempt(uint32_t tx)
{
    checkIndex(tx);
    ++nTotal_[tx];
}

void
SaqrProtocol::noteTxSuccess(uint32_t tx)
{
    checkIndex(tx);
    if (nTrans_[tx] < nTotal_[tx]) {
        ++nTrans_[tx];
    }
}

} // namespace dahello
