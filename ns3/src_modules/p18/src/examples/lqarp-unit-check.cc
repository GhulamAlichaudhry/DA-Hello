/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * ns-3 wrapper for the LQARP / DA-HELLO unit checks.
 * Sweep knobs are CommandLine values (no rebuild per beta_1). The
 * unit tests construct their own DahelloConfig and ignore these flags.
 */

#include "ns3/core-module.h"
#include "ns3/dahello-run-config.h"

#define DAHELLO_NS3_INCLUDES
#include "lqarp-unit-check-common.h"

int
main (int argc, char *argv[])
{
  dahello::DahelloRunConfig run;
  ns3::CommandLine cmd (__FILE__);
  cmd.AddValue ("dataDir", "Directory containing TL/SNR extended CSVs", run.dataDir);
  cmd.AddValue ("beta_1", "DA-HELLO beta_1 (swept; default 0)", run.dahello.beta_1);
  cmd.AddValue ("beta_2", "DA-HELLO beta_2 (swept; default 0)", run.dahello.beta_2);
  cmd.AddValue ("epsilon_0", "Trigger epsilon_0 [dB]", run.dahello.epsilon_0);
  cmd.AddValue ("lambda", "Trigger lambda", run.dahello.lambda);
  cmd.AddValue ("W_mp", "Median pre-filter window", run.dahello.W_mp);
  cmd.AddValue ("W", "Persistence window", run.dahello.W);
  cmd.AddValue ("rho", "Persistence fraction", run.dahello.rho);
  cmd.AddValue ("zeta", "Scenario III Gauss-Markov correlation", run.gm.zeta);
  cmd.AddValue ("T_ctrl", "Periodic ablation period [s]", run.dahello.T_ctrl);
  cmd.AddValue ("dt", "EKF / GM step [s]", run.dahello.dt);
  cmd.Parse (argc, argv);
  run.gm.dt = run.dahello.dt;
  std::cout << "--- runtime sweep config (NOT applied to unit tests) ---\n";
  run.dump (std::cout);
  std::cout << "\n";
  return RunLqarpUnitChecks (run.dataDir);
}
