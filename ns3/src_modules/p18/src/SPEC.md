# DA-HELLO System Model — Authoritative Specification

This document is the ONLY authoritative source for the LQARP baseline 
and DA-HELLO protocol math. It supersedes anything inferred from 
phase1/, paper/iotj_et_hold.tex, or any other code/document already 
in this repository. Those are unrelated prior work (a different 
protocol, event-triggered discovery + hold-time-aware forwarding, 
explicitly not Q-learning) and must not be treated as a reference 
implementation, merged with, or cited as related work for this 
protocol.

All variable names below must be used VERBATIM in code (as comments 
or identifier names, adapted to code style, e.g. c_ene, c_del, c_nei, 
c_lin, c_pred, c_drift, e_k, epsilon_k, S_k, v_rel) so that code and 
paper stay traceable to each other.

## Part 1: LQARP baseline (the protocol being extended)

### 1.1 MDP tuple and state

Q-learning MDP: (S, A, P, R). Each node s_i is an agent. Action a_j 
selects neighbor s_j as next hop.

State s_i includes: position, residual energy E_{s_i}, depth, and 
number_{s_i} (count of feasible forwarding neighbors, defined below).

### 1.2 Candidate set restriction

A neighbor s_j is a valid action (candidate) only if ALL THREE hold:

  N_range(s_i)  = { s_j : d(s_i, s_j) < r_range }
  N_SNR(s_i)    = { s_j : SNR(s_i, s_j) > SNR_threshold, j != i }
  N_depth(s_i)  = { s_j : depth_{s_i} - depth_{s_j} > 0, j != i }
                  (candidate must be at shallower depth, i.e. closer 
                   to the surface, than s_i)

  Candidate set: I(s_i) = N_range(s_i) ∩ N_SNR(s_i) ∩ N_depth(s_i)
  Action space: A = I(s_i)

SNR(s_i, s_j) MUST be computed via the BellhopDataLoader-backed 
lookup (the fixed SnrDb path from the prior turn), using the 
transducer-depth-floored query convention already agreed:
  depth_query = max(receiver_z, 5.0)   [meters]
NOT the Thorp+Wenz+SL=185 formula from phase1/. This applies to 
every protocol variant without exception (LQARP, RLOR, Fixed-Periodic 
ablation, DA-HELLO) -- this is a non-negotiable fairness requirement 
already agreed in the prior turn.

Reception condition (binary, for whether a transmitted packet is 
actually received, separate from candidate-set filtering above):
  status = RECEPTION   if SNR >= SNR_threshold
  status = INVALID     otherwise
  (packet dropped if INVALID)

### 1.3 Reward matrix construction

For candidate s_j given current node s_i, reward for action a_j:

  R^{a_j}_{s_i,s_j} = c_ene(s_j) * c_del(s_i, s_j) * c_nei(s_j) 
                      / c_lin(s_i, s_j)

Four factors:

  c_lin(s_i, s_j) = log( 1 / P_succ(s_i, s_j) )

  where P_succ(s_i, s_j) = (1 - p_e(s_i, s_j))^l
  l = packet length in bits (400 bits, per Table II convention)
  p_e is the average bit error rate for the chosen modulation 
  (M = 2, 4, or 8; BPSK/QPSK), computed from SNR(s_i, s_j) via the 
  standard OFDM BER formula:

  p_e(s_i,s_j) = 
    [ (sqrt(M)-1) / (sqrt(M) * log2(sqrt(M))) ] 
      * erfc( sqrt( 3 * 10^(SNR/10) * log2(M) / (2*(M-1)) ) )
    + [ (sqrt(M)-2) / (sqrt(M) * log2(sqrt(M))) ]
      * erfc( 3 * sqrt( 3 * 10^(SNR/10) * log2(M) / (2*(M-1)) ) )

  (M=2 for this baseline; use standard erfc implementation, e.g. 
  std::erfc in C++.)

  c_ene(s_j) = (2/pi) * arctan( (E_{s_j} - E_avg) / E_ini )

  where E_avg is the average residual energy across ALL nodes 
  currently in the candidate set I(s_i) (not global average), 
  E_ini is initial energy (same constant for all nodes).

  c_del(s_i, s_j) = (E_{s_j} / E_ini) * (depth_{s_i} - depth_{s_j})

  c_nei(s_j) = exp( number_{s_j} )

  where number_{s_j} = count of s_j's OWN feasible forwarding 
  neighbors, i.e. |I(s_j)| computed the same way as I(s_i) above but 
  from s_j's perspective. If number_{s_j} = 0, c_nei(s_j) = 1 
  (exp(0)), meaning s_j has no onward candidates -- a potential void 
  node.

### 1.3 Addendum: numerical floor on BER (spec clarification, not a hack)

To prevent R^{a_j}_{s_i,s_j} -> infinity at high SNR (a structural 
property of Eq. 34's c_lin in the denominator, inherited from LQARP, 
which becomes the TYPICAL case at short range in our channel 
environment, not a rare edge case), p_e is floored:

  p_e_effective = max(p_e_computed, 1e-9)

This represents a residual bit-error floor from receiver noise/
quantization effects not captured by the ideal AWGN BER model -- a 
standard modeling convention, not an ad hoc numerical patch. This 
floor must be applied IDENTICALLY in LQARP and DA-HELLO (and any other 
baseline using c_lin/c_pred), so no protocol gains an unfair 
numerical advantage from how it happens to hit the floor.

With this floor, the high-SNR plateau of c_lin is

  c_lin(p_e = 1e-9) = ln(1 / (1 - 1e-9)^l)    for l=400 bits

which is finite. Code identifier: kBerFloor in link-quality.h, used 
by both LqarpProtocol and DahelloProtocol. P_succ and c_lin are 
computed from p_e_effective, not the raw AWGN BER.

## Section 4.1 parameter table (shared constants)

  alpha          = 0.9
  gamma          = 0.8     (DA-HELLO Sec. 4.1; LQARP-paper gamma not in extraction)
  SNR_threshold  = 8.68 dB
  l              = 400 bits
  M              = 2
  E_ini          = 10000 J
  kBerFloor      = 1e-9
  depth_query    = max(receiver_z, 5.0) m
  R* scan depth  = 50 m
  dt             = 1.0 s
  rMeas          = 4.0 dB^2

  Validated paper operating point after Steps 60–67: event trigger
  on, beta_1 = beta_2 = 0 (LQARP reward). Compiled DahelloConfig
  defaults match that point. O(1) beta_1 / beta_2 were a
  diagnostic-stage placeholder (Steps 53 / 60); those tables are
  not headline results. Both knobs remain overrideable at run time
  (CLI or config file) without rebuilding:

  W_mp           = 3
  W              = 5
  rho            = 0.6
  epsilon_0      = 2.0 dB   (Step 46; 70-80% contour of the
                             ZOH-init saturation ceiling)
  s_cap          = 2.0 dB   (= sqrt(rMeas); kSCapDb)
  lambda         = 1.0
  zeta           = 0.95     (Scenario III Gauss-Markov correlation)
  T_ctrl         = 5.0 s
  beta_1         = 0.0      (validated default; SWEPT at run time)
  beta_2         = 0.0      (validated default for Scenario III;
                             Scenario II non-zero sweep remains open)


### 1.4 Temporal-difference Q-update

  Q(s_i, a_j) <- (1-alpha) * Q(s_i, a_j) 
                 + alpha * [ R^{a_j}_{s_i,s_j} 
                             + gamma * max_{a in I(s_j)} Q(s_j, a) ]

  alpha = 0.9 (learning rate, Table II)
  gamma = 0.8 (discount factor -- NOTE: this is DA-HELLO's chosen 
          gamma, stated explicitly in our Section 4.1 parameter 
          table; if a separate gamma is needed for the LQARP 
          baseline to match its original paper exactly, flag this 
          back rather than assuming -- gamma is not stated in the 
          LQARP extraction matrix, only alpha=0.9 was confirmed)

Next-hop selection:
  s_i.NextHop = argmax_{a_j in I(s_i)} Q(s_i, a_j)

All Q-values initialize to zero at network start.

### 1.5 Detour mechanism (documented approximation — LQARP's paper 
describes this qualitatively as "considering two-hop neighbor 
information," with no explicit equation given; this is our specified, 
bounded reading of it, not a verified reproduction)

When I(s_i) = ∅ for a packet at non-sink node s_i, do NOT drop 
immediately. Instead:

  1. Compute a relaxed detour set, dropping the N_depth constraint 
     but keeping range and SNR feasibility:
       D(s_i) = { s_j in S : s_j in N_range(s_i) ∩ N_SNR(s_i), j != i }

  2. Two-hop lookahead filter: keep only s_j in D(s_i) such that 
     EITHER s_j is the sink, OR I(s_j) != ∅ (s_j itself has a valid 
     forward path) OR D(s_j) has at least one member not yet visited 
     by this packet (s_j itself has a viable detour option).

  3. If this filtered set is non-empty, select next hop from it using 
     the SAME reward/Q-update machinery as a normal hop, but log this 
     hop as DETOUR, distinct from a normal forward hop, for metrics 
     purposes.

  4. Loop prevention (required): each packet carries a visited-node 
     list. A detour hop may never route to a node already visited by 
     that packet. Cap total detour hops per packet at 3 (design 
     parameter, not derived from LQARP -- flagged as such). If no 
     valid, unvisited detour target exists, or the cap is reached, 
     THEN drop as VOID (unchanged from before).

This mechanism is applied identically to LQARP, DA-HELLO, and the 
Fixed-Periodic ablation (all inherit I(s_i) semantics). RLOR-inspired 
does NOT use this mechanism, since RLOR's candidate set was already 
defined without N_depth in Part 3 -- RLOR has no "depth dead end" 
failure mode to detour around by construction.

## Part 2: DA-HELLO extensions (what this paper adds)

### 2.1 Kinematic state (relative baseline)

For a candidate pair (s_i, s_j):

  p_ij(t)     = p_i(t) - p_j(t)                [3D relative position]
  pdot_ij(t)  = pdot_i(t) - pdot_j(t)           [relative velocity]

  v_rel(i,j)(t) = ( pdot_ij(t) . p_ij(t) ) / || p_ij(t) ||
  
  (LOS relative velocity; negative = closing range, i.e. j 
  approaching i)

  pdot_i(t) is known exactly (own odometry). pdot_j(t) is the most 
  recent value received from j's last control-packet broadcast, held 
  constant (zero-order hold) between broadcasts. Until i has received 
  at least one broadcast from j, pdot_j is unknown: do NOT assume 
  zero. c_drift uses c_del (the beta_2=0 case) for that pair until 
  the first HELLO (SPEC 2.7). A zero default would make v_rel equal 
  to i's own velocity under uniform drift — a fabricated signal.

### 2.1 / 2.2 Addendum: broadcast writes velocity as ground truth

Upon receiving a broadcast from neighbor j, the received pdot_j is 
written directly into the velocity components of x_hat_{k|k}, 
overwriting the EKF's own estimate there, since Part 2.1 defines 
pdot_j as the received ground-truth value, not an inferred quantity. 
The EKF state stores relative velocity, so the write is

  x_hat_{k|k}[3:5] <- pdot_i - pdot_j_received

with no blending or averaging against the prior estimate. Position 
components of x_hat are unaffected by this write, since they remain 
independently observable from the SNR measurement stream (Step 35 
finding: radial position converges reliably; velocity does not, so 
velocity must be supplied directly rather than estimated).

x_forecast is then reset to this corrected x_hat_{k|k} (and 
P_forecast to P_{k|k}), as in the Part 2.2 addendum. The Kalman 
update on this tick, if any, runs first; the overwrite is applied 
after it so the filter cannot immediately blend the written velocity 
back toward the stale estimate.

### 2.2 EKF state and update

Per-neighbor EKF state (7-dim):
  x_k = [ p_ij^T, pdot_ij^T, SNR_ij ]^T

Kinematic block propagates linearly (constant-velocity model):
  [p_ij; pdot_ij]_k = F_kin * [p_ij; pdot_ij]_{k-1} + w_k
  F_kin = [[I_3, dt*I_3], [0_3, I_3]]

SNR component recomputed each predict step via the SAME 
BellhopDataLoader-backed lookup as Part 1 (not re-derived 
analytically):
  SNR_ij,k|k-1 = BellhopDataLoader.query(range(p_ij,k|k-1), 
                                          depth_query).snr_db

Standard EKF predict/update:
  x_hat_{k|k-1} = f(x_hat_{k-1|k-1})
  P_{k|k-1}     = F_k * P_{k-1|k-1} * F_k^T + Q_k
  x_hat_{k|k}   = x_hat_{k|k-1} + K_k * (z_tilde_k - h(x_hat_{k|k-1}))

  F_k = Jacobian of f at x_hat_{k-1|k-1}, including the coupling 
  term d(SNR)/d(p_ij) -- since SNR is now a real, non-analytic 
  lookup, this Jacobian must be computed via finite differences 
  (numerical Jacobian), not a closed-form derivative of the Bellhop 
  grid. Use a small perturbation (e.g. +/- 10m in range) and query 
  BellhopDataLoader twice to estimate d(SNR)/d(range) numerically.

  The implemented measurement Jacobian for the Kalman SNR update is
  H = [0 0 0 0 0 0 1] (the measurement is the SNR state). The
  measurement *function* for predicted SNR, h = SNR(range_H(p_ij),
  depth_query), has kinematic Jacobian

    ∂h/∂p_ij = (dSNR/drange) * (p_ij_horiz / ||p_ij_horiz||)
    ∂h/∂p_ij_z = 0
    ∂h/∂pdot_ij = 0

  because Bellhop SNR depends on horizontal range only.

Observability note: because SNR(range) depends only on horizontal
range through the BellhopDataLoader grid, the EKF's observation
Jacobian H_k has nonzero sensitivity only along the instantaneous
radial direction of p_ij; the two tangential directions, and by
extension the full velocity state x[3:5], are not observable from
SNR measurements alone. Consequently, x[3:5] should not be treated
as a reliable full 3D relative-velocity estimate absent a received
broadcast. The event-triggered broadcast mechanism (Part 2.4) is
therefore not merely a bandwidth-efficient alternative to periodic
beaconing -- it is the sole channel through which DA-HELLO obtains
trustworthy relative-velocity information for c_drift. This is a
structural property of the estimation architecture, stated here as
a design rationale and limitation, not a defect discovered post
hoc.

### 2.3 Measurement pipeline (pre-filter + innovation)

Raw instantaneous sample z_k = SNR_ij(t_k) [from the SAME lookup, at 
the actual current range/depth, not the predicted one]

  Architecture (Step 41): z_k is computed EVERY dt tick by querying 
  BellhopDataLoader at the nodes' TRUE current positions 
  (DahelloProtocol::tick → linkSample(i,j) → ChannelGate::queryLink 
  on nodes_[i/j].x,y,queryDepthM). It is NOT gated on a broadcast, 
  packet overhearing, or any communication event. Predicted SNR 
  h = x[6] is the EKF's own range-propagated lookup. The Kalman 
  update therefore pulls the radial position toward ground-truth 
  range every second whether or not a HELLO has ever been received. 
  Consequently the one-step innovation e_k = z_tilde - h cannot 
  accumulate a velocity-model error: the only observable dimension 
  is healed from truth each tick. This is a design-level statement 
  about the current estimator, not a recommended trigger rewrite.

Median pre-filter:
  z_tilde_k = median(z_{k-W_mp+1}, ..., z_k)

  W_mp smooths successive SNR observations against measurement 
  noise and small-scale geometric jitter between EKF updates. Given 
  the observation cadence (dt=1.0s) substantially exceeds the 
  channel's measured coherence time (~3ms, from measured coherence 
  bandwidth ~330 Hz), this window does not resolve fast multipath 
  fading directly; it instead reduces variance from per-sample 
  measurement noise.

  First-pass default: W_mp = 3 samples.

Innovation (SAME time index, no averaging mismatch):
  e_k = z_tilde_k - SNR_ij,k|k-1

### 2.4 Event-triggered broadcast condition

Innovation covariance: S_k = H_k * P_{k|k-1} * H_k^T + R_k
(H_k is the observation Jacobian, also numerical if needed)

Adaptive threshold:
  epsilon_k = epsilon_0 + lambda * sqrt(S_k)
  First-pass: epsilon_0 = 4.0 dB, lambda = 1.0.

Persistence-gated trigger (over window W samples, persistence 
fraction rho):
  Broadcast_k = 1  if  |e_k| >= epsilon_k  AND
                (1/W) * sum_{m=k-W+1}^{k} [ |e_m| >= epsilon_m ] >= rho
              = 0  otherwise
  First-pass: W = 5, rho = 0.6.

A control-packet transmission occurs on each 0→1 transition of
Broadcast_k (a rising edge), not on every tick where Broadcast_k=1
persists. This distinguishes event-triggered signaling from a
periodically-gated alternative. Persistence (W, rho) is a gate that
decides whether to fire once; it is not a license to re-announce
every tick the condition remains true.

### 2.2 Addendum: dual-track state (corrected estimate vs. open-loop 
forecast)

Per Step 41's finding: because z_k (SNR measurement) is computed at 
every dt tick from ground-truth current positions, the standard EKF 
update pulls the radial (observable) position component toward truth 
every step, regardless of how wrong the underlying ZOH velocity is. 
This means the standard Kalman innovation e_k = z_k - h(x_hat_{k|k-1}) 
CANNOT carry a signal about velocity/ZOH staleness, for any threshold 
epsilon_0 -- the observable-dimension error is repaired before it can 
accumulate into anything a one-step innovation would detect.

DA-HELLO therefore maintains TWO parallel state tracks per neighbor 
pair, not one:

  1. The CORRECTED estimate x_hat_{k|k} (Part 2.2, unchanged): 
     standard EKF predict/update, still used for the SNR-prediction 
     coupling term (Eq. snr_prediction_coupling) wherever a best 
     current estimate of range/SNR is needed elsewhere in the reward 
     computation.

  2. A new OPEN-LOOP FORECAST x_forecast_k: propagated ONLY via F_k 
     (Eq. relative_transition) using the last-received (or ZOH-held) 
     velocity, NEVER corrected by any measurement:

       x_forecast_k = F_k * x_forecast_{k-1}   [no update step, ever]

     x_forecast is RESET to match x_hat_{k|k} exactly at the moment a 
     broadcast is received from that neighbor (i.e. whenever 
     zohHeard transitions to a fresh value) -- this is the only time 
     x_forecast is allowed to jump. Between broadcasts, it evolves 
     purely by the (possibly wrong) ZOH-held velocity, exactly as if 
     no correction were available -- which is the honest state of 
     information a real receiver would have absent a new broadcast.

### 2.3 Addendum: forecast-vs-truth innovation (replaces the 
one-step Kalman innovation for TRIGGER purposes only)

The event-trigger innovation is redefined:

  h_forecast_k = SNR(range(x_forecast_k), depth_query)   [predicted 
                  SNR from the UNCORRECTED forecast]

  e_k^{trigger} = z_k - h_forecast_k

  [z_k is the same real, per-tick ground-truth SNR query already in 
  use -- Part 2.3's original z_k definition is unchanged]

This is DISTINCT from the Kalman innovation used inside the EKF's own 
update step (Part 2.2, unaffected, still needed for x_hat's own 
internal consistency). e_k^{trigger} accumulates real divergence 
between "what a node would believe if it heard nothing" and "what is 
actually true," growing over the broadcast-free interval exactly as 
Step 40's accumulated-divergence analysis modeled it -- not wiped 
every tick, because x_forecast is never measurement-corrected.

The persistence-gated trigger condition (Part 2.4, Eq. 
trigger_condition_revised) is unchanged in FORM, but now operates on 
e_k^{trigger} instead of the original one-step e_k:

  Broadcast_k = 1  if  |e_k^{trigger}| >= epsilon_k  AND
                (1/W) * sum [ |e_m^{trigger}| >= epsilon_m ] >= rho
              = 0  otherwise

The Part 2.4 rising-edge transmission rule applies to this revised
condition unchanged: one HELLO per 0→1 of Broadcast_k, not one per
persisted tick.

S_k (innovation covariance) is computed against x_forecast's own 
(unbounded, non-measurement-corrected) covariance growth, NOT 
x_hat's Kalman-updated P -- since x_forecast has no update step, its 
uncertainty is tracked as pure process-noise accumulation:

  P_forecast_k = F_k * P_forecast_{k-1} * F_k^T + Q_k   [predict 
                  only, no update, ever -- this is the SAME recursion 
                  already used for P_{k|k-1} in Part 2.2's predict 
                  step, just never followed by an update]

  P_forecast is RESET to match P_{k|k} at the same broadcast-received 
  moment that resets x_forecast.

The trigger threshold uses that S_k but MUST NOT inherit P_forecast's 
unbounded growth (Step 45 Option B). The original 

  epsilon_k = epsilon_0 + lambda * sqrt(S_k)

was written assuming a Kalman-updated S that stays O(R). On 
never-corrected P_forecast it makes long-silent pairs progressively 
harder to trigger -- backwards from the intended behavior. The 
trigger formula is therefore:

  epsilon_k = epsilon_0 + lambda * min(sqrt(S_k), s_cap)

  s_cap = 2.0 dB   [kSCapDb = sqrt(rMeas)]

s_cap caps the forecast-uncertainty contribution to the trigger 
threshold, since P_forecast is intentionally never 
measurement-corrected (Part 2.2 addendum) and its unbounded growth 
would otherwise make long-silent pairs progressively harder to 
trigger -- backwards from the intended behavior. s_cap is set to 
sqrt(rMeas), matching the original heuristic scale before 
P_forecast's decoupled growth made that scale unreachable in 
practice.

epsilon_0 = 2.0 dB (Step 46). Calibrated to the 70-80% contour of 
the unheard-window max |e_k^{trigger}| distribution on the known 
ZOH-init error class (Step 44: 74.9% of never-heard live AUV-AUV 
pairs and 78.5% of all live AUV-AUV pairs reach >= 4.0 dB, which 
is the late-time threshold epsilon_0 + s_cap). Conservative 
relative to 1.5 dB; may be loosened after Scenario III if genuine 
relative motion under-triggers.

This is a structural correction to the estimator, identified via 
Steps 35-41's diagnostic chain, not a tuning patch: the original 
one-step Kalman innovation is provably incapable of detecting ZOH 
staleness given a per-tick ground-truth measurement architecture, 
regardless of threshold value.

### 2.5 Candidate set restriction (DA-HELLO version)

DA-HELLO adds a fourth condition to Part 1.2's three-condition filter:

  N_stab(s_i) = { s_j : Broadcast_k(i,j) = 0  OR  
                         |e_k(i,j)| < epsilon_k(i,j) }

  I_DAHELLO(s_i) = N_range(s_i) ∩ N_SNR(s_i) ∩ N_depth(s_i) ∩ N_stab(s_i)

Action space for DA-HELLO: A = I_DAHELLO(s_i)

### 2.6 Predictive link quality factor (replaces c_lin)

  c_pred(s_i, s_j) = c_lin(s_i, s_j) 
                     + beta_1 * max(0, e_k(i,j) - e_{k-1}(i,j))

  beta_1 >= 0, swept at run time (compiled default 0.0). beta_1 = 0 recovers 
  LQARP's c_lin exactly (required sanity-check limiting case -- MUST 
  verify this numerically in testing: running DA-HELLO with beta_1=0, 
  beta_2=0, and event-triggering disabled must produce IDENTICAL 
  Q-values and routing decisions to the LQARP baseline, given the 
  same random seed and inputs).

  Addendum (Steps 63–67) — scale inconsistency of the additive form.
  The additive form c_pred = c_lin + beta_1*max(0, e_k - e_{k-1})
  is scale-inconsistent when c_lin sits at its BER floor
  (~4e-7 for l=400 bits, per Part 1.3's addendum), since typical
  one-step Kalman innovation differences on this channel are
  O(0.01) dB. At O(1) beta_1, the term does not blend with c_lin --
  it overrides it, since beta_1*Delta_e exceeds c_lin by 4+ orders
  of magnitude. At beta_1 scaled to genuinely blend (1e-5 to 1e-6,
  keeping the correction within 0.3x-0.03x of c_lin's floor), the
  term is too weak to measurably influence routing decisions in
  either direction (Step 66). The underlying signal -- one-step
  Kalman SNR-prediction-error trend -- has median same-sign
  persistence of ~1-4s (p25=1s), comparable to or shorter than a
  single hop's transit time (~2s), and is insufficiently persistent
  to usefully inform forwarding decisions at this site under either
  scaling regime. This is a documented finding, not an unresolved
  bug: beta_1=0 (c_pred=c_lin, LQARP's original factor) is the
  validated default.

### 2.7 Kinematic-aware forwarding delay factor (replaces c_del)

  c_drift(s_i, s_j) = c_del(s_i, s_j) 
                       * (1 + beta_2 * sigmoid(-v_rel(i,j)))

  If i has not yet received a control broadcast from j, 
  c_drift(s_i, s_j) = c_del(s_i, s_j)  (beta_2 term omitted).

  sigmoid(u) = 1 / (1 + exp(-u))
  beta_2 >= 0, swept at run time (compiled default 0.0). beta_2 = 0 recovers 
  LQARP's c_del exactly (same sanity-check requirement as above).

  Addendum (Steps 61–62) — scenario-dependent persistence mismatch,
  not a scale bug. c_drift's beta_2 term shows a smaller, non-
  monotonic cost under Scenario III's rotational eddy drift (Step
  62: 0.998x-1.056x hops across beta_2 in {0, 0.25, 0.5, 1, 2}, no
  value producing improvement). Unlike c_pred, this is not a scale
  bug -- it reflects a genuine mismatch between c_drift's design
  assumption (persistent closing/opening relative motion) and the
  eddy's radial-velocity sign persistence (median 3s, p25 1s,
  comparable to one hop's ~2s transit), per Step 61. c_drift remains
  well-formed and beneficial in scenarios with sustained directional
  relative motion (Scenario II's AUV-sink geometry); its benefit is
  scenario-dependent, not universal. beta_2=0 (c_drift=c_del,
  LQARP's original factor) is the validated default for Scenario
  III; Scenario II may warrant a separate, non-zero beta_2 -- this
  has not yet been swept for Scenario II specifically and remains
  open.

### 2.8 Composite DA-HELLO reward

  R^{a_j}_{s_i,s_j} = c_ene(s_j) * c_drift(s_i, s_j) * c_nei(s_j) 
                      / c_pred(s_i, s_j)

  (c_ene and c_nei UNCHANGED from Part 1.3 -- same formulas, same 
  inputs, no modification)

### 2.9 Q-update (DA-HELLO)

Identical structure to Part 1.4, but over I_DAHELLO(s_i) instead of 
I(s_i), and using the composite reward from 2.8 instead of Part 
1.3's reward:

  Q(s_i, a_j) <- (1-alpha) * Q(s_i, a_j) 
                 + alpha * [ R^{a_j}_{s_i,s_j} 
                     + gamma * max_{a in I_DAHELLO(s_j)} Q(s_j, a) ]

## Part 3: RLOR-inspired baseline (simplified)

LQARP's paper describes RLOR only briefly: "an opportunistic routing 
protocol featuring two reward mechanisms, designed to improve the 
data transmission rate in sparse UWSNs," and separately, "a 
distributed routing strategy in which the packet transmission 
success rate is used to guide next-hop selection." This is not 
enough detail to faithfully reproduce RLOR's actual algorithm.

Class name: `RlorInspiredProtocol`. This is a documented 
simplification, not a faithful reimplementation of RLOR, and must 
not be cited as one. Code carries the same limitation block at the 
top of `rlor-inspired-protocol.h`.

### 3.1 Candidate set (no depth-progressive filter)

Same N_range and N_SNR conditions as LQARP (Part 1.2), WITHOUT the 
N_depth condition (RLOR is described as opportunistic, not 
depth-progressive):

  I_RLOR(s_i) = N_range(s_i) ∩ N_SNR(s_i)

SNR, r_range, SNR_threshold, depth_query floor, and NaN-safe 
comparisons are the shared ChannelGate / BellhopDataLoader path 
(Part 1.2 fairness requirement).

### 3.2 Single reward term

  R(s_i, s_j) = P_succ(s_i, s_j)

using the SAME c_lin / P_succ machinery already built (same 
kBerFloor = 1e-9, same OFDM BER, same l=400, M=2). No c_ene, 
c_del, c_nei, c_pred, or c_drift.

### 3.3 Next-hop selection (greedy opportunistic)

  s_i.NextHop = argmax_{s_j in I_RLOR(s_i)} P_succ(s_i, s_j)

Ties → smallest node id. No Q-table, no TD update, no alpha/gamma. 
This is consistent with RLOR being described as opportunistic 
routing rather than reinforcement-learning-based in the way 
LQARP / DA-HELLO are.

## Part 4: Fixed-Periodic DA-HELLO ablation (Section 4.3)

Variant of `DahelloProtocol` selected by 
`DahelloConfig.broadcastMode = BroadcastMode::FixedPeriodic`.

The event-triggered Broadcast_k condition (Part 2.4) is replaced by 
a fixed periodic broadcast every T_ctrl seconds:

  Broadcast_k = 1  at t = 0, T_ctrl, 2 T_ctrl, ...
              = 0  otherwise

T_ctrl first-pass default is 5.0 s (runtime-overridable).
In Scenarios II and III the ablation uses the same beta_1 / beta_2
as DA-HELLO (validated default 0.0; Steps 60–67) so the comparison
isolates broadcast timing, not the reward weights. The
diagnostic-stage placeholder beta_1=beta_2=1 (Steps 53 / 60) is
retained only as a documented Known Limitation, not as the paper
operating point.

O_ctrl is a shared metric, not a per-protocol formula:
  O_ctrl = (control-packet bytes transmitted on [0, T_sim]) / T_sim
Drain exists so in-flight data packets can finish arriving for
PDR / latency. Drain-period control traffic is not billed. LQARP
and RLOR-inspired have no control plane (O_ctrl = 0). DA-HELLO and
Fixed-Periodic share this window.

Parts 2.1, 2.2, 2.3, 2.5, 2.6, 2.7, 2.8, 2.9 (state, EKF, 
pre-filter, candidate restriction N_stab, c_pred, c_drift, reward, 
Q-update) remain UNCHANGED. This isolates the event-triggering 
contribution from the reward-structure contribution, per Section 
4.3's ablation design.

`BroadcastMode::EventTriggered` is the default (full DA-HELLO).

## Scenario III Gauss-Markov (velocity process only)

Not a network scenario and not traffic/metrics. Discrete 3-D 
velocity with first-pass zeta = 0.95, implemented in 
`gauss-markov.h` as `GaussMarkovConfig` / `gaussMarkovStep`:

  v_{k+1} = zeta * v_k + (1 - zeta) * mu
            + sqrt(1 - zeta^2) * sigma * w_k
  p_{k+1} = p_k + v_{k+1} * dt

  w_k ~ N(0,1) independent per axis. zeta is a run-time field.

Section 4.2 Scenario III traffic is wired (`--scenario=iii`): same
four-protocol pipeline as Scenario II, with per-AUV Rankine +
Gauss-Markov velocity instead of uniform drift. Steps 57–59 were
kinematics-only. Step 60 is the first full comparison and used the
diagnostic placeholder beta_1=beta_2=1. The paper headline table
is Step 70 at the validated default beta_1=beta_2=0.

## Part 9: Scenario III eddy mean flow (kinematics only)

Section 4.2's rotational / eddy drift. One counterclockwise eddy
centred at the volume XY centroid (3000, 3000) m. Horizontal only
(same FLAG as Scenario II: a vertical component exits the 500 m
column in a few hundred seconds).

Rankine-capped tangential mean, so |v_eddy_mean| stays inside the
Section 4.1 [0.5, 3.5] m/s operating range:

  r = ||(x, y) - (3000, 3000)||
  ê_θ = (-(y-3000), x-3000) / r     (counterclockwise; 0 at r=0)
  v_θ(r) = V_eddy * min(r / R_core, 1)
  v_eddy_mean = (v_θ ê_θx, v_θ ê_θy, 0)

  V_eddy = 2.0 m/s   (same midpoint as Scenario II ||v_drift||)
  R_core = 2000 m    (solid-body core; constant |v|=V outside)

Independent per-AUV Gauss-Markov (Part "Scenario III Gauss-Markov"):

  v^i(t+dt) = ζ v^i(t) + (1-ζ) v_eddy_mean(p_i)
              + sqrt(1-ζ²) σ ⊙ w^i(t)

  ζ = 0.95, dt = 1 s, σ = (1, 1, 0) m/s, w^i ~ N(0,I)
  sink velocity = 0, v_cmd = 0
  v^i(0) = v_eddy_mean(p_i(0))

σ_z = 0 is a permanent, documented modeling choice, not a
placeholder and not a diagnostic-only safety rail. Mid-latitude
mesoscale / submesoscale eddies at these length and depth scales
are geostrophic: the dominant flow is horizontal. A 1 m/s vertical
Gauss-Markov innovation would also empty the 500 m column in a few
hundred seconds (the same FLAG that forbade 3-D isotropic drift in
Scenario II). Vertical position stays at the placement z. The
generic stepper still accepts a nonzero σ_z if a later, explicitly
justified experiment sets it; Scenario III does not.

This is the property Scenario II structurally lacked: two AUVs at
different bearings from the eddy centre have different v_eddy_mean,
so true AUV–AUV v_rel is nonzero even with σ=0, and time-varying
once the Gauss-Markov innovation is on.

A Rankine vortex is predominantly rotational. For any pair both
inside the solid-body core, the mean-flow (v_b − v_a) is
identically perpendicular to the pair LOS (ω × r_ij · r_ij = 0).
H_k observes only the radial / range-rate component (Steps 35–38).
Step 59 decomposes true AUV–AUV v_rel into |v_radial| and
|v_tangential| on the same kinematics-only setup as Step 57.
Under the documented process (σ_xy=1, σ_z=0) mean |v_radial| is
1.06 m/s against mean |v_tangential| 2.54 m/s — same order, so
the trigger has an observable range-rate. The Rankine mean flow
alone is mostly tangential (core-core radial ≈ 0); the GM
innovation supplies most of the radial projection.

`--scenario=iii` runs the four-protocol network. Step 60 used
beta_1=beta_2=1 (diagnostic placeholder). Steps 61–62 test whether
c_drift's closing-range assumption holds on the eddy's radial-sign
persistence, and isolate β₂ at β₁=0. Steps 63–67 do the same for
c_pred's Kalman (e_k − e_{k-1}), isolate β₁ at β₂=0, check β₁×β₂
interaction vs Step 60, then test a scale-consistent β₁. Step 70
is the headline four-protocol table at beta_1=beta_2=0.

## Part 5: Non-negotiable implementation constraints (from prior turns)

1. Every protocol variant (LQARP, RLOR, Fixed-Periodic ablation, 
   DA-HELLO) MUST use the same BellhopDataLoader instance for every 
   SNR decision. No protocol may use the phase1 Thorp+Wenz+SL=185 
   formula. This is required for a fair comparative claim.

2. Depth-floor convention for channel lookups only: 
   depth_query = max(receiver_z, 5.0). Raw kinematic z is used 
   unmodified for kinematics, drift, and c_del/c_drift's 
   depth-progress term. A node may set a distinct acoustic query 
   depth (LqarpNode.acousticZ); that value is receiver_z for 
   Bellhop lookups only. The Scenario I sink uses this for a 
   towed/hull-offset USV transducer (kinematic z stays 5 m so the 
   sink remains uniquely shallowest for N_depth).

3. Every SNR comparison against SNR_threshold must guard against NaN 
   explicitly: `!isfinite(snr) || snr < threshold` treated as link 
   failure -- never `snr < threshold` alone, since NaN < threshold 
   evaluates false in IEEE floating point and would silently accept 
   an invalid/out-of-envelope link.

4. RangeAtSnrThreshold-style range queries should scan the real 
   BellhopDataLoader grid at a canonical receiver depth of 50 m 
   (matching the Bellhop source depth and our Step G3 crossing-range 
   analysis: ~5125 m January, ~4525 m July), not an analytic Thorp 
   formula.

5. phase1/ (AquaSimHelloRouting, TopologyDiscoveryApp, ETHA, oracle 
   PickNextHop, EWMA hold-time scoring) and paper/iotj_et_hold.tex 
   are UNRELATED PRIOR WORK. Do not merge, extend, cite, or treat as 
   a reference implementation for LQARP/DA-HELLO. They may remain in 
   the repository untouched as a separate testbed, but the new 
   protocol code (Part 1 and Part 2 above) must be implemented as 
   its own, independent module.

## Part 6: Design-parameter inventory (first-pass defaults)

Compiled constructor defaults, overridable at run time via 
DahelloRunConfig (CLI `--key=value` or a key=value file). Unit-test 
overrides are listed separately; they do not change the compiled 
defaults. beta_1 and beta_2 remain the experimental sweep pair.
The validated paper operating point is 0 / 0 with the event
trigger on (Steps 60–70).

  beta_1     Default 0.0 (validated; c_pred = c_lin). SWEPT at run
             time. Tests: 0.0 (identity), 2.0 unused on beta_1,
             0.5 (14a). O(1) values are diagnostic only (Part 2.6).
  beta_2     Default 0.0 (validated for Scenario III; c_drift =
             c_del). SWEPT at run time. Tests: 0.0, 2.0 (6a/6b),
             0.5 (14a). Scenario II non-zero sweep remains open
             (Part 2.7).
  W_mp       Default 3 (was placeholder 5). Test 14 uses the default.
  W          Default 5. Test 7 overrides W=3.
  rho        Default 0.6. Test 7 overrides rho=1.0.
  epsilon_0  Default 2.0 dB (was 4.0; Step 46). Identity tests 
             use 1e12. Test 7 uses 1.0. Test 14 uses the default.
  s_cap      Default 2.0 dB (kSCapDb = sqrt(rMeas)). Caps 
             lambda*sqrt(S_k) on the trigger (Option B).
  lambda     Default 1.0. Identity tests use 0.0.
  zeta       Default 0.95 on GaussMarkovConfig (Scenario III 
             velocity correlation). Used by `--scenario=iii`.
  T_ctrl     Default 5.0 s. Test 14 uses 5.0 s.

Related EKF numbers: dt=1.0 s, fdRangeM=10 m, qPos=1.0, qVel=0.1, 
qSnr=1.0, rMeas=4.0 dB^2.

## Part 7: Scenario I pipeline (LQARP only)

Stationary Anchor Baseline. Four protocols share the same
BellhopDataLoader / ChannelGate / energy / placement / traffic:
LqarpProtocol, DahelloProtocol (identity: beta_1=beta_2=0, epsilon_0=1e12,
event trigger off), RlorInspiredProtocol (no detour), and
Fixed-Periodic DahelloProtocol (beta_1=beta_2=0, T_ctrl=5 s).
Scenario II (`--scenario=ii`) and Scenario III (`--scenario=iii`)
are wired in the same example.

CLI `--protocol=lqarp|dahello|rlor|periodic`. Default sink acoustic
query depth is 50 m (Step 24); kinematic z stays 5 m.
`--diagCandidates` reports N_range / N_SNR / N_depth binding on
AUV–AUV pairs (Step 25).

Placement: 1 sink at the XY centre of a 6000 m × 6000 m × 500 m 
volume, kinematic depth z=5 m (unique shallowest for N_depth). 
N=20 AUVs drawn i.i.d. uniform in [0,6000]² × [10,500] m (z≥10 m 
so N_depth toward the sink is feasible; z≥5 m is the 
transducer-floor convention). Reject 3-D separation < 1 m. 
Mobility is ns-3 ConstantPositionMobilityModel (v_drift=v_cmd=0 
identically). Default seed 20260921, run 1.

Sink acoustic query depth is independent of kinematic z: CLI 
`--sinkAcousticDepth` (metres), default 5 (the transducer floor). 
BellhopDataLoader queries for this node use that depth; N_depth, 
c_del, and 3-D N_range still see z=5 m. Setting 50 m is a 
towed/hull-offset USV transducer — the same physical justification 
as the general depth_query = max(z, 5.0) convention — and selects 
the Bellhop source-depth slice (canonical 50 m, Step G3). 
`--diagSnrToSink` re-queries the January grid at 5/10/50/100 m 
for the Step 22 (N, seed) placements and exits (no traffic).

Traffic: each AUV emits a 400-bit data packet every 30 s for 
T_sim=1600 s (54 packets/node, 1080 generated). phase1's default 
dataInterval is 12 s (phase2 README cites 16 s); 30 s is an 
independent first-pass choice, not a silent copy. No MAC / 
collisions in this pipeline.

Control: SPEC Part 1 does not define HELLO or a broadcast. Neighbor 
state is the protocol node list. O_ctrl = 0 for this baseline.

Energy: invented in acoustic-energy.h (no prior per-hop deduction 
existed). E_tx from SL=175 dB → P_ac via Urick 170.8 dB/W, eta=0.5; 
E_rx=0.1 W × airtime; R_b=1000 bit/s; no idle listen.

Forwarding: learnFrom → nextHop; VOID if I empty; else deduct E_tx 
and apply ChannelGate::receptionOk (SNR-reception-failure, distinct 
from VOID). Depth-progressive candidates cannot cycle; TTL=32 is a 
safety net.

## Part 8: Scenario II (Linear Uniform Current Drift)

Section 4.2. Same volume, placement, traffic, ChannelGate, energy, 
and sink as Part 7. Differences:

  pdot_i(t) = v_cmd_i(t) + v_drift(t)
  v_cmd_i(t) = 0                          (no active propulsion)
  v_drift(t) = v_drift^(0)                (constant, identical on every AUV)
  pdot_sink(t) = 0                        (sink does not drift)

||v_drift^(0)|| is swept in [0.5, 3.5] m/s in later experiments. The 
first correctness check uses the midpoint 2.0 m/s. Direction is a 
uniform horizontal azimuth drawn from UniformRandomVariable stream 2 
(stream 1 stays the Scenario I placement stream). vz = 0.

FLAG — 3-D isotropic direction was not used. A uniform-on-sphere 
draw at 2 m/s would exit the 500 m depth column in at most 250 s 
for any node with a vertical component. Ocean current is horizontal; 
the interesting relative kinematics with a fixed sink are entirely 
AUV-to-sink. AUV-to-AUV relative velocity is exactly zero under 
shared v_drift and v_cmd=0.

Mobility model: ns-3 ConstantVelocityMobilityModel on every node; 
sink velocity is the zero vector. Protocol node poses are copied 
from ns-3 every forwarding event and every EKF tick (dt=1 s). They 
were previously snapshotted at t=0 (Scenario I, ConstantPosition).

FLAG — volume-exit policy is REMOVE (Step 30). An AUV that leaves
[0,6000]² is taken out of the active simulation (velocity zeroed,
excluded from I and from new traffic). Node event: OUT_OF_ENVELOPE.
In-flight packets at or arriving at that node fail as LOST_NODE_EXIT,
not VOID. `--boundary=open` remains as a HOLD ablation. Reflect /
clamp are not implemented. z-exit is not used (vz=0).

EKF (Part 2.2) ticks under this motion. Neighbor velocity ZOH 
cold-starts at 0 (SPEC 2.1: pdot_j is the last received broadcast; 
none yet — that is a stored placeholder, not a physical claim).
Until i has received at least one broadcast from j, c_drift = c_del
for that pair (SPEC 2.7); beta_2 is not applied against a fabricated
zero neighbor velocity. Predicted SNR uses horizontal range and the
neighbor's queryDepthM() (acousticZ for the sink), matching the
measurement path. Invalid (NaN) measurements skip the EKF update and
do not fire Broadcast_k. The EKF SNR update (H = [0..0 1]) runs every
dt = 1 s and is not a ZOH-velocity measurement; P's velocity block
is not a staleness clock for pdot_j.

CLI: `--scenario=ii --driftSpeed=2 --protocol=lqarp|dahello 
--beta_1=0 --beta_2=0`. Omitting the beta flags now also yields
0 / 0 (validated default). Scenario I `--protocol=dahello` remains
the identity configuration (trigger off, epsilon_0 huge).
`--beta_1=1 --beta_2=1` reproduces the diagnostic-stage
placeholder used in Steps 53 / 60; do not treat those tables as
headline results.

## Known Limitations and Future Work

Steps 60–67. Diagnostic-stage multi-seed tables used
beta_1=beta_2=1 (Scenario II Steps 53–54, Scenario III Step 60).
Those CSVs and reports are preserved as diagnostic artifacts, not
deleted. The validated operating point going forward is
beta_1=beta_2=0 with the event trigger active (Steps 69–70).
DA-HELLO's demonstrated contribution at that point is the
event-triggered discovery layer, not the drift-aware reward terms
as currently formulated.

c_pred (Part 2.6). The additive form
c_pred = c_lin + beta_1*max(0, e_k - e_{k-1}) is scale-inconsistent
when c_lin sits at its BER-floor plateau (~4e-7 for l=400 bits).
At O(1) beta_1 the trend term overrides c_lin rather than blending
with it (four-plus orders of magnitude). At a scale-consistent
beta_1 (1e-5 to 1e-6) the term has no favorable effect either:
the one-step Kalman SNR-prediction-error trend has median
same-sign persistence of ~1–4 s (p25 = 1 s), comparable to or
shorter than one hop (~2 s). beta_1=0 is the validated default.
This is two independent findings (arithmetic scale bug; signal
too weak/short-lived), not one.

c_drift (Part 2.7). Unlike c_pred, beta_2 has no scale bug. Under
Scenario III's rotational eddy the cost is small and non-monotonic
(Step 62: 0.998×–1.056× hops; no value improves on LQARP). The
mechanism is well-formed; the design assumption (persistent
closing/opening relative motion) does not hold at eddy-rotation
timescales (Step 61: median same-sign v_rad = 3 s, p25 = 1 s).
The limitation is scenario-dependent. Scenario II's sustained
AUV–sink range-rate may still warrant a non-zero beta_2; that
sweep has not been run and remains open. beta_2=0 is the
validated default for Scenario III.

Redesign directions not implemented. A relative-form c_pred
(normalizing Delta_e by c_lin's own scale, or a longer-memory
trend statistic averaged over multiple hops rather than one
Kalman step) is a natural next design. It is not implemented or
tested in this work. A Scenario II-specific beta_2 sweep is
likewise future work.