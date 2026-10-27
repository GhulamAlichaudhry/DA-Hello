**SIGNED** by Ali (t145u / t145s758 answer, 2026-10-23 ~10:44 Asia/Shanghai): "approved from ali". Authority for t107 under A2.10.

# Amendment 2 v2.7 (addendum A2.10) and prompt t107 — Port A host audit (read-only) + QELAR DA tuning

**Status:** UNSIGNED. No audit and no runs until Ali signs (e.g. “approved from ali”).

## Why

t106 showed the `host_verification.md` audit was done on a different source tree (ns-3 `contrib/uwsn-elqtp` @`257895a`), not on Port A, which is the code that actually ran. So we do not yet know which paper deviations exist in Port A. Also, on QELAR the DA arm (`hostst` ~239 events/run) costs 7.7 pp PDR in Scenario II vs host alone (paired, 5 seeds, CI [−14.8, −0.6]); we test, on tuning seeds only, whether the rank-event margin is the cause.

## Addendum A2.10 (to be approved by Ali before t107)

### A2.10.1 Viability labels (applied by the assistant to t106, rule A2.8 unchanged)

Rule A2.8.4 unchanged: host-alone Scenario II mean wpdr ≥ 0.30 on tuning seeds 20261114–18. Applied to t106 (`--hostFidelity=1`) host-alone II means:

| Host | t106 mean wpdr [95% t] | Label |
|---|---|---|
| QELAR | 0.665208 [0.616310, 0.714106] | **viable** |
| SAQR | 0.082238 [−0.007716, 0.172192] | **not viable** |
| LQARP | 0.284318 [0.194397, 0.374238] | **not viable** (CI spans 0.30; reported as **borderline**) |

**Not campaigned:** SAQR, LQARP (reported as pilots in the paper, with the t105 and t106 numbers). QELAR may proceed to further E-LQTP / no-harm work under later signed prompts; this addendum does **not** authorize a QELAR evaluation campaign.

### A2.10.2 QELAR DA tuning (tuning seeds 20261114–23 only; p16; `--hostFidelity=1`)

- **Binary:** p16 (exe sha256 `b295ab77…a83f0`; lib `4bed1453…8ec45`). p15v2 / p14 untouched.
- **Common base / locked geometry:** as A2.8 / A2.9 (eddy (2000,2000) R=2828; I static unused here; II V=0.15 σ=0.3; GM2 V=0.3 σ=0.6; `--linkOff=1` under `--disc=heard`; `--marvPsatScope=top`).
- **Arms:** `--hostSched=da` with `--daMargin` ∈ {0.02, 0.1, 0.5, 10} (10 = rank events effectively off); `daImin=10`, `daDoublings=5`, `daRefresh=0` (membership + first-contact stay on). **Reference arm:** host alone (no `--hostSched=da`).
- **Scenarios:** II and GM2. **Seeds:** tuning **20261114–23** (10 seeds). **Runs:** 4×2×10 + 2×10 = **100**.
- **Selection rule (fixed now):** pooled over II and GM2, pick the `daMargin` with the smallest paired PDR loss vs host alone; configurations within 0.5 pp of the best are tied and the tie goes to fewer `hostst` events. Report all four. If the chosen configuration still loses PDR with a CI excluding 0, the paper says “small PDR cost, far below periodic and Trickle”, not “no harm”.
- **No evaluation seeds.** No viability verdicts beyond A2.10.1 (already fixed). No code changes. No MARV retune. No SAQR/LQARP campaign.

### A2.10.3 What t107 does not do

- No evaluation seeds; no host-campaign for QELAR/SAQR/LQARP beyond the 100-run DA grid; no editing Port A or p16 sources; no rebuilding p16 unless Gate/identity requires it (it should not — read-only audit + runs only); no changing A2.8 threshold.

---

## Prompt t107 for Grok

t107 - Authority: Ali's approval of A2.10. p16 binary (exe sha256 `b295ab77…a83f0`), tuning seeds **20261114–23 only**, no evaluation seeds, no viability verdicts beyond recording A2.10.1. Ledger lines before and after.

1. **READ-ONLY audit of Port A hosts** (`qelar-protocol.*`, `saqr-protocol.*`, `lqarp-protocol.*`, adapters) against `host_verification.md`. For **EVERY** row of its tables (**L1–L22, Q1–Q19, S1–S24**) write: **MATCH / MISMATCH / N.S.** for Port A at `--hostFidelity=0` and at `=1`, with **file:line**. Do not change code.

   **1b. READ-ONLY, same audit, p16 frozen setup:**
   - (i) Does the simulator charge a modem preamble (`T_pre`) on HELLO and data frames, and what are the frame sizes in bits for HELLO, data header, ACK?
   - (ii) What are `P_tx`, `P_rx` and `P_idle` actually used (file:line)?
   - (iii) Do the per-run outputs or logs hold per-node time in each radio state (tx/rx/idle) so energy can be recomputed for other `P_idle` values?
   - (iv) What does the code do on overhearing data from a sender not in the table: does MARV P send a first-contact reply (cause code), and is that logic equivalent to “beacon on hearing an unknown neighbour”?

   Report only.

2. **Run A2.10.2 (100 runs).** Report per config and scenario: wpdr, E_bit, O_ctrl, `hostst` event count split into membership events and rank events (if separable), paired wpdr difference vs host alone with 95% t, and the pooled II+GM2 paired difference. Apply the A2.10.2 selection rule and state the chosen `daMargin` (and tie break if any).

3. **STOP.** Write `t107_report.md` with hashes, per-run CSV, ledger lines. Also record A2.10.1 viability labels (QELAR viable; SAQR not viable; LQARP not viable / borderline) against the t106 numbers — no new judgements.
