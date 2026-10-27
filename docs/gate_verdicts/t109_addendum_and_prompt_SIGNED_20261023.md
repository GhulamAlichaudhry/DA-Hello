**SIGNED** by Ali (t151u, 2026-10-23 ~14:47 Asia/Shanghai): "approved from ali". Authority for t109 under A2.12.

# Amendment 2 v2.9 (addendum A2.12) and prompt t109 — QELAR: small switch (p18), short pilot, then the evaluation campaign

## Why (from t108, tuning seeds, n=10)

- QELAR HELLO-only (state only in HELLOs): no-HELLO B0 collapses (II 0.075, GM2 0.082). DA with refresh 480 s: PDR +2.3 pp (II) and +0.5 pp (GM2) vs jittered T120, intervals include 0; control bytes 48.7% / 45.7% fewer than T120 (tight intervals); energy per bit about 10% / 5% lower (intervals include 0). Paired SD of PDR differences is 6 to 10 pp on this host, so the MARV margin of 0.5 pp cannot be tested at any feasible n.
- QELAR piggyback: with the first-contact reply working, DA loses 10 to 17 pp vs host alone (CI excludes 0), and switching rank events off (margin 10) does not help (−12.5 / −17.1 pp). The cost comes with about 2800 to 3000 first-contact HELLOs per run. Without first-contact (t107, margin hard-wired 0.02) the loss was −4.4 pp (II) and 0 (GM2), intervals include 0.

## Addendum A2.12

### A2.12.1 Build p18 = p17 + one switch

`--qelarDaFc=0|1` (default **1**). With `--qelarDaFix=1` and `--qelarDaFc=0` the QELAR DA arm runs membership, rank and refresh events but sends no first-contact reply (`fc_reply` must be 0). With the default, p18 must equal p17 bit-for-bit. Nothing else changes.

### A2.12.2 Phase 1 pilot (tuning seeds 20261114–23, II and GM2, piggyback QELAR, `--qelarDaFix=1` `--qelarDaFc=0`)

Arms: alone; DA with `--daMargin` in {0.02, 0.5, 10}, daImin 10, daDoublings 5, daRefresh 0. 4 × 2 × 10 = **80** runs. Selection (mechanical): smallest pooled paired PDR loss vs alone; configurations within 0.5 pp tie, tie goes to fewer HELLOs. Gate: if the chosen configuration's pooled mean loss exceeds **5 pp**, the piggyback DA arm is dropped from Phase 2 (reported as pilot only) and Phase 2 runs the HELLO-only panel only. Smoke test first: margin 0.02 and 10 must give different rank-event counts and `fc_reply = 0` (else STOP).

### A2.12.3 Phase 2 campaign (evaluation seeds **20261841–20261880**; authorised only for these runs)

Scenarios I, II, GM2, `--hostFidelity=1` `--qelarDaFix=1`, locked geometry and common base as t108.

- **HELLO-only panel** (`--qelarHelloOnly=1`): B0; periodic jittered T60 and T120 (`--hostSched=periodic --periodicJitter=1`); `--hostSched=trickle`; DA with daMargin 0, daImin 2, daDoublings 5, daRefresh 480 ("DA480"); DA with daRefresh 0 ("DA0", ablation). 6 × 3 × 40 = **720** runs.
- **Piggyback panel**: alone; T60; T120; trickle; DA (FC off, margin from Phase 1; skipped if the gate fails); DA with first-contact ON, daMargin 0.5 (descriptive). 6 × 3 × 40 = **720** runs (or fewer if DA FC-off skipped). Stopping rule: more than 2% of runs fail. No interim analysis; no re-runs except identical repeats after rc ≠ 0.

### A2.12.4 Statements (fixed before any evaluation run)

Paired by seed, Student-t 95% intervals, Holm within each family. The margin is **3 pp** (not 0.5 pp) because the pilot paired SD on QELAR is 6 to 10 pp; this choice was informed by the pilot and is disclosed as such.

- **Q1** (HELLO-only, DA480 vs T120, per scenario; family of 3): lower bound of paired PDR difference > −3 pp AND upper bound of relative control-bytes difference < −40%.
- **Q1′** (same comparison): lower bound of PDR difference > −3 pp AND upper bounds of the paired differences in control bytes and in energy per bit both < 0.
- **Q2** (HELLO-only): B0 PDR more than 5 pp below DA480 in every scenario.
- **Q3** (piggyback, DA FC-off vs alone, per scenario; only if the Phase 1 gate passes): lower bound of paired PDR difference > −3 pp AND upper bound of relative energy-per-bit difference < +5%.
- **Descriptive, no verdict:** DA480 vs trickle and vs T60; DA480 vs DA0; FC-on vs FC-off; HELLO counts by cause; relay-removal fraction.

### A2.12.5 What t109 does not do

No other hosts; no changing p14/p15v2/p16/p17; no pass/fail wording on Q1–Q3 in the report (numbers + intervals + Holm only).

---

## Prompt t109 for Grok

t109 - Authority: Ali's approval of A2.12. p17 exe sha256 `6305cb60…bf99` stays untouched; build p18 from p17; p14, p15v2, p16, p17 untouched. Evaluation seeds **20261841–80** only for the Phase 2 runs listed. No verdicts. Ledger lines before (all seeds, arms, job-list sha256) and after.

1. Implement A2.12.1. Write `p18_vs_p17.diff` and its sha256.
2. Gate 1 (identity, defaults): p18 vs p17 bit-identical `run.txt` for QELAR alone, QELAR DA (`qelarDaFix=1`, FC on), QELAR HELLO-only alone, MARV piggyback P*, MARV HELLO-only P* (II, seed 20261114). Any FAIL: STOP.
3. Smoke (A2.12.2). Then Phase 1 (80 runs); apply the mechanical selection and the 5 pp gate; write the decision to the ledger.
4. Phase 2 exactly as A2.12.3 (job list with sha256 in the ledger BEFORE the first run). Per run record: wpdr, window E_bit, O_ctrl, total control bytes, HELLO counts by cause (first-contact, membership, rank, refresh, periodic, trickle), hops, relay-removal fraction, rc.
5. Compute the paired statistics of A2.12.4 with Holm and 95% intervals, numerically, WITHOUT pass/fail wording.
6. STOP. Write `t109_report.md` with hashes, per-run CSV, paired tables, ledger lines.
