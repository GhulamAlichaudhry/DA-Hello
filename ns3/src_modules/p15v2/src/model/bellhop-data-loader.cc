// bellhop-data-loader.cc

#include "bellhop-data-loader.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <set>
#include <tuple>
#include <cctype>

namespace dahello {

BellhopDataLoader::BellhopDataLoader(const std::string& tlCsvPath,
                                       const std::string& snrCsvPath,
                                       Season season)
    : season_(season)
{
    // Load TL first -- this call also populates rangeGrid_/depthGrid_.
    loadGrid(tlCsvPath, tlData_, /*isFirstGridLoaded=*/true);
    // Load SNR against the SAME axes -- this call validates that the
    // SNR grid's axes match the TL grid's exactly, since the two files
    // must be co-registered for a query to make sense.
    loadGrid(snrCsvPath, snrData_, /*isFirstGridLoaded=*/false);

    if (rangeGrid_.empty() || depthGrid_.empty()) {
        throw std::runtime_error(
            "BellhopDataLoader: parsed grid has zero extent; "
            "check CSV format for " + tlCsvPath);
    }
}

void BellhopDataLoader::loadGrid(const std::string& csvPath,
                                   std::vector<double>& outData,
                                   bool isFirstGridLoaded)
{
    std::ifstream file(csvPath);
    if (!file.is_open()) {
        throw std::runtime_error(
            "BellhopDataLoader: could not open file: " + csvPath);
    }

    std::string line;
    bool headerSkipped = false;

    // Temporary storage: map (range, depth) -> value, before we know
    // the final sorted axis lists.
    std::vector<std::tuple<double, double, double>> rows;

    while (std::getline(file, line)) {
        if (line.empty()) continue;
        if (!headerSkipped) {
            // Skip the header row (range_m,receiver_depth_m,TL_dB or ...,SNR_dB).
            // Detected by checking whether the first field parses as a number.
            std::stringstream headerCheck(line);
            std::string firstField;
            std::getline(headerCheck, firstField, ',');
            bool looksNumeric = !firstField.empty() &&
                (std::isdigit(firstField[0]) || firstField[0] == '-');
            headerSkipped = true;
            if (!looksNumeric) {
                continue; // genuine header row, skip it
            }
            // else fall through -- first line was already data
        }

        std::stringstream ss(line);
        std::string rangeStr, depthStr, valueStr;
        if (!std::getline(ss, rangeStr, ',') ||
            !std::getline(ss, depthStr, ',') ||
            !std::getline(ss, valueStr, ',')) {
            throw std::runtime_error(
                "BellhopDataLoader: malformed row in " + csvPath +
                ": \"" + line + "\"");
        }

        double range_m, depth_m, value_db;
        try {
            range_m = std::stod(rangeStr);
            depth_m = std::stod(depthStr);
            // NaN cells (range=0 singularity, or explicitly flagged
            // depth=0 artifact rows if the CSV encodes them as "nan")
            // parse via stod's acceptance of "nan"/"NaN" literals; if
            // the CSV instead left the field blank, valueStr will be
            // empty and we treat that as NaN too.
            value_db = valueStr.empty()
                ? std::numeric_limits<double>::quiet_NaN()
                : std::stod(valueStr);
        } catch (const std::exception& e) {
            throw std::runtime_error(
                "BellhopDataLoader: failed to parse numeric row in " +
                csvPath + ": \"" + line + "\" (" + e.what() + ")");
        }

        rows.emplace_back(range_m, depth_m, value_db);
    }

    if (rows.empty()) {
        throw std::runtime_error(
            "BellhopDataLoader: no data rows found in " + csvPath);
    }

    // Build sorted, de-duplicated axis lists.
    std::set<double> rangeSet, depthSet;
    for (const auto& [r, d, v] : rows) {
        rangeSet.insert(r);
        depthSet.insert(d);
    }
    std::vector<double> thisRangeGrid(rangeSet.begin(), rangeSet.end());
    std::vector<double> thisDepthGrid(depthSet.begin(), depthSet.end());

    if (isFirstGridLoaded) {
        rangeGrid_ = std::move(thisRangeGrid);
        depthGrid_ = std::move(thisDepthGrid);
    } else {
        // Co-registration check: the SNR grid's axes must exactly
        // match the TL grid's. A mismatch here almost certainly means
        // the two CSVs came from different Bellhop runs and querying
        // them together would silently misalign range/depth indices.
        if (thisRangeGrid.size() != rangeGrid_.size() ||
            thisDepthGrid.size() != depthGrid_.size()) {
            throw std::runtime_error(
                "BellhopDataLoader: grid size mismatch between TL and "
                "SNR files -- refusing to load, as this indicates the "
                "two files are not co-registered (" + csvPath + ")");
        }
        for (size_t i = 0; i < rangeGrid_.size(); ++i) {
            if (std::abs(thisRangeGrid[i] - rangeGrid_[i]) > 1e-6) {
                throw std::runtime_error(
                    "BellhopDataLoader: range axis mismatch at index " +
                    std::to_string(i) + " in " + csvPath);
            }
        }
        for (size_t i = 0; i < depthGrid_.size(); ++i) {
            if (std::abs(thisDepthGrid[i] - depthGrid_[i]) > 1e-6) {
                throw std::runtime_error(
                    "BellhopDataLoader: depth axis mismatch at index " +
                    std::to_string(i) + " in " + csvPath);
            }
        }
    }

    // Populate the flattened grid, checking for completeness.
    outData.assign(rangeGrid_.size() * depthGrid_.size(),
                    std::numeric_limits<double>::quiet_NaN());
    std::vector<bool> filled(outData.size(), false);

    for (const auto& [r, d, v] : rows) {
        auto rIt = std::lower_bound(rangeGrid_.begin(), rangeGrid_.end(), r);
        auto dIt = std::lower_bound(depthGrid_.begin(), depthGrid_.end(), d);
        size_t rIdx = std::distance(rangeGrid_.begin(), rIt);
        size_t dIdx = std::distance(depthGrid_.begin(), dIt);
        size_t flatIdx = rIdx * depthGrid_.size() + dIdx;

        if (filled[flatIdx]) {
            throw std::runtime_error(
                "BellhopDataLoader: duplicate cell (" + std::to_string(r) +
                ", " + std::to_string(d) + ") in " + csvPath);
        }
        outData[flatIdx] = v; // may legitimately be NaN (range=0 singularity)
        filled[flatIdx] = true;
    }

    // Completeness check: every (range, depth) pair should have been
    // seen exactly once (with value possibly NaN). A gap here means
    // the CSV is missing rows that Grok's extraction should have
    // produced for the full grid -- surfacing this now is much safer
    // than discovering it as an unexplained hole during simulation.
    size_t missingCount = std::count(filled.begin(), filled.end(), false);
    if (missingCount > 0) {
        throw std::runtime_error(
            "BellhopDataLoader: " + std::to_string(missingCount) +
            " grid cells missing from " + csvPath +
            " -- expected a complete rectangular grid");
    }
}

void BellhopDataLoader::findBracket(const std::vector<double>& axis,
                                      double value,
                                      size_t& lowIdx,
                                      size_t& highIdx,
                                      double& frac)
{
    if (value <= axis.front()) {
        lowIdx = highIdx = 0;
        frac = 0.0;
        return;
    }
    if (value >= axis.back()) {
        lowIdx = highIdx = axis.size() - 1;
        frac = 0.0;
        return;
    }

    auto it = std::upper_bound(axis.begin(), axis.end(), value);
    highIdx = std::distance(axis.begin(), it);
    lowIdx = highIdx - 1;

    double lowVal = axis[lowIdx];
    double highVal = axis[highIdx];
    frac = (highVal > lowVal) ? (value - lowVal) / (highVal - lowVal) : 0.0;
}

double BellhopDataLoader::interpolate(const std::vector<double>& grid,
                                        size_t rLow, size_t rHigh, double rFrac,
                                        size_t dLow, size_t dHigh, double dFrac) const
{
    size_t nDepth = depthGrid_.size();

    double v00 = grid[rLow  * nDepth + dLow];
    double v01 = grid[rLow  * nDepth + dHigh];
    double v10 = grid[rHigh * nDepth + dLow];
    double v11 = grid[rHigh * nDepth + dHigh];

    // Conservative NaN propagation: if any corner used in the
    // interpolation is an excluded cell (range=0 singularity), the
    // result is NaN. Depth=0 is finite and is interpolated; the
    // nearPressureRelease flag is raised by query() instead.
    if (std::isnan(v00) || std::isnan(v01) ||
        std::isnan(v10) || std::isnan(v11)) {
        return std::numeric_limits<double>::quiet_NaN();
    }

    double v0 = v00 * (1.0 - dFrac) + v01 * dFrac;
    double v1 = v10 * (1.0 - dFrac) + v11 * dFrac;
    return v0 * (1.0 - rFrac) + v1 * rFrac;
}

ChannelSample BellhopDataLoader::query(double range_m, double depth_m) const
{
    ChannelSample result; // NaN payload, valid=false, nearPressureRelease=false

    const bool outOfEnvelope = (range_m < 0.0 || range_m > maxRangeM() ||
                                depth_m < 0.0 || depth_m > maxDepthM());
    if (outOfEnvelope) {
        // Do not return clamped-edge interpolants. valid=false ⇔ NaN payload.
        result.tl_db = std::numeric_limits<double>::quiet_NaN();
        result.snr_db = std::numeric_limits<double>::quiet_NaN();
        result.valid = false;
        result.nearPressureRelease = false;
        return result;
    }

    size_t rLow, rHigh, dLow, dHigh;
    double rFrac, dFrac;
    findBracket(rangeGrid_, range_m, rLow, rHigh, rFrac);
    findBracket(depthGrid_, depth_m, dLow, dHigh, dFrac);

    const double tl = interpolate(tlData_, rLow, rHigh, rFrac, dLow, dHigh, dFrac);
    const double snr = interpolate(snrData_, rLow, rHigh, rFrac, dLow, dHigh, dFrac);

    result.tl_db = tl;
    result.snr_db = snr;
    result.valid = !std::isnan(tl) && !std::isnan(snr);

    // Surface half-cell: a bilinear corner on the z=0 row (axis depth < 5 m)
    // AND the query itself inside that 5 m half-step. Depth=8 m still mixes
    // with z=0 in bilinear interpolation but is outside the half-step, so
    // it is not flagged (matches the requested tests).
    const bool cornerOnSurface =
        (depthGrid_[dLow] < 5.0) || (depthGrid_[dHigh] < 5.0);
    result.nearPressureRelease = cornerOnSurface && (depth_m < 5.0);

    return result;
}

} // namespace dahello