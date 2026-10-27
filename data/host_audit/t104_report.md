# t104 — QELAR / SAQR / LQARP host-arm audit (p15v2, read-only)

**Authority:** Ali t140u. **Scope:** define neighbour-state paths and CLI arms from code as-is.  
**Constraints observed:** no simulations; no source/binary/seed changes; no pass/fail verdict language.  
**Binary (t99–t103):** `build_p15v2/bin_p15v2/ns3.41-scenario-i-lqarp-optimized`  
sha256 `e66b8970b6d4ad91cbe939b77b0ba2fb10e21521d68e08f4853fefee4dff7396`  
**Lib:** `build_p15v2/lib_p15v2/libns3.41-dahello-optimized.so`  
sha256 `ad9cc623ee99eb44def7b243bf1b62e3954364b4f213ac486e7ff1ead3f24395`  
**Help capture:** `${OUT}/t104_host_audit_20261023/p15v2_help.txt`  
sha256 `c5d939fa02ba1f0f4543394889a2f758e01b29c0431d0d67b47e1bafcb75cb3e`  
**Source tree audited:** `${WORKSPACE}/DA-HELLO-porta/build_p15v2/src`  
**CST:** 2026-10-23 (Asia/Shanghai).

## Ledger

**BEFORE** (verbatim from `${OUT}/portA_seed_ledger.jsonl`):

```json
{"authority": "Ali t140u; read-only audit approved", "binary_sha256": "e66b8970b6d4ad91cbe939b77b0ba2fb10e21521d68e08f4853fefee4dff7396", "cst": "2026-10-23 08:49:33 CST", "event": "t104_BEFORE", "lib_sha256": "ad9cc623ee99eb44def7b243bf1b62e3954364b4f213ac486e7ff1ead3f24395", "note": "Ledger BEFORE audit. No simulations. No source or binary modification.", "purpose": "t104 read-only audit of QELAR/SAQR/LQARP arms on p15v2; no sims; no code/binary change; no seeds", "run": false, "seeds": [], "set": "t104-host-audit", "src_tree": "${WORKSPACE}/DA-HELLO-porta/build_p15v2/src"}
```

**AFTER:** see end of this report / ledger append at close.

## Files read (sha256)

| Path | sha256 |
|---|---|
| `src/examples/scenario-porta.inc` | `0f4db192cb0b5c12fd4077b423cfd04a41a59bd49cc930510345dcc6ca4ab01c` |
| `src/examples/scenario-i-lqarp.cc` | `95b1451806ff7d53751c3464ebe40b7a8e13154d2a8761af59f2087f3f793023` |
| `src/model/qelar-protocol.h` | `1fa46244c06a1c90d7878cdd9026c15191f254abf4319bc4b67eb684a93732bd` |
| `src/model/qelar-protocol.cc` | `cc90c76a327eee7a97debeb3dbacd1a5b42a1bbce2334c31443fc186ec6489e0` |
| `src/model/saqr-protocol.h` | `3ccd72713de60886f61171fc905705e0804d9ed4e6ef3b2b3d31f8e612fd701b` |
| `src/model/saqr-protocol.cc` | `4e0a77f4a58704a9fdddfcedd57e26779b92d62fb4f3f7efc6d630700eb7a495` |
| `src/model/lqarp-protocol.h` | `c9a63367a813cb626f34b1eea3ccbf5bc9ea091ba672a41ddee161a5f1f45099` |
| `src/model/lqarp-protocol.cc` | `3b3d41bc194cab3d26fe4e3e5b1706624e69960c4cebb113a70738b9e708ea44` |
| `src/model/lqarp-posda.h` | `5c5af08c246415faa7393f1dc90bbaf6a69db69012a6be7afd303d265bd86f94` |
| `src/model/dahello-protocol.h` | `f9178ee31aad119eb1a964e0aee664a1b4c5fbd6c2955195cd4713872561ad71` |
| `src/model/oracle-guard.h` | `874f1a35802424ff515c54135673baf5bccd80dfbe8597d5fec0be1156699bc8` |
| `p15v2_help.txt` (PrintHelp dump) | `c5d939fa02ba1f0f4543394889a2f758e01b29c0431d0d67b47e1bafcb75cb3e` |

Also consulted (evidence search only, not claim sources): `t102_p15_20261023/step3_host_cli_map.md`, `portA_progress.md`, Gate-1 `t101_p15_20261022/gate1_run/manifest.jsonl`.

## Verbatim `--help` fragments (host-relevant)

From `p15v2_help.txt`:

```
--hostSched:             PORT A: QELAR/SAQR baseline HELLO schedule off|periodic|trickle|speedadapt [off]
--lqarpPosDa:            PORT A Gate 2: LQARP position-aware DA HELLO: 0 off; 1 displacement proxy (scaffold); 2 membership/rank test (p6, protocol=lqarp) [0]
--lqarpNoPosePiggyback:  p14: 1 = pre-fix LQARP data header (no pose/energy piggyback); 0 = Liu III-D position+energy+V+neighbour count (default) [0]
--periodicJitter:        p15 A2.2(a) v2: 1 = per-node phase U[0,T) for marv-T<s>, QELAR/SAQR energyRefresh, and --hostSched=periodic; 0 = p14 [false]
--helloOnlyHost:         p15 A2.2(b): 1 = neighbour state written ONLY from HELLO/beacon decodes (forces --pollStored=1); 0 = p14 [false]
--eddyCx:                p15 A2.2(d): eddy centre x (m), default 3000 = p14 [3000]
--sinkBeacon:            PORT A item 4: sink pose beacon period (s), medium on, all arms; 0 = off [0]
--sinkBeaconPhase:       p15 A2.2(e): sink beacons at phase + sinkBeacon*k, k>=1 (s); default 0 = p14 [0]
--tNb:                   PORT A Gate 2: neighbour freshness T_nb (s), heard mode ... [3600]
--protocol:              ... qelar | qelar-hello | qelar-hello-energy | saqr | saqr-hello | saqr-hello-energy | ... lqarp | ... periodic | dahello | ...
```

## Shared medium mechanics (all three hosts)

**Decode path:** `Pipeline::ApplyHeardFromDecode` — `scenario-porta.inc:811–979`.  
**Tx fill:** `Pipeline::MedOnTxStart` — `scenario-porta.inc:267–350`.  
**Join HELLO (heard):** cause `2`, `kFrameHello`, U[0,10]s per node — `scenario-porta.inc:175–191`.  
**Sink beacon:** cause `6`, `kFrameBeacon`, period `--sinkBeacon` — `scenario-porta.inc:193–210`.  
**ACK:** no hold write — `scenario-porta.inc:942–945` (`else { ++rwAckDec; return; }`).  
**Cause codes (DahelloProtocol setTxHook comment):** `dahello-protocol.h` — 0 link/event, 1 periodic, 2 bootstrap, 3 host-state, 4 energy beacon, 5 adaptive (Trickle / speed-adaptive). Medium report maps 0–5: `scenario-porta.inc:692–694`. Cause 6 = sink beacon (separate counter); 7 = first-contact (MARV comparators only).

**Ctrl enqueue:** `setCtrlTxHook` → `MedEnqueue(..., cause==4 ? kFrameBeacon : kFrameHello, ...)` — `scenario-porta.inc:156–164`.

**Common locked-geometry campaign base (from t102 map; not run here):**  
`--topo=t1 --nAuv=149 --simTime=14400 --warm=900 --drain=300 --chan=formula --mac=cs --disc=heard --power=s2c --sinkBeacon=60 --pktInterval=1200 --tNb=3600 --eddyCx=2000 --eddyCy=2000 --eddyR=2828` plus scenario drift/gmSigma flags.

---

# QELAR

## 1. Neighbour state travel (heard)

| Field | Data header | HELLO / beacon | ACK |
|---|---|---|---|
| Position / velocity | **not** carried into QELAR hold | yes (`poseCarried`) | none |
| Energy | yes (`HeardFrame.energy`) | yes | none |
| Group average | yes (`HeardFrame.groupAvg`) | yes | none |
| V | yes (`HeardFrame.V`) | yes | none |
| Neighbour count | not used by QELAR routing | carried in frame (`hnum`) but QELAR `hearFrame` does not store number | none |

**Tx snapshot (HELLO/beacon):** `MedOnTxStart` fills `hz,he,hV,hx,hy,hvx,hvy,hvz,hnum,hG` — `scenario-porta.inc:300–310`.  
**Tx snapshot (data):** `hz,he,hV,hnum,hG` — `:327–332` (no `hx/hy` for QELAR).  
**Decode → host:** `applyHostFrame` ← `HeardFrame` — `:949–968`. Data comment: “QELAR = E, group avg, V” — `:964`. HELLO/beacon sets `poseCarried` + pose/vel/E/groupAvg/number — `:955–960`.  
**`applyHeardHold` for non-LQARP data:** pose/energy NaN’d — `:934–939`; QELAR still gets E/G/V via `applyHostFrame`.

**Routing reads** (`QelarProtocol`):

- Record: matrices `heardE_`, `heardGroup_`, `heldV_`, `heard_`, `tHeard_` written by `hearFrame` — `qelar-protocol.cc:45–63`.
- Candidates: fresh held peers — `candidateSet` uses `fresh(i,j)` when `hc_.on` — `:140`.
- Reward: `heardE_[i][j]` (else `eIni`), own/group energy, `heldV_[i][j]` for next-hop V — reward path `:197`, `:258`.
- Pose: **not** read by QELAR reward (energy/V only).

## 2. Scheduler arm

`--hostSched` applies only when `--protocol=qelar` (not `qelar-hello*`) — `scenario-i-lqarp.cc:3566–3585`.

| hostSched | BroadcastMode | adaptiveScheme | HELLO cause via txHook |
|---|---|---|---|
| `off` (default) | no `sched_` (`Hello::Off`) | — | none from host scheduler |
| `periodic` | `FixedPeriodic` | — | cause **1** (`dahello-protocol.cc` FixedPeriodic path) |
| `trickle` | `Adaptive` | **1** | cause **5** |
| `speedadapt` | `Adaptive` | **2** | cause **5** |

Setting non-off forces `Hello::Pose` and constructs `DahelloProtocol` sched_ — `:3570`, `QelarFwd` ctor `:550–553`.

**What is sent in heard mode:**

- Always (disc=heard): join HELLOs cause **2** (medium) — porta `:186–188`.
- If `--sinkBeacon>0`: beacons cause **6**.
- hostSched=periodic: additional HELLOs cause **1** on `T_ctrl` (default Dahello `T_ctrl=5` unless `--tCtrl` set) — `dahello-protocol.h:70`.
- hostSched=trickle|speedadapt: HELLOs cause **5** (Adaptive).

**This is a host-side DA-HELLO Adaptive / FixedPeriodic scheduler attached to QELAR, not the MARV `marv-dahello-hs` DA-HELLO scheduler.** `daMode` is MARV-hs only. `--hostSched=trickle` = Dahello `adaptiveScheme=1` (Trickle-style), not MARV DA.

**Counts from t99–t103:** none for QELAR (those campaigns are MARV-only).

**Exact CLI (protocol flags; prepend common base):**

| Arm | CLI |
|---|---|
| (a) host alone | `--protocol=qelar` (`--hostSched=off` default) |
| (b) jittered periodic P60 | `--protocol=qelar --hostSched=periodic --tCtrl=60 --periodicJitter=1` |
| (c) scheduler arm | `--protocol=qelar --hostSched=trickle` (host-side Trickle / Dahello Adaptive scheme 1; **not** MARV DA). Alternate non-periodic: `--hostSched=speedadapt`. Does **not** reduce to first-contact-only. |
| (d) host + Trickle | same as (c): `--protocol=qelar --hostSched=trickle` |

## 3. LQARP-only flags

N/A for QELAR.

## 4. Placeholders / paper diffs

From `qelar-protocol.h:1–13`, `:30–36`:

| Item | Code value | Note in header |
|---|---|---|
| `gamma` | `0.5` | “worked example in Section 2.4, signed as a choice, not a named experimental default” |
| `g` | `1.0` | Section 4.2 |
| `alpha1` | `0.5` | Section 4.2 |
| `alpha2` | `0.05` | Section 4.2 |
| Failure reward Eq. (13) | **not applied** | “collision-free, so P_success=1” |
| Tie-break | smallest id | paper example draws uniformly; harness needs reproducible rule |
| Candidate set | acoustic range only | no depth filter, no SNR cut |

## 5. Existing locked-geometry PDR evidence

**None** under locked A2.2 eddy `(2000,2000) R=2828` with II `driftSpeed=0.15 gmSigma=0.3` / GM2 `driftSpeed=0.3 gmSigma=0.6`.

Gate-1 (`t101_p15_20261022/gate1_run/`): `qelar` / `qelar-hello` / `qelar-hello-energy` rows exist (tag `gen`/`gen20`/`new`), **N=40**, `simTime=1600`, **no** `eddyR=2828` in cmds. Example ROW lines report PDR under that short Gate-1 geometry (e.g. gen Scenario III files show `PDR=1.0`), not locked geometry. t99–t103: no QELAR runs.

---

# SAQR

## 1. Neighbour state travel (heard)

| Field | Data header | HELLO / beacon | ACK |
|---|---|---|---|
| Position / velocity | **not** (`poseCarried` false on data) | yes | none |
| Energy | yes | yes | none |
| Number | yes (`HeardFrame.number`) | yes | none |
| V | yes | yes | none |
| Group avg | carried in frame for HELLO; SAQR `hearFrame` does not store groupAvg | — | — |
| PLR | — | — | via **own** ACK stats (`noteOwnSend` / `noteOwnAck`), not ACK payload hold |

**Decode:** same porta `:949–968`; data comment “SAQR = E, number, V” — `:964`. Pose only if `poseCarried` — `saqr-protocol.cc:67–74`.  
**Routing reads** (`SaqrProtocol`):

- `Held` record: `number`, `V`, pose (`x,y,z,vx,vy,vz`, `tPose`) + `heardE_` — `hearFrame` `:50–75`.
- `heldPos`: aged pose with velocity; **sink** uses live deployment pose (I5) — `:82–84`.
- Candidates: fresh + closer-to-sink + popular (held number) — header comment Eqs. 12–14.
- Reward: PLR from own ACK counters; delay = prop + airtime; ER from energy; ALM from held number — config weights in `SaqrConfig`.

## 2. Scheduler arm

Symmetric to QELAR — `scenario-i-lqarp.cc:3628–3647` (`protocol=saqr` only).

Same cause table: off → join+sink only; periodic → cause 1; trickle/speedadapt → cause 5. Host-side Dahello Adaptive/FixedPeriodic, **not** MARV DA.

**t99–t103 counts:** none for SAQR.

| Arm | CLI |
|---|---|
| (a) host alone | `--protocol=saqr` |
| (b) jittered periodic P60 | `--protocol=saqr --hostSched=periodic --tCtrl=60 --periodicJitter=1` |
| (c) scheduler arm | `--protocol=saqr --hostSched=trickle` (host-side Trickle; not MARV DA). Alternate: `--hostSched=speedadapt`. Not first-contact-only. |
| (d) host + Trickle | `--protocol=saqr --hostSched=trickle` |

## 3. LQARP-only flags

N/A.

## 4. Placeholders / paper diffs

From `saqr-protocol.h:1–16`, `:30–45`:

| Item | Code | Note |
|---|---|---|
| Service A weights | rho=0.7, sigma/tau/phi=0.1 | Section 7.2 |
| alpha / gamma | 0.1 / 0.8 | Table 2 / Sec 7.4 |
| epsilon | 0.5 → 0.01 after 20 decisions | page 14 |
| `bitRateBps` | **1000.0** | used in airtime (`saqr-protocol.cc:339`); not a named SAQR-paper bit rate in header |
| Star in Eq. (15) | min-max over this decision’s actions; one-candidate → all terms 0 | “signed implementation choice, not published formula” |
| Delay | propagation + airtime; **holding time omitted**, no MAC queue | signed choice |
| Path loss | Bellhop gate (not SAQR’s own scenario) | header |

## 5. Existing locked-geometry PDR evidence

**None** under locked A2.2 eddy. Gate-1 has `saqr` / `saqr-hello` (N=40, short sims, no eddy 2828). Progress note (historical ceiling): “SAQR heard PDR is ≈0 on every arm (0.000–0.004)” under **pre-A2.2 / non-locked** ceiling cells — not locked-geometry t99–t103. t99–t103: no SAQR runs.

---

# LQARP

## 1. Neighbour state travel (heard)

| Field | Data header (default `lqarpNoPosePiggyback=0`) | Data (`=1`) | HELLO / beacon | ACK |
|---|---|---|---|---|
| Position (x,y,z) | yes (`hx,hy` + `hz`) | **suppressed** (x,y,z → NaN in applyHeardHold path) | yes | none |
| Velocity | not filled on data tx | — | yes | none |
| Energy | yes (`he`) | **suppressed** (e → NaN) | yes | none |
| V | yes | **still applied** (`V=pf.hV`) | yes | none |
| Neighbour count | yes (`hnum`) | **still applied** | yes | none |

Evidence:

- Tx data piggyback when LQARP and `!lqarpNoPosePiggyback`: `pf->hx/hy` — `scenario-porta.inc:342–349`.
- Decode default LQARP: `x=pf.hx; y=pf.hy` keep z,e,V,num — `:927–932`.
- Decode NoPose / non-LQARP branch: `z=nan; e=nan` — `:934–939` (V,num remain set above).
- HELLO/beacon full vector — `:904–912`.
- Hold write: `LqarpProtocol::applyHeardHold` → `FrozenNeighbor` (`x_j_zoh`…`V_zoh`, `number_zoh`, …) — `lqarp-protocol.cc:160–195` (approx lines in file; `applyHeardHold` body).

**Routing reads** (`LqarpProtocol` / `FrozenNeighbor`):

- Pose / energy / SNR / `number_zoh` / `V_zoh` from held record for `inNRange` / `inNSnr` / `inNDepth` / reward.
- `c_nei = exp(number)` — `lqarp-protocol.cc:321–324`.
- Reward `R = c_ene * c_del * c_nei / c_lin` — `:367–372`.

## 2. Scheduler arm

**`--hostSched` does not apply to `protocol=lqarp`** (wired only for `qelar`|`saqr` — `:3566`, `:3628`). No `sched_` on `LqarpFwd`.

HELLO sources while routing stays LQARP:

- Join cause **2** + sink beacon cause **6** (medium).
- `--lqarpPosDa=1|2`: HELLOs cause **3** — `InstallLqarpPosDa` / `LqarpPosDaTick` — porta `:1199+`, enqueue `:1329` / `:1397`.

Separate **protocol replacements** (routing becomes DahelloFwd, not LqarpProtocol):

- `--protocol=periodic --tCtrl=60`: FixedPeriodic, cause **1**.
- `--protocol=dahello`: EventTriggered link/event HELLOs, cause **0** (and bootstrap 2).

| Arm | CLI (LQARP routing unless noted) |
|---|---|
| (a) host alone | `--protocol=lqarp` |
| (b) jittered periodic P60 | **Not** `--protocol=lqarp --hostSched=periodic` (unsupported). Closest FixedPeriodic stack: `--protocol=periodic --tCtrl=60 --periodicJitter=1` (**DahelloFwd**, not LqarpFwd). |
| (c) scheduler arm | Position DA on LQARP host: `--protocol=lqarp --lqarpPosDa=2` (membership/rank; cause 3). Scaffold: `--lqarpPosDa=1` (displacement proxy; cause 3). Alternate **non-LQARP** event scheduler: `--protocol=dahello`. **Not** first-contact-only; PosDa fires on membership/displacement events with I_min rate limit. |
| (d) host + Trickle | **No** `--hostSched=trickle` on LQARP. No LQARP-attached Trickle arm in code. (MARV `--b2Trickle` requires `marv-ctrl-off`.) |

## 3. LQARP flags (detail)

### `--lqarpNoPosePiggyback=1`

- Help: “1 = pre-fix LQARP data header (no pose/energy piggyback)” — help line above; CLI default 0 — `scenario-i-lqarp.cc:2404`, `:8477`.
- Tx: skips `pf->hx/hy` fill — porta `:342–349` (`&& !lqarpNoPosePiggyback`).
- Rx: takes else branch NaNing z,e for `applyHeardHold` — `:934–939`; V and num still passed.
- **Pose from HELLOs only?** Pose (and energy via applyHeardHold) yes — only HELLO/beacon supply finite x,y,z,e into the hold when NoPose=1. **V and neighbour count still update from data headers.** Sink beacon (kind beacon) also carries full pose vector.

### `--lqarpPosDa=2`

- Install: porta `:1199–1211`; requires `protocol=lqarp` for mode 2 — `:1207–1208`.
- Mode 2: membership/rank test vs last-advertised pose (`EvaluateLqarpPosDa`); on change enqueues HELLO cause **3** — tick path ~`:1228–1330`.
- Mode 1: displacement scaffold `> 0.25 * R_op` since last adv — `:1390–1400` area; also cause **3**.
- Own pose snapshot on HELLO tx when PosDa=2: `:311–314`.

### HELLO-dependent LQARP (`--lqarpNoPosePiggyback=1`) arm CLIs

Pose (and hold energy) depend on HELLO/beacon after join:

| Concept | CLI |
|---|---|
| host alone | `--protocol=lqarp --lqarpNoPosePiggyback=1` (pose refresh = join HELLO + `--sinkBeacon` only) |
| periodic | No FixedPeriodic attached to LqarpFwd. Cannot express “LQARP routing + P60 HELLO” with `--hostSched`. `--protocol=periodic --tCtrl=60 --periodicJitter=1 --lqarpNoPosePiggyback=1` changes host to DahelloFwd (NoPose flag only prints for lqarp/lqarp-refreshed — `:8536–8539`). |
| scheduler | `--protocol=lqarp --lqarpNoPosePiggyback=1 --lqarpPosDa=2` (or `--lqarpPosDa=1`) |

## 4. Placeholders / paper diffs

From `lqarp-protocol.h:9–11`, `:27–46`, `lqarp-protocol.cc:321–324`, detour `:389–391`:

| Item | Code | Note |
|---|---|---|
| `alpha` | 0.9 | Table II |
| `gamma` | **0.8** | “DA-HELLO Sec. 4.1; LQARP-paper gamma not given. Flagged, not assumed.” |
| `kMaxDetourHops` | **3** | “Design parameter, not from the LQARP paper. Flagged.” |
| `c_nei` | **`exp(number_sj)`** | `cNei` — long-path pressure from neighbour count (confirmed in current code `:323–324`) |
| `holdRefreshS` | 0.0 default | manuscript freeze-at-first-discovery; `lqarp-refreshed` sets 5.0 |
| Detour / sink | `detourTargets` / `detourSet` with sinkId exclusion paths | paper gives no detour equation (header); sink handling via candidate/detour filters in `.cc` |

**Confirmed vs earlier audits:** `c_nei = exp(neighbour count)` still present; `gamma=0.8` and `kMaxDetourHops=3` still flagged placeholders / non-paper constants.

## 5. Existing locked-geometry PDR evidence

**None** under locked A2.2 eddy for LQARP in t99–t103 (MARV-only).  

Gate-1 tag `lqarp` / `lqarp20`: arms `lqarp`, `periodic-T5..T60`, `dahello`, `dahello-energy*`, `rlor` — **N=40**, `simTime=1600`, **no** eddy 2828.  

Historical ceiling campaigns (progress 2026-10-12) include LQARP×B0/B1/B2/B4 cells; those predate A2.2 locked eddy `(2000,2000,2828)` and are not cited as locked-geometry evidence here. Locked-geometry trees under `t100_p15_20261022/runs/pgeo2/` are MARV B0 geometry probes, not QELAR/SAQR/LQARP host PDR.

---

## Cause-code summary (heard, host-relevant)

| Cause | Meaning | Who emits |
|---|---|---|
| 0 | link/event | Dahello EventTriggered (`protocol=dahello`) |
| 1 | periodic | `hostSched=periodic` or `protocol=periodic` |
| 2 | bootstrap / join | medium join HELLO (all heard arms) |
| 3 | host-state / PosDa | `--lqarpPosDa=1\|2` |
| 4 | energy beacon | `*-energy` / energyRefresh paths |
| 5 | adaptive | `hostSched=trickle\|speedadapt` |
| 6 | sink beacon | `--sinkBeacon>0` (frame kind beacon) |

## Confirmations

- Zero simulations run for t104.
- Binary/lib sha256 unchanged vs t99–t103 (`e66b8970…7396` / `ad9cc623…4395`).
- Sources under `build_p15v2/src` not modified.
- No seeds consumed.
- No pass/fail verdict stated.


## Ledger AFTER (appended at close)

```json
{"authority": "Ali t140u; read-only audit approved", "binary_sha256": "e66b8970b6d4ad91cbe939b77b0ba2fb10e21521d68e08f4853fefee4dff7396", "cst": "2026-10-23 08:53:04 CST", "event": "t104_AFTER", "help_sha256": "c5d939fa02ba1f0f4543394889a2f758e01b29c0431d0d67b47e1bafcb75cb3e", "lib_sha256": "ad9cc623ee99eb44def7b243bf1b62e3954364b4f213ac486e7ff1ead3f24395", "note": "Ledger AFTER audit. Report written. No simulations. Binary/sources unchanged.", "purpose": "t104 read-only audit complete", "report_path": "${OUT}/t104_host_audit_20261023/t104_report.md", "report_sha256": "7d1bc932d41538f01857fe630dc888ff65c6f4cd009b747436d15037786e2f74", "run": false, "seeds": [], "set": "t104-host-audit", "simulations": 0, "src_tree": "${WORKSPACE}/DA-HELLO-porta/build_p15v2/src"}
```

Final report sha256 after this AFTER block: `db5bdf7d3e72b9f2c5bf1440074d472181de14622de1672d08b6542be363cb1c`
