# Build record — MARV evaluation binary **p15v2**

| Field | Value |
|---|---|
| Role | MARV Port A evaluation binary used for t103 campaign (A2.7) |
| Path (box) | `${WORKSPACE}/DA-HELLO-porta/build_p15v2/` |
| Scenario exe | `bin_p15v2/ns3.41-scenario-i-lqarp-optimized` |
| **exe SHA256** | `e66b8970b6d4ad91cbe939b77b0ba2fb10e21521d68e08f4853fefee4dff7396` |
| lib | `lib_p15v2/libns3.41-dahello-optimized.so` |
| lib SHA256 | `ad9cc623ee99eb44def7b243bf1b62e3954364b4f213ac486e7ff1ead3f24395` |
| Git commit (clean) | `e77113e8aa8def911298556a381c5cf0ba810614` (`p15-amend2-v2`) |
| Build script | `build_p15v2.sh` (out-of-tree; links against ns-3.41 core libs) |
| Compiler flags | `-O3 -DNDEBUG -std=c++20 -fno-semantic-interposition -Wall -march=native -mtune=native` |
| DEFS | `-DNS3_BUILD_PROFILE_OPTIMIZED -D__LINUX__` + `PROJECT_SOURCE_PATH` |
| ns-3 root | `${WORKSPACE}/DA-HELLO/opt/ns-allinone-3.41/ns-3.41` |
| Build date (file mtime) | 2026-10-23 ~08:58–08:59 Asia/Shanghai (from box mtimes / t99 logs) |
| Authority | Amendment 2 v2 / t99 (A2.6); evaluation use under t103 signed A2.7 |

## Compile order (from build_p15v2.sh)

`acoustic-phy acoustic-medium bellhop-data-loader channel-gate link-quality lqarp-protocol dahello-protocol rlor-inspired-protocol qelar-protocol saqr-protocol marv-protocol gauss-markov dahello-run-config` (+ optional `dahello-audit`) then `scenario-i-lqarp.cc`.

## Host / scheduler switches relevant to MARV eval (t103)

These are **runtime CLI** flags (not compile-time). p15v2 provides the MARV host / HELLO-only / DA path used in t103:

| Switch | Typical t103 value | Notes |
|---|---|---|
| `--protocol` | `marv` / `marv-T*` / `marv-dahello-hs` | MARV host |
| `--helloOnlyHost` | `0` or `1` | Header vs HELLO-only |
| `--hostSched` | `periodic` / `trickle` / `da` | Scheduler |
| `--daMode` / `--daMargin` / `--daImin` / `--daDoublings` / `--daRefresh` | per arm | DA-HELLO params |
| `--periodicJitter` | `1` where required | A2.2 jitter |
| `--linkOff` | `1` | Port A lock |
| `--eval` | `1` | Eval-seed guard |
| `--hostFidelity` | **n/a on p15v2** | Introduced later in p16 (A2.9); MARV t103 ran on p15v2 |
| `--qelarDaFix` / `--qelarHelloOnly` / `--qelarDaFc` | **n/a** | QELAR-only; p18 |

## Acceptance / Gate notes (from signed trail)

- **t99** (A2.6): p15v2 built; identity vs p14; pilots; exe/lib hashes recorded (`t99_report_20261022.md`, `logs/build_p15v2*.log`).
- **t103** (A2.7): evaluation campaign on this exact exe hash; `binary_verify.txt` matches; 2800 unique rundirs ok; zip intentionally **omitted** raw `runs/` (see `04_raw_logs/marv_t103/MISSING.md`).
- **t105 / t106**: p15v2 left **untouched** while p16 added `--hostFidelity` (`p15v2_hashes_BEFORE_ns3_restore.txt`, `p16_vs_p15v2.diff`).
- **t108 / t109**: p15v2 remained untouched through QELAR p17/p18 work.

## Included snapshot contents

`src/` (all headers + `.cc` compiled into the module + `examples/scenario-i-lqarp.cc` and helpers), `build_p15v2.sh`, `bin/`, `lib/`, thin `include/` wrappers, `SRC_SHA256SUMS.txt`, diffs vs p14.
