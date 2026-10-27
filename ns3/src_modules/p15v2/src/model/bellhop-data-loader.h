// bellhop-data-loader.h
//
// Loads the WOA23/GEBCO/Bellhop-derived TL and SNR lookup grids
// (data/channel_db/TL_grid_extended_{january,july}.csv,
//  data/channel_db/SNR_grid_extended_{january,july}.csv)
// and provides bilinear-interpolated queries for arbitrary
// (range, receiver_depth) pairs during ns-3/Aqua-Sim simulation.
//
// Grid provenance (Option A, per project decision):
//   - Ray-summed H_eq(f) reconstruction, NOT native Bellhop .shd.
//   - Known limitation: optimistic SNR estimate in the July,
//     4-6 km band, by up to ~25 dB, due to unmodeled SSP-dependent
//     beam defocusing (see validation report, Step G4).
//   - Grid: range 0-6000 m @ 25 m steps (241 points, range=0 is NaN),
//            depth 0-500 m @ 10 m steps (51 points, depth=0 flagged
//            as a pressure-release artifact -- do not place receivers
//            there).

#ifndef BELLHOP_DATA_LOADER_H
#define BELLHOP_DATA_LOADER_H

#include <string>
#include <vector>
#include <stdexcept>
#include <cmath>
#include <limits>

namespace dahello {

// Which month's channel database to load. Extend this enum if
// additional seasonal SSPs are added later.
enum class Season {
    January,
    July
};

// A single interpolated channel-query result.
struct ChannelSample {
    double tl_db = std::numeric_limits<double>::quiet_NaN();
    double snr_db = std::numeric_limits<double>::quiet_NaN();
    bool valid = false;              // false iff the payload is NaN: range=0
                                     // singularity (NaN corners) or out of the
                                     // [0, maxRange] x [0, maxDepth] envelope.
                                     // Depth=0 is NOT invalid; see nearPressureRelease.
    bool nearPressureRelease = false; // true if a bilinear corner sits on the
                                     // z=0 row (axis depth < 5 m) AND the query
                                     // depth is inside that 5 m half-step
                                     // (depth_m < 5). valid may still be true.
                                     // Caller may exclude these (e.g. never
                                     // place a receiver at z < 5 m).
};

class BellhopDataLoader {
public:
    // Loads both TL and SNR grids for the given season from the
    // specified CSV files. Throws std::runtime_error on any parse
    // failure or missing file -- this is deliberate: a silently
    // empty channel database would make every link look infinitely
    // lossy or infinitely good depending on how zero-fill is handled,
    // which is worse than a hard failure at startup.
    BellhopDataLoader(const std::string& tlCsvPath,
                       const std::string& snrCsvPath,
                       Season season);

    // Bilinear-interpolated query at an arbitrary (range, depth) pair.
    // range_m must be in [0, 6000], depth_m in [0, 500]. Values outside
    // this envelope are NOT extrapolated: tl_db and snr_db are quiet_NaN
    // and valid is false. valid=false always means a NaN payload.
    ChannelSample query(double range_m, double depth_m) const;

    // Convenience accessor for the season this loader was built with,
    // useful when a node needs to know which grid it's running against
    // (e.g. for logging or for a seasonal-switch experiment).
    Season getSeason() const { return season_; }

    // Grid extents, exposed so calling code (e.g. the mobility model
    // or the EKF's SNR-prediction coupling term, Eq. snr_prediction_coupling)
    // can validate its own range/depth bounds against what the loader
    // actually supports, rather than hardcoding 6000/500 in multiple places.
    double maxRangeM() const { return rangeGrid_.back(); }
    double maxDepthM() const { return depthGrid_.back(); }

    // Sorted axis samples as stored in the CSVs. ChannelGate scans
    // rangeAxis() at 50 m to obtain r_range / RangeAtSnrThreshold.
    const std::vector<double>& rangeAxis() const { return rangeGrid_; }
    const std::vector<double>& depthAxis() const { return depthGrid_; }

private:
    Season season_;

    // Grid axis values, sorted ascending, as read from the CSVs.
    // rangeGrid_[0] should be 0 (flagged NaN cell) or 25 (first valid
    // cell) depending on how the CSV stored it -- checked at load time.
    std::vector<double> rangeGrid_;
    std::vector<double> depthGrid_;

    // Flattened grids, indexed as data_[rangeIdx * depthGrid_.size() + depthIdx].
    // Range=0 singularity cells are NaN in the CSV and stored as quiet_NaN.
    // Depth=0 (pressure-release surface) is stored as the CSV's finite value;
    // query() flags those interpolations with nearPressureRelease instead of
    // turning them into NaN.
    std::vector<double> tlData_;
    std::vector<double> snrData_;

    // Parses one CSV of the form [range_m, receiver_depth_m, VALUE_dB]
    // into a flattened grid matching rangeGrid_/depthGrid_. Called once
    // per grid (TL, SNR) from the constructor. Throws on malformed rows,
    // duplicate cells, or a grid that doesn't form a complete rectangle
    // (every (range, depth) combination in rangeGrid_ x depthGrid_ must
    // appear exactly once, except the explicitly-known excluded cells).
    void loadGrid(const std::string& csvPath,
                  std::vector<double>& outData,
                  bool isFirstGridLoaded);

    // Finds the two grid indices bracketing `value` along `axis`, and
    // the fractional interpolation weight between them. Used for both
    // range and depth axes. Clamps to [0, axis.size()-1] at the edges
    // rather than throwing, since a query exactly at the boundary
    // (e.g. depth_m = 500.0 exactly) is a legitimate, expected case,
    // not an error.
    static void findBracket(const std::vector<double>& axis,
                             double value,
                             size_t& lowIdx,
                             size_t& highIdx,
                             double& frac);

    // Core bilinear interpolation over a flattened grid, given
    // precomputed range/depth brackets and fractions. Returns NaN if
    // any of the four corner cells used in the interpolation is itself
    // NaN (i.e. a range=0 singularity cell) -- this is a deliberate
    // conservative choice: a query near an excluded cell should not be
    // silently "smoothed over" using three good corners and one bad one.
    // Depth=0 is finite in the CSV and is interpolated; query() raises
    // nearPressureRelease instead of turning those cells into NaN.
    double interpolate(const std::vector<double>& grid,
                        size_t rLow, size_t rHigh, double rFrac,
                        size_t dLow, size_t dHigh, double dFrac) const;
};

} // namespace dahello

#endif // BELLHOP_DATA_LOADER_H
