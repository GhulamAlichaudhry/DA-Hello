# Pre-registration — decision-aware E-LQTP evaluation

Written 2026-10-12 (CST) before any tuning or evaluation run. Design: `${OUT}/decision_aware_design.md`.

## Seeds
- **Tuning:** 20260921–20260925 only (MARV, scenarios I, II, III, HD, GM2, WP, N = 40).
- **Evaluation:** 20261001–20261020 (20 fresh seeds, never used before), paired across all arms.

## Tuning rule (the only free parameter is the rank margin m)
Candidates m ∈ {0, 0.005, 0.02}. On tuning seeds, for arm "final": pick the m with the lowest mean O_total over the 6 scenarios among those whose mean ΔPDR(final − periodic 5 s) ≥ −0.1 pp in every scenario. Ties (O_total within 0.5 %) go to the smallest m. If none qualifies, take m = 0. I_min = 5 s, I_max = 80 s and T_nb = 80 s are fixed (not tuned). E-LQTP link parameters stay at their defaults. **No parameter changes after evaluation data are seen;** if one were unavoidable, both results would be reported.

## Hypotheses (MARV, N = 40, evaluation seeds; paired differences with 95 % Student-t CIs)
- **H1 (primary).** For each of the 6 scenarios (I, II, III, HD, GM2, WP), all three must hold:
  (a) non-inferiority to periodic 5 s: lower 95 % CI bound of ΔPDR(final − P5) > −0.1 pp;
  (b) O_total(final) < O_total(Trickle, event-reset version): upper 95 % CI bound of the paired difference < 0;
  (c) E_bit_total(final) < E_bit_total(Trickle, event-reset): upper CI bound < 0.
  H1 passes only if (a)–(c) hold in all 6 scenarios. Secondary reports: the same tests against Trickle-strict, a pooled test over all 120 paired runs, and the count of scenarios passing.
- **H2 (scaling).** MARV, scenarios II and III, N ∈ {20, 40, 60, 80} in the fixed 6×6×0.5 km volume. For each seed, fit the slope b of log O_total vs log N for "final" and for current E-LQTP (full). H2 passes if the paired slope difference b_final − b_E-LQTP has an upper 95 % CI bound < 0 in both scenarios. Also reported: O_total ratio final/E-LQTP at N = 80, and final vs Trickle at each N.

## Secondary / exploratory (not used for pass/fail)
Delay, hops, O_ctrl; speed sweep 0.5–3.5 m/s (MARV II); LQARP/QELAR/SAQR on the 6 scenarios; the "final+link" arm; DA and DA+BO arms (component attribution); DA event counts (strong/weak/deferred).

## Analysis
Paired by seed within scenario/N/speed. Two-sided 95 % t-intervals. No outlier removal. Runs that fail (rc ≠ 0) are reported, not dropped silently.

## Addendum: tuning outcome (recorded before any evaluation run)
Tuning runs: `results/novelty_da_tune/` (120 runs, seeds 20260921–25; table in `tuning_result.md`). All three margins are non-inferior to P5 in all 6 scenarios on the tuning seeds. Mean O_total: m=0 → 8.85 B/s, m=0.005 → 8.30, m=0.02 → 8.30 (tie). **Selected m = 0.005** (lowest O_total; tie broken to the smaller m). At m ≥ 0.005 no rank (weak) events fire on the tuning seeds, so in practice DA reduces to the membership (void-flip-with-upstream) rule. This matches the expectation stated in the design (§1.2).
