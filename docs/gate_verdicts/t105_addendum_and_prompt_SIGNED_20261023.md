**SIGNED** by Ali (t142u, 2026-10-23 ~09:00 Asia/Shanghai): "its approved from ali". Authority for t105 under A2.8.

# Amendment 2 v2.5 (addendum A2.8) and prompt t105 — host viability pilots (QELAR / SAQR / LQARP)

## Addendum A2.8 (to be approved by Ali before t105 runs)

### A2.8.1 Purpose
Decide, **before any evaluation seed is spent**, which of QELAR, SAQR and LQARP can serve as hosts under the locked geometry and channel/MAC. This is a pilot only. No evaluation seeds. No manuscript campaign for these hosts until after A2.8 / t105 and a later signed prompt.

### A2.8.2 Locked setup (unchanged from A2.7 / t104)
- Binary p15v2 unchanged: exe sha256 `e66b8970b6d4ad91cbe939b77b0ba2fb10e21521d68e08f4853fefee4dff7396`; lib sha256 `ad9cc623ee99eb44def7b243bf1b62e3954364b4f213ac486e7ff1ead3f24395`.
- `--marvPsatScope=top` (D15).
- Geometry: eddy centre (2000,2000), R 2828; Scenario I static (`--scenario=i`); Scenario II `--scenario=iii --driftSpeed=0.15 --gmSigma=0.3 --gmZeta=0.85`; GM2 `--scenario=iii --driftSpeed=0.3 --gmSigma=0.6 --gmZeta=0.85`.
- Common base flags (t104 / frozen campaign base):  
  `--topo=t1 --nAuv=149 --simTime=14400 --warm=900 --drain=300 --quiet=1 --phase=random --chan=formula --mac=cs --disc=heard --power=s2c --sinkBeacon=60 --marvPsatScope=top --marvSinkOverheard=ack --pktInterval=1200 --tNb=3600 --eddyCx=2000 --eddyCy=2000 --eddyR=2828`  
  plus the scenario flags above, `--seed`, `--protocol` / arm flags, `--out=run.txt`, and `--dataDir` as in t99–t103.
- Seeds: **tuning seeds 20261114–18 only** (five seeds). No evaluation seeds (not 20261801–40, 20261601–20, 20261401–20, 20261201–300).

### A2.8.3 Arms (no tuning; fixed from t104 code audit)
**QELAR** (3 arms):
1. host alone: `--protocol=qelar`
2. jittered periodic P60: `--protocol=qelar --hostSched=periodic --tCtrl=60 --periodicJitter=1`
3. host-side Trickle: `--protocol=qelar --hostSched=trickle`

**SAQR** (3 arms): same three with `--protocol=saqr` instead of `qelar`.

**LQARP** (6 arms):
1. `--protocol=lqarp`
2. `--protocol=lqarp --lqarpPosDa=2`
3. `--protocol=lqarp --lqarpNoPosePiggyback=1`
4. `--protocol=lqarp --lqarpNoPosePiggyback=1 --lqarpPosDa=2`
5. `--protocol=periodic --tCtrl=60 --periodicJitter=1` (ElqtpFwd FixedPeriodic; **reference only**, not LqarpFwd)
6. `--protocol=elqtp` (reference only)

Scenarios: {I, II, GM2}. Seeds: {20261114, 20261115, 20261116, 20261117, 20261118}.

**Run count:** QELAR 3×3×5 = 45; SAQR 3×3×5 = 45; LQARP 6×3×5 = 90; **total 180** physical runs (no duplicate logical/physical split expected). Stopping rule: if more than 2% of runs fail (rc ≠ 0), stop and report.

### A2.8.4 Viability rule (fixed now; applied after t105 numbers, not by inventing a gate during the run)
A host is **viable** if its **host-alone** mean window PDR is **≥ 0.30** in **Scenario II** on these five tuning seeds.  
A host below 0.30 is **not viable under this channel/MAC** and is **not campaigned** later.  
The rule uses only the host-alone arm (QELAR/SAQR: `--protocol=qelar` / `--protocol=saqr`; LQARP: `--protocol=lqarp` with default piggyback, i.e. arm 1). Other arms are reported for context and for choosing a later no-harm comparator; they do not change the viability threshold.  
**t105 itself reports the Scenario II host-alone mean window PDR (and 95% t interval) numerically beside the 0.30 threshold and does not write the words pass/fail or viable/not viable** (same discipline as t103 on S1/S1′/S2). Application of the label for later campaign inclusion follows this rule after Ali reads t105.

### A2.8.5 What t105 does not do
- No evaluation seeds.
- No retuning of host parameters.
- No MARV re-runs.
- No QELAR/SAQR/LQARP manuscript campaign.
- No interim looking that changes the arm list mid-run.
- Re-run only an identical command that returned rc ≠ 0; report every such case.

---

## Prompt t105 for Grok

t105 - Authority: Ali's approval of Amendment 2 v2.5 (A2.8). p15v2 (exe sha256 e66b8970…7396), `--marvPsatScope=top`, tuning seeds **20261114–18 only**. No evaluation seeds. No gate or viability verdicts in the report text. Ledger lines before and after.

1. Verify binary and lib hashes match A2.8.2. Write the complete job list (host, arm, scenario, seed, full command) to the ledger **BEFORE** the first run, with the job-list sha256.
2. Run the arms listed in A2.8.3 for QELAR, SAQR and LQARP in Scenarios I, II, GM2, five seeds each (all other flags as the t104 common base with the locked geometry in A2.8.2). Single runner; do not start a second campaign process.
3. Report **per cell** (host × arm × scenario, n=5): mean window PDR, window E_bit, O_ctrl, HELLO counts by cause, mean hop count of delivered packets (if reported), delivered and generated packet counts, relay-removal fraction, each with 95% Student-t intervals.
4. For **SAQR** and **LQARP**, additionally report first-contact join/beacon decode counts (or the nearest available counters from run.txt) and whether any run delivered **zero** packets.
5. Report the Scenario II host-alone mean window PDR (n=5) for each of QELAR, SAQR, LQARP next to the A2.8.4 threshold 0.30, **numerically only** (no viable / not-viable / pass / fail wording).
6. STOP. Do not run anything else, do not re-tune, do not touch MARV or evaluation seeds. Write `t105_report.md` with all hashes, per-run CSV, cell tables, and ledger lines.
