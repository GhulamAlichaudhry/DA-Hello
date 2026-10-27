// lqarp-protocol.cc
//
// LQARP baseline exactly as SPEC.md Part 1. No DA-HELLO extensions.

#include "lqarp-protocol.h"
#include "oracle-guard.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace dahello {

LqarpProtocol::LqarpProtocol(ChannelGate& gate,
                             LqarpConfig cfg,
                             std::vector<LqarpNode> nodes)
    : gate_(&gate),
      cfg_(cfg),
      nodes_(std::move(nodes)),
      rRangeM_(cfg.rRangeM)
{
    if (nodes_.empty()) {
        throw std::runtime_error("LqarpProtocol: empty node list");
    }
    for (size_t i = 0; i < nodes_.size(); ++i) {
        nodes_[i].id = static_cast<uint32_t>(i);
    }
    if (!std::isfinite(rRangeM_)) {
        rRangeM_ = gate_->rangeAtSnrThreshold();
    }
    const size_t n = nodes_.size();
    Q_.assign(n, std::vector<double>(n, 0.0));
    frozen_.assign(n, std::vector<FrozenNeighbor>(n));
    discoverySuppressed_.assign(n, 0);
    if (cfg_.holdRefreshS > 0.0) {
        nextHoldRefresh_ = cfg_.holdRefreshS;
    }
}

void
LqarpProtocol::checkIndex(uint32_t i) const
{
    if (i >= nodes_.size()) {
        throw std::out_of_range("LqarpProtocol: node index out of range");
    }
}

ChannelSample
LqarpProtocol::linkSample(uint32_t i, uint32_t j) const
{
    checkIndex(i);
    checkIndex(j);
    if (cfg_.heardOnly && !observing()) {
        oracleLeak("LqarpProtocol::linkSample", i, j); // live link to a peer
    }
    const LqarpNode& a = nodes_[i];
    const LqarpNode& b = nodes_[j];
    return gate_->queryLink(a.x, a.y, a.queryDepthM(),
                            b.x, b.y, b.queryDepthM());
}

bool
LqarpProtocol::liveInRange(uint32_t i, uint32_t j) const
{
    if (i == j) {
        return false;
    }
    const LqarpNode& a = nodes_[i];
    const LqarpNode& b = nodes_[j];
    const double d = ChannelGate::distance3D(a.x, a.y, a.z, b.x, b.y, b.z);
    return d < rRangeM_;
}

bool
LqarpProtocol::liveInSnr(uint32_t i, uint32_t j) const
{
    if (i == j) {
        return false;
    }
    return !gate_->sampleFailsCandidateSnr(linkSample(i, j));
}

void
LqarpProtocol::ingestHold(uint32_t j, FrozenNeighbor& h) const
{
    const LqarpNode& b = nodes_[j];
    h.x_j_zoh = b.x;
    h.y_j_zoh = b.y;
    h.z_j_zoh = b.z;
    h.energy_j_zoh = b.energy;
    h.query_z_j_zoh = b.queryDepthM();
    h.vx_j_zoh = b.vx;
    h.vy_j_zoh = b.vy;
    h.vz_j_zoh = b.vz;
    h.heard = true;
}

void
LqarpProtocol::emitHoldRefresh()
{
    if (!(cfg_.holdRefreshS > 0.0)) {
        return;
    }
    // Same pattern as DahelloProtocol::emitPeriodicControl: one grid
    // snapshot per directed pair, no N_stab or trigger gate.
    for (uint32_t i = 0; i < nodes_.size(); ++i) {
        for (uint32_t j = 0; j < nodes_.size(); ++j) {
            if (i == j || discoverySuppressed_[j]) {
                continue;
            }
            ingestHold(j, frozen_[i][j]);
        }
    }
}

void
LqarpProtocol::tick(double dt)
{
    const double step = std::isfinite(dt) ? dt : 0.0;
    simTime_ += step;
    simTimeHeard_ = simTime_;
    if (!(cfg_.holdRefreshS > 0.0)) {
        return;
    }
    const double T = cfg_.holdRefreshS;
    while (simTime_ + 1e-12 >= nextHoldRefresh_) {
        emitHoldRefresh();
        if (!(T > 0.0) || !std::isfinite(T)) {
            break;
        }
        nextHoldRefresh_ += T;
    }
}

void
LqarpProtocol::noteDiscovery(uint32_t i, uint32_t j) const
{
    if (cfg_.heardOnly) {
        // Port A Gate 2: oracle discovery is refused. Holds come only from
        // applyHeardHold (decoded frames).
        return;
    }
    if (i == j) {
        return;
    }
    if (discoverySuppressed_[j]) {
        return;
    }
    if (cfg_.holdRefreshS > 0.0) {
        // Holds are overwritten on the T_ctrl grid only.
        return;
    }
    FrozenNeighbor& h = frozen_[i][j];
    if (h.heard) {
        return;
    }
    // First time the live pair is inside N_range and N_SNR. N_depth is
    // applied to z_j_zoh by inNDepth, so a deeper neighbor is stored
    // for the detour set and still excluded from I(s_i). Not refreshed.
    if (!(liveInRange(i, j) && liveInSnr(i, j))) {
        return;
    }
    ingestHold(j, h);
}

void
LqarpProtocol::applyHeardHold(uint32_t i, uint32_t j, double t,
                              double x, double y, double z,
                              double vx, double vy, double vz,
                              double energy, double snrDb,
                              double number, double V)
{
    checkIndex(i);
    checkIndex(j);
    if (i == j || discoverySuppressed_[j]) {
        return;
    }
    FrozenNeighbor& h = frozen_[i][j];
    if (std::isfinite(x)) { h.x_j_zoh = x; }
    if (std::isfinite(y)) { h.y_j_zoh = y; }
    if (std::isfinite(z)) { h.z_j_zoh = z; h.query_z_j_zoh = z; }
    if (std::isfinite(vx)) { h.vx_j_zoh = vx; }
    if (std::isfinite(vy)) { h.vy_j_zoh = vy; }
    if (std::isfinite(vz)) { h.vz_j_zoh = vz; }
    if (std::isfinite(energy)) { h.energy_j_zoh = energy; }
    if (std::isfinite(snrDb)) { h.snr_db_zoh = snrDb; }
    if (std::isfinite(number)) { h.number_zoh = number; }
    if (std::isfinite(V)) { h.V_zoh = V; }
    h.lastHeardTime = t;
    h.heard = true;
    simTimeHeard_ = std::max(simTimeHeard_, t);
}

bool
LqarpProtocol::inNRange(uint32_t i, uint32_t j) const
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
    if (cfg_.heardOnly && cfg_.tNbS > 0.0
        && !(std::isfinite(h.lastHeardTime)
             && (simTimeHeard_ - h.lastHeardTime) <= cfg_.tNbS)) {
        return false; // expired
    }
    const LqarpNode& a = nodes_[i];
    const double d = ChannelGate::distance3D(a.x, a.y, a.z,
                                            h.x_j_zoh, h.y_j_zoh, h.z_j_zoh);
    return d < rRangeM_;
}

bool
LqarpProtocol::inNSnr(uint32_t i, uint32_t j) const
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
    const ChannelSample sample = gate_->queryLink(a.x, a.y, a.queryDepthM(),
                                                  h.x_j_zoh, h.y_j_zoh,
                                                  h.query_z_j_zoh);
    if (gate_->sampleFailsCandidateSnr(sample)) {
        return false;
    }
    // Step 7: the reward path reads the LIVE sample (linkSample), so a pair
    // whose live query has no defined SNR — it has drifted outside the Bellhop
    // envelope since its last HELLO — is not in N_SNR under SPEC Part 1.2 and
    // must not reach the reward as a candidate. Only channel *definedness* is
    // tested against the live pose; the SNR_threshold comparison stays on the
    // held sample above, which is what the staleness model is about.
    // Heard mode (amend1 A5/I4): j's live pose is not allowed information, so the
    // live definedness re-check is skipped; the held-sample test above decides.
    if (!cfg_.heardOnly || leakLiveLink_) {
        if (!gate_->sampleHasDefinedSnr(linkSample(i, j))) {
            return false;
        }
    }
    return true;
}

bool
LqarpProtocol::inNDepth(uint32_t i, uint32_t j) const
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
    // depth_{s_i} - depth_{s_j} > 0  ⇒  s_j is shallower (closer to surface)
    return (nodes_[i].z - h.z_j_zoh) > 0.0;
}

std::vector<uint32_t>
LqarpProtocol::candidateSet(uint32_t i) const
{
    checkIndex(i);
    std::vector<uint32_t> I;
    for (uint32_t j = 0; j < nodes_.size(); ++j) {
        if (inNRange(i, j) && inNSnr(i, j) && inNDepth(i, j)) {
            I.push_back(j);
        }
    }
    return I;
}

uint32_t
LqarpProtocol::numberSj(uint32_t j) const
{
    if (cfg_.heardOnly) {
        // Held neighbour count from j's last decoded header. Missing → 0.
        // (Caller is computing reward(i,j); the hold is frozen_[i][j].)
        // numberSj is also used inside reward via c_nei(numberSj(j)); the
        // hold that matters is the one at the deciding node. We approximate
        // with any fresh hold of j (they all carry the same advertised number).
        for (uint32_t i = 0; i < nodes_.size(); ++i) {
            if (i == j) continue;
            const FrozenNeighbor& h = frozen_[i][j];
            if (h.heard && std::isfinite(h.number_zoh)) {
                return static_cast<uint32_t>(std::max(0.0, h.number_zoh));
            }
        }
        return 0;
    }
    return static_cast<uint32_t>(candidateSet(j).size());
}

double
LqarpProtocol::cEne(double eSj, double eAvg, double eIni)
{
    // c_ene(s_j) = (2/pi) * arctan((E_{s_j} - E_avg) / E_ini)
    return (2.0 / std::acos(-1.0)) * std::atan((eSj - eAvg) / eIni);
}

double
LqarpProtocol::cDel(double eSj, double eIni, double depthSi, double depthSj)
{
    // c_del(s_i, s_j) = (E_{s_j} / E_ini) * (depth_{s_i} - depth_{s_j})
    // Raw depths, not the transducer floor.
    return (eSj / eIni) * (depthSi - depthSj);
}

double
LqarpProtocol::cNei(uint32_t number_sj)
{
    // c_nei(s_j) = exp(number_{s_j}); number=0 ⇒ exp(0)=1 (void node).
    return std::exp(static_cast<double>(number_sj));
}

LqarpReward
LqarpProtocol::reward(uint32_t i, uint32_t j) const
{
    checkIndex(i);
    checkIndex(j);
    LqarpReward out;

    noteDiscovery(i, j);
    const FrozenNeighbor& hj = frozen_[i][j];
    const double eJ = hj.heard ? hj.energy_j_zoh : 0.0;
    const double zJ = hj.heard ? hj.z_j_zoh : 0.0;

    const auto I_i = candidateSet(i);
    double eSum = 0.0;
    for (uint32_t k : I_i) {
        eSum += frozen_[i][k].energy_j_zoh;
    }
    const double eAvg = I_i.empty() ? 0.0 : eSum / static_cast<double>(I_i.size());

    double snrDb;
    if (cfg_.heardOnly && !leakLiveLink_) {
        snrDb = hj.snr_db_zoh;
        if (!std::isfinite(snrDb)) {
            // No held SNR yet: treat as undefined link (c_lin → fail closed).
            snrDb = std::numeric_limits<double>::quiet_NaN();
        }
    } else {
        // Legacy (oracle) path, or planted leak (--leakTest=2).
        snrDb = linkSample(i, j).snr_db;
    }
    out.c_lin = cLin(snrDb, cfg_.packetBits, cfg_.constellationM);
    out.c_ene = cEne(eJ, eAvg, cfg_.eIni);
    out.c_del = cDel(eJ, cfg_.eIni, nodes_[i].z, zJ);
    // Heard mode: prefer the neighbour count carried in j's header (held at i).
    if (cfg_.heardOnly && std::isfinite(hj.number_zoh)) {
        out.c_nei = cNei(static_cast<uint32_t>(std::max(0.0, hj.number_zoh)));
    } else {
        out.c_nei = cNei(numberSj(j));
    }

    // R = c_ene * c_del * c_nei / c_lin
    // A zero numerator is R=0 even if c_lin underflows to 0 (otherwise
    // 0/0 is NaN). A non-finite c_lin (infinite or undefined cost, e.g. no
    // held SNR) gives R=0. A non-zero numerator over an exactly-zero c_lin
    // gives ±inf.
    const double num = out.c_ene * out.c_del * out.c_nei;
    if (num == 0.0) {
        out.R = 0.0;
    } else if (!std::isfinite(out.c_lin)) {
        // p4: reward divided by an infinite (or undefined) cost is 0, not +-inf.
        out.R = 0.0;
    } else if (out.c_lin == 0.0) {
        out.R = std::copysign(std::numeric_limits<double>::infinity(), num);
    } else {
        out.R = num / out.c_lin;
    }
    return out;
}

double
LqarpProtocol::qValue(uint32_t i, uint32_t j) const
{
    checkIndex(i);
    checkIndex(j);
    return Q_[i][j];
}

void
LqarpProtocol::updateQ(uint32_t i, uint32_t j)
{
    checkIndex(i);
    checkIndex(j);
    const LqarpReward rw = reward(i, j);

    double maxNext = 0.0;
    if (cfg_.heardOnly) {
        // V-value advertised in j's last decoded data header (held at i).
        const FrozenNeighbor& hj = frozen_[i][j];
        maxNext = (hj.heard && std::isfinite(hj.V_zoh)) ? hj.V_zoh : 0.0;
    } else {
        const auto I_j = candidateSet(j);
        if (!I_j.empty()) {
            maxNext = Q_[j][I_j.front()];
            for (uint32_t a : I_j) {
                maxNext = std::max(maxNext, Q_[j][a]);
            }
        }
    }

    // Q(s_i, a_j) <- (1-alpha) Q + alpha [ R + gamma * max_{a in I(s_j)} Q ]
    Q_[i][j] = (1.0 - cfg_.alpha) * Q_[i][j]
               + cfg_.alpha * (rw.R + cfg_.gamma * maxNext);
}

void
LqarpProtocol::learnFrom(uint32_t i)
{
    learnFrom(i, candidateSet(i));
}

void
LqarpProtocol::learnFrom(uint32_t i, const std::vector<uint32_t>& actions)
{
    for (uint32_t j : actions) {
        updateQ(i, j);
    }
}

int
LqarpProtocol::nextHop(uint32_t i) const
{
    return nextHop(i, candidateSet(i));
}

int
LqarpProtocol::nextHop(uint32_t i, const std::vector<uint32_t>& actions) const
{
    if (actions.empty()) {
        return -1;
    }
    uint32_t best = actions.front();
    double bestQ = Q_[i][best];
    for (uint32_t j : actions) {
        const double q = Q_[i][j];
        if (q > bestQ || (q == bestQ && j < best)) {
            bestQ = q;
            best = j;
        }
    }
    return static_cast<int>(best);
}

bool
LqarpProtocol::containsId(const std::vector<uint32_t>& ids, uint32_t id)
{
    for (uint32_t v : ids) {
        if (v == id) {
            return true;
        }
    }
    return false;
}

std::vector<uint32_t>
LqarpProtocol::detourSet(uint32_t i) const
{
    checkIndex(i);
    std::vector<uint32_t> D;
    for (uint32_t j = 0; j < nodes_.size(); ++j) {
        if (inNRange(i, j) && inNSnr(i, j)) {
            D.push_back(j);
        }
    }
    return D;
}

std::vector<uint32_t>
LqarpProtocol::detourTargets(uint32_t i,
                             uint32_t sinkId,
                             const std::vector<uint32_t>& visited) const
{
    std::vector<uint32_t> T;
    for (uint32_t j : detourSet(i)) {
        if (containsId(visited, j)) {
            continue;
        }
        if (j == sinkId) {
            T.push_back(j);
            continue;
        }
        if (cfg_.heardOnly) {
            // Heard mode (amend1 A5): j's candidate/detour sets are j's private state.
            // Onward eligibility from the held neighbour count number_{s_j} of j's last
            // decoded header (> 0: j advertised a non-empty candidate set). The visited
            // filter on j's set cannot be applied without j's table. FLAGGED for v3.
            const FrozenNeighbor& hj = frozen_[i][j];
            if (std::isfinite(hj.number_zoh) && hj.number_zoh > 0.0) {
                T.push_back(j);
            }
            continue;
        }
        bool onwardI = false;
        for (uint32_t k : candidateSet(j)) {
            if (!containsId(visited, k)) {
                onwardI = true;
                break;
            }
        }
        if (onwardI) {
            T.push_back(j);
            continue;
        }
        bool onwardD = false;
        for (uint32_t k : detourSet(j)) {
            if (!containsId(visited, k)) {
                onwardD = true;
                break;
            }
        }
        if (onwardD) {
            T.push_back(j);
        }
    }
    return T;
}

bool
LqarpProtocol::peekFilter(uint32_t i, uint32_t j, FilterPeek& out) const
{
    checkIndex(i);
    checkIndex(j);
    out = FilterPeek{};
    if (i == j) {
        return false;
    }
    const FrozenNeighbor& h = frozen_[i][j];
    const LqarpNode& a = nodes_[i];
    const LqarpNode& b = nodes_[j];
    out.heard = h.heard;
    out.stab = true;
    out.heldX = h.x_j_zoh;
    out.heldY = h.y_j_zoh;
    out.heldZ = h.z_j_zoh;
    out.heldE = h.energy_j_zoh;
    out.heldQueryZ = h.query_z_j_zoh;
    out.energyGapJ = b.energy - (h.heard ? h.energy_j_zoh : 0.0);
    if (!h.heard) {
        return true;
    }
    out.poseErrM = ChannelGate::distance3D(b.x, b.y, b.z,
                                           h.x_j_zoh, h.y_j_zoh, h.z_j_zoh);
    out.range = ChannelGate::distance3D(a.x, a.y, a.z,
                                        h.x_j_zoh, h.y_j_zoh, h.z_j_zoh) < rRangeM_;
    const ChannelSample sample = gate_->queryLink(a.x, a.y, a.queryDepthM(),
                                                  h.x_j_zoh, h.y_j_zoh,
                                                  h.query_z_j_zoh);
    out.snr = !gate_->sampleFailsCandidateSnr(sample);
    out.depth = (a.z - h.z_j_zoh) > 0.0;
    return true;
}

void
LqarpProtocol::setEnergy(uint32_t i, double joules)
{
    checkIndex(i);
    nodes_[i].energy = joules;
}

double
LqarpProtocol::energy(uint32_t i) const
{
    checkIndex(i);
    return nodes_[i].energy;
}

void
LqarpProtocol::setKinematics(uint32_t i, double x, double y, double z,
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
LqarpProtocol::setDiscoverySuppressed(uint32_t j, bool suppressed)
{
    checkIndex(j);
    discoverySuppressed_[j] = suppressed ? 1 : 0;
}

} // namespace dahello
