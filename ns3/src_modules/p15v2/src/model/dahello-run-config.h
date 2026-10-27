// dahello-run-config.h
//
// Runtime (no-rebuild) experiment knobs. DahelloProtocol already takes
// DahelloConfig by value; this parser is how a single built binary
// accepts beta_1 / beta_2 / epsilon_0 / W / W_mp / ... as CLI flags
// or a key=value file. Unit tests construct DahelloConfig themselves
// and do not read this object.

#ifndef DAHELLO_DAHELLO_RUN_CONFIG_H
#define DAHELLO_DAHELLO_RUN_CONFIG_H

#include "dahello-protocol.h"
#include "gauss-markov.h"

#include <iosfwd>
#include <string>
#include <vector>

namespace dahello {

struct DahelloRunConfig {
    DahelloConfig dahello;
    GaussMarkovConfig gm;
    std::string dataDir = "${WORKSPACE}/data/channel_db";
    std::string configFile;

    // --key=value or --key value. Unknown keys return false.
    bool applyToken(const std::string& key, const std::string& value);

    // Load key=value / key value lines. '#' comments. Returns false on IO error.
    bool loadFile(const std::string& path, std::string& err);

    // argv[0] skipped. Positional non-flag → dataDir.
    // --config=PATH is applied when seen (later CLI flags override the file).
    static DahelloRunConfig parseArgv(int argc, char** argv, std::string& err);

    void dump(std::ostream& os) const;
};

std::vector<std::string> sweepFlagNames();

} // namespace dahello

#endif // DAHELLO_DAHELLO_RUN_CONFIG_H
