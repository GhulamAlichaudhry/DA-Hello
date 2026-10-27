// lqarp-posda.h — LQARP position-aware decision-aware (DA) test (K-impl), p6 2026-10-16.
//
// Spec: strengthening_prereg.md §H2-LQARP prerequisite ("position-aware LQARP DA test
// (§2.3 of the plan) must be implemented and unit-tested before tuning") and §H3(c);
// amend1 §A7/§A8 (K-impl, Gate 2, Fri 23 Oct); generic DA test: decision_aware_design.md
// §1.1 (VoI_j: membership change OR sign flip of S_i(j) - S_i(k) with margin m, local
// information only); design: phase2_LQARP_rank_test_design_20261016.md §Design 3-4, 6.
//
// Evaluated at the advertiser s_j with OWN state (its pose as last advertised in a HELLO,
// its current pose, own energy/V/number) and HELD records only (what s_j decoded from i's
// and k's HELLO/data headers, fresh within T_nb). No peer live state is read.
//
//  Upstream Â_j : held i (fresh, finite pose) with j in i's estimated candidate set
//                 under the advertised OR the current pose of j.
//  Membership   : member_i(P) = |p_i - P| < R  AND  z_i - P.z > 0  AND  snrDefined(i, P)
//                 (LQARP N_range ∩ N_depth ∩ N_SNR-definedness, lqarp-protocol.cc
//                 inNRange/inNDepth/inNSnr with i's held pose standing in for i's own).
//                 FIRE if member_i(P_adv) != member_i(P_now) for some i in Â_j.
//  Rank         : for i with j a member under both poses, co-candidates k (held, fresh,
//                 finite, k != i, j; member_i(p_k)). Score = LQARP reward numerator +
//                 gamma*V (lqarp-protocol.cc reward + updateQ heard target):
//                   S_i(x) = cEne(E_x, Eavg_i, Eini) * cDel(E_x, Eini, z_i, z_x) * cNei(n_x)
//                            + gamma * V_x
//                 c_lin is taken equal across candidates (s_j cannot observe SNR_ik; same
//                 documented approximation class as equal Psat in the MARV instantiation,
//                 decision_aware_design.md §1.2). Eavg_i = mean held energy over {j} ∪ K_i.
//                 Own energy/V/number are the CURRENT values in both evaluations, so only
//                 the position change can fire (position-aware test).
//                 Preference pref(j over k) = S_j > S_k, ties -> smaller id (LqarpProtocol::
//                 nextHop tie rule). FIRE if pref_adv != pref_now AND |S_j,now - S_k| >= m.
//  Defined edge behaviour: non-finite own pose -> no fire (selfNonfinite); held record with
//  non-finite pose -> ignored (counted); non-finite held energy -> excluded from rank only
//  (counted); missing V / number -> 0 (LQARP heard convention); non-finite score -> pair
//  skipped (counted); empty Â_j -> no fire.
#pragma once
#include "lqarp-protocol.h"

#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <vector>

namespace dahello {

struct PosDaPose {
    double x = 0.0, y = 0.0, z = 0.0;
};

struct PosDaRec { // a held record at s_j
    uint32_t id = 0;
    double x = 0.0, y = 0.0, z = 0.0;
    double energy = 0.0;
    double V = std::numeric_limits<double>::quiet_NaN();
    double number = std::numeric_limits<double>::quiet_NaN();
    bool fresh = false;
};

struct PosDaParams {
    double rRangeM = 0.0;
    double eIni = kDefaultEiniJ;
    double gamma = 0.8;
    double margin = 0.0; // m >= 0 (score units)
};

struct PosDaInput {
    uint32_t self = 0;
    PosDaPose adv; // own pose as last advertised (HELLO)
    PosDaPose now; // own current pose
    double eSelf = 0.0, vSelf = 0.0, numSelf = 0.0;
    std::vector<PosDaRec> recs;
};

struct PosDaResult {
    bool membership = false;
    bool rank = false;
    uint32_t memI = std::numeric_limits<uint32_t>::max(); // first upstream with a membership change
    uint32_t i = std::numeric_limits<uint32_t>::max();    // upstream of the rank event
    uint32_t k = std::numeric_limits<uint32_t>::max();    // co-candidate of the rank event
    uint32_t nUpstream = 0, nPairs = 0;
    uint32_t nSkipPose = 0, nSkipEnergy = 0, nSkipScore = 0;
    bool selfNonfinite = false;
    bool fire() const { return membership || rank; }
};

// i's held pose decides "is P within i's candidate filters"; optional SNR definedness.
using PosDaSnrFn = std::function<bool(const PosDaRec& i, const PosDaPose& p)>;

inline bool
PosDaMember(const PosDaRec& i, const PosDaPose& p, double r, const PosDaSnrFn& snr)
{
    const double dx = i.x - p.x, dy = i.y - p.y, dz = i.z - p.z;
    const double d = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (!(d < r) || !((i.z - p.z) > 0.0)) {
        return false;
    }
    return snr ? snr(i, p) : true;
}

inline double
PosDaScore(double eX, double eAvg, double zI, double zX, double nX, double vX, const PosDaParams& q)
{
    const double n = std::isfinite(nX) ? std::max(0.0, nX) : 0.0;
    const double v = std::isfinite(vX) ? vX : 0.0;
    return LqarpProtocol::cEne(eX, eAvg, q.eIni) * LqarpProtocol::cDel(eX, q.eIni, zI, zX) *
               LqarpProtocol::cNei(static_cast<uint32_t>(n)) +
           q.gamma * v;
}

inline bool
PosDaPref(double sj, double sk, uint32_t j, uint32_t k)
{
    return sj > sk || (sj == sk && j < k);
}

inline PosDaResult
EvaluateLqarpPosDa(const PosDaInput& in, const PosDaParams& q, const PosDaSnrFn& snr = {})
{
    PosDaResult r;
    auto fin3 = [](double a, double b, double c) {
        return std::isfinite(a) && std::isfinite(b) && std::isfinite(c);
    };
    if (!fin3(in.adv.x, in.adv.y, in.adv.z) || !fin3(in.now.x, in.now.y, in.now.z)) {
        r.selfNonfinite = true;
        return r;
    }
    std::vector<const PosDaRec*> valid;
    for (const auto& h : in.recs) {
        if (!h.fresh || h.id == in.self) {
            continue;
        }
        if (!fin3(h.x, h.y, h.z)) {
            ++r.nSkipPose;
            continue;
        }
        valid.push_back(&h);
    }
    for (const PosDaRec* ip : valid) {
        const PosDaRec& i = *ip;
        const bool mAdv = PosDaMember(i, in.adv, q.rRangeM, snr);
        const bool mNow = PosDaMember(i, in.now, q.rRangeM, snr);
        if (!mAdv && !mNow) {
            continue;
        }
        ++r.nUpstream;
        if (mAdv != mNow) {
            if (!r.membership) {
                r.membership = true;
                r.memI = i.id;
            }
            continue;
        }
        if (r.membership || r.rank) {
            continue; // already firing; keep counting upstream only
        }
        if (!std::isfinite(in.eSelf)) {
            ++r.nSkipEnergy;
            continue;
        }
        std::vector<const PosDaRec*> K;
        double eSum = in.eSelf;
        for (const PosDaRec* kp : valid) {
            if (kp->id == i.id) {
                continue;
            }
            PosDaPose pk{kp->x, kp->y, kp->z};
            if (!PosDaMember(i, pk, q.rRangeM, snr)) {
                continue;
            }
            if (!std::isfinite(kp->energy)) {
                ++r.nSkipEnergy;
                continue;
            }
            K.push_back(kp);
            eSum += kp->energy;
        }
        const double eAvg = eSum / static_cast<double>(K.size() + 1);
        const double sAdv = PosDaScore(in.eSelf, eAvg, i.z, in.adv.z, in.numSelf, in.vSelf, q);
        const double sNow = PosDaScore(in.eSelf, eAvg, i.z, in.now.z, in.numSelf, in.vSelf, q);
        for (const PosDaRec* kp : K) {
            const double sK = PosDaScore(kp->energy, eAvg, i.z, kp->z, kp->number, kp->V, q);
            if (!std::isfinite(sAdv) || !std::isfinite(sNow) || !std::isfinite(sK)) {
                ++r.nSkipScore;
                continue;
            }
            ++r.nPairs;
            if (PosDaPref(sAdv, sK, in.self, kp->id) != PosDaPref(sNow, sK, in.self, kp->id) &&
                std::fabs(sNow - sK) >= q.margin) {
                r.rank = true;
                r.i = i.id;
                r.k = kp->id;
                break;
            }
        }
    }
    return r;
}

} // namespace dahello
