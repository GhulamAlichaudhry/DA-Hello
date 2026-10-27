# Build record — QELAR evaluation binary **p18**

| Field | Value |
|---|---|
| Role | QELAR Port A evaluation binary for t109 Phase 1+2 (A2.12) |
| Path (box) | `${WORKSPACE}/DA-HELLO-porta/build_p18/` |
| Scenario exe | `bin_p18/ns3.41-scenario-i-lqarp-optimized` |
| **exe SHA256** | `c57ce18e9a580655818bf06e9ecab73cefcbdb870e7d2e5f41eeb55581152f56` |
| lib | `lib_p18/libns3.41-dahello-optimized.so` |
| lib SHA256 | `df0f4213a03753caa04ab52af9e74958bf196ec655fb961250771354e358aa2c` |
| Git HEAD label | `e77113e8…` (`p15-amend2-v2`) — **working tree dirty** with cumulative p16/p17/p18 edits (never re-tagged) |
| Provenance | Built from p17 tree + A2.12 `--qelarDaFc`; see `p18_vs_p17.diff` |
| Build script | `build_p18.sh` (same pattern as p15v2) |
| Compiler flags | identical pattern: `-O3 -DNDEBUG -std=c++20 … -march=native -mtune=native` |
| Build date | 2026-10-23 ~14:49–14:50 Asia/Shanghai (`p18_build.log`) |
| Authority | Signed A2.12 / t109 (`t109_addendum_and_prompt_SIGNED_20261023.md`) |

## Important honesty note on “frozen” sources

`build_p18/src` sits on branch `p15-amend2-v2` with **uncommitted** modifications relative to git HEAD (`WORKING_TREE_vs_GIT_HEAD.diff`). Those edits are the cumulative Port A hostFidelity / QELAR DA / HELLO-only / `qelarDaFc` work from p16→p17→p18. The **on-disk tree + binary hashes** are the evaluation freeze; there is no separate annotated git tag `p18`. Ship this working tree + `SRC_SHA256SUMS.txt` + `p18_vs_p17.diff` together.

## Runtime switches for Phase 2 (t109)

| Switch | Phase 2 value | Notes |
|---|---|---|
| `--hostFidelity` | `1` | Required for DA / HELLO-only |
| `--qelarDaFix` | `1` | Honour DA CLI + export mem/rank/fc/refresh |
| `--qelarDaFc` | `1` (default) or `0` | A2.12 first-contact on/off |
| `--qelarHelloOnly` | `0` or `1` | Piggyback vs HELLO-only panels |
| `--hostSched` | `periodic` / `trickle` / `da` | |
| `--eval` | `1` | Eval seed guard + addendum SHA |
| Seeds | **20261841–20261880** | Evaluation only |

## Acceptance / Gate notes

- **Gate 1 (identity)**: p18 vs p17 bit-identical `run.txt` under defaults for QELAR alone, QELAR DA (FC on), QELAR HELLO-only alone, MARV piggy P*, MARV HELLO P* (II, seed 20261114) — see `t109_report.md` / `gate1/`.
- **Phase 1 pilot**: tuning seeds 20261114–23; chose DA margin arm for Phase 2 piggy panel.
- **Phase 2**: 1440/1440 ok; jobs sha256 `7d2823cf83c941e176ba6875b0ab5869c4a3bc581fa289558e3427fba2aa355a`; ledger AFTER in `05_ledger_checksums/`.
- p14 / p15v2 / p16 / p17 left untouched (hashes recorded in report).

## Included snapshot contents

Full dirty `src/`, `build_p18.sh`, `bin/`, `lib/`, `include/`, `p18_vs_p17.diff`, `WORKING_TREE_vs_GIT_HEAD.diff`, `SRC_SHA256SUMS.txt`, `BINARY_HASHES.txt`.
