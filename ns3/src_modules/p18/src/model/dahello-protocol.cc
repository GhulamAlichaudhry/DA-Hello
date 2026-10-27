#include <sstream>
// dahello-protocol.cc
//
// DA-HELLO SPEC.md Part 2. BER floor comes only from link-quality.cc.

#include "dahello-protocol.h"
#include "oracle-guard.h"

#include <cstdlib>
#include <cstring>

#include <algorithm>
#include <cmath>
#include <limits>
#include <random>
#include <stdexcept>
#include <vector>

namespace dahello {
namespace {

constexpr int kDim = 7;

int
idx(int r, int c)
{
    return r * kDim + c;
}

void
zero49(std::array<double, 49>& a)
{
    a.fill(0.0);
}

void
ident49(std::array<double, 49>& a)
{
    zero49(a);
    for (int i = 0; i < kDim; ++i) {
        a[static_cast<size_t>(idx(i, i))] = 1.0;
    }
}

void
mul49(const std::array<double, 49>& A,
      const std::array<double, 49>& B,
      std::array<double, 49>& C)
{
    zero49(C);
    for (int r = 0; r < kDim; ++r) {
        for (int c = 0; c < kDim; ++c) {
            double s = 0.0;
            for (int k = 0; k < kDim; ++k) {
                s += A[static_cast<size_t>(idx(r, k))] * B[static_cast<size_t>(idx(k, c))];
            }
            C[static_cast<size_t>(idx(r, c))] = s;
        }
    }
}

void
transpose49(const std::array<double, 49>& A, std::array<double, 49>& At)
{
    for (int r = 0; r < kDim; ++r) {
        for (int c = 0; c < kDim; ++c) {
            At[static_cast<size_t>(idx(c, r))] = A[static_cast<size_t>(idx(r, c))];
        }
    }
}

bool
allFinite49(const std::array<double, 49>& a)
{
    for (double v : a) {
        if (!std::isfinite(v)) {
            return false;
        }
    }
    return true;
}

// C = F * B, summing only F's nonzero entries per row, ascending k (see OPT-1).
void
sparseLeftMul49(const std::array<double, 49>& F,
                const std::array<double, 49>& B,
                std::array<double, 49>& C)
{
    for (int r = 0; r < kDim; ++r) {
        int ks[kDim];
        double vs[kDim];
        int m = 0;
        for (int k = 0; k < kDim; ++k) {
            const double v = F[static_cast<size_t>(idx(r, k))];
            if (v != 0.0) {
                ks[m] = k;
                vs[m] = v;
                ++m;
            }
        }
        for (int c = 0; c < kDim; ++c) {
            double s = 0.0;
            for (int t = 0; t < m; ++t) {
                s += vs[t] * B[static_cast<size_t>(idx(ks[t], c))];
            }
            C[static_cast<size_t>(idx(r, c))] = s;
        }
    }
}

// C = A * F^T, i.e. C[r][c] = sum_k A[r][k] * F[c][k], nonzero F only, ascending k.
// Operand order A*F matches mul49(A, Ft) (A[r][k] * Ft[k][c]).
void
sparseRightMulT49(const std::array<double, 49>& A,
                  const std::array<double, 49>& F,
                  std::array<double, 49>& C)
{
    for (int c = 0; c < kDim; ++c) {
        int ks[kDim];
        double vs[kDim];
        int m = 0;
        for (int k = 0; k < kDim; ++k) {
            const double v = F[static_cast<size_t>(idx(c, k))];
            if (v != 0.0) {
                ks[m] = k;
                vs[m] = v;
                ++m;
            }
        }
        for (int r = 0; r < kDim; ++r) {
            double s = 0.0;
            for (int t = 0; t < m; ++t) {
                s += A[static_cast<size_t>(idx(r, ks[t]))] * vs[t];
            }
            C[static_cast<size_t>(idx(r, c))] = s;
        }
    }
}

// OPT-5 (2026-10-12, behaviour-preserving): the same products as
// sparseLeftMul49 / sparseRightMulT49 for the fixed predict Jacobian
// F = I + dt*E(0,3)+dt*E(1,4)+dt*E(2,5) + a*E(6,0) + b*E(6,1) - E(6,6).
// Per row, the nonzero entries (v != 0.0) in ascending k are:
// rows 0..2 {(r,1),(r+3,dt)}; rows 3..5 {(r,1)}; row 6 {(0,a),(1,b)}.
// Each sum starts at +0.0 and adds the same terms in the same order, so the bits are identical.
void
predictLeftMul49(double dt, double a, double b,
                 const std::array<double, 49>& B, std::array<double, 49>& C)
{
    const bool hasDt = (dt != 0.0), hasA = (a != 0.0), hasB = (b != 0.0);
    for (int c = 0; c < kDim; ++c) {
        for (int r = 0; r < 3; ++r) {
            double s = 0.0;
            s += 1.0 * B[static_cast<size_t>(idx(r, c))];
            if (hasDt) {
                s += dt * B[static_cast<size_t>(idx(r + 3, c))];
            }
            C[static_cast<size_t>(idx(r, c))] = s;
        }
        for (int r = 3; r < 6; ++r) {
            double s = 0.0;
            s += 1.0 * B[static_cast<size_t>(idx(r, c))];
            C[static_cast<size_t>(idx(r, c))] = s;
        }
        double s = 0.0;
        if (hasA) {
            s += a * B[static_cast<size_t>(idx(0, c))];
        }
        if (hasB) {
            s += b * B[static_cast<size_t>(idx(1, c))];
        }
        C[static_cast<size_t>(idx(6, c))] = s;
    }
}

void
predictRightMulT49(double dt, double a, double b,
                   const std::array<double, 49>& A, std::array<double, 49>& C)
{
    const bool hasDt = (dt != 0.0), hasA = (a != 0.0), hasB = (b != 0.0);
    for (int r = 0; r < kDim; ++r) {
        for (int c = 0; c < 3; ++c) {
            double s = 0.0;
            s += A[static_cast<size_t>(idx(r, c))] * 1.0;
            if (hasDt) {
                s += A[static_cast<size_t>(idx(r, c + 3))] * dt;
            }
            C[static_cast<size_t>(idx(r, c))] = s;
        }
        for (int c = 3; c < 6; ++c) {
            double s = 0.0;
            s += A[static_cast<size_t>(idx(r, c))] * 1.0;
            C[static_cast<size_t>(idx(r, c))] = s;
        }
        double s = 0.0;
        if (hasA) {
            s += A[static_cast<size_t>(idx(r, 0))] * a;
        }
        if (hasB) {
            s += A[static_cast<size_t>(idx(r, 1))] * b;
        }
        C[static_cast<size_t>(idx(r, 6))] = s;
    }
}

double
medianCopy(std::deque<double> w)
{
    if (w.empty()) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    std::sort(w.begin(), w.end());
    const size_t n = w.size();
    if (n % 2 == 1) {
        return w[n / 2];
    }
    return 0.5 * (w[n / 2 - 1] + w[n / 2]);
}

} // namespace

DahelloProtocol::DahelloProtocol(ChannelGate& gate,
                             DahelloConfig cfg,
                             std::vector<LqarpNode> nodes)
    : gate_(&gate),
      cfg_(cfg),
      nodes_(std::move(nodes)),
      rRangeM_(cfg.rRangeM)
{
    if (nodes_.empty()) {
        throw std::runtime_error("DahelloProtocol: empty node list");
    }
    for (size_t i = 0; i < nodes_.size(); ++i) {
        nodes_[i].id = static_cast<uint32_t>(i);
    }
    if (!std::isfinite(rRangeM_)) {
        rRangeM_ = gate_->rangeAtSnrThreshold();
    }
    const size_t n = nodes_.size();
    {
        const char* eager = std::getenv("DAHELLO_EAGER_EKF"); // verification switch only
        lazyPredict_ = gate_->depthIndependent() && !(eager && eager[0] == '1');
    }
    Q_.assign(n, std::vector<double>(n, 0.0));
    ekf_.assign(n, std::vector<NeighborEkf>(n));
    helloSuppressed_.assign(n, 0);
    bootstrapSuppressed_.assign(n, 0);
    prevBroadcast_.assign(n * n, 0);
    for (uint32_t i = 0; i < n; ++i) {
        for (uint32_t j = 0; j < n; ++j) {
            if (i != j) {
                initEkf(i, j);
            }
        }
    }
    // Fixed-periodic ablation (SPEC Part 4): first control round at t=0.
    // p4: heard-only runs skip the t=0 round (see DahelloConfig::skipInitialControlRound).
    if (cfg_.broadcastMode == BroadcastMode::FixedPeriodic) {
        if (!cfg_.skipInitialControlRound) {
            emitPeriodicControl();
        }
        nextPeriodicTime_ = cfg_.T_ctrl;
    }
    if (cfg_.broadcastMode == BroadcastMode::Adaptive) {
        // Same t=0 round as Fixed-Periodic, then per-node schedules.
        if (!cfg_.skipInitialControlRound) {
            emitPeriodicControl();
        }
        adaptiveInit();
    }
    if (cfg_.energyRefreshS > 0.0) {
        nextEnergyRefresh_ = cfg_.energyRefreshS;
    }
    // p15 A2.2(a): default false leaves nextPeriodicTime_/nextEnergyRefresh_ grids unchanged (= p14).
    initPeriodicJitterOffsets();
}

void
DahelloProtocol::checkIndex(uint32_t i) const
{
    if (i >= nodes_.size()) {
        throw std::out_of_range("DahelloProtocol: node index out of range");
    }
}

NeighborEkf&
DahelloProtocol::slot(uint32_t i, uint32_t j)
{
    checkIndex(i);
    checkIndex(j);
    return ekf_[i][j];
}

const NeighborEkf&
DahelloProtocol::slot(uint32_t i, uint32_t j) const
{
    checkIndex(i);
    checkIndex(j);
    return ekf_[i][j];
}

const NeighborEkf&
DahelloProtocol::ekf(uint32_t i, uint32_t j) const
{
    checkIndex(i);
    checkIndex(j);
    if (i != j) {
        materialize(i, j);
    }
    return slot(i, j);
}

// One tick's predict for pair (i,j): the exact per-pair body of the eager
// tick loop (OPT-2 copy when the forecast track equals the filter track).
void
DahelloProtocol::predictStep(uint32_t i, uint32_t j, double step)
{
    NeighborEkf& t = ekf_[i][j];
    const bool same =
        std::memcmp(t.x.data(), t.xForecast.data(), sizeof(double) * 7) == 0 &&
        std::memcmp(t.P.data(), t.PForecast.data(), sizeof(double) * 49) == 0;
    predictPair(i, j, step);
    if (same) {
        t.xForecast = t.x;
        t.PForecast = t.P;
    } else {
        predictForecast(i, j, step);
    }
}

// OPT-4 (2026-10-12, behaviour-preserving). Only used when the gate is
// depth-independent: then applyPredict(x, P, dt, z_j) does not depend on z_j,
// so a pair's predict sequence depends only on its own tracks and the tick
// steps, and pairs are independent. Replaying the logged steps in order, right
// before any read or write of x/P/xForecast/PForecast (updatePair,
// ingestBroadcastVelocity, ekf()), yields the same bits as predicting every
// pair on every tick. Pairs whose tracks are never read again are never
// computed. Nothing inside the tick's pair loop touches another pair's tracks.
void
DahelloProtocol::materialize(uint32_t i, uint32_t j) const
{
    if (!lazyPredict_) {
        return;
    }
    NeighborEkf& t = ekf_[i][j];
    auto* self = const_cast<DahelloProtocol*>(this);
    while (t.lazySynced < lazySteps_.size()) {
        const double step = lazySteps_[t.lazySynced];
        ++t.lazySynced;
        self->predictStep(i, j, step);
    }
}

void
DahelloProtocol::initEkf(uint32_t i, uint32_t j)
{
    NeighborEkf& s = ekf_[i][j];
    const LqarpNode& a = nodes_[i];
    const LqarpNode& b = nodes_[j];
    s.x[0] = a.x - b.x;
    s.x[1] = a.y - b.y;
    s.x[2] = a.z - b.z;
    // SPEC 2.1: pdot_j is the last received broadcast, ZOH between
    // broadcasts. No packet has arrived yet, so ZOH starts at 0.
    // Copying true neighbor velocity here would make constant-drift
    // Scenario II a process-model match and silence the trigger.
    s.vx_j_zoh = 0.0;
    s.vy_j_zoh = 0.0;
    s.vz_j_zoh = 0.0;
    s.x[3] = a.vx - s.vx_j_zoh;
    s.x[4] = a.vy - s.vy_j_zoh;
    s.x[5] = a.vz - s.vz_j_zoh;
    // SNR state matches the measurement path (queryDepthM / acousticZ),
    // not kinematic z. The sink's towed transducer is acousticZ=50 m.
    s.x[6] = linkSample(i, j).snr_db;
    ident49(s.P);
    for (int k = 0; k < 3; ++k) {
        s.P[static_cast<size_t>(idx(k, k))] = 10.0;
        s.P[static_cast<size_t>(idx(k + 3, k + 3))] = 1.0;
    }
    s.P[static_cast<size_t>(idx(6, 6))] = 4.0;
    s.e_k = 0.0;
    s.e_km1 = 0.0;
    s.epsilon_k = cfg_.epsilon_0;
    s.S_k = cfg_.rMeas;
    s.xForecast = s.x;
    s.PForecast = s.P;
    s.lazySynced = static_cast<uint32_t>(lazySteps_.size()); // OPT-4
    s.eTrigger = 0.0;
    s.STrigger = cfg_.rMeas;
    s.epsilonTrigger = cfg_.epsilon_0;
    s.maxAbsETrigger = 0.0;
    s.maxAbsETriggerUnheard = 0.0;
    s.maxEpsilonTriggerUnheard = cfg_.epsilon_0;
    s.broadcast_k = 0;
    s.initialized = true;
    s.zohHeard = false;
    s.lastZohTime = std::numeric_limits<double>::quiet_NaN();
    s.firstBroadcastTime = std::numeric_limits<double>::quiet_NaN();
    s.nBroadcastRising = 0;
}

ChannelSample
DahelloProtocol::linkSample(uint32_t i, uint32_t j) const
{
    checkIndex(i);
    checkIndex(j);
    if (cfg_.heardOnly && !observing()) {
        oracleLeak("DahelloProtocol::linkSample", i, j); // live link to a peer
    }
    const LqarpNode& a = nodes_[i];
    const LqarpNode& b = nodes_[j];
    return gate_->queryLink(a.x, a.y, a.queryDepthM(),
                            b.x, b.y, b.queryDepthM());
}

bool
DahelloProtocol::noteReceptionImpl(uint32_t rx, uint32_t tx) const
{
    if (rx == tx) {
        return false;
    }
    checkIndex(rx);
    checkIndex(tx);
    if (cfg_.heardOnly) {
        return false; // heard: decoded frames reach applyHeardHold (medium decode)
    }
    // Same reception condition as a data hop (tx -> rx): valid sample,
    // not near pressure release, SNR >= threshold.
    if (!gate_->receptionOk(linkSample(tx, rx))) {
        return false;
    }
    NeighborEkf& s = ekf_[rx][tx];
    s.rxPending = true;
    // z_k keeps the EKF's measurement convention for pair (rx, tx)
    // (same argument order as h / initEkf), sampled at reception time.
    s.zRxPending = linkSample(rx, tx).snr_db;
    ++s.nRx;
    ++receptionCount_;
    return true;
}

bool
DahelloProtocol::helloReaches(uint32_t rx, uint32_t tx) const
{
    if (!cfg_.helloRxGate) {
        return true;
    }
    // Same reception condition as a data hop and as noteReceptionImpl.
    return gate_->receptionOk(linkSample(tx, rx));
}

bool
DahelloProtocol::noteReception(uint32_t rx, uint32_t tx)
{
    return noteReceptionImpl(rx, tx);
}

void
DahelloProtocol::broadcastFrom(uint32_t tx)
{
    ctrlRxHello_ += broadcastFromCount(tx);
}

uint64_t
DahelloProtocol::broadcastFromCount(uint32_t tx)
{
    uint64_t n = 0;
    for (uint32_t rx = 0; rx < nodes_.size(); ++rx) {
        if (rx != tx) {
            if (noteReceptionImpl(rx, tx)) {
                ++n;
            }
        }
    }
    return n;
}

bool
DahelloProtocol::liveInRange(uint32_t i, uint32_t j) const
{
    if (i == j) {
        return false;
    }
    const LqarpNode& a = nodes_[i];
    const LqarpNode& b = nodes_[j];
    return ChannelGate::distance3D(a.x, a.y, a.z, b.x, b.y, b.z) < rRangeM_;
}

bool
DahelloProtocol::liveInSnr(uint32_t i, uint32_t j) const
{
    if (i == j) {
        return false;
    }
    return !gate_->sampleFailsCandidateSnr(linkSample(i, j));
}

void
DahelloProtocol::noteBootstrap(uint32_t i, uint32_t j) const
{
    if (cfg_.heardOnly) {
        // Port A Gate 2 / Decision D-A: no oracle bootstrap in heard mode.
        return;
    }
    if (i == j) {
        return;
    }
    checkIndex(i);
    checkIndex(j);
    if (bootstrapSuppressed_[j]) {
        return;
    }
    NeighborEkf& s = ekf_[i][j];
    if (s.zohHeard) {
        return;
    }
    if (cfg_.bootstrapBroadcast) {
        // DECISION-AWARE bootstrap fix: i's first contact with j makes j emit
        // one broadcast; every not-yet-bootstrapped receiver passing the same
        // live gate keeps it (same per-receiver effect as the per-pair rule).
        if (!(liveInRange(i, j) && liveInSnr(i, j))) {
            return;
        }
        ++bootstrapHelloCount_;
        ++bootBcast_;
        if (txHook_) {
            txHook_(j, 2);
        }
        if (bootstrapFrom_.size() != nodes_.size()) {
            bootstrapFrom_.assign(nodes_.size(), 0);
        }
        ++bootstrapFrom_[j];
        for (uint32_t r = 0; r < nodes_.size(); ++r) {
            if (r == j) {
                continue;
            }
            NeighborEkf& t = ekf_[r][j];
            if (t.zohHeard || !(liveInRange(r, j) && liveInSnr(r, j))) {
                continue;
            }
            ingestBroadcastVelocity(t, r, j, nodes_[j].vx, nodes_[j].vy, nodes_[j].vz);
            if (noteReceptionImpl(r, j)) {
                ++ctrlRxBoot_;
            }
        }
        return;
    }
    // Same live gate as LQARP/RLOR noteDiscovery: N_range ∩ N_SNR.
    // N_depth is applied later to the stored z, so a deeper neighbor
    // is kept for the detour set. Not a Broadcast_k refresh.
    if (!(liveInRange(i, j) && liveInSnr(i, j))) {
        return;
    }
    ingestBroadcastVelocity(s, i, j, nodes_[j].vx, nodes_[j].vy, nodes_[j].vz);
    ++bootstrapHelloCount_;
    if (txHook_) {
        txHook_(j, 2);
    }
    if (bootstrapFrom_.size() != nodes_.size()) {
        bootstrapFrom_.assign(nodes_.size(), 0);
    }
    ++bootstrapFrom_[j];
    if (noteReceptionImpl(i, j)) {
        ++ctrlRxBoot_;
    }
}

bool
DahelloProtocol::inNRange(uint32_t i, uint32_t j) const
{
    checkIndex(i);
    checkIndex(j);
    if (i == j) {
        return false;
    }
    noteBootstrap(i, j);
    const NeighborEkf& s = slot(i, j);
    if (!heardFresh(s)) { // item 4: T_nb (default +inf = zohHeard)
        return false;
    }
    const LqarpNode& a = nodes_[i];
    return ChannelGate::distance3D(a.x, a.y, a.z,
                                   s.x_j_zoh, s.y_j_zoh, s.z_j_zoh) < rRangeM_;
}

bool
DahelloProtocol::inNSnr(uint32_t i, uint32_t j) const
{
    checkIndex(i);
    checkIndex(j);
    if (i == j) {
        return false;
    }
    noteBootstrap(i, j);
    const NeighborEkf& s = slot(i, j);
    if (!heardFresh(s)) { // item 4: T_nb (default +inf = zohHeard)
        return false;
    }
    const LqarpNode& a = nodes_[i];
    const ChannelSample sample = gate_->queryLink(a.x, a.y, a.queryDepthM(),
                                                  s.x_j_zoh, s.y_j_zoh,
                                                  s.query_z_j_zoh);
    if (gate_->sampleFailsCandidateSnr(sample)) {
        return false;
    }
    // Step 7: the reward path reads the LIVE sample (linkSample), so a pair
    // whose live query has no defined SNR — it has drifted outside the Bellhop
    // envelope since its last HELLO — is not in N_SNR under SPEC Part 1.2 and
    // must not reach the reward as a candidate. Only channel *definedness* is
    // tested against the live pose; the SNR_threshold comparison stays on the
    // held sample above, which is what the staleness model is about.
    // Heard mode (amend1 A5/I4): j's live pose is not allowed information; the
    // held-sample test above decides (same correction as LqarpProtocol::inNSnr).
    if (!cfg_.heardOnly) {
        if (!gate_->sampleHasDefinedSnr(linkSample(i, j))) {
            return false;
        }
    }
    return true;
}

bool
DahelloProtocol::inNDepth(uint32_t i, uint32_t j) const
{
    checkIndex(i);
    checkIndex(j);
    if (i == j) {
        return false;
    }
    noteBootstrap(i, j);
    const NeighborEkf& s = slot(i, j);
    if (!heardFresh(s)) { // item 4: T_nb (default +inf = zohHeard)
        return false;
    }
    return (nodes_[i].z - s.z_j_zoh) > 0.0;
}

bool
DahelloProtocol::inNStab(uint32_t i, uint32_t j) const
{
    if (i == j) {
        return false;
    }
    const NeighborEkf& s = slot(i, j);
    // N_stab = { Broadcast_k = 0  OR  |e_k| < epsilon_k }
    return s.broadcast_k == 0 || std::fabs(s.e_k) < s.epsilon_k;
}

std::vector<uint32_t>
DahelloProtocol::lqarpCandidateSet(uint32_t i) const
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

std::vector<uint32_t>
DahelloProtocol::candidateSet(uint32_t i) const
{
    checkIndex(i);
    std::vector<uint32_t> I;
    for (uint32_t j = 0; j < nodes_.size(); ++j) {
        if (inNRange(i, j) && inNSnr(i, j) && inNDepth(i, j) && inNStab(i, j)) {
            I.push_back(j);
        }
    }
    return I;
}

uint32_t
DahelloProtocol::numberSj(uint32_t j) const
{
    // SPEC 2.8: c_nei unchanged from Part 1.3, so |I_LQARP(s_j)|.
    return static_cast<uint32_t>(lqarpCandidateSet(j).size());
}

void
DahelloProtocol::relativeState(uint32_t i, uint32_t j,
                             double& pijx, double& pijy, double& pijz,
                             double& vijx, double& vijy, double& vijz) const
{
    checkIndex(i);
    checkIndex(j);
    const LqarpNode& a = nodes_[i];
    const LqarpNode& b = nodes_[j];
    const NeighborEkf& s = slot(i, j);
    if (cfg_.heardOnly) {
        // heard: j's held pose aged by its held velocity (I2)
        const double age = s.zohHeard ? std::max(0.0, simTime_ - s.lastZohTime) : 0.0;
        pijx = a.x - (s.x_j_zoh + s.vx_j_zoh * age);
        pijy = a.y - (s.y_j_zoh + s.vy_j_zoh * age);
        pijz = a.z - (s.z_j_zoh + s.vz_j_zoh * age);
        vijx = a.vx - s.vx_j_zoh;
        vijy = a.vy - s.vy_j_zoh;
        vijz = a.vz - s.vz_j_zoh;
        (void)b;
        return;
    }
    pijx = a.x - b.x;
    pijy = a.y - b.y;
    pijz = a.z - b.z;
    vijx = a.vx - s.vx_j_zoh;
    vijy = a.vy - s.vy_j_zoh;
    vijz = a.vz - s.vz_j_zoh;
}

double
DahelloProtocol::vRel(uint32_t i, uint32_t j) const
{
    double px, py, pz, vx, vy, vz;
    relativeState(i, j, px, py, pz, vx, vy, vz);
    const double n = std::sqrt(px * px + py * py + pz * pz);
    if (n < 1e-12) {
        return 0.0;
    }
    return (vx * px + vy * py + vz * pz) / n;
}

double
DahelloProtocol::sigmoid(double u)
{
    return 1.0 / (1.0 + std::exp(-u));
}

double
DahelloProtocol::cPred(uint32_t i, uint32_t j) const
{
    const double snr = cfg_.heardOnly ? slot(i, j).snr_db_zoh : linkSample(i, j).snr_db;
    const double clin = dahello::cLin(snr, cfg_.packetBits, cfg_.constellationM);
    const NeighborEkf& st = slot(i, j);
    const double excess = std::max(0.0, st.e_k - st.e_km1);
    return clin + cfg_.beta_1 * excess;
}

double
DahelloProtocol::cDrift(uint32_t i, uint32_t j) const
{
    noteBootstrap(i, j);
    const NeighborEkf& s = slot(i, j);
    // No bootstrap and no HELLO yet: do not read nodes_[j] depth or energy.
    if (!s.zohHeard) {
        return 0.0;
    }
    const double c_del = LqarpProtocol::cDel(s.energy_j_zoh, cfg_.eIni,
                                             nodes_[i].z, s.z_j_zoh);
    const double vr = vRel(i, j);
    return c_del * (1.0 + cfg_.beta_2 * sigmoid(-vr));
}

DahelloReward
DahelloProtocol::reward(uint32_t i, uint32_t j) const
{
    checkIndex(i);
    checkIndex(j);
    DahelloReward out;
    noteBootstrap(i, j);
    const NeighborEkf& hj = slot(i, j);
    const double eJ = hj.zohHeard ? hj.energy_j_zoh : 0.0;
    const double zJ = hj.zohHeard ? hj.z_j_zoh : 0.0;

    const auto I = candidateSet(i);
    double eSum = 0.0;
    for (uint32_t k : I) {
        eSum += slot(i, k).energy_j_zoh;
    }
    const double eAvg = I.empty() ? 0.0 : eSum / static_cast<double>(I.size());

    // heard: c_lin from the held SNR of j's last decoded frame (amend1 A5)
    const double snrL = cfg_.heardOnly ? hj.snr_db_zoh : linkSample(i, j).snr_db;
    out.c_lin = dahello::cLin(snrL, cfg_.packetBits, cfg_.constellationM);
    out.e_k = hj.e_k;
    out.c_pred = cPred(i, j);
    out.c_ene = LqarpProtocol::cEne(eJ, eAvg, cfg_.eIni);
    out.c_del = LqarpProtocol::cDel(eJ, cfg_.eIni, nodes_[i].z, zJ);
    out.v_rel = vRel(i, j);
    out.c_drift = cDrift(i, j);
    if (cfg_.heardOnly) {
        // held number_{s_j} of j's last decoded header (missing -> 0)
        const double nj = hj.number_zoh;
        out.c_nei = LqarpProtocol::cNei(static_cast<uint32_t>(std::isfinite(nj) ? std::max(0.0, nj) : 0.0));
    } else {
        out.c_nei = LqarpProtocol::cNei(numberSj(j));
    }

    const double num = out.c_ene * out.c_drift * out.c_nei;
    if (num == 0.0) {
        out.R = 0.0;
    } else if (!std::isfinite(out.c_pred)) {
        // p4: reward divided by an infinite (or undefined) cost is 0, not +-inf.
        out.R = 0.0;
    } else if (out.c_pred == 0.0) {
        out.R = std::copysign(std::numeric_limits<double>::infinity(), num);
    } else {
        out.R = num / out.c_pred;
    }
    return out;
}

double
DahelloProtocol::qValue(uint32_t i, uint32_t j) const
{
    checkIndex(i);
    checkIndex(j);
    return Q_[i][j];
}

void
DahelloProtocol::updateQ(uint32_t i, uint32_t j)
{
    checkIndex(i);
    checkIndex(j);
    const DahelloReward rw = reward(i, j);
    double maxNext = 0.0;
    if (cfg_.heardOnly) {
        // held V-value of j's last decoded data header (amend1 A5)
        const double v = slot(i, j).V_zoh;
        maxNext = std::isfinite(v) ? v : 0.0;
        Q_[i][j] = (1.0 - cfg_.alpha) * Q_[i][j] + cfg_.alpha * (rw.R + cfg_.gamma * maxNext);
        return;
    }
    const auto I_j = candidateSet(j);
    if (!I_j.empty()) {
        maxNext = Q_[j][I_j.front()];
        for (uint32_t a : I_j) {
            maxNext = std::max(maxNext, Q_[j][a]);
        }
    }
    Q_[i][j] = (1.0 - cfg_.alpha) * Q_[i][j]
               + cfg_.alpha * (rw.R + cfg_.gamma * maxNext);
}

void
DahelloProtocol::learnFrom(uint32_t i)
{
    learnFrom(i, candidateSet(i));
}

void
DahelloProtocol::learnFrom(uint32_t i, const std::vector<uint32_t>& actions)
{
    for (uint32_t j : actions) {
        updateQ(i, j);
    }
}

int
DahelloProtocol::nextHop(uint32_t i) const
{
    return nextHop(i, candidateSet(i));
}

int
DahelloProtocol::nextHop(uint32_t i, const std::vector<uint32_t>& actions) const
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
DahelloProtocol::peekFilter(uint32_t i, uint32_t j, FilterPeek& out) const
{
    checkIndex(i);
    checkIndex(j);
    out = FilterPeek{};
    if (i == j) {
        return false;
    }
    const NeighborEkf& s = slot(i, j);
    const LqarpNode& a = nodes_[i];
    const LqarpNode& b = nodes_[j];
    out.heard = s.zohHeard;
    out.lastZohS = s.lastZohTime;
    out.absE = std::fabs(s.e_k);
    out.epsilon = s.epsilon_k;
    out.broadcast = s.broadcast_k;
    out.stab = s.broadcast_k == 0 || std::fabs(s.e_k) < s.epsilon_k;
    out.heldX = s.x_j_zoh;
    out.heldY = s.y_j_zoh;
    out.heldZ = s.z_j_zoh;
    out.heldE = s.energy_j_zoh;
    out.heldQueryZ = s.query_z_j_zoh;
    out.energyGapJ = b.energy - (s.zohHeard ? s.energy_j_zoh : 0.0);
    if (!s.zohHeard) {
        return true;
    }
    out.poseErrM = ChannelGate::distance3D(b.x, b.y, b.z,
                                           s.x_j_zoh, s.y_j_zoh, s.z_j_zoh);
    out.range = ChannelGate::distance3D(a.x, a.y, a.z,
                                        s.x_j_zoh, s.y_j_zoh, s.z_j_zoh) < rRangeM_;
    const ChannelSample sample = gate_->queryLink(a.x, a.y, a.queryDepthM(),
                                                  s.x_j_zoh, s.y_j_zoh,
                                                  s.query_z_j_zoh);
    out.snr = !gate_->sampleFailsCandidateSnr(sample);
    out.depth = (a.z - s.z_j_zoh) > 0.0;
    return true;
}

std::vector<uint32_t>
DahelloProtocol::detourSet(uint32_t i) const
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
DahelloProtocol::detourTargets(uint32_t i,
                             uint32_t sinkId,
                             const std::vector<uint32_t>& visited) const
{
    std::vector<uint32_t> T;
    for (uint32_t j : detourSet(i)) {
        if (LqarpProtocol::containsId(visited, j)) {
            continue;
        }
        if (j == sinkId) {
            T.push_back(j);
            continue;
        }
        if (cfg_.heardOnly) {
            // heard: onward iff j's held header number > 0 (same rule as LqarpProtocol; v3 flag)
            const double nj = slot(i, j).number_zoh;
            if (std::isfinite(nj) && nj > 0.0) {
                T.push_back(j);
            }
            continue;
        }
        bool onwardI = false;
        for (uint32_t k : candidateSet(j)) {
            if (!LqarpProtocol::containsId(visited, k)) {
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
            if (!LqarpProtocol::containsId(visited, k)) {
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

double
DahelloProtocol::triggerEpsilon(double S) const
{
    // SPEC 2.3 addendum Option B:
    // epsilon_k = epsilon_0 + lambda * min(sqrt(S_k), s_cap)
    const double sTerm = (S > 0.0) ? std::sqrt(S) : 0.0;
    return cfg_.epsilon_0 + cfg_.lambda * std::min(sTerm, cfg_.s_cap);
}

double
DahelloProtocol::dSnrDRange(double rangeM, double receiverZ) const
{
    const double h = cfg_.fdRangeM;
    const ChannelSample lo = gate_->queryRangeDepth(std::max(0.0, rangeM - h), receiverZ);
    const ChannelSample hi = gate_->queryRangeDepth(rangeM + h, receiverZ);
    if (!std::isfinite(lo.snr_db) || !std::isfinite(hi.snr_db)) {
        return 0.0;
    }
    const double denom = (rangeM + h) - std::max(0.0, rangeM - h);
    if (denom <= 0.0) {
        return 0.0;
    }
    return (hi.snr_db - lo.snr_db) / denom;
}

void
DahelloProtocol::applyPredict(std::array<double, 7>& x,
                            std::array<double, 49>& P,
                            double dt,
                            double zRx)
{
    std::array<double, 7> xp = x;
    xp[0] = x[0] + dt * x[3];
    xp[1] = x[1] + dt * x[4];
    xp[2] = x[2] + dt * x[5];
    xp[3] = x[3];
    xp[4] = x[4];
    xp[5] = x[5];

    const double range = std::hypot(xp[0], xp[1]);
    // Bellhop SNR is (horizontal range, receiver acoustic depth).
    // Reconstructing kinematic z_j = z_i - p_ij_z misses acousticZ
    // (sink transducer at 50 m vs hull at 5 m).
    const ChannelSample pred = gate_->queryRangeDepth(range, zRx);
    if (std::isfinite(pred.snr_db)) {
        xp[6] = pred.snr_db;
    }
    // else keep last finite SNR; out-of-envelope is not interpolated.

    std::array<double, 49> F;
    ident49(F);
    F[static_cast<size_t>(idx(0, 3))] = dt;
    F[static_cast<size_t>(idx(1, 4))] = dt;
    F[static_cast<size_t>(idx(2, 5))] = dt;

    const double dsnr_dr = dSnrDRange(range, zRx);
    if (range > 1e-9 && std::isfinite(dsnr_dr)) {
        F[static_cast<size_t>(idx(6, 0))] = dsnr_dr * (xp[0] / range);
        F[static_cast<size_t>(idx(6, 1))] = dsnr_dr * (xp[1] / range);
    }
    F[static_cast<size_t>(idx(6, 6))] = 0.0; // SNR fully recomputed from pose

    std::array<double, 49> FP, FPFt;
    // OPT-1 (2026-10-12, behaviour-preserving): F is identity plus <= 5
    // off-diagonal entries. For finite operands a product with an exact-zero
    // F entry is +-0 and adding +-0 to the running sum (which starts at +0 and
    // can never be -0 under round-to-nearest) leaves it bitwise unchanged, so
    // summing only the nonzero terms in the same ascending-k order gives the
    // same bits as mul49(F,P,FP); mul49(FP,F^T,FPFt). Non-finite operands take
    // the original dense path.
    if (allFinite49(P)) {
        const double fa = F[static_cast<size_t>(idx(6, 0))];
        const double fb = F[static_cast<size_t>(idx(6, 1))];
        predictLeftMul49(dt, fa, fb, P, FP); // OPT-5
        if (allFinite49(FP)) {
            predictRightMulT49(dt, fa, fb, FP, FPFt); // OPT-5
        } else {
            std::array<double, 49> Ft;
            transpose49(F, Ft);
            mul49(FP, Ft, FPFt);
        }
    } else {
        std::array<double, 49> Ft;
        transpose49(F, Ft);
        mul49(F, P, FP);
        mul49(FP, Ft, FPFt);
    }
    for (int k = 0; k < 3; ++k) {
        FPFt[static_cast<size_t>(idx(k, k))] += cfg_.qPos;
        FPFt[static_cast<size_t>(idx(k + 3, k + 3))] += cfg_.qVel;
    }
    FPFt[static_cast<size_t>(idx(6, 6))] += cfg_.qSnr;
    x = xp;
    P = FPFt;
}

void
DahelloProtocol::predictPair(uint32_t i, uint32_t j, double dt)
{
    NeighborEkf& s = slot(i, j);
    applyPredict(s.x, s.P, dt, nodes_[j].queryDepthM());
}

void
DahelloProtocol::predictForecast(uint32_t i, uint32_t j, double dt)
{
    NeighborEkf& s = slot(i, j);
    applyPredict(s.xForecast, s.PForecast, dt, nodes_[j].queryDepthM());
}

void
DahelloProtocol::resetForecast(NeighborEkf& s) const
{
    s.xForecast = s.x;
    s.PForecast = s.P;
}

void
DahelloProtocol::ingestBroadcastVelocity(NeighborEkf& s, uint32_t i, uint32_t j,
                                       double vx_j, double vy_j, double vz_j) const
{
    materialize(i, j); // OPT-4: tracks must be current before the x[3:5] write

    // SPEC 2.1: pdot_j is the received ground-truth broadcast, not an
    // EKF inference. Overwrite x[3:5] = pdot_i - pdot_j. Leave p_ij
    // and SNR (x[0:2], x[6]) as the range-corrected estimate.
    // Position and energy are copied at the same event. Routing reads
    // those holds; the EKF measurement path still uses linkSample.
    const LqarpNode& b = nodes_[j];
    s.vx_j_zoh = vx_j;
    s.vy_j_zoh = vy_j;
    s.vz_j_zoh = vz_j;
    s.x_j_zoh = b.x;
    s.y_j_zoh = b.y;
    s.z_j_zoh = b.z;
    s.energy_j_zoh = b.energy;
    s.query_z_j_zoh = b.queryDepthM();
    s.zohHeard = true;
    s.lastZohTime = simTime_;
    s.x[3] = nodes_[i].vx - vx_j;
    s.x[4] = nodes_[i].vy - vy_j;
    s.x[5] = nodes_[i].vz - vz_j;
    resetForecast(s);
}

void
DahelloProtocol::updatePair(uint32_t i, uint32_t j, double z_k)
{
    materialize(i, j); // OPT-4

    NeighborEkf& s = slot(i, j);
    if (!std::isfinite(z_k)) {
        ++nanMeasCount_;
        // Out of Bellhop envelope: no measurement, do not fire.
        if (cfg_.broadcastMode == BroadcastMode::EventTriggered) {
            s.broadcast_k = 0;
        }
        return;
    }
    s.zWindow.push_back(z_k);
    while (s.zWindow.size() > cfg_.W_mp) {
        s.zWindow.pop_front();
    }
    const double z_tilde = medianCopy(s.zWindow);
    const double h = s.x[6];
    s.e_km1 = s.e_k;
    s.e_k = z_tilde - h;
    if (std::isfinite(s.e_k)) {
        maxAbsInnovation_ = std::max(maxAbsInnovation_, std::fabs(s.e_k));
    }

    s.S_k = s.P[static_cast<size_t>(idx(6, 6))] + cfg_.rMeas;
    if (!(s.S_k > 0.0)) {
        s.S_k = cfg_.rMeas;
    }
    s.epsilon_k = cfg_.epsilon_0 + cfg_.lambda * std::sqrt(s.S_k);

    // SPEC 2.3 addendum: e_k^{trigger} = z_k - h(x_forecast). z_k is the
    // SNR sampled at the last reception from j (received-only). Kalman e_k / S_k / epsilon_k above
    // stay on x_hat for the EKF update and c_pred.
    const double hForecast = s.xForecast[6];
    s.eTrigger = z_k - hForecast;
    s.STrigger = s.PForecast[static_cast<size_t>(idx(6, 6))] + cfg_.rMeas;
    if (!(s.STrigger > 0.0)) {
        s.STrigger = cfg_.rMeas;
    }
    s.epsilonTrigger = triggerEpsilon(s.STrigger);
    if (std::isfinite(s.eTrigger)) {
        const double ae = std::fabs(s.eTrigger);
        maxAbsTriggerInnovation_ = std::max(maxAbsTriggerInnovation_, ae);
        s.maxAbsETrigger = std::max(s.maxAbsETrigger, ae);
        // Unheard window includes the tick that first sets zohHeard
        // (the innovation that would have caused the fire).
        if (!s.zohHeard) {
            s.maxAbsETriggerUnheard = std::max(s.maxAbsETriggerUnheard, ae);
            if (std::isfinite(s.epsilonTrigger)) {
                s.maxEpsilonTriggerUnheard = std::max(s.maxEpsilonTriggerUnheard,
                                                      s.epsilonTrigger);
            }
        }
    }

    const bool exceed = std::fabs(s.eTrigger) >= s.epsilonTrigger;
    if (std::isfinite(s.eTrigger)) {
        sumAbsETrig_ += std::fabs(s.eTrigger);
        ++nETrig_;
        if (exceed) {
            ++nExceed_;
        }
    }
    s.exceedHist.push_back(exceed ? 1 : 0);
    while (s.exceedHist.size() > cfg_.W) {
        s.exceedHist.pop_front();
    }

    // Part 2.4 event trigger is skipped in FixedPeriodic mode (SPEC Part 4);
    // Broadcast_k is set on the T_ctrl grid in tick() instead. Parts 2.1–2.3
    // and the EKF update below are unchanged.
    if (cfg_.broadcastMode == BroadcastMode::EventTriggered) {
        recomputeBroadcast(i, j);
    }

    // EKF update on SNR measurement z_tilde. H = [0..0 1].
    const double S = s.S_k;
    std::array<double, 7> K{};
    for (int r = 0; r < kDim; ++r) {
        K[static_cast<size_t>(r)] = s.P[static_cast<size_t>(idx(r, 6))] / S;
    }
    const double innov = z_tilde - h;
    for (int r = 0; r < kDim; ++r) {
        s.x[static_cast<size_t>(r)] += K[static_cast<size_t>(r)] * innov;
    }
    // P <- (I - K H) P
    std::array<double, 49> KH;
    zero49(KH);
    for (int r = 0; r < kDim; ++r) {
        KH[static_cast<size_t>(idx(r, 6))] = K[static_cast<size_t>(r)];
    }
    std::array<double, 49> Iminus;
    ident49(Iminus);
    for (int r = 0; r < kDim; ++r) {
        for (int c = 0; c < kDim; ++c) {
            Iminus[static_cast<size_t>(idx(r, c))] -= KH[static_cast<size_t>(idx(r, c))];
        }
    }
    std::array<double, 49> Pnew;
    mul49(Iminus, s.P, Pnew);
    s.P = Pnew;

    // After the Kalman update: write received pdot_j into x[3:5] as
    // ground-truth relative velocity, then reset the forecast. Doing
    // this after the update stops the filter from blending the write
    // back toward the stale EKF velocity on the same tick.
    if (cfg_.broadcastMode == BroadcastMode::EventTriggered && s.broadcast_k == 1
        && !helloSuppressed_[j] && helloReaches(i, j)) {
        ingestBroadcastVelocity(s, i, j, nodes_[j].vx, nodes_[j].vy, nodes_[j].vz);
    }
}

void
DahelloProtocol::recomputeBroadcast(uint32_t i, uint32_t j)
{
    NeighborEkf& s = slot(i, j);
    const bool now = std::fabs(s.eTrigger) >= s.epsilonTrigger;
    double frac = 0.0;
    if (!s.exceedHist.empty()) {
        int sum = 0;
        for (int v : s.exceedHist) {
            sum += v;
        }
        frac = static_cast<double>(sum) / static_cast<double>(s.exceedHist.size());
    }
    s.broadcast_k = (now && frac >= cfg_.rho) ? 1 : 0;
    if (cfg_.linkTriggerOff && cfg_.broadcastMode == BroadcastMode::EventTriggered) {
        s.broadcast_k = 0; // NOVELTY ablation: link trigger disabled
    }
}

void
DahelloProtocol::emitPeriodicControl()
{
    // One control packet per node; every neighbor ZOH-updates that node's
    // velocity. Isolates timing from the Part 2.4 persistence gate.
    for (uint32_t i = 0; i < nodes_.size() && !cfg_.heardOnly; ++i) {
        for (uint32_t j = 0; j < nodes_.size(); ++j) {
            if (i == j) {
                continue;
            }
            NeighborEkf& s = ekf_[i][j];
            // j's HELLO refreshes i's hold only if i demodulates it.
            if (!helloReaches(i, j)) {
                s.broadcast_k = 0;
                continue;
            }
            s.broadcast_k = 1;
            if (!helloSuppressed_[j]) {
                ingestBroadcastVelocity(s, i, j, nodes_[j].vx, nodes_[j].vy, nodes_[j].vz);
            }
        }
    }
    eventHelloCount_ += static_cast<uint64_t>(nodes_.size());
    for (uint32_t j = 0; j < nodes_.size(); ++j) {
        broadcastFrom(j);
        if (txHook_) {
            txHook_(j, 1);
        }
    }
}

void
DahelloProtocol::refreshEnergyOnly()
{
    // Pose and velocity holds stay on the persistence-gated HELLO.
    // The beacon carries a quantized residual, not the full double.
    for (uint32_t i = 0; i < nodes_.size(); ++i) {
        for (uint32_t j = 0; j < nodes_.size(); ++j) {
            if (i == j) {
                continue;
            }
            NeighborEkf& s = ekf_[i][j];
            if (!s.zohHeard || !helloReaches(i, j)) {
                continue;
            }
            s.energy_j_zoh = quantizeEnergy(nodes_[j].energy);
        }
    }
    energyHelloCount_ += static_cast<uint64_t>(nodes_.size());
    for (uint32_t j = 0; j < nodes_.size(); ++j) {
        ctrlRxBeacon_ += broadcastFromCount(j);
        if (txHook_) {
            txHook_(j, 4);
        }
    }
}

void
DahelloProtocol::initPeriodicJitterOffsets()
{
    // Default false: leave shared grids alone (= p14).
    if (!cfg_.periodicJitter) {
        return;
    }
    const size_t n = nodes_.size();
    // Seed mixes run seed with a fixed salt (same family as marv-T jitter in scenario).
    std::mt19937_64 rng(cfg_.periodicJitterSeed);
    if (cfg_.broadcastMode == BroadcastMode::FixedPeriodic && cfg_.T_ctrl > 0.0
        && std::isfinite(cfg_.T_ctrl)) {
        std::uniform_real_distribution<double> u(0.0, cfg_.T_ctrl);
        periodicJitterOff_.assign(n, 0.0);
        nextPeriodicPerNode_.assign(n, 0.0);
        for (size_t j = 0; j < n; ++j) {
            periodicJitterOff_[j] = u(rng);
            // First fire at offset_j (k = 0). skipInitialControlRound already skipped t=0 all-node emit.
            nextPeriodicPerNode_[j] = periodicJitterOff_[j];
        }
    }
    if (cfg_.energyRefreshS > 0.0 && std::isfinite(cfg_.energyRefreshS)) {
        std::uniform_real_distribution<double> ue(0.0, cfg_.energyRefreshS);
        energyJitterOff_.assign(n, 0.0);
        nextEnergyPerNode_.assign(n, 0.0);
        for (size_t j = 0; j < n; ++j) {
            energyJitterOff_[j] = ue(rng);
            nextEnergyPerNode_[j] = energyJitterOff_[j];
        }
    }
}

void
DahelloProtocol::emitPeriodicControlFrom(uint32_t j)
{
    // Single-node FixedPeriodic HELLO (jittered path). Same per-receiver effect as the
    // j-loop body of emitPeriodicControl(); does not clear other nodes' broadcast_k.
    if (j >= nodes_.size()) {
        return;
    }
    for (uint32_t i = 0; i < nodes_.size() && !cfg_.heardOnly; ++i) {
        if (i == j) {
            continue;
        }
        NeighborEkf& s = ekf_[i][j];
        if (!helloReaches(i, j)) {
            s.broadcast_k = 0;
            continue;
        }
        s.broadcast_k = 1;
        if (!helloSuppressed_[j]) {
            ingestBroadcastVelocity(s, i, j, nodes_[j].vx, nodes_[j].vy, nodes_[j].vz);
        }
    }
    ++eventHelloCount_;
    broadcastFrom(j);
    if (txHook_) {
        txHook_(j, 1);
    }
}

void
DahelloProtocol::refreshEnergyOnlyFrom(uint32_t j)
{
    // Single-node energy-only beacon (jittered path).
    if (j >= nodes_.size()) {
        return;
    }
    for (uint32_t i = 0; i < nodes_.size(); ++i) {
        if (i == j) {
            continue;
        }
        NeighborEkf& s = ekf_[i][j];
        if (!s.zohHeard || !helloReaches(i, j)) {
            continue;
        }
        s.energy_j_zoh = quantizeEnergy(nodes_[j].energy);
    }
    ++energyHelloCount_;
    ctrlRxBeacon_ += broadcastFromCount(j);
    if (txHook_) {
        txHook_(j, 4);
    }
}

double
DahelloProtocol::quantizeEnergy(double joules) const
{
    const int bits = cfg_.energyQuantBits;
    if (bits <= 0) {
        return joules;
    }
    const double eIni = cfg_.eIni;
    if (!(eIni > 0.0) || !std::isfinite(joules)) {
        return joules;
    }
    double e = joules;
    if (e < 0.0) {
        e = 0.0;
    }
    if (e > eIni) {
        e = eIni;
    }
    // 2^bits - 1 in double: exact for bits <= 53. (1u << 32) is undefined
    // behaviour and gave levels = 0, i.e. NaN, at the 32-bit design point.
    if (bits > 53) {
        throw std::invalid_argument("energyQuantBits must be <= 53");
    }
    const double levels = std::ldexp(1.0, bits) - 1.0;
    const double code = std::round(e / eIni * levels);
    return code * eIni / levels;
}

void
DahelloProtocol::clearBroadcastFlags()
{
    for (uint32_t i = 0; i < nodes_.size(); ++i) {
        for (uint32_t j = 0; j < nodes_.size(); ++j) {
            if (i != j) {
                ekf_[i][j].broadcast_k = 0;
            }
        }
    }
}

void
DahelloProtocol::countEventControlTx()
{
    // SPEC 2.4: node j transmits one HELLO on a 0→1 rising edge of
    // Broadcast_k(i,j) for any neighbor i. Persisted Broadcast_k=1
    // does not re-announce.
    const size_t n = nodes_.size();
    if (prevBroadcast_.size() != n * n) {
        prevBroadcast_.assign(n * n, 0);
    }
    for (uint32_t j = 0; j < n; ++j) {
        bool rising = false;
        for (uint32_t i = 0; i < n; ++i) {
            if (i == j) {
                continue;
            }
            const int now = ekf_[i][j].broadcast_k;
            const int prev = prevBroadcast_[i * n + j];
            if (now == 1 && prev != 1) {
                rising = true;
                break;
            }
        }
        if (linkFiredTick_.size() != n) {
            linkFiredTick_.assign(n, 0);
        }
        linkFiredTick_[j] = rising ? 1 : 0;
        if (rising) {
            if (cfg_.heardOnly) {
                // Decision D-A: heard mode refuses the scheduler link trigger.
                continue;
            }
            ++eventHelloCount_;
            broadcastFrom(j);
            if (txHook_) {
                txHook_(j, 0);
            }
        }
    }
    for (uint32_t i = 0; i < n; ++i) {
        for (uint32_t j = 0; j < n; ++j) {
            prevBroadcast_[i * n + j] = ekf_[i][j].broadcast_k;
        }
    }
}

void
DahelloProtocol::tick(double dt)
{
    const size_t n = nodes_.size();
    std::vector<int> prev(n * n, 0);
    for (uint32_t i = 0; i < n; ++i) {
        for (uint32_t j = 0; j < n; ++j) {
            prev[i * n + j] = ekf_[i][j].broadcast_k;
        }
    }

    const double step = std::isfinite(dt) ? dt : cfg_.dt;
    if (lazyPredict_) {
        lazySteps_.push_back(step);
    }
    for (uint32_t i = 0; i < nodes_.size(); ++i) {
        for (uint32_t j = 0; j < nodes_.size(); ++j) {
            if (i == j) {
                continue;
            }
            // OPT-2 (2026-10-12, behaviour-preserving): applyPredict is a pure
            // function of (x, P, dt, z_j) (the gate is const and stateless), so
            // when the forecast track is bitwise equal to the filter track its
            // prediction is the same bits; copy instead of recomputing.
            if (lazyPredict_) {
                if (ekf_[i][j].rxPending) {
                    materialize(i, j); // includes this tick's step
                }
            } else {
                predictStep(i, j, step);
            }
            // Received-only z_k: correct x_hat and sample the trigger only
            // if a packet from j reached i since the last tick. Otherwise
            // both tracks stay predict-only via F_k, and (as for an
            // out-of-envelope sample) no measurement means no fire.
            NeighborEkf& s = ekf_[i][j];
            if (s.rxPending) {
                const double z_k = s.zRxPending;
                s.rxPending = false;
                s.zRxPending = std::numeric_limits<double>::quiet_NaN();
                ++measuredUpdates_;
                updatePair(i, j, z_k);
            } else {
                ++silentPredicts_;
                if (cfg_.broadcastMode == BroadcastMode::EventTriggered) {
                    s.broadcast_k = 0;
                }
            }
        }
    }
    simTime_ += step;
    if (cfg_.broadcastMode == BroadcastMode::FixedPeriodic) {
        const double T = cfg_.T_ctrl;
        if (cfg_.periodicJitter && !nextPeriodicPerNode_.empty() && T > 0.0 && std::isfinite(T)) {
            // Per-node phase: node j fires at offset_j + kT (k >= 0). No shared grid.
            clearBroadcastFlags();
            for (uint32_t j = 0; j < nextPeriodicPerNode_.size(); ++j) {
                while (simTime_ + 1e-12 >= nextPeriodicPerNode_[j]) {
                    emitPeriodicControlFrom(j);
                    nextPeriodicPerNode_[j] += T;
                }
            }
        } else {
            bool fired = false;
            while (simTime_ + 1e-12 >= nextPeriodicTime_) {
                emitPeriodicControl();
                fired = true;
                if (!(T > 0.0) || !std::isfinite(T)) {
                    break;
                }
                nextPeriodicTime_ += T;
            }
            if (!fired) {
                clearBroadcastFlags();
            }
        }
    } else if (cfg_.broadcastMode == BroadcastMode::Adaptive) {
        clearBroadcastFlags();
        adaptiveTick();
    } else {
        countEventControlTx();
        if (stateDirtyHook_) {
            // H3/H5: same tick grid and one-HELLO-per-node-per-tick rule as
            // the link trigger; a link HELLO this tick already carries the
            // host state, so the node is skipped (attributed to link).
            if (hostFiredTick_.size() != n) {
                hostFiredTick_.assign(n, 0);
            }
            for (uint32_t j = 0; j < n; ++j) {
                hostFiredTick_[j] = 0;
                if (linkFiredLastTick(j)) {
                    continue;
                }
                if (stateDirtyHook_(j)) {
                    hostFiredTick_[j] = 1;
                    ++hostStateHelloCount_;
                    broadcastFrom(j);
                    if (txHook_) {
                        txHook_(j, 3);
                    }
                }
            }
        }
    }

    if (cfg_.energyRefreshS > 0.0) {
        const double Te = cfg_.energyRefreshS;
        if (cfg_.periodicJitter && !nextEnergyPerNode_.empty() && Te > 0.0 && std::isfinite(Te)) {
            for (uint32_t j = 0; j < nextEnergyPerNode_.size(); ++j) {
                while (simTime_ + 1e-12 >= nextEnergyPerNode_[j]) {
                    refreshEnergyOnlyFrom(j);
                    nextEnergyPerNode_[j] += Te;
                }
            }
        } else {
            while (simTime_ + 1e-12 >= nextEnergyRefresh_) {
                refreshEnergyOnly();
                if (!(Te > 0.0) || !std::isfinite(Te)) {
                    break;
                }
                nextEnergyRefresh_ += Te;
            }
        }
    }

    for (uint32_t i = 0; i < n; ++i) {
        for (uint32_t j = 0; j < n; ++j) {
            if (i == j) {
                continue;
            }
            const int now = ekf_[i][j].broadcast_k;
            if (now == 1) {
                ++broadcastKOnes_;
                if (prev[i * n + j] != 1) {
                    ++broadcastRising_;
                    ++ekf_[i][j].nBroadcastRising;
                    if (!std::isfinite(ekf_[i][j].firstBroadcastTime)) {
                        ekf_[i][j].firstBroadcastTime = simTime_;
                    }
                }
                if (ekf_[i][j].zohHeard) {
                    ekf_[i][j].lastZohTime = simTime_;
                }
            }
        }
    }
}

void
DahelloProtocol::forceInnovation(uint32_t i, uint32_t j, double e_k, double epsilon_k)
{
    NeighborEkf& s = slot(i, j);
    s.e_km1 = s.e_k;
    s.e_k = e_k;
    s.epsilon_k = epsilon_k;
    // Test 7 drives Broadcast_k through recomputeBroadcast, which now
    // reads e_k^{trigger}. Keep the Kalman pair and the trigger pair
    // in lockstep for the seam.
    s.eTrigger = e_k;
    s.epsilonTrigger = epsilon_k;
}

void
DahelloProtocol::pushExceedFlag(uint32_t i, uint32_t j, bool exceed)
{
    NeighborEkf& s = slot(i, j);
    s.exceedHist.push_back(exceed ? 1 : 0);
    while (s.exceedHist.size() > cfg_.W) {
        s.exceedHist.pop_front();
    }
}

void
DahelloProtocol::setNeighborVelocityZoh(uint32_t i, uint32_t j,
                                      double vx, double vy, double vz)
{
    ingestBroadcastVelocity(slot(i, j), i, j, vx, vy, vz);
}

void
DahelloProtocol::setHelloSuppressed(uint32_t j, bool suppressed)
{
    checkIndex(j);
    helloSuppressed_[j] = suppressed ? 1 : 0;
}

void
DahelloProtocol::setBootstrapSuppressed(uint32_t j, bool suppressed)
{
    checkIndex(j);
    bootstrapSuppressed_[j] = suppressed ? 1 : 0;
}

void
DahelloProtocol::setBroadcastK(uint32_t i, uint32_t j, int broadcast_k)
{
    slot(i, j).broadcast_k = broadcast_k ? 1 : 0;
}

void
DahelloProtocol::flushEventControlTx()
{
    countEventControlTx();
}

bool
DahelloProtocol::zohHeard(uint32_t i, uint32_t j) const
{
    return slot(i, j).zohHeard;
}

double
DahelloProtocol::lastZohTime(uint32_t i, uint32_t j) const
{
    return slot(i, j).lastZohTime;
}

void
DahelloProtocol::setEnergy(uint32_t i, double joules)
{
    checkIndex(i);
    nodes_[i].energy = joules;
}

double
DahelloProtocol::energy(uint32_t i) const
{
    checkIndex(i);
    return nodes_[i].energy;
}

void
DahelloProtocol::setKinematics(uint32_t i, double x, double y, double z,
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

// ---------------------------------------------------------------------------
// NOVELTY step 2: adaptive HELLO baselines (BroadcastMode::Adaptive).
// ---------------------------------------------------------------------------

void
DahelloProtocol::neighbourSets(std::vector<std::vector<char>>& nb) const
{
    // nb[j][i] = 1 iff j currently decodes i (i in range of j and the
    // i->j link passes the HELLO reception gate). Idealised detector of
    // neighbour-table change (favours the baseline).
    const uint32_t n = static_cast<uint32_t>(nodes_.size());
    nb.assign(n, std::vector<char>(n, 0));
    if (cfg_.heardOnly) {
        // heard (amend1 per-policy rule): j's own table, i.e. entries j holds a record of.
        for (uint32_t j = 0; j < n; ++j) {
            for (uint32_t i = 0; i < n; ++i) {
                if (i != j && heardFresh(ekf_[j][i])) {
                    nb[j][i] = 1;
                }
            }
        }
        return;
    }
    for (uint32_t j = 0; j < n; ++j) {
        for (uint32_t i = 0; i < n; ++i) {
            if (i != j && liveInRange(j, i) && helloReaches(j, i)) {
                nb[j][i] = 1;
            }
        }
    }
}

double
DahelloProtocol::saPeriod(uint32_t j) const
{
    // S = largest relative speed to a neighbour whose velocity j holds
    // (from that neighbour's last HELLO). T = aR/(2S), clamped.
    const uint32_t n = static_cast<uint32_t>(nodes_.size());
    double S = 0.0;
    const LqarpNode& a = nodes_[j];
    for (uint32_t i = 0; i < n; ++i) {
        if (i == j) {
            continue;
        }
        const NeighborEkf& s = ekf_[j][i];
        if (!s.zohHeard) {
            continue;
        }
        const double d = ChannelGate::distance3D(a.x, a.y, a.z, s.x_j_zoh, s.y_j_zoh, s.z_j_zoh);
        if (!(d < rRangeM_)) {
            continue;
        }
        const double vr = std::sqrt((a.vx - s.vx_j_zoh) * (a.vx - s.vx_j_zoh)
                                    + (a.vy - s.vy_j_zoh) * (a.vy - s.vy_j_zoh)
                                    + (a.vz - s.vz_j_zoh) * (a.vz - s.vz_j_zoh));
        S = std::max(S, vr);
    }
    double T = cfg_.saTmaxS;
    if (S > 1e-12) {
        T = cfg_.saA * rRangeM_ / (2.0 * S);
    }
    return std::min(cfg_.saTmaxS, std::max(cfg_.saTminS, T));
}

double
DahelloProtocol::letPeriod(uint32_t j) const
{
    // t25: QLAR Eq. 14 / Eq. 6. N_j = own true state (I1). N_i = held record
    // (last HELLO) propagated at its held constant velocity to now (Eq. 6 assumes
    // constant velocity; QLAR's EKF prediction is not reproduced) [our choice].
    // Neighbour(N_j) = fresh held records with predicted distance < R; Alg. 1
    // drops LET <= 0, which inside range cannot occur.
    const uint32_t n = static_cast<uint32_t>(nodes_.size());
    const LqarpNode& a = nodes_[j];
    const double now = simTime_;
    double minLet = std::numeric_limits<double>::infinity();
    const double R = rRangeM_;
    for (uint32_t i = 0; i < n; ++i) {
        if (i == j) {
            continue;
        }
        const NeighborEkf& s = ekf_[j][i];
        if (!(cfg_.heardOnly ? heardFresh(s) : s.zohHeard)) {
            continue;
        }
        const double age = std::isfinite(s.lastZohTime) ? std::max(0.0, now - s.lastZohTime) : 0.0;
        const double xi = s.x_j_zoh + s.vx_j_zoh * age;
        const double yi = s.y_j_zoh + s.vy_j_zoh * age;
        const double zi = s.z_j_zoh + s.vz_j_zoh * age;
        // Eq. 6 symbols: b,d,f = position difference; a,c,e = velocity difference.
        const double bb = a.x - xi, dd = a.y - yi, ff = a.z - zi;
        const double aa = a.vx - s.vx_j_zoh, cc = a.vy - s.vy_j_zoh, ee = a.vz - s.vz_j_zoh;
        const double pp = bb * bb + dd * dd + ff * ff;
        if (!(pp < R * R)) {
            continue;
        }
        const double vv = aa * aa + cc * cc + ee * ee;
        if (!(vv > 1e-18)) {
            continue; // relative rest: LET infinite
        }
        const double pv = aa * bb + cc * dd + ee * ff;
        const double disc = pv * pv - vv * (pp - R * R);
        const double t = -pv / vv + std::sqrt(std::max(0.0, disc)) / vv;
        if (t > 0.0) {
            minLet = std::min(minLet, t);
        }
    }
    double T = cfg_.letTmaxS;
    if (std::isfinite(minLet)) {
        T = cfg_.letFrac * minLet;
    }
    return std::min(cfg_.letTmaxS, std::max(cfg_.letTminS, T));
}

bool
DahelloProtocol::voidFlagHeld(uint32_t j) const
{
    // t23: void iff j holds no in-range neighbour shallower than itself.
    const uint32_t n = static_cast<uint32_t>(nodes_.size());
    const LqarpNode& a = nodes_[j];
    for (uint32_t i = 0; i < n; ++i) {
        if (i == j) {
            continue;
        }
        const NeighborEkf& s = ekf_[j][i];
        if (!(cfg_.heardOnly ? heardFresh(s) : s.zohHeard)) {
            continue;
        }
        const double d = ChannelGate::distance3D(a.x, a.y, a.z, s.x_j_zoh, s.y_j_zoh, s.z_j_zoh);
        if (d < rRangeM_ && s.z_j_zoh < a.z) {
            return false;
        }
    }
    return true;
}

void
DahelloProtocol::trickleStart(uint32_t j, double t0)
{
    std::uniform_real_distribution<double> u(0.5, 1.0);
    trStart_[j] = t0;
    trC_[j] = 0;
    trSent_[j] = 0;
    trT_[j] = t0 + trI_[j] * u(adRng_);
}

void
DahelloProtocol::adaptiveInit()
{
    const uint32_t n = static_cast<uint32_t>(nodes_.size());
    adRng_.seed(cfg_.adaptiveSeed);
    neighbourSets(nbrPrev_);
    adNext_.assign(n, 0.0);
    trI_.assign(n, cfg_.trickleIminS);
    trStart_.assign(n, 0.0);
    trT_.assign(n, 0.0);
    trC_.assign(n, 0);
    trSent_.assign(n, 0);
    for (uint32_t j = 0; j < n; ++j) {
        if (cfg_.adaptiveScheme == 1) {
            // The t=0 round counts as the transmission of interval 0.
            trickleStart(j, 0.0);
            trSent_[j] = 1;
        } else if (cfg_.adaptiveScheme == 3) {
            adNext_[j] = letPeriod(j);
            letStats_.add(adNext_[j]);
        } else if (cfg_.adaptiveScheme == 4) {
            adNext_[j] = cfg_.naiveRefreshS;
        } else {
            adNext_[j] = saPeriod(j);
        }
    }
    if (cfg_.adaptiveScheme == 4) {
        nvVoid_.assign(n, 0);
        for (uint32_t j = 0; j < n; ++j) {
            nvVoid_[j] = voidFlagHeld(j) ? 1 : 0;
        }
    }
}

void
DahelloProtocol::adaptiveEmit(uint32_t j)
{
    // One HELLO from j: identical per-receiver effect to emitPeriodicControl.
    const uint32_t n = static_cast<uint32_t>(nodes_.size());
    // heard: the HELLO is a medium frame (txHook_); receivers update only via applyHeardHold.
    for (uint32_t i = 0; i < n && !cfg_.heardOnly; ++i) {
        if (i == j) {
            continue;
        }
        NeighborEkf& s = ekf_[i][j];
        if (!helloReaches(i, j)) {
            continue;
        }
        s.broadcast_k = 1;
        if (!helloSuppressed_[j]) {
            ingestBroadcastVelocity(s, i, j, nodes_[j].vx, nodes_[j].vy, nodes_[j].vz);
        }
        if (cfg_.adaptiveScheme == 1 && !nbrPrev_.empty() && nbrPrev_[i][j]) {
            ++trC_[i]; // consistent transmission heard by i
        }
    }
    ++eventHelloCount_;
    if (!cfg_.heardOnly) {
        ctrlRxHello_ += broadcastFromCount(j);
    }
    if (txHook_) {
        txHook_(j, 5);
    }
}

void
DahelloProtocol::adaptiveTick()
{
    const uint32_t n = static_cast<uint32_t>(nodes_.size());
    const double now = simTime_;
    std::vector<std::vector<char>> nb;
    neighbourSets(nb);
    if (cfg_.adaptiveScheme == 1) {
        const double imax = cfg_.trickleIminS * std::ldexp(1.0, cfg_.trickleDoublings);
        std::vector<uint32_t> due;
        for (uint32_t j = 0; j < n; ++j) {
            // Rule 5: interval expiry doubles I.
            while (now + 1e-9 >= trStart_[j] + trI_[j]) {
                const double end = trStart_[j] + trI_[j];
                trI_[j] = std::min(2.0 * trI_[j], imax);
                trickleStart(j, end);
            }
            // Rule 6: inconsistency (neighbour set of j changed) resets I.
            if (nb[j] != nbrPrev_[j] && trI_[j] > cfg_.trickleIminS + 1e-9) {
                trI_[j] = cfg_.trickleIminS;
                trickleStart(j, now);
                ++adResets_;
            }
            // Rule 4: transmit at t iff c < k.
            if (!trSent_[j] && now + 1e-9 >= trT_[j]) {
                trSent_[j] = 1;
                if (trC_[j] < cfg_.trickleK) {
                    due.push_back(j);
                } else {
                    ++adSuppressed_;
                }
            }
        }
        for (uint32_t j : due) {
            adaptiveEmit(j);
        }
    } else if (cfg_.adaptiveScheme == 4) {
        std::vector<uint32_t> due;
        for (uint32_t j = 0; j < n; ++j) {
            const char v = voidFlagHeld(j) ? 1 : 0;
            const bool changed = (v != nvVoid_[j]);
            nvVoid_[j] = v;
            if (changed) {
                ++nvVoidChanges_;
            }
            if (changed || now + 1e-9 >= adNext_[j]) {
                due.push_back(j);
                ++(changed ? nvVoidSends_ : nvRefreshSends_); // t31 output-only
            }
        }
        for (uint32_t j : due) {
            adaptiveEmit(j);
        }
        for (uint32_t j : due) {
            adNext_[j] = now + cfg_.naiveRefreshS;
        }
    } else {
        std::vector<uint32_t> due;
        for (uint32_t j = 0; j < n; ++j) {
            if (now + 1e-9 >= adNext_[j]) {
                due.push_back(j);
            }
        }
        for (uint32_t j : due) {
            adaptiveEmit(j);
        }
        for (uint32_t j : due) {
            if (cfg_.adaptiveScheme == 3) {
                const double th = letPeriod(j);
                letStats_.add(th); // t31 output-only
                adNext_[j] = now + th;
            } else {
                adNext_[j] = now + saPeriod(j);
            }
        }
    }
    nbrPrev_ = nb;
}

void
DahelloProtocol::applyHeardHold(uint32_t i, uint32_t j, double t,
                              double x, double y, double z,
                              double vx, double vy, double vz,
                              double energy, double snrDb,
                              double number, double V)
{
    checkIndex(i);
    checkIndex(j);
    if (holdWriteAudit_) {
        // p5: inside the poisoned tick scope a non-finite pose/velocity can only come from a
        // poisoned node copy (the filter below would drop it silently).
        const double f[6] = {x, y, z, vx, vy, vz};
        for (double v : f) {
            if (!std::isfinite(v)) {
                oracleLeak("DahelloProtocol::applyHeardHold(nonfinite in tick scope)", i, j);
            }
        }
    }
    if (i == j || bootstrapSuppressed_[j]) {
        return;
    }
    NeighborEkf& slot = ekf_[i][j];
    if (std::isfinite(x)) { slot.x_j_zoh = x; }
    if (std::isfinite(y)) { slot.y_j_zoh = y; }
    if (std::isfinite(z)) { slot.z_j_zoh = z; slot.query_z_j_zoh = z; }
    if (std::isfinite(vx)) { slot.vx_j_zoh = vx; }
    if (std::isfinite(vy)) { slot.vy_j_zoh = vy; }
    if (std::isfinite(vz)) { slot.vz_j_zoh = vz; }
    if (std::isfinite(energy)) { slot.energy_j_zoh = energy; }
    if (std::isfinite(snrDb)) { slot.snr_db_zoh = snrDb; }
    if (std::isfinite(number)) { slot.number_zoh = number; }
    if (std::isfinite(V)) { slot.V_zoh = V; }
    slot.zohHeard = true;
    slot.lastZohTime = t;
    if (cfg_.heardOnly && cfg_.adaptiveScheme == 1 && !nbrPrev_.empty() && nbrPrev_[i][j]) {
        ++trC_[i]; // consistent transmission decoded by i
    }
    if (cfg_.heardOnly && std::isfinite(snrDb)) {
        // EKF measurement = the decoded SNR (I3), replacing the live linkSample
        slot.rxPending = true;
        slot.zRxPending = snrDb;
        ++slot.nRx;
        ++receptionCount_;
    }
}

} // namespace dahello

namespace dahello {
std::string
LetIntervalStats::line(const std::string& tag, double cap) const
{
    // t31 output-only: summary of every scheduled LET interval TH (s).
    std::vector<double> w = v;
    std::sort(w.begin(), w.end());
    const size_t n = w.size();
    double sum = 0.0;
    for (double x : w) sum += x;
    auto q = [&](double p) { if (!n) return std::numeric_limits<double>::quiet_NaN(); return w[std::min(n - 1, static_cast<size_t>(p * (n - 1) + 0.5))]; };
    const double edges[] = {1.0, 5.0, 15.0, 30.0, 60.0};
    uint64_t h[7] = {0, 0, 0, 0, 0, 0, 0};
    uint64_t atCap = 0;
    for (double x : w) {
        if (x >= cap - 1e-9) { ++h[6]; ++atCap; continue; }
        int b = 5;
        for (int k = 0; k < 5; ++k) { if (x < edges[k]) { b = k; break; } }
        ++h[b];
    }
    std::ostringstream o;
    o.precision(6);
    o << "LET_INTERVALS proto=" << tag << " n=" << n
      << " mean_s=" << (n ? sum / n : std::numeric_limits<double>::quiet_NaN())
      << " median_s=" << (n ? (n % 2 ? w[n / 2] : 0.5 * (w[n / 2 - 1] + w[n / 2])) : std::numeric_limits<double>::quiet_NaN())
      << " min_s=" << (n ? w.front() : std::numeric_limits<double>::quiet_NaN())
      << " max_s=" << (n ? w.back() : std::numeric_limits<double>::quiet_NaN())
      << " p10_s=" << q(0.10) << " p90_s=" << q(0.90)
      << " n_cap" << cap << "=" << atCap
      << " n_lt1=" << h[0] << " n_lt5=" << (h[0] + h[1])
      << " hist[0-1,1-5,5-15,15-30,30-60,60-cap,=cap]=" << h[0] << "," << h[1] << "," << h[2] << "," << h[3]
      << "," << h[4] << "," << h[5] << "," << h[6];
    return o.str();
}
}
