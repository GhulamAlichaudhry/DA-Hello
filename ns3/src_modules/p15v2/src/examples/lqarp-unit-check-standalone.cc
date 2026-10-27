// Standalone (no ns-3) LQARP / DA-HELLO unit checks.
// Sweep flags (--beta_1=..., --config=file, ...) are parsed and
// printed so a single binary can accept them without a rebuild.
// The unit tests construct their own DahelloConfig and ignore
// those flags.

#include "dahello-run-config.h"
#include "lqarp-unit-check-common.h"

#include <iostream>

int
main (int argc, char **argv)
{
  std::string err;
  const dahello::DahelloRunConfig run = dahello::DahelloRunConfig::parseArgv (argc, argv, err);
  if (!err.empty ())
    {
      std::cerr << err << "\n";
      return 2;
    }
  std::cout << "--- runtime sweep config (NOT applied to unit tests) ---\n";
  run.dump (std::cout);
  std::cout << "\n";
  return RunLqarpUnitChecks (run.dataDir);
}
