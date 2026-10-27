// channel-gate.h
//
// Shared SNR / range gate for every protocol variant (LQARP, RLOR,
// Fixed-Periodic, DA-HELLO). SPEC.md Part 5 items 1--4.
//
// All variants MUST go through this class. None may use phase1::SnrDb
// (Thorp + Wenz + SL=185 + 30 dB margin).

#ifndef DAHELLO_CHANNEL_GATE_H
#define DAHELLO_CHANNEL_GATE_H

#include "bellhop-data-loader.h"

#include <cmath>
#include <memory>

namespace dahello {

// Shared Bellhop-backed channel lookup. One instance is handed to
// every protocol so they see the same physical channel.
class ChannelGate {
public:
    // Transducer depth floor: query() uses this, raw z does not.
    // SPEC.md Part 5 item 2.
    static constexpr double kTransducerFloorM = 5.0;

    // Canonical receiver depth for r_range / RangeAtSnrThreshold.
    // Matches Bellhop source depth and Step G3 crossing tables.
    // SPEC.md Part 5 item 4.
    static constexpr double kCanonicalQueryDepthM = 50.0;

    // LQARP Table II detection threshold (dB).
    static constexpr double kDefaultSnrThresholdDb = 8.68;

    ChannelGate(std::shared_ptr<const BellhopDataLoader> loader,
                double snrThresholdDb = kDefaultSnrThresholdDb);

    virtual ~ChannelGate() = default;

    const BellhopDataLoader& loader() const { return *loader_; }
    double snrThresholdDb() const { return snrThresholdDb_; }

    // Horizontal range (Bellhop r-axis). Not 3-D slant.
    static double horizontalRangeM(double x1, double y1, double x2, double y2);

    // depth_query = max(receiver_z, 5.0). SPEC.md Part 5 item 2.
    static double depthQueryM(double receiverZ);

    // 3-D Euclidean d(s_i, s_j) used by N_range.
    static double distance3D(double x1, double y1, double z1,
                             double x2, double y2, double z2);

    // Lookup at floored receiver depth (receiverZ may be a node's
    // acousticZ, e.g. a towed USV transducer, rather than kinematic z).
    // nearPressureRelease samples are treated as failed links
    // (SPEC: bypass of the 5 m floor).
    virtual ChannelSample queryLink(double txX, double txY, double txZ,
                                    double rxX, double rxY, double rxZ) const;

    double snrDb(double txX, double txY, double txZ,
                 double rxX, double rxY, double rxZ) const;

    // Direct (range, receiver_depth) lookup, still applying the
    // transducer floor. Used by the EKF Jacobian (finite differences
    // along the Bellhop range axis).
    virtual ChannelSample queryRangeDepth(double rangeM, double receiverZ) const;

    // Part 5 item 3: NaN-safe failure. Reception uses SNR >= threshold,
    // so failure is !isfinite(snr) || snr < threshold. Also fails when
    // the sample is invalid or nearPressureRelease.
    bool snrFailsThreshold(double snrDb) const;
    bool sampleFailsLink(const ChannelSample& sample) const;

    // N_SNR (SPEC 1.2) is strict: SNR > SNR_threshold. NaN / invalid /
    // nearPressureRelease still fail.
    bool sampleFailsCandidateSnr(const ChannelSample& sample) const;

    // True iff the query produced a channel value at all: it landed inside the
    // Bellhop envelope and yielded a finite SNR. Strictly weaker than
    // sampleFailsCandidateSnr — there is no threshold comparison here. SPEC
    // Part 1.2 defines N_SNR as { s_j : SNR(s_i,s_j) > SNR_threshold }, which
    // presupposes that SNR is defined; this is that precondition.
    bool sampleHasDefinedSnr(const ChannelSample& sample) const;

    // Reception condition (SPEC 1.2, binary): RECEPTION iff SNR >= th
    // and the sample is usable; INVALID otherwise.
    bool receptionOk(const ChannelSample& sample) const;

    // First range-axis sample at 50 m whose SNR is below threshold.
    // January ~5125 m, July ~4525 m (Step G3). If none fail, maxRange.
    virtual double rangeAtSnrThreshold() const;

    // OPT-4 (2026-10-12): true iff queryRangeDepth ignores receiverZ. Lets
    // DahelloProtocol defer EKF predicts exactly (see DahelloProtocol::materialize).
    virtual bool depthIndependent() const { return false; }

protected:
    ChannelGate() = default; // test doubles that do not own a loader
    void setSnrThresholdDb(double v) { snrThresholdDb_ = v; }

private:
    std::shared_ptr<const BellhopDataLoader> loader_;
    double snrThresholdDb_ = kDefaultSnrThresholdDb;
};

} // namespace dahello

#endif // DAHELLO_CHANNEL_GATE_H
