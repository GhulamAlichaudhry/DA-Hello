#!/usr/bin/env python3
"""t109 analysis: per-run CSV (phase1+phase2), cell means, A2.12.4 paired stats (numeric only)."""
import re, json, math, statistics as st, hashlib, csv
from pathlib import Path
from collections import defaultdict

O = Path("${OUT}/t109_p18_20261023")
PHASE1 = json.loads((O / "phase1_jobs.json").read_text())
PHASE2 = json.loads((O / "phase2_jobs.json").read_text())
JOBS = PHASE1 + PHASE2

def num(pat, txt, flags=re.M):
    m = re.search(pat, txt, flags)
    return float(m.group(1)) if m else float("nan")

def parse_run(p: Path):
    t = p.read_text(errors="replace")
    d = {}
    d["wpdr"] = num(r"^window gen=\d+ delivered=\d+ pdr=(\S+)", t)
    d["window_gen"] = num(r"^window gen=(\d+)", t)
    d["window_delivered"] = num(r"^window gen=\d+ delivered=(\d+)", t)
    d["hops_mean"] = num(r"^window gen=\d+ delivered=\d+ pdr=\S+ lat_mean_s=\S+ hops_mean=(\S+)", t)
    d["ebit"] = num(r"^ebit_total_mJ_per_bit window=(\S+)", t)  # mJ/bit
    d["O_ctrl"] = num(r"^O_ctrl=(\S+)", t)
    d["N_gen"] = num(r"^N_gen=(\d+)", t)
    d["N_succ"] = num(r"^N_gen=\d+ N_succ=(\d+)", t)
    d["VOID"] = num(r"VOID \(empty I\)=(\d+)", t)
    d["first_contacts"] = num(r"heard_gaps first_contacts=(\d+)", t)
    d["removed"] = num(r"OUT_OF_ENVELOPE nodes_removed=(\d+)", t)
    if math.isnan(d["removed"]):
        d["removed"] = num(r"n_exit_tsim=(\d+)", t)
    d["rem_frac"] = (d["removed"] / 149.0) if not math.isnan(d["removed"]) else float("nan")
    d["hello_tx"] = num(r"frames kind=hello tx=(\d+)", t)
    m = re.search(r"^OCTRL_SPLIT .*$", t, re.M)
    if m:
        line = m.group(0)
        for k, v in re.findall(
            r"(link|periodic|join_or_boot|hoststate|ebeacon|adaptive|sink_beacon|fc_reply|b6|other)=(\S+)",
            line,
        ):
            d[f"cause_{k}"] = float(v)
        d["total_bytes"] = num(r"total_bytes=(\S+)", line)
        d["total_frames"] = num(r"total_frames=(\S+)", line)
    d["hostst"] = d.get("cause_hoststate", float("nan"))
    d["join"] = d.get("cause_join_or_boot", float("nan"))
    d["fc_reply_octrl"] = d.get("cause_fc_reply", float("nan"))
    d["periodic"] = d.get("cause_periodic", float("nan"))
    d["trickle"] = d.get("cause_adaptive", float("nan"))
    qd = re.search(r"^QELAR_DA .*$", t, re.M)
    if qd:
        line = qd.group(0)
        d["da_margin"] = num(r"margin=(\S+)", line)
        d["mem_events"] = num(r"mem_events=(\S+)", line)
        d["rank_events"] = num(r"rank_events=(\S+)", line)
        d["refresh_events"] = num(r"refresh_events=(\S+)", line)
        d["fc_reply"] = num(r"fc_reply=(\S+)", line)
        d["fc_sched"] = num(r"fc_sched=(\S+)", line)
        d["data_writes_suppressed"] = num(r"data_writes_suppressed=(\S+)", line)
        d["qelarHelloOnly_flag"] = num(r"qelarHelloOnly=(\S+)", line)
    else:
        for k in ("da_margin","mem_events","rank_events","refresh_events","fc_reply",
                  "fc_sched","data_writes_suppressed","qelarHelloOnly_flag"):
            d[k] = float("nan")
    d["hello_fc"] = d.get("fc_reply", float("nan"))
    d["hello_membership"] = d.get("mem_events", float("nan"))
    d["hello_rank"] = d.get("rank_events", float("nan"))
    d["hello_refresh"] = d.get("refresh_events", float("nan"))
    d["hello_periodic"] = d.get("periodic", float("nan"))
    d["hello_trickle"] = d.get("trickle", float("nan"))
    d["hello_total_excl_sink"] = sum(
        d.get(k, 0) or 0 for k in (
            "cause_join_or_boot","cause_hoststate","cause_fc_reply",
            "cause_periodic","cause_adaptive","cause_other","cause_ebeacon","cause_link"
        ) if not math.isnan(d.get(k, float("nan")))
    )
    d["txt_sha"] = hashlib.sha256(t.encode()).hexdigest()
    return d

def tcrit(df):
    table = {1: 12.706, 2: 4.303, 3: 3.182, 4: 2.776, 5: 2.571,
             6: 2.447, 7: 2.365, 8: 2.306, 9: 2.262, 10: 2.228,
             19: 2.093, 20: 2.086, 39: 2.023, 79: 1.990}
    if df in table:
        return table[df]
    try:
        from scipy.stats import t as T
        return float(T.ppf(0.975, df))
    except Exception:
        return 1.96 if df > 40 else 2.042

def mean_ci(xs):
    xs = [x for x in xs if x is not None and not (isinstance(x, float) and math.isnan(x))]
    n = len(xs)
    if n == 0:
        return dict(n=0, mean=float("nan"), lo=float("nan"), hi=float("nan"), sd=float("nan"))
    m = st.mean(xs)
    if n < 2:
        return dict(n=n, mean=m, lo=float("nan"), hi=float("nan"), sd=float("nan"))
    sd = st.stdev(xs)
    half = tcrit(n - 1) * sd / math.sqrt(n)
    return dict(n=n, mean=m, lo=m - half, hi=m + half, sd=sd)

def paired_ci(diffs):
    return mean_ci(diffs)

def holm_adjust(pvals):
    """Return dict index->holm_adjusted_p. pvals is list of (idx, p)."""
    m = len(pvals)
    ordered = sorted(pvals, key=lambda x: x[1])
    adj = {}
    prev = 0.0
    for rank, (idx, p) in enumerate(ordered, 1):
        # Holm: (m-rank+1)*p, monotone nondecreasing
        a = min(1.0, (m - rank + 1) * p)
        a = max(a, prev)
        adj[idx] = a
        prev = a
    return adj

def two_sided_t_p(diffs):
    """Two-sided p from paired t (approximate via scipy if available)."""
    xs = [x for x in diffs if not (isinstance(x, float) and math.isnan(x))]
    n = len(xs)
    if n < 2:
        return float("nan")
    m = st.mean(xs)
    sd = st.stdev(xs)
    if sd == 0:
        return 0.0 if m != 0 else 1.0
    tstat = m / (sd / math.sqrt(n))
    df = n - 1
    try:
        from scipy.stats import t as T
        return float(2 * T.sf(abs(tstat), df))
    except Exception:
        # crude normal approx
        from math import erfc, sqrt
        return float(erfc(abs(tstat) / sqrt(2)))

rows = []
fail_parse = 0
for j in JOBS:
    p = O / j["rundir"] / "run.txt"
    meta = {
        "phase": j.get("phase", 1 if "phase1" in j.get("rundir","") else 2),
        "family": j["family"], "arm": j["arm"], "sc": j["scen"],
        "seed": j["seed"], "rundir": j["rundir"],
        "helloOnly": j.get("helloOnly", j.get("qelarHelloOnly", 0)),
        "qelarDaFc": j.get("qelarDaFc", ""),
    }
    # normalize phase from rundir if needed
    if "phase1" in j["rundir"] or "/phase1/" in j["rundir"]:
        meta["phase"] = 1
    elif "phase2" in j["rundir"]:
        meta["phase"] = 2
    done = (O / j["rundir"] / "DONE_rc0").exists()
    failf = (O / j["rundir"] / "FAIL_rc").exists()
    if not p.exists():
        fail_parse += 1
        rows.append({**meta, "rc": "missing", "wpdr": float("nan"), "zero_delivered": True,
                     "rem_frac": float("nan")})
        continue
    met = parse_run(p)
    wd = met.get("window_delivered", float("nan"))
    zero = (not math.isnan(wd) and wd == 0)
    rc = 0 if done else ("fail" if failf else "unknown")
    rows.append({**meta, "rc": rc, "zero_delivered": bool(zero), **met})

print(f"rows={len(rows)} fail_parse={fail_parse} done={sum(1 for r in rows if r.get('rc')==0)}")

fields = [
    "phase","family","arm","sc","seed","helloOnly","qelarDaFc","rundir","rc",
    "wpdr","ebit","O_ctrl","total_bytes","total_frames",
    "window_gen","window_delivered","hops_mean","N_gen","N_succ","VOID","first_contacts",
    "removed","rem_frac",
    "hostst","join","fc_reply","fc_reply_octrl","periodic","trickle",
    "mem_events","rank_events","refresh_events","da_margin","data_writes_suppressed",
    "hello_fc","hello_membership","hello_rank","hello_refresh","hello_periodic","hello_trickle",
    "hello_total_excl_sink","hello_tx",
    "cause_link","cause_periodic","cause_join_or_boot","cause_hoststate",
    "cause_ebeacon","cause_adaptive","cause_sink_beacon","cause_fc_reply","cause_b6","cause_other",
    "zero_delivered","txt_sha",
]
with open(O / "per_run.csv", "w", newline="") as f:
    w = csv.DictWriter(f, fieldnames=fields, extrasaction="ignore")
    w.writeheader()
    for r in rows:
        w.writerow(r)

# Phase2-only rows for A2.12.4
p2 = [r for r in rows if r.get("phase") == 2 and r.get("rc") == 0]
p1 = [r for r in rows if r.get("phase") == 1 and r.get("rc") == 0]

metrics = [
    "wpdr","ebit","O_ctrl","total_bytes","hops_mean","hostst","join",
    "fc_reply","mem_events","rank_events","refresh_events",
    "hello_periodic","hello_trickle","hello_total_excl_sink","hello_fc",
    "hello_membership","hello_rank","hello_refresh",
    "window_gen","window_delivered","N_gen","N_succ","VOID","first_contacts",
    "rem_frac","data_writes_suppressed",
]
by_cell = defaultdict(list)
for r in p2:
    by_cell[(r["family"], r["arm"], r["sc"])].append(r)

cell_rows = []
for (fam, arm, sc), rs in sorted(by_cell.items()):
    out = {"family": fam, "arm": arm, "sc": sc, "n": len(rs),
           "n_zero": sum(1 for r in rs if r.get("zero_delivered"))}
    for m in metrics:
        ci = mean_ci([r.get(m, float("nan")) for r in rs])
        out[f"{m}_mean"] = ci["mean"]
        out[f"{m}_lo"] = ci["lo"]
        out[f"{m}_hi"] = ci["hi"]
        out[f"{m}_sd"] = ci["sd"]
    cell_rows.append(out)

with open(O / "cell_means_phase2.csv", "w", newline="") as f:
    if cell_rows:
        w = csv.DictWriter(f, fieldnames=list(cell_rows[0].keys()))
        w.writeheader(); w.writerows(cell_rows)

# Index phase2 by (family, arm, sc, seed)
idx = {}
for r in p2:
    idx[(r["family"], r["arm"], r["sc"], int(r["seed"]))] = r

def get_pairs(fam, arm_a, arm_b, sc, metric="wpdr"):
    """Return list of (seed, a-b) for seeds present in both."""
    seeds = sorted({s for (f,a,s_sc,s) in idx if f==fam and a==arm_a and s_sc==sc})
    out = []
    for s in seeds:
        ra = idx.get((fam, arm_a, sc, s))
        rb = idx.get((fam, arm_b, sc, s))
        if ra is None or rb is None:
            continue
        va, vb = ra.get(metric), rb.get(metric)
        if va is None or vb is None or math.isnan(va) or math.isnan(vb):
            continue
        out.append((s, va - vb))
    return out

SCENS = ["I", "II", "GM2"]
MARGIN_PP = 0.03  # disclosed: 3 pp, informed by pilot paired SD 6-10 pp
results = {"margin_pp": 3.0, "margin_note": "3 pp (not 0.5 pp); informed by pilot paired SD 6-10 pp on QELAR; disclosed as such.",
           "Q1": [], "Q1prime": [], "Q2": [], "Q3": [], "descriptive": {}}

# ---- Q1: HELLO DA480 vs T120 per scenario (family of 3)
# lower bound paired PDR diff > -3pp AND upper bound relative control-bytes diff < -40%
q1_raw = []  # for Holm on the PDR tests? A2.12.4 says Holm within each family.
# We'll report both component CIs and apply Holm to two-sided p of PDR paired diffs (family of 3).
for sc in SCENS:
    d_pdr = get_pairs("hello", "DA480", "T120", sc, "wpdr")
    d_bytes = get_pairs("hello", "DA480", "T120", sc, "total_bytes")
    # relative control-bytes: (bytes_DA - bytes_T120) / bytes_T120
    rel_bytes = []
    for s, _ in d_bytes:
        a = idx[("hello","DA480",sc,s)]["total_bytes"]
        b = idx[("hello","T120",sc,s)]["total_bytes"]
        if b and b != 0 and not math.isnan(b) and not math.isnan(a):
            rel_bytes.append((a - b) / b)
    ci_pdr = paired_ci([d for _, d in d_pdr])
    ci_rel = paired_ci(rel_bytes)
    p_pdr = two_sided_t_p([d for _, d in d_pdr])
    entry = {
        "sc": sc, "n": ci_pdr["n"],
        "d_wpdr_mean": ci_pdr["mean"], "d_wpdr_lo": ci_pdr["lo"], "d_wpdr_hi": ci_pdr["hi"], "d_wpdr_sd": ci_pdr["sd"],
        "d_wpdr_p_two_sided": p_pdr,
        "rel_bytes_mean": ci_rel["mean"], "rel_bytes_lo": ci_rel["lo"], "rel_bytes_hi": ci_rel["hi"], "rel_bytes_sd": ci_rel["sd"],
        "statement_components": {
            "d_wpdr_lo": ci_pdr["lo"],
            "rel_bytes_hi": ci_rel["hi"],
            "threshold_d_wpdr_lo_gt": -MARGIN_PP,
            "threshold_rel_bytes_hi_lt": -0.40,
        },
    }
    q1_raw.append(entry)
    results["Q1"].append(entry)

# Holm within Q1 family (on PDR two-sided p)
holm = holm_adjust([(i, e["d_wpdr_p_two_sided"]) for i, e in enumerate(results["Q1"])])
for i, e in enumerate(results["Q1"]):
    e["holm_p_wpdr"] = holm[i]

# ---- Q1': same comparison; PDR lo > -3pp AND upper bounds of paired diffs in bytes AND ebit both < 0
for sc in SCENS:
    d_pdr = [d for _, d in get_pairs("hello", "DA480", "T120", sc, "wpdr")]
    d_bytes = [d for _, d in get_pairs("hello", "DA480", "T120", sc, "total_bytes")]
    d_ebit = [d for _, d in get_pairs("hello", "DA480", "T120", sc, "ebit")]
    ci_pdr = paired_ci(d_pdr)
    ci_b = paired_ci(d_bytes)
    ci_e = paired_ci(d_ebit)
    results["Q1prime"].append({
        "sc": sc, "n": ci_pdr["n"],
        "d_wpdr_mean": ci_pdr["mean"], "d_wpdr_lo": ci_pdr["lo"], "d_wpdr_hi": ci_pdr["hi"],
        "d_bytes_mean": ci_b["mean"], "d_bytes_lo": ci_b["lo"], "d_bytes_hi": ci_b["hi"],
        "d_ebit_mean": ci_e["mean"], "d_ebit_lo": ci_e["lo"], "d_ebit_hi": ci_e["hi"],
        "statement_components": {
            "d_wpdr_lo": ci_pdr["lo"],
            "d_bytes_hi": ci_b["hi"],
            "d_ebit_hi": ci_e["hi"],
            "threshold_d_wpdr_lo_gt": -MARGIN_PP,
            "threshold_d_bytes_hi_lt": 0.0,
            "threshold_d_ebit_hi_lt": 0.0,
        },
    })

# ---- Q2: B0 PDR more than 5 pp below DA480 in every scenario
# report mean (DA480 - B0) and CI; also B0 - DA480
for sc in SCENS:
    # DA480 - B0 (positive => B0 below DA480)
    d = [d for _, d in get_pairs("hello", "DA480", "B0", sc, "wpdr")]
    ci = paired_ci(d)
    results["Q2"].append({
        "sc": sc, "n": ci["n"],
        "d_wpdr_DA480_minus_B0_mean": ci["mean"],
        "d_wpdr_lo": ci["lo"], "d_wpdr_hi": ci["hi"], "d_wpdr_sd": ci["sd"],
        "threshold_mean_gt_pp": 5.0,
        "B0_mean_wpdr": mean_ci([idx[("hello","B0",sc,s)]["wpdr"] for s in sorted({ss for (f,a,sc2,ss) in idx if f=="hello" and a=="B0" and sc2==sc})])["mean"],
        "DA480_mean_wpdr": mean_ci([idx[("hello","DA480",sc,s)]["wpdr"] for s in sorted({ss for (f,a,sc2,ss) in idx if f=="hello" and a=="DA480" and sc2==sc})])["mean"],
    })

# ---- Q3: piggy DA_FCoff_m10 vs alone per scenario (Phase1 gate passed)
for sc in SCENS:
    d_pdr = [d for _, d in get_pairs("piggy", "DA_FCoff_m10", "alone", sc, "wpdr")]
    # relative ebit: (ebit_DA - ebit_alone) / ebit_alone
    rel_e = []
    seeds = sorted({s for (f,a,sc2,s) in idx if f=="piggy" and a=="DA_FCoff_m10" and sc2==sc})
    for s in seeds:
        a = idx.get(("piggy","DA_FCoff_m10",sc,s))
        b = idx.get(("piggy","alone",sc,s))
        if not a or not b: continue
        ea, eb = a["ebit"], b["ebit"]
        if eb and eb != 0 and not math.isnan(ea) and not math.isnan(eb):
            rel_e.append((ea - eb) / eb)
        if a and b:
            pass
    ci_pdr = paired_ci(d_pdr)
    ci_re = paired_ci(rel_e)
    p_pdr = two_sided_t_p(d_pdr)
    results["Q3"].append({
        "sc": sc, "n": ci_pdr["n"],
        "d_wpdr_mean": ci_pdr["mean"], "d_wpdr_lo": ci_pdr["lo"], "d_wpdr_hi": ci_pdr["hi"], "d_wpdr_sd": ci_pdr["sd"],
        "d_wpdr_p_two_sided": p_pdr,
        "rel_ebit_mean": ci_re["mean"], "rel_ebit_lo": ci_re["lo"], "rel_ebit_hi": ci_re["hi"], "rel_ebit_sd": ci_re["sd"],
        "statement_components": {
            "d_wpdr_lo": ci_pdr["lo"],
            "rel_ebit_hi": ci_re["hi"],
            "threshold_d_wpdr_lo_gt": -MARGIN_PP,
            "threshold_rel_ebit_hi_lt": 0.05,
        },
    })
holm3 = holm_adjust([(i, e["d_wpdr_p_two_sided"]) for i, e in enumerate(results["Q3"])])
for i, e in enumerate(results["Q3"]):
    e["holm_p_wpdr"] = holm3[i]

# ---- Descriptive panels
desc = {}
# DA480 vs trickle, vs T60; DA480 vs DA0; FC-on vs FC-off; HELLO counts by cause; rem_frac
for sc in SCENS:
    for arm_a, arm_b, key in [
        ("DA480", "trickle", "DA480_vs_trickle"),
        ("DA480", "T60", "DA480_vs_T60"),
        ("DA480", "DA0", "DA480_vs_DA0"),
    ]:
        d = [x for _, x in get_pairs("hello", arm_a, arm_b, sc, "wpdr")]
        db = [x for _, x in get_pairs("hello", arm_a, arm_b, sc, "total_bytes")]
        de = [x for _, x in get_pairs("hello", arm_a, arm_b, sc, "ebit")]
        desc.setdefault(key, {})[sc] = {
            "d_wpdr": paired_ci(d), "d_bytes": paired_ci(db), "d_ebit": paired_ci(de),
        }
    # FC-on vs FC-off piggy
    d = [x for _, x in get_pairs("piggy", "DA_FCon_m0.5", "DA_FCoff_m10", sc, "wpdr")]
    db = [x for _, x in get_pairs("piggy", "DA_FCon_m0.5", "DA_FCoff_m10", sc, "total_bytes")]
    de = [x for _, x in get_pairs("piggy", "DA_FCon_m0.5", "DA_FCoff_m10", sc, "ebit")]
    desc.setdefault("FCon_m0.5_vs_FCoff_m10", {})[sc] = {
        "d_wpdr": paired_ci(d), "d_bytes": paired_ci(db), "d_ebit": paired_ci(de),
    }

# HELLO counts by cause (means per cell already in cell_means)
results["descriptive"] = desc

# Also write paired_tables.csv for key comparisons
paired_out = []
def add_pair_rows(fam, arm_a, arm_b, label):
    for sc in SCENS:
        for metric in ("wpdr", "total_bytes", "ebit", "O_ctrl", "rem_frac"):
            pairs = get_pairs(fam, arm_a, arm_b, sc, metric)
            if not pairs:
                continue
            ci = paired_ci([d for _, d in pairs])
            paired_out.append({
                "label": label, "family": fam, "arm_a": arm_a, "arm_b": arm_b,
                "sc": sc, "metric": metric, "n": ci["n"],
                "mean_a_minus_b": ci["mean"], "lo": ci["lo"], "hi": ci["hi"], "sd": ci["sd"],
            })
        # relative bytes / ebit
        for metric, rname in (("total_bytes", "rel_bytes"), ("ebit", "rel_ebit")):
            rels = []
            seeds = sorted({s for (f,a,sc2,s) in idx if f==fam and a==arm_a and sc2==sc})
            for s in seeds:
                a = idx.get((fam, arm_a, sc, s))
                b = idx.get((fam, arm_b, sc, s))
                if not a or not b: continue
                va, vb = a.get(metric), b.get(metric)
                if vb and vb != 0 and va is not None and not math.isnan(va) and not math.isnan(vb):
                    rels.append((va - vb) / vb)
            if rels:
                ci = paired_ci(rels)
                paired_out.append({
                    "label": label, "family": fam, "arm_a": arm_a, "arm_b": arm_b,
                    "sc": sc, "metric": rname, "n": ci["n"],
                    "mean_a_minus_b": ci["mean"], "lo": ci["lo"], "hi": ci["hi"], "sd": ci["sd"],
                })

add_pair_rows("hello", "DA480", "T120", "Q1_DA480_vs_T120")
add_pair_rows("hello", "DA480", "B0", "Q2_DA480_vs_B0")
add_pair_rows("piggy", "DA_FCoff_m10", "alone", "Q3_FCoff_vs_alone")
add_pair_rows("hello", "DA480", "trickle", "desc_DA480_vs_trickle")
add_pair_rows("hello", "DA480", "T60", "desc_DA480_vs_T60")
add_pair_rows("hello", "DA480", "DA0", "desc_DA480_vs_DA0")
add_pair_rows("piggy", "DA_FCon_m0.5", "DA_FCoff_m10", "desc_FCon_vs_FCoff")

with open(O / "paired_tables.csv", "w", newline="") as f:
    if paired_out:
        w = csv.DictWriter(f, fieldnames=list(paired_out[0].keys()))
        w.writeheader(); w.writerows(paired_out)

# Serialize results (convert nested mean_ci dicts)
def ser(o):
    if isinstance(o, dict):
        return {k: ser(v) for k, v in o.items()}
    if isinstance(o, list):
        return [ser(v) for v in o]
    if isinstance(o, float) and (math.isnan(o) or math.isinf(o)):
        return None
    return o

(O / "a2124_stats.json").write_text(json.dumps(ser(results), indent=2) + "\n")
print("Wrote per_run.csv, cell_means_phase2.csv, paired_tables.csv, a2124_stats.json")
print("Q1 entries:", len(results["Q1"]))
for e in results["Q1"]:
    print(f"  Q1 {e['sc']}: d_wpdr={e['d_wpdr_mean']:.6f} [{e['d_wpdr_lo']:.6f},{e['d_wpdr_hi']:.6f}] rel_bytes_hi={e['rel_bytes_hi']:.6f} holm_p={e['holm_p_wpdr']:.6g}")
for e in results["Q1prime"]:
    print(f"  Q1' {e['sc']}: d_wpdr_lo={e['d_wpdr_lo']:.6f} d_bytes_hi={e['d_bytes_hi']:.4f} d_ebit_hi={e['d_ebit_hi']:.4f}")
for e in results["Q2"]:
    print(f"  Q2 {e['sc']}: DA480-B0={e['d_wpdr_DA480_minus_B0_mean']:.6f} [{e['d_wpdr_lo']:.6f},{e['d_wpdr_hi']:.6f}]")
for e in results["Q3"]:
    print(f"  Q3 {e['sc']}: d_wpdr={e['d_wpdr_mean']:.6f} [{e['d_wpdr_lo']:.6f},{e['d_wpdr_hi']:.6f}] rel_ebit_hi={e['rel_ebit_hi']:.6f} holm_p={e['holm_p_wpdr']:.6g}")
