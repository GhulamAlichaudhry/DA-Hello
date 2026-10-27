// marv-protocol.h
//
// MARV host (Wang et al., "MARL-based routing protocol for UWSNs",
// IEEE Sensors J. 24(5), 2024, pp. 7042-7054). Stage-1 build.
//
// Published (page): Eq. 8 update (p.7046), Eq. 9 reward, Eq. 10 E(x),
// Eq. 11 D, Eq. 12 C (p.7047), Eq. 13/14 holding time (p.7048),
// Eq. 2 Psat = m_ij/n_ij (p.7045), depth qualification and
// suppression (p.7046, p.7048-7049, Alg. 1), alpha 0.9 gamma 0.5
// omega 0.2 delta 1 g_c 1 k 0.4 (p.7049/Table I p.7050), lambda1 0.2
// lambda2 0.01 (p.7051), VoI 0.2 fixed (p.7051/7052), processing delay
// 0.05 s and v = 1500 m/s (Table I, p.7050).
//
// Declared (user, signed): D1 Psat cumulative, initial 1.0; D2 m_ij by
// overhearing the next hop's forward; D3 congestion 0; D4 h = 1; D5
// control packet fields and sizes; D6 binary's own mobility / channel /
// energy / sink / range, perfect overhearing, no MAC; D7 Q init 0; D8
// V(i) = max_j Q(i,j) over qualified j, sink V = 0; D9 void flag; D10
// failure transition with R(i->i); D11 s' = qualified neighbours of i
// other than j; D12 one update per qualified neighbour at forwarding
// time; D13 R = binary range, v = 1500 m/s.
//
// D14 (signed): the sink's acceptance is overheard like a forward
// (--marvSinkOverheard=ack). D15 (signed): n_ij / m_ij count only the top
// of the priority list (--marvPsatScope=top). D16 (signed): D(i,j) =
// 1 - (d(i) - d(j))/R (p.7047 "tend to select the neighbor node with a
// small depth"). Both flags stay explicit on the command line; the
// 'list' and 'literal' settings exist only for the sensitivity check.

#ifndef DAHELLO_MARV_PROTOCOL_H
#define DAHELLO_MARV_PROTOCOL_H

#include "lqarp-protocol.h"
#include "oracle-guard.h"

#include <limits>

#include <cstdint>
#include <string>
#include <vector>

namespace dahello {

enum class MarvPsatScope { Unset, List, Top };
enum class MarvSinkOverheard { Unset, Ack, Literal };

struct MarvConfig {
    double alpha = 0.9;     // p.7049, Table I p.7050
    double gamma = 0.5;     // p.7049, Table I
    double omega = 0.2;     // p.7049, Table I
    double delta = 1.0;     // p.7049, Table I
    double gc = 1.0;        // p.7049, Table I
    double k = 0.4;         // p.7049, Table I
    double lambda1 = 0.2;   // p.7051
    double lambda2 = 0.01;  // p.7051
    double voi = 0.2;       // p.7051/7052, fixed for every packet
    double h = 1.0;         // D4
    double qInit = 0.0;     // D7
    double psatInit = 1.0;  // D1
    double procDelayS = 0.05;      // Table I p.7050
    double soundSpeedMps = 1500.0; // Table I p.7050, D13
    double rRangeM = 0.0;          // D13: binary's range
    double eIni = kDefaultEiniJ;
    MarvPsatScope psatScope = MarvPsatScope::Unset;
    MarvSinkOverheard sinkOverheard = MarvSinkOverheard::Unset;
};

// What node k currently holds about node j (from data headers or control
// packets). Initialised to j's true initial state: V = 0 (D7 + D8),
// no void flag, e_res = E_ini, congestion 0 (D3).
struct MarvRecord {
    double V = 0.0;
    bool voidFlag = false;
    double energy = 0.0;
    double t = -1.0;
    bool heard = false;
    // Port A heard mode: depth as last advertised in a decoded header (amend1 A5).
    double z = std::numeric_limits<double>::quiet_NaN();
    double tZ = -1.0;
};

struct MarvReward {
    double psat = 1.0;
    double Rij = 0.0;
    double Rii = 0.0;
    double r = 0.0;
    double Ei = 0.0;
    double Ej = 0.0;
    double D = 0.0;
    double C = 0.0;
};

class MarvProtocol {
public:
    MarvProtocol(ChannelGate& gate, MarvConfig cfg, std::vector<LqarpNode> nodes);

    const MarvConfig& config() const { return cfg_; }
    const std::vector<LqarpNode>& nodes() const { return nodes_; }
    double rRangeM() const { return rRangeM_; }
    void setSink(uint32_t sinkId);
    uint32_t sinkId() const { return sinkId_; }

    // Binary range only (3-D distance < R), as for the QELAR/SAQR hosts.
    bool inRange(uint32_t i, uint32_t j) const;
    std::vector<uint32_t> rangeSet(uint32_t i) const;
    // MARV qualification: d_j < d_i (p.7046, Alg. 1 line 7), and not
    // void-flagged in i's record unless j is the sink (D9).
    bool qualifies(uint32_t i, uint32_t j) const;

    double psat(uint32_t i, uint32_t j) const;
    MarvReward reward(uint32_t i, uint32_t j) const;
    double qValue(uint32_t i, uint32_t j) const;

    // Alg. 1 lines 11-14: Eq. 8 for every qualified j (D12), V(i) =
    // max Q (D8), returns the priority list in descending Q (ties: smaller
    // id first). Empty U sets i's void flag (D9) and returns {}.
    std::vector<uint32_t> updateAndRank(uint32_t i, const std::vector<uint32_t>& U);

    double advertisedV(uint32_t j) const;
    bool advertisedVoid(uint32_t j) const;

    void hear(uint32_t k, uint32_t j, double V, bool voidFlag, double energy, double t);
    void hearEnergy(uint32_t k, uint32_t j, double energy, double t);
    const MarvRecord& record(uint32_t k, uint32_t j) const;

    void noteSent(uint32_t i, const std::vector<uint32_t>& list);
    void noteForwardOverheard(uint32_t i, uint32_t j, uint32_t topOfList);

    double tMaxS() const;
    double holdingTimeS(uint32_t n) const; // n = 1-based priority position

    ChannelSample linkSample(uint32_t i, uint32_t j) const;
    void setEnergy(uint32_t i, double joules);
    double energy(uint32_t i) const;
    void setKinematics(uint32_t i, double x, double y, double z,
                       double vx, double vy, double vz);

    // Port A heard mode (amend1 A5): held records only; legacy oracle paths abort (exit 3).
    HeardCtl& heard() { return heard_; }
    const HeardCtl& heard() const { return heard_; }
    void hearZ(uint32_t k, uint32_t j, double z, double t);
    bool fresh(uint32_t i, uint32_t j) const;
    // Mutable protocol copy, for the poison scope only.
    std::vector<LqarpNode>& nodesMut() { return nodes_; }

    uint64_t qUpdates() const { return qUpdates_; }
    uint64_t voidSets() const { return voidSets_; }

private:
    ChannelGate* gate_;
    MarvConfig cfg_;
    std::vector<LqarpNode> nodes_;
    double rRangeM_;
    uint32_t sinkId_ = 0;
    std::vector<std::vector<double>> Q_;
    std::vector<std::vector<uint64_t>> n_;
    std::vector<std::vector<uint64_t>> m_;
    std::vector<double> ownV_;
    std::vector<char> ownVoid_;
    std::vector<std::vector<MarvRecord>> rec_;
    uint64_t qUpdates_ = 0;
    uint64_t voidSets_ = 0;
    HeardCtl heard_;
    double heldZ(uint32_t i, uint32_t j) const; // sink: fixed deployment depth (I5)

    void checkIndex(uint32_t i) const;
    double eCost(double eRes) const; // Eq. 10
};

} // namespace dahello

#endif
