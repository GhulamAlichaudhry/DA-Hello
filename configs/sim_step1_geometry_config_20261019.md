# Simulation Step 1 — Geometry and Configuration Sheet
## For Ali to execute himself (supervised hand-off)

**Date:** 2026-10-19 (CST, UTC+8)  
**Request (t79, verbatim):** "lets start simulation from scracth, please do write the simulation configureation and geometry parametrs, you have to supervise, i wil do all by my self. start from very 1st step"  
**Authority:** Ali ([AUTHOR]). Message from Windows machine `[MACHINE_ID]`.  
**Role of this sheet:** configuration and geometry only. Ali runs every command on his own machine (or a fresh bot). The supervisor writes this sheet; it does not run simulations, install packages, or change code, prereg, amendments, or the ledger.  
**Code of record:** DA-HELLO Port A, commit `9d9c286c34ffbe9e2d12dce877c9d54afb8b70fd` (branch `p13-baselines`).  
**Frozen documents:**  
- `strengthening_prereg.md` sha256 `b0dc38c59e6f01418d700230428946f1cc00808a7ae1cfd722840267a41fdeae`  
- `strengthening_prereg_amend1.md` sha256 `45a8e975a31b3e02aeac0bb4cbee44287cdaa1c96accae9d1724aa2413fda8c0`  
(both under `${HOME_BOX}/restore_check/out` on the shared box; copy them to your machine and verify).  
**Companion plan (do not rewrite):** `${WORKSPACE}/simulation_plan_20261018.md` (sha256 `c44e75a7…`). Use it for rebuild/bundle details; this sheet is Step 1 only.  
**Rule:** every number below was checked against the p13 source or the frozen prereg on 2026-10-19. Anything not found is marked **UNVERIFIED**.

---

## Step 1.1 — Geometry (topology T1)

| Quantity | Value | Source |
|---|---|---|
| Domain (box) | **4000 × 4000 × 3500 m** | `scenario-i-lqarp.cc:8359–8363` (`--topo=t1` sets `kVolumeXyM=4000`, `kVolumeZM=3500`) |
| Default before `--topo=t1` | 6000 × 6000 × 500 m (legacy slab) | `scenario-i-lqarp.cc:61–62` |
| Stale report literal | The report header still prints `volume=6000x6000x500 m` | `scenario-i-lqarp.cc:5724`. **Ignore it.** The simulated box under `--topo=t1` is 4000×4000×3500 m. |
| Topology label | **T1** | Prereg §3; flag `--topo=t1` |
| Number of nodes | **N = 150** = 149 relays + 1 sink | Cells use `--nAuv=149`. Prereg §3: “N = 150”. |
| Relay placement | Uniform in [0, 4000]² × [10, 3500] m; reject if closer than 1 m to any already placed node | `scenario-i-lqarp.cc:2836–2849`; `kMinAuvDepthM=10`, `kMinSepM=1` (`:63–65`) |
| Sink kinematic position | **(2000, 2000, 5) m** — XY centre of the 4000 m box, hull depth 5 m | `SamplePositions`: `Vector(kVolumeXyM*0.5, kVolumeXyM*0.5, kSinkDepthM)` (`:2836`); `kSinkDepthM=5` (`:64`) |
| Sink transducer (acoustic) depth | **50 m** | `sinkAcousticDepthM=50` (`scenario-i-lqarp.cc:2220`); used by the medium in `MedPos` (`scenario-porta.inc:91–97`: for the sink, `z = sinkAcousticDepthM`) |
| Eddy centre | **(3000, 3000) m** — off the box centre and off the sink | `model/gauss-markov.h:43–46` (`cx=3000`, `cy=3000`, `R=2000`) |
| Eddy core radius | **2000 m** | same |
| Eddy sense | Counter-clockwise Rankine; \|v\| = V · min(r/R, 1); vz = 0 | `gauss-markov.h:48–50` |
| Vertical motion | **σ_z = 0** (no heave) | `scenario-i-lqarp.cc:2938` (“permanent, SPEC Part 9”) |
| Field **I** (static) | `--scenario=i` | — |
| Field **II** (default mobile) | `--scenario=iii --driftSpeed=0.3 --gmSigma=0.3 --gmZeta=0.85` | Eddy speed V = 0.3 m/s; GM σ_xy = 0.3 m/s; memory ζ = 0.85 |
| Field **GM2** | Same as II with `--gmSigma=2` | σ_xy = 2 m/s |
| Field **WP** (label) | II flags plus `--nWpAuv=15 --wpSpeed=1.5` | See limitation below |
| Waypoint limitation | `--nWpAuv` is applied **only** under `--scenario=ii`. Under Scenario III (eddy), waypoints are inactive: `AdvanceEddyTo` returns early to waypoints only if `isScenarioII() && nWpAuv > 0` (`scenario-i-lqarp.cc:5321–5325`). Under the study mobility, **WP cells reproduce II**. Disclose this; do not treat WP as a different field unless the code is changed (needs Ali’s approval). |
| Removal | A relay that leaves [0, L]² is removed with its held packets and generates no further traffic | `scenario-i-lqarp.cc:5155–5166` |
| Removal rate (II, disclosed) | At T_sim = 14400 s, exited/149 over five burned diagnostic seeds 20269101–05: **100, 96, 102, 96, 93** → mean **65.4 %** (range **62.4–68.5 %**). Report as “62–69 %, mean 65 %”. | Evidence: `${WORKSPACE}/DA-HELLO-porta/results/gate2_bl_20261016T061621Z/` (mobility code identical in that binary and p13). GM2/WP rates: **UNVERIFIED** (not measured). |

**Sink coordinates (verified):** under `--topo=t1`, the sink is at **(2000, 2000) m** horizontally; kinematic depth **5 m**; acoustic query depth **50 m**.

---

## Step 1.2 — Acoustic channel and PHY

Primary configuration = formula channel (Bellhop is a switch only).

| Parameter | Primary (5 kbit/s) | Low-rate (1 kbit/s) | Source |
|---|---|---|---|
| Channel | `--chan=formula` (TL = 10 κ log₁₀ d + α(f)·d/1000) | same | `acoustic-phy.h:21–40`; prereg §1 |
| Spreading κ | 1.5 | 1.5 | `acoustic-phy.h:27` |
| Absorption α | Thorp (1967); α(24 kHz) = 5.69 dB/km | same | `acoustic-phy.cc:12`; prereg §1 |
| Carrier f_c | 24 kHz | 24 kHz | `acoustic-phy.h:24` |
| Bandwidth B | 5 kHz | 2 kHz (`--phyB=2`) | `acoustic-phy.h:25`; CLI `--phyB` |
| Bit rate R_b | **5000 bit/s** | **1000 bit/s** (`--phyRb=1000`) | `acoustic-phy.h:26`; CLI `--phyRb` |
| Noise | Wenz, shipping s = 0.2, wind w = 2 m/s | same | `acoustic-phy.h:32–33` |
| NL (printed) | **69.79 dB** re µPa | **65.77 dB** (prereg) | P smoke: `nl_db=69.791` (`p12_P_…/P_smoke_1800.txt`); 1 kbit/s NL at scenario level: **UNVERIFIED** (flag exists; never run at T1) |
| PER | Non-coherent BFSK: BER = ½ exp(−γ_b/2), PER = 1 − (1 − BER)^L | same | `acoustic-phy.cc:84–85` |
| snr_req | **11.79 dB** (noise-only PER = 10 % at 400 bit) | **8.78 dB** | P smoke: `snr_req_db=11.789`; prereg §1 |
| SL (calibrated once so SNR(R_op) = snr_req) | **134.60 dB** | **127.57 dB** | P smoke: `sl_db=134.597`; prereg §1 |
| R_op | 1200 m | 1200 m | `acoustic-phy.h:30` |
| DI | 0 dB (omni) | 0 | `acoustic-phy.h:29` |
| Lock / CS threshold | snr_req − 3 dB | same | `acoustic-phy.h:36–37` (`detDelta`, `csDelta`) |
| Interference floor | RL < NL − 10 dB ignored | same | `acoustic-phy.h:38` (`floorDb=-10`) |
| Preamble T_pre | **50 ms** | 50 ms | `acoustic-phy.h:39` (`tPre=0.05`) |
| Sound speed c | 1500 m/s | 1500 | `acoustic-phy.h:40` |
| Data / HELLO / beacon | **400 bit** | 400 | `scenario-i-lqarp.cc:2425` (`dataBits`, `helloBits`) |
| ACK | **64 bit** | 64 | `scenario-i-lqarp.cc:2425` (`ackBits`) |
| Bellhop | `--chan=bellhop` only for regression / cross-check | — | Not used in primary T1 cells |

---

## Step 1.3 — Shared medium and MAC

| Parameter | Value | Source |
|---|---|---|
| MAC | **CS-ALOHA** (`--mac=cs`): ALOHA with delay-aware carrier sense | `acoustic-medium.h:40–46`; prereg §2 |
| Back-off | U[0, T_pkt + R_op/c] · 2^min(n−1, 4) after the n-th busy attempt; drop after **5** busy attempts | `acoustic-medium.cc:172–186`; `boMaxAttempts=5` (`acoustic-medium.h:64`) |
| Queue | FIFO capacity **20** | `acoustic-medium.h:63`; `scenario-i-lqarp.cc:2426` |
| ARQ | Unicast: up to **4** transmission attempts (`arqMax=4`) | `scenario-i-lqarp.cc:2426` |
| MARV ACK | Implicit: overhear the next forwarder (`--marvSinkOverheard=ack` for sink acceptance) | Required for all `marv-*` arms |
| Sink ACK | When `marvSinkOverheard=ack` and the medium is on, the sink emits a 64-bit ACK on accepting a packet | `scenario-i-lqarp.cc:4988–4995` |
| Half-duplex | Transmitting blocks reception; starting Tx aborts Rx | Medium model |
| Comparison MACs (later) | `--mac=aloha` (pure), `--mac=slotted` | Available; not used in Step 1 smoke |

---

## Step 1.4 — Discovery (heard-only)

| Parameter | Value | Source / note |
|---|---|---|
| Mode | `--disc=heard` (tables start empty; learn only by decoding) | Prereg §2; amend1 A5 |
| Freshness T_nb | Default **3600 s** (= 3 × mid data interval 1200 s). Set explicitly with load: low `--tNb=7200`, mid `3600`, high `1800` | `scenario-i-lqarp.cc:2364` |
| Join HELLO | One HELLO per node at time ~ U[0, **10**] s | `scenario-porta.inc:175–191`. **Deviation:** prereg §2 and amend1 A5 say 30 s / U[0,30 s]. Disclose; do not “fix” without Ali’s approval. |
| Sink beacon | `--sinkBeacon=60` (every 60 s from t = 60 s), all arms including host alone | `scenario-porta.inc:193–207`; prereg §2 |
| First-contact reply | On decode of a non-ACK frame from a node **never heard** before: schedule one HELLO after U[0, **2**] s, at most one per **5** s; one pending reply serves all newcomers; cancelled if the node first starts any other own state-carrying frame | `scenario-porta.inc:806–827`, `:280–287`; `comparators.h` FcOnDecode/FcCancel. Enabled for P (`marv-dahello-hs`), B8, B9 (`scenario-porta.inc:1047–1052`). |
| Leak guard | Oracle reads abort with exit code 3 (`ORACLE_LEAK`); other nodes’ copies set non-finite during host/policy calls | `model/oracle-guard.h`; amend1 A5. `--leakTest` is for positive controls only — **never** in study runs. |

---

## Step 1.5 — Energy

| Parameter | Value | Source |
|---|---|---|
| Profile | `--power=s2c` (EvoLogics S2C R 18/34 class) | — |
| P_tx / P_rx / P_idle / P_sleep | **8 / 1.3 / 0.285 / 0.0025 W** | `acoustic-phy.cc:103` |
| Sleep in primary runs | **Never entered** (no duty-cycling) | Nodes stay tx / rx / idle while alive |
| Energy while alive | E = P_tx T_tx + P_rx T_rx + P_idle T_idle; kill freezes energy time | `acoustic-medium.h:107–122` |
| Primary metric | ebit^tot = (Σ_i E_i) / (N_del · 400 bit), **including idle**, window [warm, T_sim) | Prereg §5; amend1 A3 |
| Window | `--warm=900 --simTime=14400 --drain=300` (study). Metrics count packets generated in [900, 14400) s | Prereg §3 |
| O_ctrl (Fix B) | In heard mode: (bytes of all HELLO + beacon frames whose Tx the medium started in [0, T_sim]) / T_sim. MAC drops not counted. | `scenario-i-lqarp.cc:5687–5695`; `comparators.h:114–130` |
| Battery | E_batt = **864 kJ**; lifetime is **projected**, not drained | `scenario-i-lqarp.cc:2427` (`eBattJ=864000`); `scenario-porta.inc:738–760` |

---

## Step 1.6 — Hosts and arms (configuration list for later steps)

**Hosts (roles fixed by Ali’s decisions):**
- **MARV** — primary host.
- **LQARP** — second host.
- **QELAR, SAQR** — no-harm checks only (equivalence to host alone).

**MARV arm flags** (all need `--marvPsatScope=list --marvSinkOverheard=ack` and the common flags of §1.7):

| Arm | Flags | Code defaults (if not overridden) |
|---|---|---|
| **B0** host alone | `--protocol=marv-ctrl-off` | — |
| **P** (proposed) | `--protocol=marv-dahello-hs --daMode=2 --bootFix=1 --linkOff=1 --daMargin=0.005 --daRefresh=0 --daImin=5 --daDoublings=4` | Defaults in source: `daMargin=0`, `daImin=5`, `daDoublings=4`, `daRefresh=0` (`scenario-i-lqarp.cc:2249–2251`, `:2346`). The 0.005 / Imax=80 point is the **discarded** v8 tuning choice — treat as a **starting smoke point only**, not as the final tuned point. Retuning is a later step. |
| **B2\*** Trickle | `--protocol=marv-ctrl-off --b2Trickle=1 --b2Imin=I --b2Doublings=d --b2K=k --b2Incons=c` | Defaults: I=5, d=4, k=5, c=`table` (`:2343`). Tuning grid (prereg): I∈{2,5,10} × d∈{4,6,8} × k∈{1,3,5,0=∞} × c∈{table,host}. |
| **B3** TAP | `--protocol=marv-ctrl-off --b3Tap=1 --b3Ropt=r` | Default r=0.04 (`:2344`); grid {0.02, 0.04, 0.08} |
| **B4** SpeedOpt | `--protocol=marv-ctrl-off --b4Speed=1 --b4A=a` | Default a=0.1 (`:2345`); grid {0.05, 0.1, 0.2} |
| **B6** distance | `--protocol=marv-ctrl-off --b6Dist=1 --b6Dth=d --b6Tmax=T --b6Zth=50` | Defaults: d=250, T=300, z=50 (`:2328`) |
| **B8** Naive-B | `--protocol=marv-naive --b8NaiveB=1 --b8Tr=T` | Default T=120 (`:2330`); T∈{60,120,300,0=∞} |
| **B9** FC-only | `--protocol=marv-ctrl-off --b9FcOnly=1` | No parameters |

**LQARP (verified):**
- B0: `--protocol=lqarp`
- P (position-aware test, `--lqarpPosDa=2`): `--protocol=lqarp --lqarpPosDa=2 --lqarpPosDaMargin=0`  
  Under σ_z = 0 and equal link cost, rank events cannot fire (structural finding; smoke previously showed `rank_events=0`). Keep for disclosure.
- Comparators B2\*/B3/B6/B8/B9 are **MARV-only** in the code (`scenario-porta.inc:1037–1045`). LQARP has no FC reply on its P arm (`fcOn` only for marv-dahello-hs / B8 / B9).

**QELAR / SAQR:** `--protocol=qelar` / `--protocol=saqr` for host alone. P-style arms (`qelar-hello` / `saqr-hello`) in heard mode: **UNVERIFIED** (leave for a later step after Ali decides).

---

## Step 1.7 — Exact smoke command (one MARV P run)

**Purpose of this command:** environment check only. Seed `20261001` is a **burned pilot seed** (amend1 A6). It is allowed for smoke; it must never appear in tuning or evaluation.

**Copy-paste (one line).** Replace `BIN` with the absolute path of your p13 executable, and `OUT` with a directory you create.

```bash
BIN=/path/to/ns3.41-scenario-i-lqarp-optimized
OUT=/path/to/smoke_step1
mkdir -p "$OUT"
"$BIN" --dataDir=/path/to/DA-HELLO/data/channel_db --topo=t1 --nAuv=149 --simTime=1800 --drain=100 --warm=100 --quiet=1 --phase=random --chan=formula --linkOff=1 --mac=cs --disc=heard --power=s2c --sinkBeacon=60 --marvPsatScope=list --marvSinkOverheard=ack --scenario=iii --driftSpeed=0.3 --gmSigma=0.3 --gmZeta=0.85 --pktInterval=1200 --tNb=360 --seed=20261001 --protocol=marv-dahello-hs --daMode=2 --bootFix=1 --daMargin=0.005 --daRefresh=0 --daImin=5 --daDoublings=4 --out="$OUT/MARV_P_smoke_1800.txt" > "$OUT/MARV_P_smoke_1800.log" 2>&1; echo rc=$?
```

**Pass checks after the run:**
1. `rc=0`.
2. Log contains no `ORACLE_LEAK`.
3. Report contains `metrics_all_finite=true` (or `finite=1` on the SWEEP line).
4. Report shows `topo`/volume consistent with T1 behaviour (alive count starts at 149 relays + sink; eddy line with centre (3000,3000), V=0.3, σ=(0.3,0.3,0)).
5. Lines present: `window gen=… delivered=… pdr=…`, `ebit_total_mJ_per_bit` or energy lines, `O_ctrl=…`.
6. Wall time on the order of a few seconds to tens of seconds on one core (reference: 3.07 s wall for this 1800 s smoke on p12, 2026-10-16).

**Reference hashes of the shared-box binaries (if you copy them):**  
- exe `8668e634651fc9daf3fb1171d312400c8a5cef9cbc26bd6efa0f12e4243dcd19`  
- lib `93bac61808caee664fe1d521c5790ca66060aa8f9cbaf611ce63424352391b40`  
Confirmed present on the shared box on 2026-10-19. On a different CPU, a rebuild may change the exe hash; Gate 1 / optref2 (later steps) decide whether the rebuild is acceptable.

---

## Step 1.8 — What you must have on YOUR machine before Step 2

### Checklist (tick before you report back)

- [ ] **A. Source.** Git tree at commit `9d9c286c34ffbe9e2d12dce877c9d54afb8b70fd`, clean (`git status --short` empty). Path example: `…/DA-HELLO-porta/build_p13/src`.
- [ ] **B. Binary.** Either:
  - (B1) the shared-box exe/lib with the two sha256 values in §1.7, **and** the ns-3.41 optimized core/network/mobility/stats libraries that the exe’s RUNPATH expects; or  
  - (B2) a local rebuild via `build_p13.sh` after installing g++/cmake/ninja and rebuilding ns-3.41 from the original `cmake-cache` (see `simulation_plan_20261018.md` Step 1). Record the **new** exe/lib sha256.
- [ ] **C. Channel data.** Directory `…/DA-HELLO/data/channel_db` (Bellhop CSVs). Required by `--dataDir` even when `--chan=formula`.
- [ ] **D. Frozen documents.** Local copies of `strengthening_prereg.md` and `strengthening_prereg_amend1.md` whose sha256 match § header. Do not edit them.
- [ ] **E. Smoke.** §1.7 command completed with all six pass checks.
- [ ] **F. Environment note.** OS, g++ version, `nproc`, free disk, and whether you used (B1) or (B2).
- [ ] **G. No study run yet.** No tuning or evaluation seed (≥ 20261201) has been used. No ledger line has been forged.

### How to verify the frozen hashes
```bash
sha256sum strengthening_prereg.md strengthening_prereg_amend1.md
# expect:
# b0dc38c59e6f01418d700230428946f1cc00808a7ae1cfd722840267a41fdeae  strengthening_prereg.md
# 45a8e975a31b3e02aeac0bb4cbee44287cdaa1c96accae9d1724aa2413fda8c0  strengthening_prereg_amend1.md
```

### How to verify the binary (if using shared-box copies)
```bash
sha256sum ns3.41-scenario-i-lqarp-optimized libns3.41-dahello-optimized.so
ldd ns3.41-scenario-i-lqarp-optimized | grep 'not found'   # must print nothing
```

### Rebuild pointer
If the exe cannot run (`ldd` shows missing `libns3.41-*-optimized.so`), follow **Step 1 of** `${WORKSPACE}/simulation_plan_20261018.md` (toolchain install, rebuild ns-3.41 libraries from the original cache, then re-check hashes). Do not invent new compile flags.

---

## Step 1.9 — Prereg deviations to keep in mind (disclose; do not silently “fix”)

1. **Heave:** prereg §3 gives II a heave σ_w = 0.05 m/s. The code has **σ_z = 0** (`scenario-i-lqarp.cc:2938`).
2. **Join window:** prereg §2 / amend1 A5: 30 s (LQARP init U[0, 30] s). Code: **U[0, 10] s** (`scenario-porta.inc:175–191`).
3. **WP:** `--nWpAuv` is **not applied** under Scenario III; WP = II byte-for-byte under the study mobility (`scenario-i-lqarp.cc:5321`).
4. **First-contact trigger:** amend1 A5 text says “not fresh”; the code uses **never-heard-only** (p10). Keep the implemented rule; disclose the wording gap.
5. **Report header volume:** the string `6000x6000x500` is stale (`:5724`). The live box is 4000×4000×3500 m.

---

## Step 1.10 — STOP

**Ali:** run only what this sheet asks (environment check + one smoke). Then report back, in writing:

1. Pass/fail for each item in the §1.8 checklist.  
2. The two prereg sha256 values you measured.  
3. The exe and lib sha256 values you measured (and whether B1 or B2).  
4. The smoke `rc`, whether `ORACLE_LEAK` appeared, and the printed `snr_req_db` / `sl_db` / `nl_db` / `O_ctrl` / window `pdr` lines.  
5. Any problem you hit (missing libraries, path differences, hash mismatch).

**Supervisor:** does not start Step 2 (ledger restore, seed amendment, Gate 1, factorial, or any campaign) until Ali’s report is received and checked.

---

*End of Step 1 sheet. No simulation beyond the optional single smoke on burned seed 20261001 is authorised by this document.*
