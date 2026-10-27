**SIGNED** by Ali (t150u, 2026-10-23 ~14:12 Asia/Shanghai): "approved from ali". Authority for t108 under A2.11.

# Amendment 2 v2.8 (addendum A2.11) and prompt t108 — build p17: QELAR DA = MARV mechanism + QELAR HELLO-only, then pilot

## Why (from t107)

1. The QELAR DA arm is not the MARV P* mechanism: margin hard-coded 0.02 (`scenario-i-lqarp.cc:3865`, CLI `--daMargin` ignored), first-contact state never allocated (`fc_reply = 0`), membership vs rank counts not exported. The t107 margin sweep therefore tested nothing (all four arms byte-identical).
2. On QELAR, state travels in data headers; "alone" already gives II 0.672 / GM2 0.577. A scheduler can only match it there (DA: II −4.4 pp, GM2 0, CIs include 0). The case where a scheduler can win is a QELAR whose state travels only in HELLOs, as with MARV `--helloOnlyHost=1`.

## Addendum A2.11

### A2.11.1 Rules (fixed before building)

New switches, all default OFF so that p17 = p16 bit-for-bit when they are not set:

- `--qelarDaFix=1`: QELAR DA honours `--daMargin`/`--daImin`/`--daDoublings`/`--daRefresh`; allocates and enables first-contact reply (same rule as MARV P*: decode of a never-heard sender, reply after U[0,2] s, ≥ 5 s gap, cause code 7); exports separate counts for membership events, rank events, first-contact replies, refresh.
- `--qelarHelloOnly=1` (requires `--hostFidelity=1`): QELAR data headers carry no neighbour attributes (V, residual energy, average energy, depth/position); every neighbour record is created and updated only by HELLO or beacon decodes. Mirror MARV `--helloOnlyHost=1` exactly in spirit; the report must describe what is removed from the header and where. No other behaviour changes. Labelled in the paper as a QELAR variant, not a reproduced protocol.

### A2.11.2 Smoke test (STOP rule)

Before any grid: run QELAR DA, II, seed 20261114, with `--daMargin` 0.02 and 10. They must differ in rank-event count. If identical, STOP and report.

### A2.11.3 Pilot (tuning seeds 20261114–23 only; `--hostFidelity=1` `--qelarDaFix=1`)

Piggyback QELAR (`qelarHelloOnly=0`), scenarios II and GM2: alone; DA with `--daMargin` in {0.02, 0.1, 0.5, 10}, daImin 10, daDoublings 5, daRefresh 0. 5 arms × 2 × 10 = 100 runs.

HELLO-only QELAR (`qelarHelloOnly=1`), scenarios II and GM2: alone (B0); periodic jittered T60 and T120; `--hostSched=trickle`; DA with daMargin 0, daImin 2, daDoublings 5 and daRefresh in {0, 480} (the MARV HELLO-only values). 6 arms × 2 × 10 = 120 runs.

Total 220. Stopping rule: more than 2% failures. Selection (fixed now): piggyback: daMargin with the smallest pooled paired PDR loss vs alone; configurations within 0.5 pp tie, tie goes to fewer HELLOs. HELLO-only: daRefresh is chosen as in the MARV rule v2 (best pooled PDR, ties within 0.5 pp to fewer control bytes). Report all arms; no verdict by Grok.

### A2.11.4 What t108 does not do

No evaluation seeds; no SAQR/LQARP campaign; no editing p14/p15v2/p16 trees; no host-viability re-judgement.

---

## Prompt t108 for Grok

t108 - Authority: Ali's approval of A2.11. Build p17 from p16 (exe sha256 `b295ab77…83f0`); keep p14, p15v2 and p16 untouched. Tuning seeds 20261114–23 only. No evaluation seeds. No verdicts. Ledger lines before and after.

1. Implement A2.11.1. Write `p17_vs_p16.diff` and its sha256.
2. Gate 1 (identity, switches off): p17 vs p16 bit-identical `run.txt` for QELAR alone, QELAR f=1 DA (old hard-coded), MARV piggyback P*, MARV HELLO-only P*, LQARP alone; II, seed 20261114. Any FAIL: STOP.
3. A2.11.2 smoke test. Report the two rank-event counts.
4. Gate 2: QELAR `--qelarHelloOnly=1` alone, II, seed 20261114: report PDR (expected to collapse like MARV B0), neighbour-table size over time, and confirm by log that no header attribute reaches the neighbour table.
5. Run A2.11.3 (220 runs). Per cell: wpdr, E_bit, O_ctrl, HELLO counts by cause (first-contact, membership, rank, refresh, periodic, trickle), hops, zero-delivery runs, paired wpdr difference vs the reference arm (alone for piggyback; T120 for HELLO-only) with 95% t.
6. STOP. Write `t108_report.md` with hashes, per-run CSV, diff, ledger lines.
