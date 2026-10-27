# Amendment 1 to the strengthening pre-registration (FILED)

- **Approved by user 2026-10-12 09:53 CST** (DRAFT v2 approved as drafted, including the LQARP 1.0 pp rationale).
- **Filed:** 2026-10-12 CST. The text is DRAFT v2 verbatim (DRAFT SHA-256 `0e5d0a58aa74dd8345eb7f3cdae9bbc59c8adc264bec1ab3f0ffa878f7e7bf08`), except for this header and the LQARP confirmation note in A4.

- **Drafted:** 2026-10-12, 07:40 CST.
- **Revised:** 2026-10-12, 08:05 CST, after the user review. Decisions 1–4 and fixes F1–F6 are applied; v1 is kept as `strengthening_prereg_amend1_DRAFT_v1.md`.
- **Amends:** `strengthening_prereg.md`, SHA-256 `b0dc38c59e6f01418d700230428946f1cc00808a7ae1cfd722840267a41fdeae` (frozen 2026-10-12 07:05 CST). The frozen file is not edited.
- **On approval:** this text is filed as `strengthening_prereg_amend1.md`, then hashed (`.sha256`) and set read-only.
- **Status at drafting:** no tuning run and no evaluation run exists on any engine.

## A0. Disclosure: a withdrawn earlier version

An earlier Amendment 1 text was filed prematurely, before user review, at 2026-10-12 07:21 CST:
- file `strengthening_prereg_amend1.md`, SHA-256 `2d238736ccfc4fe7edcffc6445cfef79b2a8653e4eee1a1f8fd768c3722793b3`, set read-only;
- at 07:35 CST it was renamed to `strengthening_prereg_amend1_WITHDRAWN_0721.md` (+ `.sha256`) and marked WITHDRAWN;
- it is kept for the record and was **never in force**;
- it contained the SD-dependent margin rule, which this version replaces (A4), and none of A5–A10.

No run of any kind was made under it.

## A1. Simulator: switch to the ns-3 port (user decision, Option A)

**The engine changes.** The frozen prereg named the standalone v2 kernel (`src/v2/uwsim.cc`, commit 42e8692). It is replaced by a port into the **existing ns-3.41 legacy engine**:
- `examples/scenario-i-lqarp.cc`;
- the legacy host and E-LQTP classes in `model/*`.

**Where the work happens:** `${NS3_PORTA}/src`, branch `portA`, which starts from legacy commit `adb0317`. The manuscript code and results (`${WORKSPACE}/novelty`, `${NS3_BASE}`) are never modified.

**What is ported:**
- channel switch (formula | Bellhop);
- shared acoustic medium: SINR errors, interference, half-duplex, propagation delay, finite tx time;
- delay-aware CS-ALOHA, plus pure and slotted ALOHA;
- heard-only discovery with first-contact reply and sink beacon;
- tx/rx/idle/sleep power model and lifetime;
- the T1/T2 topologies and the prereg mobility models.

**Flags:** every feature is behind a flag whose default reproduces the legacy engine.

**Binary identity:** every result and validation claim names:
- the binary path;
- its SHA-256;
- the git commit;
- the exact command.

A check counts as passed only if it ran on the new ns-3 binary.

**v2 code status:** the v2 code is reference only.

## A2. Voided results

The following are **void as evidence** for the paper and for any decision:
- all v2 runs: the pilot, smoke runs, traffic/geometry calibration, unit/validation runs;
- the v2 Aqua-Sim-NG and ns-3 uan comparisons;
- `week1_validation.md`.

The v2 MARV "red flag" is not a finding. It counts only if it re-appears on the new binary.

## A3. What stays frozen

**Hypotheses, margins and kill rules:**
- H1, H1-B0, H2, H3, H4, the decision rules, K1–K5, K-impl, K2b and the go rule: **criteria unchanged**. Only the dates move (A8).
- Metrics, the tuning rule and the grids (prereg §§4–6) are unchanged.

**Energy metrics, as frozen (F3). Nothing is changed here.**
- **Primary energy metric** (frozen prereg §5, line 112): **E_bit_total** = Σ_i E_i / (delivered × 400 bit), with the **S2C** power profile, **including idle**.
- **Where E_bit_total is used:**
  - **H1** uses it: H1(b) superiority on ΔE_bit_total(P − BFB) with the −5 % practical floor. So H1 is a **total-energy** test, not a control-energy test.
  - H1-B0, H2 (MARV and LQARP), H3(b), the K4 criterion and the tuning rule (§6.4) use it too.
- **Pre-specified secondary and decomposition** (frozen prereg §5): reported, but no pass/fail except where a hypothesis names them:
  - E_bit_total split into data tx, data rx, control (HELLO / first-contact / beacon / ACK) tx, control rx, idle and sleep;
  - E_bit_active (tx + rx only);
  - lifetime FND/HND;
  - O_total, the control overhead (H4(b) uses it as its metric);
  - delay, hops, collision fraction.
- Because idle power dominates at S2C, H1 can be met or missed for reasons unrelated to control traffic. The decomposition is therefore reported next to every H1 cell. This is a reporting obligation only; the test is unchanged.

**PHY parameters (prereg §1), unchanged:**
- k = 1.5, Thorp, f_c 24 kHz;
- B 5/2 kHz, R_b 5k/1k;
- Wenz s 0.2 / w 2 m/s (NL 69.79 / 65.77 dB);
- BFSK PER;
- snr_req 11.79 / 8.78 dB;
- lock and CS thresholds at snr_req − 3 dB;
- floor NL − 10 dB;
- R_op 1.2 km;
- SL 134.60 / 127.57 dB, DI 0;
- T_pre 50 ms;
- packet sizes;
- power profiles.

These are already reproduced by the port's PHY unit test (see A10).

**Topology, traffic and run length (prereg §3; D-2…D-6 as conditionally accepted):**
- T1 N = 150, T2 N = 600;
- traffic 1/2400, 1/1200, 1/600 s;
- T_sim 14,400 s, warm-up 900 s.

These were set from v2 calibration runs that are now void. They therefore stand **provisionally** and are re-checked at Gate 3 on the new binary (geometry census, realised routes, host-only/P60 load curves). Any change is filed as Amendment 2, decided only from those geometry/host-only/P60 curves, and **before** any run of an E-LQTP arm.

## A4. Margins (F2: fixed on practical grounds, not from variance)

The prereg §6.1 justified δ from a power calculation, but the σ came from the void v2 pilot. The margins are now **fixed on practical grounds** and are **not** recomputed from any new data.

**MARV: δ_PDR = 0.5 pp.**
- On the legacy engine, HELLO-based delivery beat MARV host-alone by **+1.5 to +4.1 pp** (`decision_aware_report.md`; the gain over host-alone shrank from 5.9 to 2.4 pp with speed: `novelty_report.md`).
- A 0.5 pp margin is ≤ 1/3 of the smallest of these gaps. A P that is non-inferior at 0.5 pp therefore keeps at least about 2/3 of the delivery benefit that HELLOs buy over host-alone (the usual "preserve a fraction of the control effect" rule).

**LQARP: δ_PDR = 1.0 pp.**
- On the legacy engine, no HELLO policy changed LQARP delivery measurably: host-alone vs the HELLO arms gave ΔPDR = 0.00 in 6/6 scenarios, and the periodic/Trickle/speed arms differed by ≤ about 1.1 pp with ±1.4 pp CIs (`decision_aware_report.md`, `novelty_report.md`).
- A 1.0 pp margin tolerates no more loss than the band within which HELLO policies were indistinguishable on LQARP. Tighter is not justified by any known effect, and looser would allow losses as large as the largest observed policy difference.
- *LQARP rationale confirmed by the user 2026-10-12 09:53 CST.*

**Both:**
- The margins come from the legacy (no-MAC) engine, and they are fixed now. They are not re-derived from new-engine data, so that the margin cannot depend on the outcome.
- QELAR/SAQR TOST ±1.0 pp is unchanged.

**Seed count (variance only sets n, never δ):**
- n_eval = min(cap, max(20, ⌈(2.24·1.2·σ̂_tune/δ)²⌉)), with σ̂_tune the paired SD on the 10 tuning seeds (20261112–21) on the new binary, and caps of 100 (MARV) and 160 (LQARP).
- **If a cell hits the cap:** its achieved power at the true difference 0 is computed from σ̂_tune and reported, and the cell is labelled **"underpowered"**. The decision rule does not change: a non-inferiority or superiority claim still needs the CI to exclude the margin. A failed underpowered cell is reported as **"not shown (underpowered)"**, not as evidence of inferiority. For the H1/H2 cell counts it still counts as not passed.
- The n_eval values and achieved powers are written in the hashed tuning addendum.

## A5. Allowed-information list (binding; enforced at run time in `--disc=heard`)

At time t, node i (host routing logic and HELLO policy alike) may use **only** the following.

**I1 — own state:**
- own position, velocity and depth (own navigation);
- own residual energy (own modem accounting);
- own queue, Q-tables, counters, timers, and own advertised values (V, void flag, neighbour count).

**I2 — fields of frames i decoded,** as written by the sender at its transmission start. The header contents are **per host** (F1; the source is in A11):

| frame | fields |
|---|---|
| every frame | sender id, frame kind |
| MARV data header | V, void flag, residual energy, depth (legacy `MarvHeader`, congestion 0) |
| LQARP data header | V-value (max_a Q of the sender) and neighbour count number_{s_j}. **No position, depth or energy.** |
| LQARP initialization broadcast (once per node, in every arm including host-alone, jitter U[0, 30 s]) | position x, y, z, velocity, residual energy |
| QELAR data header | residual energy, group-average energy (legacy `noteHeader`) |
| SAQR data header | residual energy, neighbour count |
| HELLO / first-contact / sink beacon (all hosts) | the full advertised vector of that host, plus position x, y, z and velocity |
| data payload fields | packet id, origin, generation time, hops, visited list, MARV priority list, prevSender/prevTop/parentTx |
| ACK | packet id, addressee; MARV sink ACK adds prevSender/prevTop |

**I3 — PHY measurements of frames i decoded:**
- received SNR (RSSI-based) and decode time.

Also allowed: own receiver's carrier-sense busy/idle state, for the MAC only. Nothing about frames i did not decode, and no SNR of any pair at a time when i decoded nothing.

**I4 — held records:**
- per neighbour j: the last I2/I3 values with timestamps (possibly stale);
- "fresh" iff t − t_last ≤ T_nb;
- aging is allowed: held pose + held velocity × age.

**I5 — a-priori constants:**
- frozen PHY/MAC constants and R_op;
- sink id(s), and the **fixed deployment position of the surface sink(s)**. This is known infrastructure, available to **every host that uses distance to the sink** (SAQR distance-to-sink and ALM; any future host). Approved by the user 2026-10-12.
- **Paper assumption:** "sink positions are fixed and known a priori to all nodes".

**I6 — outcomes of own transmissions:**
- ACK received or timeout (unicast);
- an overheard forward or sink ACK (MARV implicit ack).

**Forbidden:**
- any other node's true current position, depth, velocity, energy or alive state;
- the true neighbour set or true range test;
- the channel table or formula evaluated at a neighbour's true position;
- global counters.

Only the medium/PHY (not hosts, not policies) reads true geometry, to propagate frames.

**"Measured geometry" for MARV in the port:**
- **depth only**, as last advertised in a decoded frame header, aged by t − t_last;
- MARV headers carry no horizontal position, so there is no range test;
- "neighbour" = decoded within T_nb.

Legacy MARV `rangeSet`/`inRange` (true 3-D range) and `qualifies`/`reward` (true depth z_j) are oracle reads. In heard mode they are replaced by held depth and freshness.

**Per host (heard mode):**

- **MARV:**
  - U(i) = fresh j with held z_j < own z, and not held-void (the sink is exempt);
  - D uses held z_j;
  - E_j uses held energy;
  - psat comes from own counts (I6).

- **LQARP (LqarpProtocol and the LQARP part of ElqtpProtocol):**
  - candidate = fresh (any decoded frame within T_nb) and held SNR ≥ threshold;
  - N_range and N_depth on the **held pose/depth from the last initialization broadcast or HELLO**. No pose is held before one is decoded, so the neighbour is not a candidate. The pose is not aged: LQARP has no motion model, so the held pose is zero-order hold, as in legacy;
  - c_lin from held SNR, not the live `linkSample` (legacy reads the live link SNR: an oracle read);
  - c_ene and c_del from held energy/depth;
  - c_nei and max_a Q(s_j, a) from the **held** number_{s_j} and V-value of j's last decoded data header. Legacy reads j's live candidate set and live Q row: oracle reads;
  - no oracle bootstrap: legacy `noteDiscovery` / `noteBootstrap` give a free first-contact hold from the true live gate. It is replaced by the initialization broadcast.
- **E-LQTP on LQARP (decision 2):**
  - heard mode **refuses the scheduler's link trigger**, as it refuses RLOR. The run aborts if `linkTriggerOff = 0` or β₁ ≠ 0 or β₂ ≠ 0.
  - This is recorded as a **removal**, consistent with dropping the Kalman link trigger (legacy novelty/DA campaigns: no routing value, link trigger off in the final arm).
  - A legacy-semantics link trigger is allowed only in an ablation that truly needs it, labelled "idealised signalling (j learns i's filter state for free)".
  - EKF ownership: see A12.

- **QELAR:**
  - candidates = fresh records;
  - group average over fresh records;
  - neighbour energy and group average from held headers.

- **SAQR:**
  - candidates = fresh and closer to the sink (aged held pose vs the I5 sink position, else held depth) and popular (held "number" ≥ mean held "number"; legacy reads the neighbours' true neighbour counts);
  - delay from aged held-pose distance, else SNR-inverted range (frozen formula);
  - ALM from held pose;
  - PLR from own ACK statistics.

- **RLOR:** not a study host. It runs in legacy mode only; heard mode refuses to start it.

**Per policy (heard mode):**

- **Periodic / Trickle / speed-adaptive / TAP / LET / distance / naive A-B / FC-only / E-LQTP DA:**
  - use own state + own held records + own timers only;
  - Trickle inconsistency = own table change (a neighbour entry appears or expires) or own host-state change. Legacy `neighbourSets()` reads the true live gate: an oracle read;
  - speed-adaptive S and LET use held velocities and poses;
  - MARV DA (`daFire`) uses held z of upstream nodes and co-candidates (legacy reads their true depth).

- **The first-contact reply** is triggered only by decoding a frame from an id that is not fresh. The "never-seen vs expired" variant is a diagnosis-phase design decision.

- **Sink beacon** (all arms): the sink broadcasts its pose every 60 s.

- **Decision D-A (resolved 2026-10-12): no link trigger in heard mode.**
  - The proposal "i sends its own HELLO" was rejected: it refreshes j's view of i, not i's stale view of j.
  - The scheduler link trigger is refused in heard mode (see LQARP above, and A12).
  - On MARV, E-LQTP = DA host-state trigger + back-off + first-contact reply. Its `sched_` link part is off (`linkOff = 1`), and `inNStab` is then vacuous (Broadcast_k ≡ 0).

**Runtime enforcement (fails the run with exit code 3, `ORACLE_LEAK <function> <i> <j>`):**
1. Guard flags make every legacy oracle function throw in heard mode when called by a host or policy: `liveInRange`, `liveInSnr`, `noteBootstrap`, peer `linkSample`, `neighbourSets`, MARV `inRange`/`rangeSet`, QELAR/SAQR `inRange`/`neighborCount`, live `distToSink` of j.
2. Poison: before each host/policy call for actor i, the protocol copies of all other nodes' kinematics/energy are set to NaN (own state kept). Finiteness asserts on every Q, reward, score and timer written abort on NaN.
3. Differential unit test: two heard-mode runs with different poison values (NaN vs a seeded ±1e9) must give byte-identical reports.
4. **Positive control (F4):** a test-only flag `--leakTest=<k>` plants a deliberate leak. Each must make the run exit with **code 3**, and the unit suite fails otherwise:
   - k = 1: MARV `qualifies` reads the true z_j;
   - k = 2: LQARP reward reads the live `linkSample`;
   - k = 3: a policy reads the true neighbour set;
   - k = 4: SAQR reads the true neighbour count.
   The flag is refused in tuning and evaluation runs.
5. **Direct-truth audit (F4; grep at drafting, `portA` working tree):**
   - `model/*` has no ns-3 include and no `MobilityModel` or `Simulator` use, except comments.
   - The policy wrapper classes in the scenario (`MarvFwd`, `QelarFwd`, `SaqrFwd`, `LqarpFwd`, `ElqtpFwd`, lines 106–1425) read only `Simulator::Now()` (time, allowed) and their protocol's node copy (poisoned).
   - True positions are read only by `Pipeline`:
     - `SyncPoses`, which writes the protocol copies: in heard mode the actor-only rule + poison applies;
     - mobility and boundary code;
     - `RemoveNode`;
     - the medium's propagation;
     - diagnostics: `liveGeometricPass` / `BfsHopsTo` / `LiveBfsHops` for the `staleHop` / `noteVoid` / `hopDiag` / `GeoCensus` counters.
   - The diagnostics are write-only counters. A unit test checks that turning every diagnostic off leaves all routing outputs byte-identical.
   - Any future direct read from a host or policy is forbidden by code review. A CI grep rejects `GetObject<MobilityModel>` and `GetPosition` outside `Pipeline::{SyncPoses, Place, Advance*, RemoveNode, SweepExits, Med*, diagnostics}`.

## A6. Seeds: burned set and overlap proof

**Burned** (never used for tuning or evaluation in the port):
- 1–20: v2 geometry census, unit/validation, cross-validation seeds 1–5, unit seeds 1/3/7;
- 20260921–20260945: legacy R1 regression and the v2 architecture check;
- **20261001–20261010** (void pilot);
- **20261101–20261105** (v2 load/geometry calibration);
- **20261111**: the v2 smoke runs used this tuning seed.

**Tuning set (approved 2026-10-12):** **20261112–20261121** (10 seeds), replacing 20261111–20. Calibration on the new binary uses **20261106–20261110**. Evaluation stays **20261201+**.

**Overlap proof:**
- (a) `grep` over `${NS3_BASE}-v2`, `${WORKSPACE}/novelty` and `${OUT}` finds the string 202612xx only in planning text: `pilot_power.py` comment, the prereg and the plan. No run manifest, output or script invocation uses it.
- (b) The legacy regression manifest uses only 20260921–45.
- (c) Ad-hoc shell runs are not logged, so to be safe they are covered by the burned ranges above. In the v2 kernel no seed ≥ 20261112 was ever passed. Evidence: the grep, attached to the Gate 1 report.
- (d) From now on, the port's run harness refuses `--seed ≥ 20261201` unless `--eval=1` is given, and `--eval=1` requires the hashed tuning addendum to exist. Every run is appended to a seed ledger (`${OUT}/portA_seed_ledger.jsonl`), so that no overlap can arise.

## A7. Gates

**Gate 1 — regression.** The new binary with all new features off reproduces the 152 audited + 141 novelty configurations bit-identically, or every difference is explained and approved by the user.

Evidence = raw regression output:
- binary path and SHA-256;
- git commit;
- exact command;
- count of matching/compared configurations per class;
- saved log file (`${NS3_PORTA}/results/regression/REPRO.txt` + manifest + stdout log).

- **Allowed differences (F6):** only differences produced by a **named, intended change listed in this table before the Gate 1 run**. Each entry names the change, the commit, the affected configurations and the expected difference. Anything else is a failure, and the gate stops. **Pre-listed intended changes: none.** All new features default to off, so the expected result is 311/311 byte-identical (152 audited + 141 novelty records; the R1 definition, with 18 MARV records having no audited record).
- **Ledger (F6):** **every** Gate 1 run, not only the last, is appended to `${NS3_PORTA}/results/regression/gate1_ledger.jsonl`, with UTC and CST time, commit, dirty flag, binary SHA-256, command, compared/identical counts per class, the list of diffs, the log path and the verdict.
- Gate 1 is re-run after each feature merge. The Gate 1 report cites the whole ledger.

**Gate 2 — all features in.** Every feature implemented and passing its unit tests:
- medium: collision, capture-by-SINR, half-duplex, delay-aware CS, energy bookkeeping exact;
- ARQ;
- heard discovery;
- oracle-leak enforcement (A5);
- position-aware LQARP DA test (K-impl);
- topologies and mobility.

Gate 2 also requires a **stepwise attribution table** (for the paper). Each switch is turned on in sequence on tuning seeds, with the effect recorded on PDR, E_bit_total, O_total and mean realised hops, ±CI:
1. legacy;
2. +medium/MAC (Bellhop T0 slab, oracle discovery);
3. +heard discovery;
4. +power model;
5. +formula channel on T0;
6. +T1 deep topology;
7. +prereg mobility.

Gate 1 must still hold.

**Gate 3 — external validation on the new binary:**
- Aqua-Sim-NG same-configuration comparison (ring/hidden, 4-sender, tight cluster), including the controlled two-sender capture test;
- ALOHA theory;
- TL vs Bellhop on the old slab;
- ns-3 uan energy cross-check;
- discovery latency;
- realised hops from simulated routes (1.2 km / 150-node T1);
- re-check of D-2…D-6 (A3).

## A8. Dates (targets; kill criteria unchanged; target 8 Nov 2026)

| item | old | new |
|---|---|---|
| Gate 1 report | — | Wed 14 Oct |
| Run-time measurement with the medium on: **as early as possible** (first medium-on build), then every feature merge; factorial re-sized | Day 3–4 | first build, then Tue 13 – Wed 14 Oct (re-measured at Gate 2) |
| K-impl (LQARP position-aware DA test implemented + unit-tested) | Day 9 | Fri 23 Oct |
| Gate 2 (all features + attribution table) | — | Fri 23 Oct |
| G1/K1 (V1–V7 tolerance, T1 hops ≥ 3) = Gate 3 | Day 7 (+1 day) | Tue 27 Oct (+1 day: Wed 28 Oct; still failing → Ad Hoc Networks path) |
| Diagnosis phase (tuning seeds only) | — | Wed 28 – Thu 29 Oct |
| Margins recomputed + tuning campaign + hashed tuning addendum (n_eval) | — | Thu 29 Oct – Sun 1 Nov |
| K5 (evaluation started) | Day 11 | Mon 2 Nov (not started → cut per A9; still not by Tue 3 Nov → Ad Hoc Networks) |
| Evaluation runs | — | Mon 2 – Thu 5 Nov |
| G2: H1–H4, K2/K2b/K3/K4, venue go/no-go | — | Fri 6 Nov |
| Paper revision | — | Fri 6 – Sun 8 Nov |

**Run-time rule:**
- If the measured run time puts the planned factorial over about 130 CPU-h, the **factorial is shrunk first** (fewer grid points per baseline, then fewer N-sweep points), before any cut in A9.
- Each shrink is logged with its numbers, and decided before tuning.

**Design changes found in the diagnosis phase (Wed 28 – Thu 29 Oct):**
- A change is allowed only if it is classified BUG or DESIGN CHANGE and logged with its date, and only if re-tuning the affected arms on the **tuning seeds** fits by **Sun 1 Nov**.
- If the re-tuning does not fit, the **pre-change design is evaluated** and the change goes to future work, as a reported item.
- BUG fixes (behaviour not matching the written spec) are always applied. They re-run Gate 1 and the affected unit tests.
- **No design change of any kind** after the tuning addendum is hashed.
- After that point, a discovered bug is fixed only if it does not touch routing or HELLO decisions. Otherwise it is reported and the affected results are flagged.

## A9. Cut order if 8 Nov slips (pre-declared, approved 2026-10-12)

1. Multi-sink T2 and the robustness/OAT sets are cut first.
2. Then QELAR and SAQR (no-harm hosts).
3. Then the N sweep (H4b; H4a stays if affordable).

Never cut: MARV, LQARP, all baselines (B0–B9), P, the position-aware test / H3, Gates 1–3.

**Factorial size:** set from the measured port run time (A8 row 2) by the prereg §8 method, and recorded in the progress log before tuning.

## A10. Implementation status at drafting (for information)

- **`a79dd16` (branch `portA`):**
  - `model/acoustic-phy.{h,cc}` and `model/acoustic-medium.{h,cc}` (ns-3 scheduler injected);
  - `examples/medium-unit-check.cc`: **27/27 PASS** on `${NS3_PORTA}/bin_portA/medium-unit-check`. Covered: PHY constants equal to prereg §1, isolated-link PER, propagation timing, collisions, capture only through SINR / lock-on-first, half-duplex loss/abort, delay-aware vs oracle CS, CS back-off, interference floor, queue cap, exact energy time incl. kill mid-tx.
  - Not yet wired into the scenario.
- **Scenario hooks:** in progress, uncommitted, not yet built. No Gate 1 run yet on a binary containing scenario changes.

## A11. LQARP header contents (F1): authoritative answer for this study

**Sources checked:**
- The published paper: Liu, Zhou, Ji, Qu, Qiao, "Link Quality-Aware Routing Protocol for Underwater Wireless Sensor Networks: A Reinforcement Learning Approach", IEEE IoT-J 13(17):39561–39576, 2026.
  - The full text is **not available** on the box or to the web tools (paywalled). Only the abstract was found (eurekamag, exa.ai, ORCID listings).
  - The abstract says decisions use "link quality, current node state, and neighboring information" but does not give the packet format.
  - **The published header format is therefore not verified.**
- The legacy code (the validated LQARP host, `model/lqarp-protocol.cc`, SPEC Part 1.1):
  - position, depth and energy of j come from a hold written **once** at first contact (`noteDiscovery` → `ingestHold`) and are never refreshed. So **position is not piggybacked**, and energy and depth are not either;
  - number_{s_j} (`numberSj` = |I(s_j)|) and max_a Q(s_j, a) (`updateQ`) are read **live from j's state**, i.e. as if carried fresh in every header.

**Authoritative answer used in the port and the paper:** the legacy-code semantics, made physical:
- **V-value and neighbour count are in LQARP data headers.**
- **Position, depth, velocity and energy** come only from the one initialization broadcast (and from HELLOs, in HELLO arms).

**Consistency:**
- **The amendment:** I2 table above. It corrects v1's "every header carries depth/energy".
- **The port:** LQARP header = {V, number}; initialization broadcast = {pose, velocity, energy}.
- **The manuscript:**
  - Sec V-D (`sec5_evaluation.tex` l.231–232: "held neighbor positions, which it does not piggyback") is correct.
  - Sec II Table I (`sec2_related.tex` l.30: "Header fields (position, energy, V-value, neighbor count); one initialization broadcast") is **wrong on position and energy**. Proposed correction: "Header fields (V-value, neighbor count); position and energy from one initialization broadcast."
  - Paper note: "LQARP header contents follow our implementation of [liu2026lqarp]; position and energy are taken from the initialization broadcast." This should be checked against the published full text before submission.
- **H3** (LQARP stale pose) depends on position being held and not piggybacked. That is now the stated model.

## A12. EKF ownership (answer to decision 2)

**The EKF is not part of the LQARP host.** Plain LQARP (`LqarpProtocol`) has no filter, only zero-order frozen holds. The EKF (`NeighborEkf` in `ElqtpProtocol`) is E-LQTP's link module. It feeds two things:
1. **The scheduler part:** Broadcast_k(i, j) and the link-HELLO rising-edge rule (`recomputeBroadcast`, `countEventControlTx`). This is removed in heard mode.
2. **Three E-LQTP additions on the host side:**
   - the N_stab candidate filter: Broadcast_k = 0 or |e_k| < ε_k;
   - c_pred: β₁ · max(0, e_k − e_{k−1}) added to c_lin;
   - c_drift: β₂ · sigmoid(−v_rel) applied to c_del.

These additions are E-LQTP's, not LQARP's (SPEC Part 2).

With the link trigger removed:
- Broadcast_k ≡ 0, so N_stab admits every candidate;
- β₁ = β₂ = 0 is the frozen default (identity), so c_pred = c_lin and c_drift = c_del.

The EKF therefore has **no effect on any decision**. Heard mode does not run it, and it asserts β₁ = β₂ = 0. The removal thus covers the scheduler part, and leaves the host-side additions at their frozen identity values. Re-enabling β₁/β₂ would be a design change needing its own unit tests and the A8 rule.
