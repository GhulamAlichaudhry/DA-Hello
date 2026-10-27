// channel-gate.cc

#include "channel-gate.h"

#include <algorithm>
#include <stdexcept>

namespace dahello {

ChannelGate::ChannelGate(std::shared_ptr<const BellhopDataLoader> loader,
                         double snrThresholdDb)
    : loader_(std::move(loader)),
      snrThresholdDb_(snrThresholdDb)
{
    if (!loader_) {
        throw std::runtime_error("ChannelGate: null BellhopDataLoader");
    }
}

double
ChannelGate::horizontalRangeM(double x1, double y1, double x2, double y2)
{
    return std::hypot(x1 - x2, y1 - y2);
}

double
ChannelGate::depthQueryM(double receiverZ)
{
    return std::max(receiverZ, kTransducerFloorM);
}

double
ChannelGate::distance3D(double x1, double y1, double z1,
                        double x2, double y2, double z2)
{
    const double dx = x1 - x2;
    const double dy = y1 - y2;
    const double dz = z1 - z2;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

ChannelSample
ChannelGate::queryLink(double txX, double txY, double txZ,
                       double rxX, double rxY, double rxZ) const
{
    (void) txZ; // source depth is baked into the Bellhop table (50 m)
    const double rangeM = horizontalRangeM(txX, txY, rxX, rxY);
    const double depthM = depthQueryM(rxZ);
    return loader_->query(rangeM, depthM);
}

double
ChannelGate::snrDb(double txX, double txY, double txZ,
                   double rxX, double rxY, double rxZ) const
{
    return queryLink(txX, txY, txZ, rxX, rxY, rxZ).snr_db;
}

ChannelSample
ChannelGate::queryRangeDepth(double rangeM, double receiverZ) const
{
    return loader_->query(rangeM, depthQueryM(receiverZ));
}

bool
ChannelGate::snrFailsThreshold(double snrDbValue) const
{
    // Never `snr < th` alone: NaN < th is false in IEEE arithmetic.
    return !std::isfinite(snrDbValue) || snrDbValue < snrThresholdDb_;
}

bool
ChannelGate::sampleFailsLink(const ChannelSample& sample) const
{
    if (!sample.valid || sample.nearPressureRelease) {
        return true;
    }
    return snrFailsThreshold(sample.snr_db);
}

bool
ChannelGate::sampleFailsCandidateSnr(const ChannelSample& sample) const
{
    // N_SNR: SNR(s_i, s_j) > SNR_threshold, plus the NaN/nearPR guard.
    if (!sample.valid || sample.nearPressureRelease || !std::isfinite(sample.snr_db)) {
        return true;
    }
    return !(sample.snr_db > snrThresholdDb_);
}

bool
ChannelGate::sampleHasDefinedSnr(const ChannelSample& sample) const
{
    return sample.valid && !sample.nearPressureRelease
           && std::isfinite(sample.snr_db);
}

bool
ChannelGate::receptionOk(const ChannelSample& sample) const
{
    return !sampleFailsLink(sample);
}

double
ChannelGate::rangeAtSnrThreshold() const
{
    // Scan the loader's own range axis at the canonical 50 m depth.
    const auto& ranges = loader_->rangeAxis();
    for (double r : ranges) {
        const ChannelSample s = loader_->query(r, kCanonicalQueryDepthM);
        if (sampleFailsLink(s)) {
            // Skip the range=0 singularity (NaN) so R* is the first
            // acoustically-below-threshold cell, matching Step G3.
            if (r <= 0.0 && !s.valid) {
                continue;
            }
            return r;
        }
    }
    return loader_->maxRangeM();
}

} // namespace dahello
