**SIGNED** by Ali (t144u, 2026-10-23 ~10:03 Asia/Shanghai): "approved from ali". Authority for t106 under A2.9.

# Amendment 2 v2.6 (addendum A2.9) and prompt t106 — host fidelity (build p16)

## Why

Ali's host verification (`host_verification.md`, 2026-10-22) found paper-vs-code differences in QELAR, LQARP and SAQR. t105 (viability pilot) ran that code, so its host-alone PDRs (QELAR 0.385, SAQR 0.079, LQARP 0.280) are not trustworthy for paper claims about those hosts. **MARV is unaffected** (all fixes are per-host overrides under `--hostFidelity=1`).

## Addendum A2.9 (to be approved by Ali before t106 build or runs)

### A2.9.1 Rules (fixed before building)

1. **New switch** `--hostFidelity=0|1`, default **0**. With `0`, p16 must behave **exactly** like p15v2 (so every MARV and t105 number stays reproducible). All items below apply **only** when `--hostFidelity=1` and **only** to the named host.

2. **Items applied** (paper values only, **no tuning**):
   - **QELAR (A8-H1, H2, H3):** candidate set = all fresh heard neighbours with record SNR ≥ threshold, no depth filter, previous hop allowed (MaxHops stays as loop guard); Q recomputed for every neighbour at every decision with Eq. (7), no learning rate; V = max of the recomputed Q; add Beta1/Beta2 (0.5/0.05) for Eq. (13). Explicit ACK + reroute kept (documented deviation A8-X1).
   - **LQARP (A8-L1, L2):** E_avg in Eq. (39) = mean residual energy of the candidate set; alpha = 0.9; Q initialised to 0. gamma stays 0.95 (paper gives no value). BFSK PER kept (documented). SNR threshold and r_range stay at scenario values.
   - **SAQR (A8-S1):** alpha 0.1, gamma 0.8, reward weights Service A (0.7/0.1/0.1/0.1), k = 2 (R_op/c)/0.5 = 3.2 s, Q_min initial 1, epsilon 0.5 during the 900 s warm-up and 0.01 afterwards (the paper's "round" has no meaning in our time base; this is a labelled placeholder).
   - **Not applied, disclosed as remaining deviations:** SAQR PLR definition, measured delay, holding semantics (A8-S2, S3, S4), QELAR implicit ACK (A8-H4), LQARP BPSK formula (A8-L3).

3. **DA hook:** add `--hostSched=da` for QELAR and SAQR, using the same membership event (candidate admission / void-flag flip) and rank event (score-gap sign change, margin m) as MARV, driven by the host's existing E-LQTP score (`elqtp-hosts.cc` QELAR ~404–412, SAQR ~862–874 — paths as in the verification tree; confirm in t106 Step 1), with first-contact reply. LQARP already has it (`--lqarpPosDa=2`). **No tuning** of daMargin/daImin: use the MARV piggyback P* values (**0.02 / 10 / refresh 0 / doublings 5**).

4. **Viability rule UNCHANGED** (A2.8.4): host-alone mean window PDR ≥ 0.30 in Scenario II on tuning seeds 20261114–18. No other parameter may be changed to reach it. If a host fails the rule, it is reported as not viable under this channel/MAC (that label is applied **after** t106 numbers, by Ali / a later signed step; **t106 itself does not write viable / not-viable / pass / fail** for the pilot cells — same discipline as t105 — except Gate 1 identity PASS/FAIL which is a binary-identity check, not a host-viability judgement).

5. **Seeds:** tuning seeds **20261114–18 only** (same as t105 so before/after are comparable). **No evaluation seeds.**

### A2.9.2 Pilot arms (`--hostFidelity=1`), Scenarios {I, II, GM2}, 5 seeds

Common base and locked geometry: as A2.8.2 / t105 (eddy (2000,2000) R=2828; I static; II V=0.15 σ=0.3; GM2 V=0.3 σ=0.6; `--linkOff=1` under `--disc=heard` as required by the binary; p15v2 frozen setup otherwise). Binary for the pilot: **p16** (built in t106). Keep p15v2 and p14 trees **untouched**.

- **QELAR and SAQR** (4 arms each): host alone; `--hostSched=periodic --tCtrl=60 --periodicJitter=1`; `--hostSched=trickle`; `--hostSched=da` (with MARV piggyback P* DA params: daMargin=0.02, daImin=10, daRefresh=0, daDoublings=5).
- **LQARP** (4 arms): `--protocol=lqarp`; `+ --lqarpPosDa=2`; `+ --lqarpNoPosePiggyback=1`; `+ both`.

**Run count:** QELAR 4×3×5 = 60; SAQR 4×3×5 = 60; LQARP 4×3×5 = 60; **total 180**. Stopping rule: if more than 2% of runs fail (rc ≠ 0), stop and report. Single runner; no second campaign process.

### A2.9.3 What t106 does not do
- No evaluation seeds; no MARV retune; no changing A2.8 viability threshold; no applying paper fixes under `--hostFidelity=0`; no rebuilding or editing p15v2/p14.

---

## Prompt t106 for Grok

t106 - Authority: Ali's approval of Amendment 2 v2.6 (A2.9). Build **p16** from p15v2 (exe sha256 `e66b8970…7396`); keep p15v2 and p14 untouched. Tuning seeds **20261114–18 only**. No evaluation seeds. No host-viability verdicts by you in the pilot tables. Ledger lines before and after.

1. **First, read-only:** confirm whether `model/elqtp-hosts.{h,cc}` and `elqtp-routing-host.{h,cc}` in the p15v2 tree are byte-identical to commit `257895a` (the commit the verification was done on). Give the diff stat. If they differ, list every difference touching QELAR, LQARP or SAQR. If those filenames are absent and the logic lives under `qelar-protocol.*` / `saqr-protocol.*` / `lqarp-protocol.*` / related Port A files, say so and diff **those** against `257895a` instead — still read-only.

2. **Implement** A2.9.1 items 1–3 exactly. Show the diff p16 vs p15v2 (write `p16_vs_p15v2.diff`) and its sha256. Do not change behaviour when `--hostFidelity=0`.

3. **Gate 1 (identity):** with `--hostFidelity=0`, p16 must give **bit-identical** output to p15v2 for: MARV piggyback P*, MARV HELLO-only P*, B0, and one t105 cell per host (Scenario II, seed 20261114, host alone). Report **PASS/FAIL per run**. If any FAIL, **STOP**.

4. **Gate 2 (sanity of the fixes):** for each host, `--hostFidelity=1`, Scenario II, seed 20261114, host alone: report PDR, delivered/generated, drop_void, reroutes, mean hops. Also run one QELAR run with a node that has no shallower neighbour, if you can find one, and say whether the packet is forwarded or dropped. Do not tune anything.

5. **Pilot:** run A2.9.2 (180 runs). Per cell report as in t105 (wpdr, E_bit, O_ctrl, HELLO counts by cause, hops, delivered/generated, zero-delivery runs, relay-removal fraction, 95% t). Add a **before/after table** of host-alone Scenario II wpdr: **t105 vs t106** (numbers only).

6. **STOP.** Write `t106_report.md` with hashes, per-run CSV, diff, ledger lines. Do not campaign further hosts; do not spend evaluation seeds.
