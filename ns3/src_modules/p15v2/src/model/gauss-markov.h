// gauss-markov.h
//
// Scenario III velocity process (Gauss-Markov) plus the Rankine-capped
// eddy mean flow (SPEC Part 9). The stepper is the same discrete
// update as Steps 15/17. Position-dependent mu is supplied by the
// caller from eddyMeanFlow at the current (x, y) each tick.
//
//   v_{k+1} = zeta * v_k + (1 - zeta) * mu(p_k)
//             + sqrt(1 - zeta^2) * sigma * w_k
//   p_{k+1} = p_k + v_{k+1} * dt
//
// w_k ~ N(0,1) independent per axis. zeta = 0.95 is the first-pass
// default (high temporal correlation). zeta is a run-time field,
// not a compile-time constant.
//
// Used by `--scenario=iii` (per-AUV eddy + GM) and by
// `--diagKinematics`.

#ifndef DAHELLO_GAUSS_MARKOV_H
#define DAHELLO_GAUSS_MARKOV_H

#include <random>

namespace dahello {

struct GaussMarkovConfig {
    double zeta = 0.95; // Scenario III first-pass correlation
    double dt = 1.0;    // seconds
    double mu_x = 0.0;
    double mu_y = 0.0;
    double mu_z = 0.0;
    double sigma_x = 1.0; // m/s, horizontal
    double sigma_y = 1.0;
    // Permanent Scenario III choice (SPEC Part 9): eddies at this
    // scale are geostrophic / horizontal. A 1 m/s vertical innovation
    // also exits the 500 m column in a few hundred seconds.
    double sigma_z = 0.0;
};

// One counterclockwise eddy at the volume XY centroid. Rankine-capped
// so |v_eddy_mean| never exceeds V (Section 4.1 operating range).
struct EddyConfig {
    double cx = 3000.0; // m
    double cy = 3000.0;
    double V = 2.0;     // m/s outer / cap speed
    double R = 2000.0;  // m core radius
};

// v_eddy_mean(x, y): tangential, vz = 0. At the centre, 0 (no
// singularity). |v| = V * min(r/R, 1).
void eddyMeanFlow(double x, double y, const EddyConfig& e,
                  double& mux, double& muy, double& muz);

// Decompose v_b - v_a along the pair LOS (p_b - p_a).
// v_radial is the range-rate (the only component H_k / SNR can see).
// v_tangential is the remainder (unobservable, Steps 35-38).
void vRelRadialTangential(double ax, double ay, double az,
                          double avx, double avy, double avz,
                          double bx, double by, double bz,
                          double bvx, double bvy, double bvz,
                          double& vRadAbs, double& vTanAbs, double& vRelAbs);

// Signed range-rate (v_b-v_a)·rhat. Same sign as SPEC 2.1 v_rel(a,b):
// negative = closing (b approaching a).
double signedRangeRate(double ax, double ay, double az,
                       double avx, double avy, double avz,
                       double bx, double by, double bz,
                       double bvx, double bvy, double bvz);

struct GaussMarkovState {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    double vx = 0.0;
    double vy = 0.0;
    double vz = 0.0;
};

void gaussMarkovStep(GaussMarkovState& s,
                     const GaussMarkovConfig& cfg,
                     std::mt19937& rng);

} // namespace dahello

#endif // DAHELLO_GAUSS_MARKOV_H
