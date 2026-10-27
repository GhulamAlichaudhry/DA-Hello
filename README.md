# DA-HELLO

Decision-Aware HELLO scheduling for Q-learning underwater acoustic routers
(hosts: MARV and QELAR), evaluated in ns-3.41.

This repository contains **only the finalized material** needed to reproduce
the numbers and figures in the DA-HELLO manuscript: datasets of record,
locked configs, analysis/figure scripts, frozen module sources, and the paper sources.

## Directory map

| Path | Contents |
|---|---|
| `paper/` | IEEE-style LaTeX sources + figure PDFs (build with `./build.sh`) |
| `data/marv_eval/` | t103 MARV per-run CSV (seeds 20261801–40) |
| `data/qelar_eval/` | t109 QELAR Phase-2 per-run CSV (seeds 20261841–80) |
| `data/tuning/` | QELAR Phase-1 tuning summaries |
| `data/pilots/` | t105 LQARP/SAQR pilot summaries (CSV only) |
| `data/host_audit/` | Port A t104 host-arm audit |
| `configs/` | Locked geometry, job matrices, seed ledger, eval addenda |
| `scripts/run/` | Campaign drivers (do not re-run evaluation seeds casually) |
| `scripts/analysis/` | CSV → tables/numbers |
| `scripts/figures/` | Figure generators |
| `figures/` | Final figure PDFs used in the paper |
| `results/` | `numbers_trace.csv` + table extracts |
| `ns3/` | Module sources (p15v2/p18 freeze), patches, build notes — **no binaries** |
| `docs/` | Dataset-of-record stub, gate verdicts, prereg, build records |

## How to reproduce (high level)

1. **Numbers / tables:** use CSVs under `data/` with scripts in `scripts/analysis/` (dry-run / `--help` only unless you intend a full recompute).
2. **Figures:** PDFs in `figures/` are authoritative; optional regenerators in `scripts/figures/`.
3. **Paper:** `cd paper && ./build.sh` (or `make`).
4. **Binaries:** not shipped; see `ns3/BINARIES.md` for SHA256 of p15v2 / p18 and `ns3/build.md` to rebuild.

## Legacy names

The project was formerly called **E-LQTP**. In this public tree, documentation and
copied sources use **DA-HELLO** / `dahello`. Original evaluation binaries and
libraries used the module name `elqtp` and box paths under `E-LQTP-portA/`.
See `rename_map.csv` and `ns3/BINARIES.md`. Locked CLI option names in job JSON
(`--helloOnlyHost`, `--qelarDaFc`, …) are unchanged.

Absolute paths in copied JSON/scripts were replaced with `${NS3_PORTA}`, `${NS3_BASE}`,
`${WORKSPACE}`, `${OUT}` placeholders.

## Citation

See `CITATION.cff` (placeholder — fill before publish). Licence: see `LICENSE` (placeholder).

## What is not here

Raw per-run ns-3 logs, compiled binaries, intermediate task reports, copyrighted
reference PDFs, and superseded campaigns (archived outside this zip).
