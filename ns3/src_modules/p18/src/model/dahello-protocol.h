// dahello-protocol.h
//
// DA-HELLO (SPEC.md Part 2) on top of the LQARP baseline. Same
// ChannelGate / kBerFloor as LqarpProtocol. beta_1=0, beta_2=0 and
// a trigger that never fires recover LQARP exactly.

#ifndef DAHELLO_DAHELLO_PROTOCOL_H
#define DAHELLO_DAHELLO_PROTOCOL_H

#include "lqarp-protocol.h"

#include <array>
#include <cstdint>
#include <deque>
#include <functional>
#include <limits>
#include <random>
#include <vector>
#include <string>

namespace dahello {

enum class BroadcastMode {
    EventTriggered, // SPEC 2.4
    FixedPeriodic,  // Section 4.3 ablation: Broadcast_k every T_ctrl
    // NOVELTY step 2 (design choice, default unused): per-node adaptive
    // HELLO baselines. adaptiveScheme selects Trickle (RFC 6206) or the
    // speed-adaptive optimal frequency f = 2S/(aR) (Ingelrest et al.,
    // MASCOTS 2007, Eq. 1). A HELLO behaves exactly like a Fixed-Periodic
    // HELLO of that one sender (same hold write, same reception gate).
    Adaptive
};

// Caps lambda*sqrt(S_k) on the event trigger. Equals sqrt(rMeas):
// the original heuristic scale, used because P_forecast is never
// measurement-corrected and would otherwise inflate epsilon_k
// without bound (SPEC 2.3 addendum, Step 45 Option B).
constexpr double kSCapDb = 2.0;

// t31 (output-only): LET interval summary for letadapt / marv-let reports.
struct LetIntervalStats {
    std::vector<double> v;
    void add(double t) { v.push_back(t); }
    std::string line(const std::string& tag, double cap) const;
};

struct DahelloConfig : LqarpConfig {
    // p4 (2026-10-16): heard-only runs (--disc=heard) skip the t=0 control round in the
    // constructor. That round runs before setHeardOnly(true) and copies every neighbour's
    // true pose/energy with no held SNR (NaN -> c_lin=inf). Default false = old behaviour.
    bool skipInitialControlRound = false;
    // Item 4 (2026-10-12): heard-mode neighbour freshness for this host (s).
    // +inf (default) = no expiry = behaviour before item 4. LqarpConfig::tNbS is not read here.
    double tNbSchedS = std::numeric_limits<double>::infinity();
    double beta_1 = 0.0;             // default identity; SWEPT at run time
    double beta_2 = 0.0;             // default identity; SWEPT at run time
    double epsilon_0 = 2.0;          // Step 46; 70-80% ZOH-init contour
    double s_cap = kSCapDb;          // dB; min(sqrt(S_k), s_cap)
    double lambda = 1.0;             // first-pass
    uint32_t W_mp = 3;               // first-pass median window
    uint32_t W = 5;                  // first-pass persistence window
    double rho = 0.6;                // first-pass
    double dt = 1.0;                 // EKF predict step (s)
    double fdRangeM = 10.0;          // finite-difference range (m)
    double qPos = 1.0;               // process noise, position
    double qVel = 0.1;               // process noise, velocity
    double qSnr = 1.0;               // process noise, SNR
    double rMeas = 4.0;              // measurement noise R_k (dB^2)
    BroadcastMode broadcastMode = BroadcastMode::EventTriggered;
    double T_ctrl = 5.0;             // first-pass periodic ablation (s)
    // 0 = energy_j_zoh moves only with a pose HELLO. >0 = overwrite
    // energy_j_zoh on this grid without touching pose or velocity.
    double energyRefreshS = 0.0;
    // 0 = full-precision residual on that beacon. 8 or 16 = uniform
    // quantizer over [0, E_ini]. Addressing is a separate field.
    int energyQuantBits = 0;
    int energyAddrBits = 8;
    // p15 A2.2(a): when true, FixedPeriodic HELLOs and energyRefreshS beacons use
    // one per-node phase offset U[0,T) drawn at start-up (seeded from
    // periodicJitterSeed). Default false = p14 lock-step grid.
    bool periodicJitter = false;
    uint64_t periodicJitterSeed = 0;
    // Step 5: a HELLO / energy beacon from j writes i's hold for j only if
    // i demodulates it (ChannelGate::receptionOk on j->i). false restores
    // the pre-Step-5 network-wide write (diagnostic A/B only).
    bool helloRxGate = true;
    // NOVELTY step 2 parameters (only read in BroadcastMode::Adaptive).
    int adaptiveScheme = 0;       // 1 = Trickle, 2 = speed-adaptive
    double trickleIminS = 5.0;    // Imin (s)
    int trickleDoublings = 4;     // Imax = Imin * 2^doublings
    int trickleK = 5;             // redundancy constant k
    double saA = 0.1;             // a in f_opt = 2S/(aR)
    double saTminS = 5.0;         // clamp
    double saTmaxS = 60.0;        // clamp (also S = 0)
    // t23/t25 (default unused): adaptiveScheme 3 = QLAR HELLO interval
    // (Li et al., JMSE 13(12):2374, 2025): Eq. 14 (p.12) T_H = tau * min LET,
    // LET by Eq. 6 (p.7). tau = 0.3: Table 2 (p.16), tau in (0,1). [paper]
    // letTminS = 0: the paper has no floor [paper; effective floor = host tick].
    // letTmaxS = 120: interval when no neighbour has a finite LET (empty table or
    // relative rest), undefined in the paper [our choice].
    double letFrac = 0.3;
    double letTminS = 0.0;
    double letTmaxS = 120.0;
    // adaptiveScheme 4 = naive (not from QLAR): HELLO only when j's void flag
    // changes (void = no held in-range neighbour satisfying the host's depth
    // candidate condition, dahello-protocol.cc N_depth), plus a slow periodic
    // refresh every naiveRefreshS (user range 60-120 s; 90 = midpoint, our choice).
    double naiveRefreshS = 90.0;
    uint32_t adaptiveSeed = 1;    // Trickle t ~ U[I/2, I)
    // NOVELTY ablation (design choice, default off): suppress the link
    // (Kalman-forecast) trigger so only bootstrap and host-state HELLOs fire.
    bool linkTriggerOff = false;
    // DECISION-AWARE (design choice, default off): bootstrap fix. On a first
    // contact, sender j emits ONE bootstrap broadcast that every receiver
    // which has not yet heard j (and passes the live range+SNR gate) keeps;
    // charged as one 400-bit transmission + one reception per receiver.
    bool bootstrapBroadcast = false;
};

struct DahelloReward {
    double c_lin = 0.0;
    double c_pred = 0.0;
    double c_ene = 0.0;
    double c_del = 0.0;
    double c_drift = 0.0;
    double c_nei = 0.0;
    double v_rel = 0.0;
    double e_k = 0.0;
    double R = 0.0;
};

// Per-neighbor EKF (7-dim: p_ij, pdot_ij, SNR_ij).
struct NeighborEkf {
    std::array<double, 7> x{};
    std::array<double, 49> P{}; // row-major 7x7
    // SPEC 2.2 addendum: open-loop forecast, predict-only, never updated.
    std::array<double, 7> xForecast{};
    std::array<double, 49> PForecast{};
    // OPT-4: number of tick predict steps already applied to x/P/xForecast/PForecast
    uint32_t lazySynced = 0;
    std::deque<double> zWindow;
    std::deque<int> exceedHist; // 1 if |e_m^{trigger}| >= epsilon_m
    double e_k = 0.0;
    double e_km1 = 0.0;
    double epsilon_k = 0.0;
    double S_k = 0.0;
    // SPEC 2.3 addendum: forecast-vs-truth innovation (trigger only).
    double eTrigger = 0.0;
    double STrigger = 0.0;
    double epsilonTrigger = 0.0;
    // Running maxima for Step 44 (diagnostic; not used by the trigger).
    double maxAbsETrigger = 0.0;
    double maxAbsETriggerUnheard = 0.0;
    double maxEpsilonTriggerUnheard = 0.0;
    int broadcast_k = 0;
    double vx_j_zoh = 0.0;
    double vy_j_zoh = 0.0;
    double vz_j_zoh = 0.0;
    // Last HELLO's pose and residual energy. Routing filters read these,
    // not the live MobilityModel copy in nodes_[j].
    double x_j_zoh = 0.0;
    double y_j_zoh = 0.0;
    double z_j_zoh = 0.0;
    double energy_j_zoh = 0.0;
    double query_z_j_zoh = 0.0; // Bellhop receiver depth at that HELLO
    // Port A heard mode (amend1 A5): I2/I3 fields of j's last decoded frame
    double snr_db_zoh = std::numeric_limits<double>::quiet_NaN ();
    double number_zoh = std::numeric_limits<double>::quiet_NaN ();
    double V_zoh = std::numeric_limits<double>::quiet_NaN ();
    bool initialized = false;
    // SPEC 2.1: pdot_j is last received broadcast. False until the
    // first Broadcast_k=1 (or periodic HELLO) copies neighbor velocity.
    bool zohHeard = false;
    double lastZohTime = std::numeric_limits<double>::quiet_NaN ();
    double firstBroadcastTime = std::numeric_limits<double>::quiet_NaN ();
    uint64_t nBroadcastRising = 0;
    // Received-only z_k: set when a packet from j is demodulated at i
    // (ChannelGate::receptionOk on the j->i link). tick() runs
    // updatePair only on the next tick after such a reception, with the
    // SNR sampled at that reception; otherwise both tracks predict only.
    bool rxPending = false;
    double zRxPending = std::numeric_limits<double>::quiet_NaN ();
    uint64_t nRx = 0;
};

class DahelloProtocol {
public:
    DahelloProtocol(ChannelGate& gate,
                  DahelloConfig cfg,
                  std::vector<LqarpNode> nodes);

    const DahelloConfig& config() const { return cfg_; }
    const std::vector<LqarpNode>& nodes() const { return nodes_; }
    std::vector<LqarpNode>& nodesMut() { return nodes_; } // poison scope only
    double rRangeM() const { return rRangeM_; }

    // Part 1.2 three-condition set (c_nei still uses this, SPEC 2.8).
    std::vector<uint32_t> lqarpCandidateSet(uint32_t i) const;

    // I_DAHELLO = N_range ∩ N_SNR ∩ N_depth ∩ N_stab
    std::vector<uint32_t> candidateSet(uint32_t i) const;
    bool inNRange(uint32_t i, uint32_t j) const;
    bool inNSnr(uint32_t i, uint32_t j) const;
    bool inNDepth(uint32_t i, uint32_t j) const;
    bool inNStab(uint32_t i, uint32_t j) const;

    uint32_t numberSj(uint32_t j) const; // |I_LQARP(s_j)|

    // SPEC 2.1
    void relativeState(uint32_t i, uint32_t j,
                       double& pijx, double& pijy, double& pijz,
                       double& vijx, double& vijy, double& vijz) const;
    double vRel(uint32_t i, uint32_t j) const;

    static double sigmoid(double u);

    // SPEC 2.6–2.7 (c_lin uses the shared BER floor).
    double cPred(uint32_t i, uint32_t j) const;
    double cDrift(uint32_t i, uint32_t j) const;

    DahelloReward reward(uint32_t i, uint32_t j) const;

    double qValue(uint32_t i, uint32_t j) const;
    void updateQ(uint32_t i, uint32_t j);
    void learnFrom(uint32_t i);
    void learnFrom(uint32_t i, const std::vector<uint32_t>& actions);
    int nextHop(uint32_t i) const;
    int nextHop(uint32_t i, const std::vector<uint32_t>& actions) const;

    // SPEC 1.5. D = N_range ∩ N_SNR (no N_depth, no N_stab).
    // I(s_j) in the two-hop filter is I_DAHELLO.
    std::vector<uint32_t> detourSet(uint32_t i) const;
    std::vector<uint32_t> detourTargets(uint32_t i,
                                        uint32_t sinkId,
                                        const std::vector<uint32_t>& visited) const;

    // EKF tick: predict, measure, median, innovation, trigger, update.
    void tick(double dt);

    double simTime() const { return simTime_; }
    // Event/periodic HELLOs plus one-time bootstrap discoveries.
    // Position-trigger HELLOs plus bootstrap. Energy-only beacons are excluded.
    // Host-state HELLOs (H1-H5) are 0 unless a host installs the hook.
    uint64_t controlTxCount() const
    {
        return eventHelloCount_ + bootstrapHelloCount_ + hostStateHelloCount_;
    }
    // H1: optional host hook "advertised state changed since last sent".
    // Evaluated once per tick after the link trigger (EventTriggered mode
    // only); a node that already sent a link HELLO this tick is skipped.
    void setStateDirtyHook(std::function<bool(uint32_t)> hook) { stateDirtyHook_ = std::move(hook); }
    // Port A (2026-10-12): optional control-transmission hook, called once per
    // control frame the scheduler sends (tx node, cause). Unset by default:
    // no behaviour change. cause: 0 link/event, 1 periodic, 2 bootstrap,
    // 3 host-state, 4 energy beacon, 5 adaptive (Trickle / speed-adaptive).
    void setTxHook(std::function<void(uint32_t, int)> hook) { txHook_ = std::move(hook); }
    // Port A Gate 2: heard-only. Refuses oracle noteBootstrap and the scheduler
    // link trigger (Decision D-A). Holds come from applyHeardHold only.
    void setHeardOnly(bool on) { cfg_.heardOnly = on; }
    void setTnbSched(double s) { cfg_.tNbSchedS = s; }
    double tNbSched() const { return cfg_.tNbSchedS; }
    // Item 4: a held record counts only within T_nb of its last decode (heard mode).
    bool heardFresh(const NeighborEkf& s) const
    {
        return s.zohHeard && (!cfg_.heardOnly || !(simTime_ - s.lastZohTime > cfg_.tNbSchedS));
    }
    bool heardOnly() const { return cfg_.heardOnly; }
    // p5 (NaN-path leak guard): while on (tick-scope guard only), a held-record write whose
    // pose/velocity argument is non-finite is a read of poisoned (NaN) state -> oracleLeak (exit 3).
    // Needed because applyHeardHold silently drops non-finite fields. Default off.
    void setHoldWriteAudit(bool on) { holdWriteAudit_ = on; }
    void applyHeardHold(uint32_t i, uint32_t j, double t,
                        double x, double y, double z,
                        double vx, double vy, double vz,
                        double energy, double snrDb,
                        double number, double V);
    uint64_t hostStateHelloCount() const { return hostStateHelloCount_; }
    bool linkFiredLastTick(uint32_t j) const { return j < linkFiredTick_.size() && linkFiredTick_[j]; }
    bool hostFiredLastTick(uint32_t j) const { return j < hostFiredTick_.size() && hostFiredTick_[j]; }
    uint64_t bootstrapBroadcasts() const { return bootBcast_; }
    uint64_t bootstrapFromCount(uint32_t j) const { return j < bootstrapFrom_.size() ? bootstrapFrom_[j] : 0; }
    // Public view of the HELLO reception gate (helloRxGate).
    bool helloReachesView(uint32_t rx, uint32_t tx) const { return helloReaches(rx, tx); }
    uint64_t eventHelloCount() const { return eventHelloCount_; }
    uint64_t bootstrapHelloCount() const { return bootstrapHelloCount_; }
    uint64_t energyHelloCount() const { return energyHelloCount_; }
    int energyBeaconBits() const
    {
        if (!(cfg_.energyRefreshS > 0.0)) {
            return 0;
        }
        return cfg_.energyQuantBits + cfg_.energyAddrBits;
    }
    uint64_t broadcastKOnes() const { return broadcastKOnes_; }
    // NOVELTY observation-only counters (no protocol state is changed).
    uint64_t ctrlRxHello() const { return ctrlRxHello_; }
    uint64_t ctrlRxBeacon() const { return ctrlRxBeacon_; }
    uint64_t ctrlRxBootstrap() const { return ctrlRxBoot_; }
    double sumAbsETrigger() const { return sumAbsETrig_; }
    uint64_t nETrigger() const { return nETrig_; }
    uint64_t nExceed() const { return nExceed_; }
    uint64_t adaptiveResets() const { return adResets_; }
    // t31 (output-only diagnostics)
    uint64_t naiveVoidChanges() const { return nvVoidChanges_; }
    uint64_t naiveVoidSends() const { return nvVoidSends_; }
    uint64_t naiveRefreshSends() const { return nvRefreshSends_; }
    const LetIntervalStats& letStats() const { return letStats_; }
    int adaptiveSchemeId() const { return cfg_.adaptiveScheme; }
    uint64_t adaptiveSuppressed() const { return adSuppressed_; }
    uint64_t broadcastRising() const { return broadcastRising_; }
    uint64_t nanMeasCount() const { return nanMeasCount_; }
    double maxAbsInnovation() const { return maxAbsInnovation_; }
    double maxAbsTriggerInnovation() const { return maxAbsTriggerInnovation_; }

    const NeighborEkf& ekf(uint32_t i, uint32_t j) const;
    // OPT-4: same slot WITHOUT bringing x/P/xForecast/PForecast up to date.
    // Only for callers that read none of those four fields (they are the only
    // fields materialize() writes, so every other field is current).
    const NeighborEkf& ekfMeta(uint32_t i, uint32_t j) const { return slot(i, j); }
    bool zohHeard(uint32_t i, uint32_t j) const;
    double lastZohTime(uint32_t i, uint32_t j) const;

    // Test seams for Part 2 unit tests (do not use as protocol API).
    void forceInnovation(uint32_t i, uint32_t j, double e_k, double epsilon_k);
    void pushExceedFlag(uint32_t i, uint32_t j, bool exceed);
    void recomputeBroadcast(uint32_t i, uint32_t j);
    void setNeighborVelocityZoh(uint32_t i, uint32_t j,
                                double vx, double vy, double vz);
    // Skip ingestBroadcastVelocity for neighbor j (event and periodic).
    // Does not block the one-time bootstrap discovery.
    void setHelloSuppressed(uint32_t j, bool suppressed);
    // Skip the one-time bootstrap write for neighbor j. Later
    // broadcast_k / T_ctrl refreshes are unchanged.
    void setBootstrapSuppressed(uint32_t j, bool suppressed);
    void setBroadcastK(uint32_t i, uint32_t j, int broadcast_k);
    void flushEventControlTx();

    ChannelSample linkSample(uint32_t i, uint32_t j) const;

    // Received-only measurement path. Node rx demodulates a packet sent
    // by tx iff ChannelGate::receptionOk(linkSample(tx, rx)). Returns
    // whether it was received. broadcastFrom applies that to every rx.
    bool noteReception(uint32_t rx, uint32_t tx);
    void broadcastFrom(uint32_t tx);
    uint64_t receptionCount() const { return receptionCount_; }
    uint64_t measuredUpdates() const { return measuredUpdates_; }
    uint64_t silentPredicts() const { return silentPredicts_; }

    // Held filters and N_stab only. Does not call noteBootstrap.
    bool peekFilter(uint32_t i, uint32_t j, FilterPeek& out) const;

    void setEnergy(uint32_t i, double joules);
    double energy(uint32_t i) const;
    void setKinematics(uint32_t i, double x, double y, double z,
                       double vx, double vy, double vz);

private:
    ChannelGate* gate_;
    DahelloConfig cfg_;
    std::function<void(uint32_t, int)> txHook_;
    std::vector<LqarpNode> nodes_;
    double rRangeM_;
    std::vector<std::vector<double>> Q_;
    // Mutable so the const candidate-set path can record a one-time
    // bootstrap discovery, the same way LQARP's noteDiscovery writes
    // its frozen neighbor from a const filter.
    mutable std::vector<std::vector<NeighborEkf>> ekf_;
    // OPT-4 lazy exact predict: per-tick step log; a pair's tracks are brought
    // up to date (same per-step code, same order) only when they are read.
    bool lazyPredict_ = false;
    bool holdWriteAudit_ = false; // p5
    std::vector<double> lazySteps_;
    void materialize(uint32_t i, uint32_t j) const;
    void predictStep(uint32_t i, uint32_t j, double step);
    std::vector<char> helloSuppressed_;
    std::vector<char> bootstrapSuppressed_;
    double simTime_ = 0.0;
    double nextPeriodicTime_ = 0.0;
    uint64_t eventHelloCount_ = 0;
    mutable uint64_t bootstrapHelloCount_ = 0;
    uint64_t energyHelloCount_ = 0;
    double nextEnergyRefresh_ = 0.0;
    // p15 A2.2(a): per-node next fire times when cfg_.periodicJitter (else unused).
    std::vector<double> nextPeriodicPerNode_;
    std::vector<double> nextEnergyPerNode_;
    std::vector<double> periodicJitterOff_;
    std::vector<double> energyJitterOff_;
    uint64_t broadcastKOnes_ = 0;
    uint64_t broadcastRising_ = 0;
    std::vector<int> prevBroadcast_;
    uint64_t nanMeasCount_ = 0;
    double maxAbsInnovation_ = 0.0;
    double maxAbsTriggerInnovation_ = 0.0;
    mutable uint64_t receptionCount_ = 0;
    std::function<bool(uint32_t)> stateDirtyHook_;
    uint64_t hostStateHelloCount_ = 0;
    std::vector<char> linkFiredTick_;
    std::vector<char> hostFiredTick_;
    mutable std::vector<uint64_t> bootstrapFrom_;
    uint64_t measuredUpdates_ = 0;
    uint64_t silentPredicts_ = 0;
    // NOVELTY
    uint64_t ctrlRxHello_ = 0;
    uint64_t ctrlRxBeacon_ = 0;
    mutable uint64_t ctrlRxBoot_ = 0;
    mutable uint64_t bootBcast_ = 0;
    double sumAbsETrig_ = 0.0;
    uint64_t nETrig_ = 0;
    uint64_t nExceed_ = 0;
    uint64_t adResets_ = 0;
    uint64_t adSuppressed_ = 0;
    std::mt19937 adRng_;
    std::vector<double> adNext_;
    std::vector<double> trI_;
    std::vector<double> trStart_;
    std::vector<double> trT_;
    std::vector<int> trC_;
    std::vector<char> trSent_;
    std::vector<std::vector<char>> nbrPrev_;
    uint64_t broadcastFromCount(uint32_t tx);
    void adaptiveInit();
    void adaptiveTick();
    void adaptiveEmit(uint32_t j);
    void trickleStart(uint32_t j, double t0);
    void neighbourSets(std::vector<std::vector<char>>& nb) const;
    double saPeriod(uint32_t j) const;
    double letPeriod(uint32_t j) const;
    bool voidFlagHeld(uint32_t j) const;
    std::vector<char> nvVoid_;
    uint64_t nvVoidChanges_ = 0;
    uint64_t nvVoidSends_ = 0, nvRefreshSends_ = 0; // t31 output-only
    LetIntervalStats letStats_;                     // t31 output-only
    bool noteReceptionImpl(uint32_t rx, uint32_t tx) const;
    bool helloReaches(uint32_t rx, uint32_t tx) const;

    void checkIndex(uint32_t i) const;
    NeighborEkf& slot(uint32_t i, uint32_t j);
    const NeighborEkf& slot(uint32_t i, uint32_t j) const;
    void initEkf(uint32_t i, uint32_t j);
    void applyPredict(std::array<double, 7>& x,
                      std::array<double, 49>& P,
                      double dt,
                      double zRx);
    void predictPair(uint32_t i, uint32_t j, double dt);
    void predictForecast(uint32_t i, uint32_t j, double dt);
    void resetForecast(NeighborEkf& s) const;
    void ingestBroadcastVelocity(NeighborEkf& s, uint32_t i, uint32_t j,
                                 double vx_j, double vy_j, double vz_j) const;
    // First live N_range ∩ N_SNR. Not a trigger refresh.
    void noteBootstrap(uint32_t i, uint32_t j) const;
    bool liveInRange(uint32_t i, uint32_t j) const;
    bool liveInSnr(uint32_t i, uint32_t j) const;
    void updatePair(uint32_t i, uint32_t j, double z_k);
    double triggerEpsilon(double S) const;
    double dSnrDRange(double rangeM, double receiverZ) const;
    void emitPeriodicControl();
    void emitPeriodicControlFrom(uint32_t j);
    void refreshEnergyOnly();
    void refreshEnergyOnlyFrom(uint32_t j);
    void initPeriodicJitterOffsets();
    double quantizeEnergy(double joules) const;
    void clearBroadcastFlags();
    void countEventControlTx();
};

} // namespace dahello

#endif // DAHELLO_DAHELLO_PROTOCOL_H
