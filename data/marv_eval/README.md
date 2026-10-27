# MARV evaluation dataset of record (t103)

- Seeds: 20261801–20261840
- Binary: p15v2 (see `ns3/BINARIES.md`)
- Files: `t103_per_run.csv`, `t103_cell_means.csv`, `t103_paired.json`, ledgers, `jobs_unique.json`, `SHA256SUMS`
- Drift / scalability / scenario arms (I, II, GM2, etc.) are **rows inside** `t103_per_run.csv`
  (columns `sc`, `arm`, `nAuv`, `drift`) — there is no separate sweep campaign folder.
- Raw `runs/` logs were intentionally omitted from the t103 package.
