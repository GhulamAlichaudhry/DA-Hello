// dahello-run-config.cc

#include "dahello-run-config.h"

#include <cstdint>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace dahello {
namespace {

bool
splitEq(const std::string& tok, std::string& key, std::string& val)
{
    if (tok.size() < 4 || tok[0] != '-' || tok[1] != '-') {
        return false;
    }
    const auto eq = tok.find('=');
    if (eq == std::string::npos || eq <= 2) {
        return false;
    }
    key = tok.substr(2, eq - 2);
    val = tok.substr(eq + 1);
    return true;
}

std::string
trim(std::string s)
{
    const auto a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) {
        return {};
    }
    const auto b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

bool
parseU32(const std::string& s, uint32_t& out)
{
    try {
        size_t n = 0;
        const unsigned long v = std::stoul(s, &n);
        if (n != s.size() || v > 0xffffffffUL) {
            return false;
        }
        out = static_cast<uint32_t>(v);
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

bool
parseD(const std::string& s, double& out)
{
    try {
        size_t n = 0;
        out = std::stod(s, &n);
        return n == s.size();
    } catch (const std::exception&) {
        return false;
    }
}

} // namespace

std::vector<std::string>
sweepFlagNames()
{
    return {"beta_1", "beta_2", "epsilon_0", "lambda", "W_mp", "W", "rho",
            "zeta", "T_ctrl", "dt", "dataDir", "config"};
}

bool
DahelloRunConfig::applyToken(const std::string& key, const std::string& value)
{
    if (key == "beta_1") {
        return parseD(value, dahello.beta_1);
    }
    if (key == "beta_2") {
        return parseD(value, dahello.beta_2);
    }
    if (key == "epsilon_0") {
        return parseD(value, dahello.epsilon_0);
    }
    if (key == "lambda") {
        return parseD(value, dahello.lambda);
    }
    if (key == "W_mp") {
        return parseU32(value, dahello.W_mp);
    }
    if (key == "W") {
        return parseU32(value, dahello.W);
    }
    if (key == "rho") {
        return parseD(value, dahello.rho);
    }
    if (key == "zeta") {
        return parseD(value, gm.zeta);
    }
    if (key == "T_ctrl") {
        return parseD(value, dahello.T_ctrl);
    }
    if (key == "dt") {
        if (!parseD(value, dahello.dt)) {
            return false;
        }
        gm.dt = dahello.dt;
        return true;
    }
    if (key == "dataDir") {
        dataDir = value;
        return true;
    }
    if (key == "config") {
        configFile = value;
        return true;
    }
    return false;
}

bool
DahelloRunConfig::loadFile(const std::string& path, std::string& err)
{
    std::ifstream in(path);
    if (!in) {
        err = "cannot open config file: " + path;
        return false;
    }
    std::string line;
    int lineno = 0;
    while (std::getline(in, line)) {
        ++lineno;
        const auto hash = line.find('#');
        if (hash != std::string::npos) {
            line = line.substr(0, hash);
        }
        line = trim(line);
        if (line.empty()) {
            continue;
        }
        std::string key;
        std::string val;
        const auto eq = line.find('=');
        if (eq != std::string::npos) {
            key = trim(line.substr(0, eq));
            val = trim(line.substr(eq + 1));
        } else {
            std::istringstream iss(line);
            iss >> key >> val;
        }
        if (key.empty() || val.empty() || !applyToken(key, val)) {
            err = "config " + path + ":" + std::to_string(lineno)
                  + ": bad entry '" + line + "'";
            return false;
        }
    }
    return true;
}

DahelloRunConfig
DahelloRunConfig::parseArgv(int argc, char** argv, std::string& err)
{
    DahelloRunConfig out;
    for (int i = 1; i < argc; ++i) {
        const std::string tok = argv[i] ? argv[i] : "";
        if (tok.empty()) {
            continue;
        }
        if (tok[0] != '-') {
            out.dataDir = tok;
            continue;
        }
        std::string key;
        std::string val;
        if (splitEq(tok, key, val)) {
            if (key == "config") {
                if (!out.loadFile(val, err)) {
                    return out;
                }
                out.configFile = val;
                continue;
            }
            if (!out.applyToken(key, val)) {
                err = "unknown or invalid flag --" + key + "=" + val;
                return out;
            }
            continue;
        }
        if (tok.size() > 2 && tok[0] == '-' && tok[1] == '-' && i + 1 < argc) {
            key = tok.substr(2);
            val = argv[++i] ? argv[i] : "";
            if (key == "config") {
                if (!out.loadFile(val, err)) {
                    return out;
                }
                out.configFile = val;
                continue;
            }
            if (!out.applyToken(key, val)) {
                err = "unknown or invalid flag --" + key + " " + val;
                return out;
            }
            continue;
        }
        err = "unrecognized argument: " + tok;
        return out;
    }
    return out;
}

void
DahelloRunConfig::dump(std::ostream& os) const
{
    os << "DahelloRunConfig (runtime; no rebuild)\n"
       << "  beta_1=" << dahello.beta_1 << "  beta_2=" << dahello.beta_2
       << "  (swept; default 0.0 is the identity baseline)\n"
       << "  W_mp=" << dahello.W_mp << "  W=" << dahello.W
       << "  rho=" << dahello.rho << "\n"
       << "  epsilon_0=" << dahello.epsilon_0 << " dB  lambda=" << dahello.lambda
       << "  T_ctrl=" << dahello.T_ctrl << " s  dt=" << dahello.dt << " s\n"
       << "  zeta=" << gm.zeta << "  (Gauss-Markov, Scenario III)\n"
       << "  dataDir=" << dataDir;
    if (!configFile.empty()) {
        os << "  configFile=" << configFile;
    }
    os << "\n";
}

} // namespace dahello
