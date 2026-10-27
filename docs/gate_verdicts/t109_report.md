# t109 report — p18 QELAR `--qelarDaFc` + Phase1 pilot + Phase2 evaluation (A2.12)

- Authority: Ali t151u verbatim `approved from ali`; signed A2.12 `${OUT}/t109_addendum_and_prompt_SIGNED_20261023.md` sha256 `3c903bfda3fb91ba470098df4f5beed2cc96fa5547d9b29087b619f2da5a4e39`.
- Out: `${OUT}/t109_p18_20261023`
- Ledger BEFORE: 2026-10-23 14:49:22 CST; PHASE1_DECISION 15:00:58 CST; PHASE2_BEFORE_v2 15:02:27 CST; AFTER: 2026-10-23 16:46:15 CST
- Phase1 seeds (tuning): 20261114–23. Phase2 seeds (evaluation, authorised solely for A2.12.3): 20261841–80.
- **No verdicts.** A2.12.4 numbers reported with thresholds disclosed; pass/fail wording omitted.
- Phase1: **80/80 ok**, fail=0, elapsed_s=337.9, workers=6
- Phase2: **1440/1440 ok**, fail=0, fail_frac=0.000, elapsed_s=6054.0 (~1h41m), workers=6, runner PID 72731
- Resume note: helpers interrupted twice; Phase2 runner PID 72731 continued uninterrupted; this report written after 1440/1440.

## Hashes

| Artifact | sha256 |
|---|---|
| p15v2 exe (untouched) | `e66b8970b6d4ad91cbe939b77b0ba2fb10e21521d68e08f4853fefee4dff7396` |
| p16 exe (untouched) | `b295ab77b48ede76284ee1b65555b6387f735db4e5450344d4f8f2d7a40a83f0` |
| p17 exe (untouched; required intact) | `6305cb60f7814a3a6dcdde3c8d63ce89481d26e47d12523307d4cb8aacb0bf99` |
| p18 exe | `c57ce18e9a580655818bf06e9ecab73cefcbdb870e7d2e5f41eeb55581152f56` |
| p18 lib | `df0f4213a03753caa04ab52af9e74958bf196ec655fb961250771354e358aa2c` |
| p18_vs_p17.diff | `bdb49d5e253871277a36e40bea70ab45df3272140c0a3306fc06ca42d7c38ddd` |
| phase1_jobs.json | `60a600d8a3b0f2e3a059f503a5af21f8e7c103ae8a77c1ee9fe73d417d9fd138` |
| phase2_jobs.json (v2, `--eval=1`) | `7d2823cf83c941e176ba6875b0ab5869c4a3bc581fa289558e3427fba2aa355a` |
| eval addendum | `7806dea308dbc7dfa211853a619fb55313ca8857301310ab2e01dc385f58c002` |

## A2.12.1 Implementation (`--qelarDaFc`, default 1)

- New CLI `--qelarDaFc=0|1` (default **1**). With `--qelarDaFix=1` and `--qelarDaFc=0`: membership / rank / refresh run; **no** first-contact reply (`fc_reply` must be 0; FC state not allocated).
- Default path leaves p18 bit-identical to p17 (Gate 1). Diff only emits `qelarDaFc=0` on `QELAR_DA` / protocol line when off.
- Files touched: `scenario-i-lqarp.cc` only (see `p18_vs_p17.diff`).

## Gate 1 (identity, defaults) — 5/5 PASS

| Cell (II, seed 20261114) | run.txt sha256 (p17=p18) |
|---|---|
| QELAR alone | `c35f43906cddfc8d2f0c7ee981c3ebbc0e1d74aa0ad4612302656861be7e149c` |
| QELAR DA (`qelarDaFix=1`, FC on) | `a742e0d77607de2413258d6f8b099af9323b0b5f725e55c8f44a473515859b2e` |
| QELAR HELLO-only alone | `65b3265801d5ff4ba8bef59e7588ae9f0db0c5b910fad3288af542c551a8f3a4` |
| MARV piggyback P* | `b5de044e339654ceff990aa14601910fe0dc2034a193622b456b114f68c1781b` |
| MARV HELLO-only P* | `55c0598c16d6ae364a83f47f884c9786cf621db1ed5442e50daf098cb433d0f5` |

## Smoke (A2.12.2) — PASS

| daMargin | rank_events | mem_events | fc_reply |
|---|---|---|---|
| 0.02 | **226** | 14 | **0** |
| 10 | **0** | 13 | **0** |

Rank counts differ; `fc_reply=0` under `--qelarDaFc=0`.

## Phase 1 pilot (tuning, piggyback, FC off) — 80 runs

Arms: alone; DA `daMargin` ∈ {0.02, 0.5, 10}, `daImin=10`, `daDoublings=5`, `daRefresh=0`; II+GM2; seeds 20261114–23; `--qelarDaFix=1 --qelarDaFc=0`.

| Arm | pooled loss vs alone [95% t] | mean HELLOs |
|---|---|---|
| da_m0.02 | 0.021957 [-0.015229, 0.059142] | 386.4 |
| da_m0.5 | 0.008311 [-0.039112, 0.055734] | 287.9 |
| da_m10 | 0.005716 [-0.035603, 0.047035] | 161.7 |

- Tie candidates within 0.5 pp of best: `['da_m10', 'da_m0.5']`
- **Chosen: `da_m10` (`daMargin=10.0`)**; pooled_loss_pp=0.5716
- 5 pp gate: pooled mean loss 0.5716 pp ≤ 5 → **INCLUDE** piggy DA FC-off in Phase 2 (`gate_5pp_pass=True`).

## Phase 2 campaign — 1440 runs (eval seeds 20261841–80)

Common: `--hostFidelity=1 --qelarDaFix=1 --eval=1`; locked geometry; scenarios I, II, GM2; n=40/cell.

- HELLO-only panel (`--qelarHelloOnly=1`): B0; T60; T120; trickle; DA480 (`daMargin=0, daImin=2, daDoublings=5, daRefresh=480`); DA0 (`daRefresh=0`). 6×3×40 = 720.
- Piggyback panel: alone; T60; T120; trickle; DA_FCoff_m10 (Phase1 choice); DA_FCon_m0.5 (descriptive). 6×3×40 = 720.
- Jobs sha256 (BEFORE first sim): `7d2823cf83c941e176ba6875b0ab5869c4a3bc581fa289558e3427fba2aa355a`
- Result: ok=1440 fail=0 skip=0 fail_frac=0.000000 ≤ 0.02

### Phase 2 cell means (n=40)

| Family | Arm | Sc | wpdr | E_bit | O_ctrl | bytes | hops | rem_frac | fc | mem | rank | refresh | periodic | trickle | zero |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| hello | B0 | I | 0.0596 | 18272.8 | 1.3541 | 19499 | 1.72 | 0.000 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| hello | B0 | II | 0.0693 | 15171.7 | 1.3540 | 19498 | 1.80 | 0.223 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| hello | B0 | GM2 | 0.0706 | 14443.4 | 1.3540 | 19498 | 1.50 | 0.295 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| hello | T60 | I | 0.7195 | 1645.0 | 126.3516 | 1819464 | 4.44 | 0.000 | 0 | 0 | 0 | 0 | 35999 | 0 | 0 |
| hello | T60 | II | 0.4192 | 2919.4 | 109.1572 | 1571864 | 4.23 | 0.223 | 0 | 0 | 0 | 0 | 31048 | 0 | 0 |
| hello | T60 | GM2 | 0.3599 | 3375.3 | 100.1844 | 1442655 | 4.38 | 0.295 | 0 | 0 | 0 | 0 | 28463 | 0 | 0 |
| hello | T120 | I | 0.7923 | 1374.2 | 63.8532 | 919486 | 4.62 | 0.000 | 0 | 0 | 0 | 0 | 18000 | 0 | 0 |
| hello | T120 | II | 0.4633 | 2458.3 | 55.2565 | 795694 | 4.60 | 0.223 | 0 | 0 | 0 | 0 | 15524 | 0 | 0 |
| hello | T120 | GM2 | 0.3924 | 2871.1 | 50.7698 | 731085 | 4.67 | 0.295 | 0 | 0 | 0 | 0 | 14232 | 0 | 0 |
| hello | trickle | I | 0.8062 | 1298.1 | 46.1550 | 664632 | 4.07 | 0.000 | 0 | 0 | 0 | 0 | 0 | 12903 | 0 |
| hello | trickle | II | 0.4163 | 2715.3 | 44.0480 | 634291 | 4.98 | 0.223 | 0 | 0 | 0 | 0 | 0 | 12296 | 0 |
| hello | trickle | GM2 | 0.3492 | 3198.7 | 43.9115 | 632325 | 5.06 | 0.295 | 0 | 0 | 0 | 0 | 0 | 12257 | 0 |
| hello | DA480 | I | 0.8419 | 1192.0 | 30.9671 | 445926 | 3.88 | 0.000 | 4678 | 10 | 145 | 3796 | 0 | 0 | 0 |
| hello | DA480 | II | 0.4414 | 2428.7 | 28.6808 | 413004 | 4.94 | 0.223 | 4690 | 14 | 144 | 3742 | 0 | 0 | 0 |
| hello | DA480 | GM2 | 0.3757 | 2882.3 | 28.0043 | 403261 | 5.08 | 0.295 | 4880 | 15 | 142 | 3685 | 0 | 0 | 0 |
| hello | DA0 | I | 0.5622 | 2027.7 | 26.6852 | 384268 | 8.13 | 0.000 | 7145 | 15 | 145 | 0 | 0 | 0 | 0 |
| hello | DA0 | II | 0.3960 | 2762.0 | 24.6760 | 355335 | 5.91 | 0.223 | 6571 | 17 | 144 | 0 | 0 | 0 | 0 |
| hello | DA0 | GM2 | 0.3536 | 3258.7 | 24.6365 | 354766 | 5.52 | 0.295 | 6562 | 18 | 143 | 0 | 0 | 0 | 0 |
| piggy | alone | I | 0.9260 | 1018.0 | 1.3536 | 19492 | 3.08 | 0.000 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| piggy | alone | II | 0.6371 | 1593.5 | 1.3540 | 19498 | 3.99 | 0.223 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| piggy | alone | GM2 | 0.5737 | 1765.1 | 1.3536 | 19492 | 4.32 | 0.295 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| piggy | T60 | I | 0.8011 | 1448.4 | 126.3522 | 1819471 | 3.82 | 0.000 | 0 | 0 | 0 | 0 | 36000 | 0 | 0 |
| piggy | T60 | II | 0.5002 | 2414.9 | 109.1572 | 1571864 | 3.90 | 0.223 | 0 | 0 | 0 | 0 | 31048 | 0 | 0 |
| piggy | T60 | GM2 | 0.4314 | 2801.2 | 100.1840 | 1442650 | 4.32 | 0.295 | 0 | 0 | 0 | 0 | 28463 | 0 | 0 |
| piggy | T120 | I | 0.8845 | 1193.6 | 63.8531 | 919485 | 3.55 | 0.000 | 0 | 0 | 0 | 0 | 18000 | 0 | 0 |
| piggy | T120 | II | 0.5737 | 1938.6 | 55.2560 | 795686 | 4.15 | 0.223 | 0 | 0 | 0 | 0 | 15524 | 0 | 0 |
| piggy | T120 | GM2 | 0.5029 | 2225.0 | 50.7694 | 731079 | 4.31 | 0.295 | 0 | 0 | 0 | 0 | 14232 | 0 | 0 |
| piggy | trickle | I | 0.9155 | 1077.5 | 31.1037 | 447894 | 3.18 | 0.000 | 0 | 0 | 0 | 0 | 0 | 8568 | 0 |
| piggy | trickle | II | 0.5568 | 1948.5 | 30.0162 | 432234 | 4.46 | 0.223 | 0 | 0 | 0 | 0 | 0 | 8255 | 0 |
| piggy | trickle | GM2 | 0.4575 | 2413.9 | 31.9685 | 460346 | 4.76 | 0.295 | 0 | 0 | 0 | 0 | 0 | 8817 | 0 |
| piggy | DA_FCoff_m10 | I | 0.9276 | 1016.4 | 1.3971 | 20119 | 3.08 | 0.000 | 0 | 12 | 0 | 0 | 0 | 0 | 0 |
| piggy | DA_FCoff_m10 | II | 0.6330 | 1600.3 | 1.3968 | 20114 | 4.05 | 0.223 | 0 | 14 | 0 | 0 | 0 | 0 | 0 |
| piggy | DA_FCoff_m10 | GM2 | 0.5767 | 1750.4 | 1.3972 | 20120 | 4.37 | 0.295 | 0 | 14 | 0 | 0 | 0 | 0 | 0 |
| piggy | DA_FCon_m0.5 | I | 0.9238 | 1023.6 | 11.3972 | 164120 | 3.13 | 0.000 | 2753 | 10 | 136 | 0 | 0 | 0 | 0 |
| piggy | DA_FCon_m0.5 | II | 0.5359 | 1922.8 | 11.9148 | 171574 | 4.52 | 0.223 | 2912 | 15 | 126 | 0 | 0 | 0 | 0 |
| piggy | DA_FCon_m0.5 | GM2 | 0.4221 | 2484.8 | 12.6101 | 181585 | 5.20 | 0.295 | 3128 | 17 | 113 | 0 | 0 | 0 | 0 |

## A2.12.4 paired statistics (numeric only; no pass/fail)

**Margin disclosure:** 3 pp (not 0.5 pp), informed by Phase1/t108 pilot paired SD 6–10 pp on QELAR; disclosed as such.
Paired by seed, Student-t 95% intervals, Holm within each family (on two-sided PDR p).

### Q1 — HELLO-only DA480 vs T120 (family of 3)
Statement components (for reader): lower bound of paired PDR difference > −3 pp **AND** upper bound of relative control-bytes difference < −40%.

- Q1 I: d_wpdr=0.049547 [0.040341,0.058753] rel_bytes=-0.515027 [-0.519488,-0.510566] holm_p_wpdr=6.53598e-13 (components: d_wpdr_lo=0.040341 vs >-0.03; rel_bytes_hi=-0.510566 vs <-0.40)
- Q1 II: d_wpdr=-0.021897 [-0.043837,0.000042] rel_bytes=-0.481065 [-0.486715,-0.475415] holm_p_wpdr=0.100778 (components: d_wpdr_lo=-0.043837 vs >-0.03; rel_bytes_hi=-0.475415 vs <-0.40)
- Q1 GM2: d_wpdr=-0.016668 [-0.036523,0.003186] rel_bytes=-0.448621 [-0.455386,-0.441856] holm_p_wpdr=0.100778 (components: d_wpdr_lo=-0.036523 vs >-0.03; rel_bytes_hi=-0.441856 vs <-0.40)

### Q1′ — HELLO-only DA480 vs T120
Components: d_wpdr_lo > −3 pp **AND** upper bounds of paired diffs in control bytes and energy-per-bit both < 0.

- Q1' I: d_wpdr_lo=0.040341 d_bytes_hi=-469458.2 d_ebit_hi=-163.3645 (thresholds: d_wpdr_lo>-0.03; d_bytes_hi<0; d_ebit_hi<0)
- Q1' II: d_wpdr_lo=-0.043837 d_bytes_hi=-377907.8 d_ebit_hi=85.5174 (thresholds: d_wpdr_lo>-0.03; d_bytes_hi<0; d_ebit_hi<0)
- Q1' GM2: d_wpdr_lo=-0.036523 d_bytes_hi=-322593.8 d_ebit_hi=160.9167 (thresholds: d_wpdr_lo>-0.03; d_bytes_hi<0; d_ebit_hi<0)

### Q2 — HELLO-only B0 vs DA480
Components: B0 PDR more than 5 pp below DA480 in every scenario (report DA480−B0).

- Q2 I: DA480−B0=0.782324 [0.773144,0.791503] B0_mean=0.059566 DA480_mean=0.841890 (threshold mean>0.05)
- Q2 II: DA480−B0=0.372179 [0.345134,0.399224] B0_mean=0.069266 DA480_mean=0.441446 (threshold mean>0.05)
- Q2 GM2: DA480−B0=0.305075 [0.281397,0.328753] B0_mean=0.070636 DA480_mean=0.375711 (threshold mean>0.05)

### Q3 — piggyback DA_FCoff_m10 vs alone (Phase1 gate passed → computed)
Components: d_wpdr_lo > −3 pp **AND** rel_ebit_hi < +5%.

- Q3 I: d_wpdr=0.001591 [-0.000772,0.003953] rel_ebit=-0.001556 [-0.004179,0.001066] holm_p_wpdr=0.542906 (components: d_wpdr_lo=-0.000772 vs >-0.03; rel_ebit_hi=0.001066 vs <+0.05)
- Q3 II: d_wpdr=-0.004079 [-0.024197,0.016039] rel_ebit=0.010506 [-0.022357,0.043369] holm_p_wpdr=1 (components: d_wpdr_lo=-0.024197 vs >-0.03; rel_ebit_hi=0.043369 vs <+0.05)
- Q3 GM2: d_wpdr=0.003086 [-0.020310,0.026481] rel_ebit=0.002021 [-0.042014,0.046056] holm_p_wpdr=1 (components: d_wpdr_lo=-0.020310 vs >-0.03; rel_ebit_hi=0.046056 vs <+0.05)

### Descriptive (no statement / no verdict)

Paired DA480−ref (HELLO) and FCon−FCoff (piggy); mean [95% t], n=40.

| Comparison | Sc | Δwpdr | Δbytes | ΔE_bit | rel_bytes | rel_ebit |
|---|---|---|---|---|---|---|
| DA480 vs trickle | I | 0.0357 [0.0291, 0.0423] | -218706 [-224168, -213244] | -106.1 [-118.7, -93.5] | -0.3288 [-0.3356, -0.3221] | -0.0811 [-0.0899, -0.0723] |
| DA480 vs trickle | II | 0.0251 [-0.0023, 0.0526] | -221288 [-226513, -216062] | -286.6 [-467.9, -105.2] | -0.3489 [-0.3565, -0.3413] | -0.0793 [-0.1435, -0.0151] |
| DA480 vs trickle | GM2 | 0.0265 [0.0018, 0.0512] | -229064 [-233740, -224387] | -316.4 [-545.8, -87.0] | -0.3626 [-0.3705, -0.3546] | -0.0825 [-0.1511, -0.0139] |
| DA480 vs T60 | I | 0.1224 [0.1138, 0.1309] | -1373538 [-1377639, -1369436] | -453.0 [-473.9, -432.0] | -0.7549 [-0.7572, -0.7527] | -0.2746 [-0.2838, -0.2654] |
| DA480 vs T60 | II | 0.0222 [-0.0006, 0.0451] | -1158860 [-1168493, -1149227] | -490.7 [-651.8, -329.6] | -0.7373 [-0.7402, -0.7345] | -0.1563 [-0.2019, -0.1107] |
| DA480 vs T60 | GM2 | 0.0158 [-0.0041, 0.0358] | -1039394 [-1051007, -1027781] | -493.1 [-666.4, -319.7] | -0.7206 [-0.7240, -0.7172] | -0.1434 [-0.1919, -0.0949] |
| DA480 vs DA0 | I | 0.2797 [0.2501, 0.3093] | 61659 [55011, 68307] | -835.7 [-995.1, -676.3] | 0.1645 [0.1439, 0.1850] | -0.3826 [-0.4211, -0.3440] |
| DA480 vs DA0 | II | 0.0454 [0.0279, 0.0630] | 57669 [51949, 63389] | -333.3 [-428.6, -238.1] | 0.1666 [0.1468, 0.1865] | -0.1173 [-0.1522, -0.0825] |
| DA480 vs DA0 | GM2 | 0.0221 [0.0008, 0.0433] | 48495 [42640, 54350] | -376.5 [-641.7, -111.2] | 0.1408 [0.1224, 0.1593] | -0.0703 [-0.1310, -0.0096] |
| FCon_m0.5 vs FCoff_m10 | I | -0.0038 [-0.0075, -0.0001] | 144001 [141359, 146643] | 7.2 [3.0, 11.4] | 7.1592 [7.0210, 7.2975] | 0.0071 [0.0030, 0.0112] |
| FCon_m0.5 vs FCoff_m10 | II | -0.0971 [-0.1240, -0.0702] | 151460 [148277, 154643] | 322.5 [217.6, 427.4] | 7.5324 [7.3662, 7.6986] | 0.2170 [0.1483, 0.2856] |
| FCon_m0.5 vs FCoff_m10 | GM2 | -0.1546 [-0.1793, -0.1299] | 161465 [157781, 165149] | 734.4 [579.7, 889.1] | 8.0272 [7.8379, 8.2166] | 0.4322 [0.3509, 0.5135] |

### HELLO cause mapping
- first-contact → `QELAR_DA fc_reply` / OCTRL `fc_reply` (cause 7)
- membership / rank / refresh → `QELAR_DA mem_events` / `rank_events` / `refresh_events`
- periodic → OCTRL `periodic`; trickle → OCTRL `adaptive`

## Files

- `per_run.csv` (Phase1+Phase2, 1520 rows), `cell_means_phase2.csv`, `paired_tables.csv`, `a2124_stats.json`
- `p18_vs_p17.diff`, `phase1_jobs.json`, `phase2_jobs.json`, `run_summary_phase1.json`, `run_summary_phase2.json`
- `phase1_selection.json`, `ledger_BEFORE.json`, `ledger_PHASE1_DECISION.json`, `ledger_PHASE2_BEFORE_v2.json`, `ledger_AFTER.json`
- `gate1/`, `smoke/`, `runs/phase1/`, `runs/phase2/{hello,piggy}/...`

## STOP

p14 / p15v2 / p16 / p17 untouched (p17 exe sha256 confirmed intact). No verdicts. Evaluation seeds used only for A2.12.3 Phase 2 arms listed.
