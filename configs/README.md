# Locked configurations

- Geometry: `sim_step1_geometry_config_20261019.md`
- MARV matrix: `t103_jobs_unique.json` (seeds 20261801–40)
- QELAR Phase 1/2: `phase1_jobs.json`, `phase2_jobs.json`, `phase2_jobs_meta.json` (under data/qelar_eval)
- Seed ledger: `portA_seed_ledger.jsonl`
- Eval addenda (signed locks): `portA_eval_addendum_*`
Absolute box paths in JSON `cmd` arrays were scrubbed to `${NS3_PORTA}` / `${NS3_BASE}` placeholders.

## Absolute paths in job JSON / ledgers

`t103_jobs_unique.json`, `phase1_jobs.json`, `phase2_jobs.json`, and related
ledgers are **byte-identical** to the evaluation lock files so SHA256 matches the
campaign artifacts. Their `cmd` arrays still contain the original box paths
(` /workspace/E-LQTP-portA/...`). Replace those prefixes with your local ns-3
build when re-running; do not edit the checked-in JSON if you need hash parity
with `data/*/SHA256SUMS` and `ARTIFACT_SHA256.json`.
