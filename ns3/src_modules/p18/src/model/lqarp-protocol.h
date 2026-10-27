// lqarp-protocol.h
//
// LQARP baseline (SPEC.md Part 1). Independent of phase1/ (ETHA /
// HELLO / oracle). DA-HELLO Part 2 is not implemented here.
//
// Identifiers follow SPEC.md: c_lin, c_ene, c_del, c_nei, I(s_i),
// number_{s_j}, E_avg, E_ini, p_e, P_succ, r_range, SNR_threshold.
//
// gamma = 0.8 is DA-HELLO's Section 4.1 value (hostFidelity=0 / p15v2).
// A2.9 hostFidelity=1 sets gamma = 0.95 (paper gives no value; placeholder).
// alpha = 0.9 (Table II). E_avg = mean residual of the candidate set (Eq. 39).

#ifndef DAHELLO_LQARP_PROTOCOL_H
#define DAHELLO_LQARP_PROTOCOL_H

#include "channel-gate.h"
#include "link-quality.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace dahello {

struct LqarpConfig {
    double alpha = 0.9;              // learning rate (Table II)
    double gamma = 0.8;              // discount (DA-HELLO Sec. 4.1; see header)
    double snrThresholdDb = 8.68;    // SNR_threshold
    double rRangeM = std::numeric_limits<double>::quiet_NaN();
                                     // N_range radius. NaN → ChannelGate::rangeAtSnrThreshold()
    uint32_t packetBits = 400;       // l
    int constellationM = 2;          // M (BPSK); 4 or 8 allowed by the formula
    double eIni = kDefaultEiniJ;     // E_ini = 10000 J (Section 4.1)
    // 0 = manuscript LQARP (freeze at first discovery). >0 = overwrite
    // held neighbor pose/energy on this grid (Step 1e; matches T_ctrl).
    double holdRefreshS = 0.0;
    // Port A Gate 2: heard-only discovery (default false = legacy oracle).
    bool heardOnly = false;
    double tNbS = 3600.0; // neighbour freshness (s); used only when heardOnly
    // A2.9 --hostFidelity: when true, gamma = 0.95 (paper gives no value; stays
    // placeholder at 0.95). Default false keeps gamma=0.8 (p15v2 / DA-HELLO Sec 4.1).
    bool hostFidelity = false;
};

// SPEC 1.5: max detour hops per packet. Design parameter, not from
// the LQARP paper (paper gives no equation). Flagged.
constexpr uint32_t kMaxDetourHops = 3;

// Kinematic + energy state of node s_i. z is depth, positive down.
// number_{s_i} is NOT stored: it is |I(s_i)| computed on demand.
struct LqarpNode {
    uint32_t id = 0;
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;       // depth_{s_i} (kinematic; N_depth, c_del, 3-D range)
    double energy = 0.0;  // E_{s_i}
    double vx = 0.0;      // used by DA-HELLO; LQARP ignores kinematics
    double vy = 0.0;
    double vz = 0.0;
    // Bellhop receiver depth for this node. NaN → use kinematic z.
    // USV towed / hull-offset transducer: kinematic z stays at the
    // hull (unique shallowest for N_depth); acousticZ is the query
    // depth. ChannelGate still applies depth_query = max(., 5.0).
    double acousticZ = std::numeric_limits<double>::quiet_NaN();

    double queryDepthM() const
    {
        return std::isfinite(acousticZ) ? acousticZ : z;
    }
};

struct LqarpReward {
    double c_lin = 0.0;
    double c_ene = 0.0;
    double c_del = 0.0;
    double c_nei = 0.0;
    double R = 0.0; // c_ene * c_del * c_nei / c_lin
};

// Read-only view of a held or frozen neighbor. Does not discover,
// bootstrap, or refresh. Routing never reads this struct.
struct FilterPeek {
    bool heard = false;
    bool range = false;
    bool snr = false;
    bool depth = false;
    bool stab = true; // LQARP has no N_stab
    double lastZohS = std::numeric_limits<double>::quiet_NaN();
    double absE = 0.0;
    double epsilon = 0.0;
    int broadcast = 0;
    double heldX = 0.0;
    double heldY = 0.0;
    double heldZ = 0.0;
    double heldE = 0.0;
    double heldQueryZ = 0.0;
    double poseErrM = 0.0;     // live pose vs held pose
    double energyGapJ = 0.0;   // live residual minus held residual
};

class LqarpProtocol {
public:
    LqarpProtocol(ChannelGate& gate,
                  LqarpConfig cfg,
                  std::vector<LqarpNode> nodes);

    const LqarpConfig& config() const { return cfg_; }
    const std::vector<LqarpNode>& nodes() const { return nodes_; }
    std::vector<LqarpNode>& nodesMut() { return nodes_; } // poison scope only
    double rRangeM() const { return rRangeM_; }

    // SPEC 1.2 candidate-set pieces. j != i is enforced.
    bool inNRange(uint32_t i, uint32_t j) const; // d(s_i,s_j) < r_range
    bool inNSnr(uint32_t i, uint32_t j) const;   // SNR > SNR_threshold
    bool inNDepth(uint32_t i, uint32_t j) const; // depth_i - depth_j > 0

    // I(s_i) = N_range ∩ N_SNR ∩ N_depth
    std::vector<uint32_t> candidateSet(uint32_t i) const;

    // number_{s_j} = |I(s_j)|, from s_j's own perspective.
    uint32_t numberSj(uint32_t j) const;

    // SPEC 1.3 factor functions. BER/c_lin are the shared link-quality
    // helpers (kBerFloor applied once). Wrappers keep call sites stable.
    static double ofdmBitErrorRate(double snrDb, int M)
    {
        return dahello::ofdmBitErrorRate(snrDb, M);
    }
    static double pSucc(double snrDb, uint32_t l, int M)
    {
        return dahello::pSucc(snrDb, l, M);
    }
    static double cLin(double snrDb, uint32_t l, int M)
    {
        return dahello::cLin(snrDb, l, M);
    }
    static double cEne(double eSj, double eAvg, double eIni);            // c_ene
    static double cDel(double eSj, double eIni, double depthSi, double depthSj); // c_del
    static double cNei(uint32_t numberSj);                               // c_nei

    LqarpReward reward(uint32_t i, uint32_t j) const;

    // SPEC 1.4. Q initializes at 0. If I(s_j) is empty, the max-Q
    // bootstrap term is 0 (no onward action).
    double qValue(uint32_t i, uint32_t j) const;
    void updateQ(uint32_t i, uint32_t j);
    void learnFrom(uint32_t i); // one TD step for every a in I(s_i)
    void learnFrom(uint32_t i, const std::vector<uint32_t>& actions);

    // s_i.NextHop = argmax_{a in I(s_i)} Q. Ties → smallest id.
    // Returns -1 when I(s_i) is empty.
    int nextHop(uint32_t i) const;
    int nextHop(uint32_t i, const std::vector<uint32_t>& actions) const;

    // SPEC 1.5 documented detour approximation (not a verified LQARP
    // reproduction). D = N_range ∩ N_SNR. detourTargets applies the
    // two-hop filter and excludes visited nodes. I(s_j) "valid forward
    // path" means I(s_j) contains a node not in visited (not just a
    // bounce back to the sender).
    std::vector<uint32_t> detourSet(uint32_t i) const;
    std::vector<uint32_t> detourTargets(uint32_t i,
                                        uint32_t sinkId,
                                        const std::vector<uint32_t>& visited) const;
    static bool containsId(const std::vector<uint32_t>& ids, uint32_t id);

    ChannelSample linkSample(uint32_t i, uint32_t j) const;

    // Held filters only. Does not call noteDiscovery.
    bool peekFilter(uint32_t i, uint32_t j, FilterPeek& out) const;
    // p6 (LQARP position-aware DA test): read-only copy of i's held record of j (no discovery,
    // no refresh). fresh = heard and, in heard mode, within T_nb. Decisions never read this.
    struct HeldView {
        bool heard = false, fresh = false;
        double x = 0.0, y = 0.0, z = 0.0, energy = 0.0;
        double V = std::numeric_limits<double>::quiet_NaN();
        double number = std::numeric_limits<double>::quiet_NaN();
    };
    HeldView heldView(uint32_t i, uint32_t j) const
    {
        HeldView v;
        checkIndex(i);
        checkIndex(j);
        const FrozenNeighbor& h = frozen_[i][j];
        v.heard = h.heard;
        v.fresh = h.heard && (!cfg_.heardOnly || !(cfg_.tNbS > 0.0) ||
                              (std::isfinite(h.lastHeardTime) && (simTimeHeard_ - h.lastHeardTime) <= cfg_.tNbS));
        v.x = h.x_j_zoh; v.y = h.y_j_zoh; v.z = h.z_j_zoh; v.energy = h.energy_j_zoh;
        v.V = h.V_zoh; v.number = h.number_zoh;
        return v;
    }

    // Residual energy (J). Used by the Scenario I forwarding loop.
    void setEnergy(uint32_t i, double joules);
    double energy(uint32_t i) const;

    // Copy ns-3 mobility into the protocol node (Scenario II).
    // Does not touch energy or acousticZ. Does not refresh a hold
    // that has already been written.
    void setKinematics(uint32_t i, double x, double y, double z,
                       double vx, double vy, double vz);

    // If set, neighbor j is never copied into a hold, so i's filters
    // stay closed for j for the rest of the run.
    void setDiscoverySuppressed(uint32_t j, bool suppressed);

    // Port A Gate 2: switch to heard-only (no oracle noteDiscovery). Idempotent.
    void setHeardOnly(bool on) { cfg_.heardOnly = on; }
    bool heardOnly() const { return cfg_.heardOnly; }
    void setTnb(double s) { cfg_.tNbS = s; }

    // Apply a hold from a frame i decoded from j (heard mode). Fields are
    // whatever the frame carried; NaN leaves the previous held value.
    void applyHeardHold(uint32_t i, uint32_t j, double t,
                        double x, double y, double z,
                        double vx, double vy, double vz,
                        double energy, double snrDb,
                        double number, double V);

    // Planted-leak seam (test only): when set, reward() reads the live link.
    void setLeakLiveLink(bool on) { leakLiveLink_ = on; }

    bool periodicHoldRefresh() const { return cfg_.holdRefreshS > 0.0; }
    // Fixed-Periodic grid: copy live nodes_[j] into every hold (t=0
    // and every holdRefreshS). Does not change Q or reward weights.
    void emitHoldRefresh();
    void tick(double dt);

private:
    // One-shot copy of neighbor j, written the first time the live
    // pair is inside N_range and N_SNR. N_depth is applied to z_j_zoh
    // afterwards; requiring it before the copy would leave the detour
    // set (range ∩ SNR) with no record of a deeper neighbor.
    struct FrozenNeighbor {
        double x_j_zoh = 0.0;
        double y_j_zoh = 0.0;
        double z_j_zoh = 0.0;
        double energy_j_zoh = 0.0;
        double query_z_j_zoh = 0.0;
        double vx_j_zoh = 0.0;
        double vy_j_zoh = 0.0;
        double vz_j_zoh = 0.0;
        double snr_db_zoh = std::numeric_limits<double>::quiet_NaN();
        double number_zoh = std::numeric_limits<double>::quiet_NaN();
        double V_zoh = std::numeric_limits<double>::quiet_NaN();
        double lastHeardTime = std::numeric_limits<double>::quiet_NaN();
        bool heard = false;
    };
    bool leakLiveLink_ = false;
    mutable double simTimeHeard_ = 0.0;

    ChannelGate* gate_;
    LqarpConfig cfg_;
    std::vector<LqarpNode> nodes_;
    double rRangeM_;
    std::vector<std::vector<double>> Q_; // Q_[i][j] = Q(s_i, a_j)
    mutable std::vector<std::vector<FrozenNeighbor>> frozen_;
    std::vector<char> discoverySuppressed_;
    double simTime_ = 0.0;
    double nextHoldRefresh_ = 0.0;

    void checkIndex(uint32_t i) const;
    bool liveInRange(uint32_t i, uint32_t j) const;
    bool liveInSnr(uint32_t i, uint32_t j) const;
    void ingestHold(uint32_t j, FrozenNeighbor& h) const;
    void noteDiscovery(uint32_t i, uint32_t j) const;
};

} // namespace dahello

#endif // DAHELLO_LQARP_PROTOCOL_H
