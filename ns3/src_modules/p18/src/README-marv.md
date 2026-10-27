# MARV host (stage 1)

Wang et al., "MARL-based routing protocol for UWSNs", IEEE Sensors J. 24(5), 2024,
pp. 7042-7054. Files: `model/marv-protocol.{h,cc}`, adapter `MarvFwd` and engine
`Pipeline::Marv*` in `examples/scenario-i-lqarp.cc`. Driver: `scripts/step12_marv_stage1.py`.

## Binary-wide simplifications (say these whenever MARV numbers are quoted)

- **Perfect overhearing, no MAC.** No collisions, no carrier sense, no queueing.
  Every overheard event the protocol relies on succeeds: sibling forwards (suppression),
  the next hop's forward (implicit ack, D2), data-packet headers (all alive nodes in range).
  Data reception at a listed node still uses the binary's Bellhop reception gate.
- **Void-aware beyond the paper (D9).** A node with no qualified neighbour advertises a
  void flag; other nodes treat it as unqualified (the sink never is). MARV itself has no
  void handling.
- Congestion number is 0 everywhere (D3), so C(j) = 0 and delta*C drops out.
- Mobility, Bellhop channel, energy model (E_tx per attempt, E_rx per successful decode,
  no idle listening), sink geometry, range: the binary's own (D6). Control packets are
  not charged energy (same as the binary's HELLOs); they are counted in O_ctrl at the
  binary's HELLO size (400 bit, D5). Data packet = binary's 400 bit, not MARV's 20 B.
- Every listed receiver that decodes the data packet is charged E_rx.
- Processing delay 0.05 s (Table I, p.7050) at every reception, all MARV variants.
- VoI fixed 0.2 for every packet (p.7051, p.7052). The 4-band VoI rule is not used.
- No detour, TTL 32 hops, VOID when U(i) is empty: the binary's behaviour for the hosts.
- A packet whose last live copies were all discarded as duplicates (Alg. 1 line 5) is
  counted as `DUP_DISCARD_END` and included in N_fail.

## Variants (`--protocol=`)

| host | control information |
|---|---|
| marv-ctrl-off | data-packet headers only (MARV method 1, p.7047-7048) |
| marv-T5/T10/T20/T40/T60 | headers + periodic control packet every T s (first round t=0); kept by a receiver in range that decodes it |
| marv-dahello | headers + DA-HELLO HELLO (event trigger + one-time bootstrap via inNRange; candidate filter inNStab), same wiring as qelar-hello / saqr-hello |
| marv-dahello-energy | marv-dahello + 5 s energy beacon (18-bit residual + 8-bit id, `--energyBits=18`) |

Neighbour geometry (range set, depth) is the binary's live geometry in every variant, as
for the QELAR/SAQR hosts. Before anything is heard, node k's record of j is j's true
initial state: V = 0 (D7 + D8), no void flag, e_res = E_ini, congestion 0.

## Signed engine decisions D14-D16

- **D14** `--marvSinkOverheard=ack`: the sink's acceptance is overheard like a forward. It is
  the implicit ack for m_i,sink and suppresses every holder of that packet. Without it MARV
  cannot route to its own sink (the sink never forwards, so Psat to the sink would fall to 0).
- **D15** `--marvPsatScope=top`: n_ij and m_ij count only the first node of the priority list,
  so Psat tracks the link to the designated optimal relay.
- **D16** D(i,j) = 1 - (d(i) - d(j))/R, justified by p.7047 "tend to select the neighbor node
  with a small depth"; the prose reading of Eq. 11 ("receiving node j and sending node i")
  contradicts this.

Both flags remain explicit on the command line. `list` and `literal` exist only for the
sensitivity check in results/step12_marv_stage1_psat_list (never used for the campaign).

Accepted engine choices (signed): starting records, ties to the smaller id, E_rx for every
listed decoder, control packets cost no energy and count in O_ctrl, DUP_DISCARD_END as a
MARV-only failure type (CSV column `DUP_DISCARD_END`).

## Route 3: host-state trigger (extension; link-only marv-dahello stays as the ablation)

Hosts `marv-dahello-hs` (link OR host-state trigger) and `marv-dahello-hs-energy` (+ 5 s,
18-bit energy beacon). Driver `scripts/step13_marv_hs.py`, results `results/step13_marv_hs/`.

- **H1** `DahelloProtocol::setStateDirtyHook()`: optional host hook "the state I advertise changed
  since I last transmitted it". Not installed by LQARP, QELAR, SAQR or the non-hs MARV hosts,
  so it is false forever there (outputs verified byte-identical).
- **H2** MARV's hook (`MarvFwd::stateDirtyCause`) fires on (a) a void-flag flip in either
  direction (**D18**, no threshold) or (b) |V - V_last| > theta_V with **theta_V = g_c = 1
  (D17)**. V_last / void_last are reset by every packet that carries the host state: data
  header, link HELLO, bootstrap HELLO, host-state HELLO (not the energy-only beacon). Initial
  reference: V = 0, no void (the true initial advertisement, D7 + D8).
- **H3 / D19** Same suppression as the link trigger, no new number: evaluated on the
  scheduler tick (dt = 1 s, `Pipeline::tickDt` / `DahelloConfig::dt`), at most one HELLO per node
  per tick (the rising-edge rule in `countEventControlTx`); the existing code has no
  time-based refractory interval. N_stab (`inNStab`) and the one-time bootstrap
  (`noteBootstrap`) are unchanged and apply to every neighbour as before. The link
  detector's persistence gate (W = 5, rho = 0.6) and epsilon threshold (epsilon_0 + lambda·min(sqrt S, s_cap))
  act on per-pair SNR innovations and are not part of the host-state detector.
- **H4** A host-state HELLO carries V, void flag, residual energy (exact) and congestion 0.
  Every alive node that passes the HELLO reception gate (`helloReaches`, --helloRxGate=1)
  records it. It does not write DA-HELLO pose/velocity holds. Like every transmitted control
  packet it feeds the received-only SNR measurement (`broadcastFrom`). Counted in O_ctrl at
  400 bits; no energy charged.
- **H5** Link trigger unchanged; the scheduler fires on link OR host state.
- Attribution: the host-state check runs after the link trigger in the same tick. If node j
  sent a link HELLO in that tick, that HELLO carries j's state, so it is counted once as
  link (`link_event_hellos`) and a pending host-state change is logged as
  `hs_preempted_by_link`. A host-state fire with both causes is counted as void flip.
  Report line `MARVHS` (hs hosts only), windowed to [0, T_sim] like O_ctrl.

## Source of every MARV quantity

| quantity | source |
|---|---|
| qualification d_j < d_i | published (p.7046; Alg. 1 p.7049) |
| Eq. 8 update | published (p.7046) |
| Eq. 9 reward, Eq. 10 E, Eq. 12 C | published (p.7047) |
| Eq. 11 D sign | declared D16 (formula p.7047) |
| Eq. 2 Psat = m/n | published (p.7045) |
| holding time Eq. 13, Tmax Eq. 14 | published (p.7048) |
| priority list, holding, suppression | published (p.7048-7049, Alg. 1) |
| alpha 0.9, gamma 0.5, omega 0.2, delta 1, g_c 1, k 0.4 | published (p.7049; Table I p.7050, image only) |
| lambda1 0.2, lambda2 0.01 | published (p.7051) |
| VoI 0.2 fixed | published (p.7051, p.7052) |
| processing delay 0.05 s | published (Table I p.7050, image only) |
| Psat cumulative, initial 1.0 | declared D1 |
| m_ij by implicit ack | declared D2 |
| congestion number 0 | declared D3 |
| h = 1 | declared D4 |
| control / data packet fields and 400-bit sizes | declared D5 |
| mobility, channel, energy, sink, range; perfect overhearing, no MAC | declared D6 |
| Q initial 0 | declared D7 |
| V(i) = max Q, sink V = 0 | declared D8 |
| void flag | declared D9 |
| failure transition R(i->i) | declared D10 |
| s' = qualified neighbours other than j | declared D11 |
| one update per qualified neighbour at forwarding time | declared D12 |
| R = 2500 m (binary range), v = 1500 m/s | declared D13 (v also Table I p.7050) |
| sink acceptance overheard (ack) | declared D14 |
| Psat scope = top of list | declared D15 |
| depth-cost sign | declared D16 |
| theta_V = g_c = 1 | declared D17 |
| void flip fires without threshold | declared D18 |
| host-state fire uses link-trigger suppression | declared D19 (H3) |

## Formulas

- Qualified: d_j < d_i (kinematic z) and not void-flagged unless j is the sink.
- r = Psat_ij R(i->j) + (1 - Psat_ij) R(i->i) (D10), Psat = m/n, 1.0 while n = 0 (D1).
- R(i->j) = -g_c - (1-omega-VoI)(E(i)+E(j)) - (omega+VoI) D(i,j) + delta C(j) (Eq. 9).
- E(x) = 1 - e_res/e_init (Eq. 10). D(i,j) = 1 - (d(i) - d(j))/R (D16).
  R(i->i): D = 1, E(j) = E(i), C = 0.
- Q <- (1-alpha)Q + alpha[r + gamma lambda1 V_j + gamma lambda2 sum_{s' in U(i)\{j}} V_s'] (Eq. 8, D11, D12).
- V(i) = max_j Q(i,j) over U(i) (D8); sink V = 0.
- Priority list: U(i) by descending Q, ties to the smaller id. T(n) = k Tmax/(h VoI) (n-1),
  Tmax = 2R/v (Eqs. 13-14) = 3.333 s at R = 2500 m, so T(n) = 6.667 (n-1) s.
