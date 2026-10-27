# Port A eval addendum — t109 QELAR Phase 2 campaign (A2.12)

**SIGNED** with Amendment 2 v2.9 A2.12 (Ali t151u, 2026-10-23 CST: "approved from ali").
Signed file: ${OUT}/t109_addendum_and_prompt_SIGNED_20261023.md
sha256 3c903bfda3fb91ba470098df4f5beed2cc96fa5547d9b29087b619f2da5a4e39.

Binary p18 exe c57ce18e9a580655818bf06e9ecab73cefcbdb870e7d2e5f41eeb55581152f56.
p17 exe untouched 6305cb60f7814a3a6dcdde3c8d63ce89481d26e47d12523307d4cb8aacb0bf99.

## Frozen before Phase 2 evaluation runs

- Evaluation seeds: 20261841–20261880 ONLY (40 seeds). Not 20261801–40.
- Geometry: eddyCx=2000 eddyCy=2000 eddyR=2828; I static; II V=0.15 σ=0.3; GM2 V=0.3 σ=0.6.
- Common: --marvPsatScope=top --linkOff=1 --disc=heard --hostFidelity=1 --qelarDaFix=1.
- Phase1 (tuning 20261114–23, piggyback, --qelarDaFc=0): chosen margin=10.0 arm=da_m10; pooled paired PDR loss vs alone=0.005716 (0.572 pp); gate_5pp_pass=True; include_piggy_da_fc_off_in_phase2=True.
- HELLO-only panel: B0; T60 jittered; T120 jittered; trickle; DA480 (daMargin=0 daImin=2 daDoublings=5 daRefresh=480); DA0 (daRefresh=0). 6×3×40=720.
- Piggyback panel: alone; T60; T120; trickle; DA FC-off margin=10.0; DA FC-on daMargin=0.5 descriptive. 6×3×40=720.
- Statements A2.12.4: paired by seed, Student-t 95%, Holm within family. Margin 3 pp (informed by pilot paired SD 6–10 pp; disclosed). Q1/Q1′/Q2/Q3 as in signed A2.12. No pass/fail wording in report.
- Stopping: >2% fail. Re-run only identical cmd after rc≠0.
