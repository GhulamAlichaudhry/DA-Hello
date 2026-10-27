# E-LQTP strengthening: pre-registration (frozen before any tuning run)

- **Frozen:** 2026-10-12, CST (UTC+8). The SHA-256 of this file is recorded in `strengthening_prereg.sha256` and in `week1_progress.md`.
- **Scope:** the IEEE IoT-J strengthening campaign in `strengthening_plan.md` (v. 2026-10-12, with the §13 amendments).
- **Status at freeze:** no tuning run and no evaluation run has been made. The only protocol runs so far are:
  - smoke runs (1 seed per arm);
  - the traffic operating-point calibration (seeds 20261101–03);
  - a pilot for the power calculation (seeds 20261001–10; never reused).
- **Changes after this point:** any change is a dated amendment (§9) with a reason. It must never be made because of evaluation-seed results.
- **Code at freeze:**
  - source: `${NS3_BASE}-v2/src`, git branch `v2-week1`, commit `42e8692`;
  - `src/v2/uwsim.cc` sha256 `718dfe5ccf74b076dbc4796a35e33f813808cdef7d6d742ff1bf5c5ef8dd1ebf`;
  - `src/v2/phy.h` sha256 `a7839b265dab3665e7f4130b02158becc4cba8494f75b3ffaba3297792abc00f`.

  Binaries are rebuilt with `build_v2.sh`, and the binary hash is logged in every campaign's PROVENANCE.

## 1. Channel and PHY (frozen; binding condition 2)

| parameter | primary P-5k | stress P-1k | note |
|---|---|---|---|
| channel (T1, T2) | formula: TL = 10·k·log₁₀d + α(f)·d/1000 | same | **range-dependent only**: no multipath, refraction or shadow zones (Deviation D-1) |
| spreading k | 1.5 | 1.5 | practical spreading |
| absorption α | Thorp (1967), f in kHz: 0.11f²/(1+f²) + 44f²/(4100+f²) + 2.75·10⁻⁴f² + 0.003 dB/km; α(24 kHz) = 5.69 dB/km | same | — |
| carrier f_c / band B | 24 kHz / 5 kHz | 24 kHz / 2 kHz | inside the EvoLogics S2C R 18/34 band; 2 kHz is the legacy grid |
| bit rate R_b | 5,000 bit/s | 1,000 bit/s | — |
| noise NL | Wenz band-integrated (turbulence + shipping s = 0.2 + wind w = 2 m/s + thermal) = **69.79 dB** re µPa | **65.77 dB** (equal to the stored Bellhop-table NL) | robustness: s = 0.5, **w = 3 m/s** (NL 72.16 dB at 5 kHz), with SL held at the primary value (D-6) |
| PER model | non-coherent BFSK: BER = ½·exp(−γ_b/2), γ_b = SINR·B/R_b, PER = 1 − (1 − BER)^L; worst-case SINR over the packet | same | robustness: hard threshold SINR ≥ snr_req |
| decode reference snr_req | 11.79 dB (noise-only PER = 10 % at 400 bit) | 8.78 dB | — |
| lock / detection threshold | snr_req − 3 dB | same | the PHY locks (and pays RX energy) above this |
| carrier-sense threshold | snr_req − 3 dB, on the signal **actually arriving** at the sensing node (delay-aware) | same | — |
| interference floor | arrivals with RL < NL − 10 dB are ignored | same | — |
| R_op (SL calibration) | 1.2 km | 1.2 km | SL calibrated once so that SNR(R_op) = snr_req |
| source level SL | **134.60 dB** re µPa @ 1 m | **127.57 dB** | frozen numbers; not recalibrated in robustness runs |
| directivity DI | 0 dB | 0 dB | omni |
| preamble T_pre | 50 ms (robustness 0 and 300 ms) | 50 ms | — |
| packet sizes | data 400, HELLO / first-contact / sink beacon 400, ACK 64 bit | same | — |
| sound speed | 1500 m/s | same | — |
| Bellhop path (T0, regression) | `--chan=bellhop`, legacy January table (SL 175 dB baked in, 2 kHz NL 65.77 dB, source depth 50 m) | — | regression, T0 robustness, and the stepwise attribution |

**Stepwise attribution chain (binding condition 1), run with key arms at the default cell:**
1. legacy (R1, bit-exact);
2. T0 Bellhop slab + new MAC/discovery/power;
3. T0 slab with the formula channel;
4. T1 deep column.

## 2. MAC, discovery and power (frozen)

**MAC primary:** ALOHA with delay-aware carrier sense.
- Exponential back-off U[0, T_pkt + R_op/c]·2^{min(n−1,4)}, at most 5 busy attempts, then a MAC drop.
- FIFO queue of 20.
- Unicast hosts use a link ACK (64 bit) with up to 4 attempts. MARV uses implicit acknowledgment by overhearing.
- **Pure ALOHA (no CS) is always run as a comparison arm** in the robustness set, for key arms (binding condition 4).
- Slotted ALOHA (guard τ_max) is run on the unicast hosts only.

**Half-duplex:** transmitting blocks reception, and starting a transmission aborts an ongoing reception. There is no capture beyond SINR. A receiver locks on the first arrival above the lock threshold.

**Discovery:** heard-only (`--disc=heard`). Tables start empty.
- T_nb = 3 × data interval, the same for all arms of a cell.
- First-contact reply: jitter U[0, 2 s], at most one per 5 s per node, cancelled by piggyback.
- Join window 30 s.
- A sink beacon (400 bit) every 60 s is common infrastructure in **all** arms, host-alone included.

**Power profiles** (W: tx / rx / idle / sleep):

| profile | tx | rx | idle | sleep | role |
|---|---|---|---|---|---|
| S2C | 8 | 1.3 | 0.285 | 0.0025 | primary |
| S2C-tx28 | 2.8 | 1.3 | 0.285 | 0.0025 | sensitivity |
| S2C-wake | 8 | 1.3 | 0.005 | 0.0025 | idle-cheap bound |
| WHOI-2 | 10 | 2.3 | 0.08 | 0.000165 | robustness |
| ns3mm | 50 | 0.158 | 0.158 | 0.0058 | cross-check |
| legacy | 5.26 | 0.1 | 0 | 0 | regression |

E_batt = 864 kJ.

## 3. Topology, traffic and run length (frozen after the geometry/load calibration)

**T1 (primary):** 4 × 4 × 3.5 km, **N = 150**, one surface sink at the XY centre (transducer at 50 m), nodes uniform in z ∈ [10, 3500] m, R_op = 1.2 km.
- Census, 20 placements: depth-monotone mean hops **3.13**, p90 4.25, depth-unreachable **10.9 %**, any-path disconnected 0.0 %, degree **13.0**.
- Against the gate targets: mean hops ≥ 3 met; degree 10–25 met; p90 ≥ 5 **not met**; < 5 % depth-disconnected **not met** (D-3).

**T2 (generality):** 8 × 8 × 3.5 km, **N = 600** (density-matched to T1), 4 surface sinks at the quadrant centres, anycast delivery.
- Census, 20 placements: mean hops 3.24, p90 4.8, depth-unreachable 8.9 %, degree 15.1.

**T0 (regression and bridge):** legacy 6 × 6 × 0.5 km slab, Bellhop channel, N = 41.

**N scaling (T1):** {70, 100, 150, 200}.

**Traffic** (periodic per node, random phase), set from the host-only and P60 calibration at N = 150 (Deviation D-4):
- low = 1 packet / 2400 s;
- **mid = 1 / 1200 s (default)**;
- high = 1 / 600 s.

**Mobility:**
- I static;
- **II** (default): Gauss–Markov drift, σ_xy 0.3 m/s, ζ 0.85, mean 0.3 m/s, heave σ_w 0.05 m/s;
- **GM2**: as II with σ_xy = 2 m/s;
- **WP**: 10 % of nodes are AUVs on 3-D waypoints at 1.5 m/s;
- HD as robustness.

**Run length:** T_sim = 14,400 s, a 300 s drain, warm-up 900 s. Metrics count packets generated in [900, 14,400) s.

## 4. Seeds (item e)

- Pilot (power calculation only, never reused): 20261001–10.
- Calibration (geometry/traffic): 20261101–05.
- **Tuning: 20261111–20 (10 seeds).** These are used only for grid selection and are never reported as evidence.
- **Evaluation:** 20261201 onward. The number per cell is set by the rule in §6.2 from tuning-seed variance, with a minimum of 20.

## 5. Metrics

**Primary energy metric (item a):** E_bit_total = Σ_i E_i / (delivered × 400 bit) with the S2C profile, **including idle**. It is decomposed into data/control tx/rx, idle and sleep.

**Secondary:**
- E_bit_active;
- lifetime FND/HND (extrapolated);
- O_total;
- delay;
- hops;
- collision fraction.

**Primary delivery metric:** PDR of packets generated in the measurement window.

## 6. Hypotheses, tests and decision rules (items b, g)

**Common rules:**
- Unit = seed. Paired differences arm − comparator on the same seed.
- Tests: two-sided 95 % Student-t CIs (one-sided α = 0.025 for non-inferiority/superiority).
- "Holm" means Holm–Bonferroni over the cells named in that hypothesis.
- BFB (best feasible baseline) = the one of {B1* P_feas, B2* tuned Trickle, B5 LET} with the lowest tuning-seed E_bit_total in that cell.

### 6.1 Margins (item b), power calculation

The pilot (MARV and LQARP, T1, N = 150, mid, 5k, II and GM2, 10 seeds) gives these paired SDs of ΔPDR(P − comparator):
- **MARV 0.72–1.61 pp** (median ≈ 0.95 pp);
- **LQARP 2.99–4.42 pp**.

Required n per cell for non-inferiority at a true difference of 0, with one-sided α = 0.025 and per-cell power 1 − β, is n = ((z₀.₉₇₅ + z₁₋β)·σ/δ)².

**MARV: δ_PDR = 0.5 pp (tightened from 1.0 pp).**
- With σ = 1.0 pp and per-cell power 0.90 (z = 1.28), n = 42. With σ = 1.6 pp, n = 108.
- At about 2 s per run, this is affordable.

**LQARP: δ_PDR = 1.0 pp (kept).**
- With σ = 3.8 pp, δ = 0.5 pp would need n ≈ 600 per cell, which is infeasible. δ = 1.0 pp needs n ≈ 150 at 0.90 power.
- The power calculation therefore does **not** support a tighter margin for LQARP. Reported as sensitivity: LQARP at 0.5 pp, MARV at 1.0 pp.

**No-harm hosts (QELAR, SAQR):** TOST, ±1.0 pp PDR and ±5 % E_bit_total.

**Practical-significance floor for energy claims:** a relative saving ≥ 5 % of the comparator's E_bit_total.

### 6.2 Adaptive seed count (fixed rule)

For each host × cell × primary comparison:

n_eval = min(cap, max(20, ⌈(2.24·1.2·σ̂_tune/δ)²⌉)), where:
- σ̂_tune is the paired SD on the 10 tuning seeds;
- 1.2 is a variance-inflation factor;
- 2.24 = z₀.₉₇₅ + z₀.₈₀;
- the cap is 100 for MARV and 160 for LQARP.

The n_eval values are written in the tuning addendum before any evaluation run. A cell that hits the cap is reported with its achieved power.

### 6.3 Hypotheses

**H1 (primary, MARV): efficiency at equal delivery vs BFB.** Mobile core cells: {II, GM2, WP} × {low, mid, high} × {5k, 1k} = 18 cells.
- (a) Non-inferiority: lower 95 % CI of ΔPDR(P − BFB) > −0.5 pp in **all 18 cells** (intersection–union test; no correction).
- (b) Superiority: ΔE_bit_total(P − BFB) < 0 with a Holm-adjusted one-sided p < 0.025 over the 18 cells **and** a point estimate ≤ −5 %, in **≥ 14/18 cells**.
- **Pass** iff (a) and (b) both hold.

**H1-B0 (binding framing check, MARV):** the same tests with the comparator **B0 host-alone** in place of BFB.
- If host-alone is non-inferior to P in PDR (lower CI of PDR(B0 − P) > −0.5 pp) **and** cheaper (E_bit_total(B0 − P) upper CI < 0) in ≥ 9/18 cells, the paper must say that on MARV, under this model, sending no HELLO beyond the common sink beacon is at least as good. It must also restrict P's claim to the cells where B0 fails.

  Pilot note (not evidence; untuned): P − B0 = −1.7 pp PDR and +9 % E in II/GM2-mid-5k.

**H2 (beyond the naive rules).** The first-contact-only baseline **B9 FC-only** (first-contact reply + join, no other HELLO; item c) and B8 Naive-B are both comparators.
- **H2-MARV:**
  - P is non-inferior to **B9** (δ 0.5 pp) in all 18 mobile cells;
  - **and** P is superior to B9 on PDR (lower CI > 0) or on E_bit_total (upper CI < 0 with point ≤ −5 %), Holm over the 18 cells, in **≥ 6/18 cells**.
  - Pilot note: P − B9 = −0.5 pp and +0.8 % E (the DA events added nothing on MARV, untuned).
- **H2-LQARP:** on WP × 3 loads × 2 rates (6 cells), P beats both B8 and B9 on PDR (lower CI > 0) or on E_bit_total (upper CI < 0, point ≤ −5 %, with PDR non-inferior at 1.0 pp). Holm over 6 cells, in ≥ 4/6 cells.
  - **Prerequisite:** the position-aware LQARP DA test (§2.3 of the plan) must be implemented and unit-tested before tuning. At freeze, P ≡ B9 on LQARP, because the LQARP membership/rank events are not yet implemented in v2.
- **Pass** iff H2-LQARP passes. H2-MARV is reported either way.

**H3 (LQARP stale pose and component relevance).** WP and GM2, mid, 5k:
- (a) the upper CI of Δhops(P − B1*) < +0.3 and the upper CI of Δdelay < +0.5 s;
- (b) the upper CI of ΔE_bit_total(P − B1*) < 0;
- (c) membership or rank events ≥ 1 per node-hour, **and** the "−position test" ablation is worse on hops (CI excludes 0).
- **Pass** iff (a)–(c) hold in both mobilities (intersection–union).

**H4 (MAC load and scaling).**
- (a) At P-1k, [ΔPDR(P − B1(T = 20 s)) at high] − [the same at low] > 0, tested by a paired-seed t-test on the difference of differences, lower CI > 0, in ≥ 2 of the 3 mobile mobilities (Holm over 3).
- (b) Per seed, the slope of log O_total vs log N over N ∈ {70, 100, 150, 200}. Pass iff the upper CI of slope(P) − slope(B2*) < 0.1 **and** the upper CI of O_total(P) − O_total(B2*) at N = 200 < 0, in II and GM2.
- H4 passes iff (a) and (b) pass. If (b) fails, the scaling claim is dropped (not a kill).

**Secondary (no pass/fail):**
- QELAR/SAQR TOST equivalence to host-alone;
- lifetime;
- T2, T0 and the stepwise attribution;
- robustness OAT (pure ALOHA, slotted, threshold PER, S2C-wake, WHOI-2, noise w = 3 m/s, T_pre 0/300 ms, July Bellhop on T0);
- delay; discovery latency; event counts.

### 6.4 Tuning rule (unchanged in substance)

Per host × topology × traffic × rate cell, pooled over the tuning mobilities {II, GM2}, on the tuning seeds:
- pick the grid point with the lowest E_bit_total among points with mean PDR ≥ PDR* − δ_host (δ_MARV 0.5 pp, δ_LQARP 1.0 pp), where PDR* is the best mean PDR of any grid point of any arm;
- if none qualifies, pick the highest-PDR point.
- E-LQTP gets a grid of comparable size and the same rule. All grid points are shown (Pareto cloud).

**Grids:** as plan §3.
- B1 T ∈ {5, 10, 20, 40, 60, 120, 300};
- B1* = the smallest T with collision-loss ≤ 10 % and PDR within 0.5 pp of the best P_T;
- B2* = 72 Trickle configurations;
- B3 r_opt ∈ {paper, ×0.5, ×2};
- B4 a ∈ {0.05, 0.1, 0.2};
- B5 τ ∈ {0.1, 0.25, 0.5};
- B6 d_th ∈ {100, 250, 500} m × T_max ∈ {120, 300} s;
- B7/B8 T_r ∈ {60, 120, 300, (∞ for B8)};
- **B9 FC-only: no parameters** (fixed jitter 2 s, gap 5 s);
- P: m ∈ {0, 0.005, 0.02} × T_r ∈ {∞, 300} × I_max ∈ {80, 320}.

## 7. Kill criteria and gates (item g)

| id | when | criterion | consequence |
|---|---|---|---|
| G1/K1 | Day 7 | any of V1–V7 fails its stated tolerance (week1_validation.md), or T1 mean hops < 3.0 | one extra day; if still failing on Day 8: Ad Hoc Networks path |
| K-impl | before tuning | the LQARP position-aware DA test is not implemented and unit-tested by Day 9 | H2-LQARP and H3 are untestable: the IoT-J go rule cannot be met; venue Ad Hoc Networks |
| K2 | G2 | H1 fails | Ad Hoc Networks / Computer Networks, as a careful negative-to-modest result |
| K2b | G2 | H1-B0 triggers (host-alone ≥ P in ≥ 9/18 MARV cells) **and** H2-LQARP fails | no "energy-saving HELLO scheduler" claim. The paper is reframed as "when do HELLOs matter for Q-learning UWSN routers under a MAC" (Ad Hoc Networks) |
| K3 | G2 | H2-LQARP fails and H2-MARV fails its superiority part | the method reduces to first-contact + void-flip; Ad Hoc Networks or a Letters version |
| K4 | G2 | every relative E_bit_total saving vs BFB is < 5 % in all 18 MARV cells and all 6 LQARP WP cells | not compelling for IoT-J; reposition |
| K5 | Day 11 | the evaluation has not started | cut T2, P-1k and robustness; Day 12 → Ad Hoc Networks |

**IoT-J go rule:** H1 passes, at least one of {H2-LQARP, H3} passes, and none of K2b, K4 triggers.

## 8. Compute budget (item d, measured 2026-10-12)

Measured wall time per run (one core):

| run | s/run |
|---|---:|
| T1, N = 150, 4 h + drain, mid, 5k: host-only / E-LQTP | 0.5–0.9 |
| P20 | 2.9 |
| P5 at high load | 11 |
| P5 at P-1k, high load | 7.8 |
| T2, N = 600, P20, high load | 35 |

Peak memory is < 10 MB. Re-budgeted: a ≈ 3 s mean for T1 runs (including the adaptive seed counts up to the caps) gives ≈ 60–80 CPU-h, i.e. ≈ 10 h wall at 7 workers. T2 adds ≈ 8 CPU-h. This is inside the plan's 130 CPU-h, so no cut is needed.

## 9. Deviations and amendments (dated)

- **D-1, 2026-10-12:** Bellhop → formula channel for T1/T2. The channel is range-dependent only. Bellhop is kept selectable.
- **D-2, 2026-10-12:** R_op 1.5 → 1.2 km. This is inside the plan's allowed set {1.2, 1.5, 1.8}, chosen by the geometry gate.
- **D-3, 2026-10-12:** T1 N 100 → 150 (the plan allows changing N after R_op). The geometry targets p90 ≥ 5 and < 5 % depth-disconnected are not achievable together with mean hops ≥ 3 and degree ≥ 10 in this volume (scan in week1_validation.md). Accepted: depth-unreachable nodes (≈ 11 %) are a void fraction common to all arms. T2 N = 600 is density-matched. The scaling set becomes {70, 100, 150, 200}.
- **D-4, 2026-10-12:** traffic levels {1/300, 1/60, 1/20 s} → {1/2400, 1/1200, 1/600 s}. The plan's local-load estimate (≈ 0.2 Erlang at 1/60 s) assumed a neighbourhood interference domain. In T1 the interference range covers almost the whole volume, and host-only PDR at 1/60 s was 7–17 %. The new levels are chosen from host-only/P60 PDR curves (calibration seeds), not from any E-LQTP result.
- **D-5, 2026-10-12:** T_sim 3,600 → 14,400 s and warm-up 300 → 900 s, to keep ≥ 1,000 measured packets per run at low traffic. This is affordable given the measured run time.
- **D-6, 2026-10-12:** noise robustness w = 10 → 3 m/s. At fixed SL, w = 10 m/s raises NL by 13 dB and shrinks R_op to 0.34 km, which disconnects T1. w = 3 m/s (+2.4 dB, R_op 1.0 km) is a meaningful stress that still delivers.
- **D-7, 2026-10-12:** a B9 FC-only baseline is added (item c). The non-inferiority margin for MARV is 0.5 pp (item b). The H1-B0 framing check and K-impl/K2b are added (item g).
