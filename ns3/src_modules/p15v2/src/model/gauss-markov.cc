// gauss-markov.cc
// Scenario III Gauss-Markov velocity process. See header.

#include "gauss-markov.h"

#include <algorithm>
#include <cmath>

namespace dahello {

void
eddyMeanFlow(double x, double y, const EddyConfig& e,
             double& mux, double& muy, double& muz)
{
    muz = 0.0;
    const double dx = x - e.cx;
    const double dy = y - e.cy;
    const double r = std::sqrt(dx * dx + dy * dy);
    if (!(r > 1.0e-12) || !(e.R > 0.0) || !std::isfinite(e.V)) {
        mux = 0.0;
        muy = 0.0;
        return;
    }
    // Counterclockwise tangent ê_θ = (-dy, dx) / r.
    const double vTheta = e.V * std::min(r / e.R, 1.0);
    mux = vTheta * (-dy / r);
    muy = vTheta * (dx / r);
}

void
vRelRadialTangential(double ax, double ay, double az,
                     double avx, double avy, double avz,
                     double bx, double by, double bz,
                     double bvx, double bvy, double bvz,
                     double& vRadAbs, double& vTanAbs, double& vRelAbs)
{
    const double rx = bx - ax;
    const double ry = by - ay;
    const double rz = bz - az;
    const double vx = bvx - avx;
    const double vy = bvy - avy;
    const double vz = bvz - avz;
    vRelAbs = std::sqrt(vx * vx + vy * vy + vz * vz);
    const double r = std::sqrt(rx * rx + ry * ry + rz * rz);
    if (!(r > 1.0e-12)) {
        vRadAbs = 0.0;
        vTanAbs = vRelAbs;
        return;
    }
    const double vr = (vx * rx + vy * ry + vz * rz) / r;
    vRadAbs = std::fabs(vr);
    const double tx = vx - vr * (rx / r);
    const double ty = vy - vr * (ry / r);
    const double tz = vz - vr * (rz / r);
    vTanAbs = std::sqrt(tx * tx + ty * ty + tz * tz);
}

double
signedRangeRate(double ax, double ay, double az,
                double avx, double avy, double avz,
                double bx, double by, double bz,
                double bvx, double bvy, double bvz)
{
    const double rx = bx - ax;
    const double ry = by - ay;
    const double rz = bz - az;
    const double vx = bvx - avx;
    const double vy = bvy - avy;
    const double vz = bvz - avz;
    const double r = std::sqrt(rx * rx + ry * ry + rz * rz);
    if (!(r > 1.0e-12)) {
        return 0.0;
    }
    return (vx * rx + vy * ry + vz * rz) / r;
}

void
gaussMarkovStep(GaussMarkovState& s,
                const GaussMarkovConfig& cfg,
                std::mt19937& rng)
{
    std::normal_distribution<double> n01(0.0, 1.0);
    const double z = cfg.zeta;
    const double persist = 1.0 - z;
    const double innov = std::sqrt(std::max(0.0, 1.0 - z * z));
    s.vx = z * s.vx + persist * cfg.mu_x + innov * cfg.sigma_x * n01(rng);
    s.vy = z * s.vy + persist * cfg.mu_y + innov * cfg.sigma_y * n01(rng);
    s.vz = z * s.vz + persist * cfg.mu_z + innov * cfg.sigma_z * n01(rng);
    s.x += s.vx * cfg.dt;
    s.y += s.vy * cfg.dt;
    s.z += s.vz * cfg.dt;
}

} // namespace dahello
