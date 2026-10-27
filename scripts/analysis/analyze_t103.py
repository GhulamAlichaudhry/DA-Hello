#!/usr/bin/env python3
"""t103 post-run analysis. Paired S1/S1'/S2 + descriptive. NUMERIC CIs only; NO pass/fail."""
import re, json, math, statistics as st, hashlib, csv
from pathlib import Path
from collections import defaultdict

O = Path("${OUT}/t103_eval_20261023")
JOBS = [json.loads(l) for l in (O/"jobs_complete.jsonl").read_text().splitlines() if l.strip()]

def num(pat, txt):
    m = re.search(pat, txt, re.M)
    return float(m.group(1)) if m else float("nan")

def parse_run(p: Path):
    t = p.read_text(errors="replace")
    d = {}
    d["wpdr"] = num(r"^window gen=\d+ delivered=\d+ pdr=(\S+)", t)
    d["ebit"] = num(r"^ebit_total_mJ_per_bit window=(\S+)", t)
    d["O_ctrl"] = num(r"^O_ctrl=(\S+)", t)
    d["removed"] = num(r"OUT_OF_ENVELOPE nodes_removed=(\d+)", t)
    if math.isnan(d["removed"]):
        d["removed"] = num(r"n_exit_tsim=(\d+)", t)
    d["hello_tx"] = num(r"frames kind=hello tx=(\d+)", t)
    m = re.search(r"^OCTRL_SPLIT .*$", t, re.M)
    if m:
        for k, v in re.findall(r"(link|periodic|join_or_boot|hoststate|ebeacon|adaptive|sink_beacon|fc_reply|b6|other)=(\S+)", m.group(0)):
            d[f"cause_{k}"] = float(v)
    m = re.search(r"^ctrl_by_cause (.*)$", t, re.M)
    if m:
        for k, v in re.findall(r"(\w+)=(\S+)", m.group(1)):
            d[f"cbc_{k}"] = float(v)
    d["beacon_ok"] = num(r"sink_beacon .*rx_ok=(\d+)", t)
    d["txt_sha"] = hashlib.sha256(t.encode()).hexdigest()
    return d

def tcrit(df):
    try:
        from scipy.stats import t as T
        return float(T.ppf(0.975, df))
    except Exception:
        # common dfs
        table = {9:2.262,19:2.093,29:2.045,39:2.023,38:2.024,37:2.026}
        return table.get(df, 1.96 if df > 40 else 2.042)

def paired_ci(diffs):
    xs = [x for x in diffs if x is not None and not (isinstance(x,float) and math.isnan(x))]
    n = len(xs)
    if n == 0:
        return dict(n=0, mean=float("nan"), lo=float("nan"), hi=float("nan"))
    m = st.mean(xs)
    if n < 2:
        return dict(n=n, mean=m, lo=float("nan"), hi=float("nan"))
    half = tcrit(n-1) * st.stdev(xs) / math.sqrt(n)
    return dict(n=n, mean=m, lo=m-half, hi=m+half)

def holm(pvals):
    """Return adjusted p-values in original order. pvals list of float."""
    m = len(pvals)
    order = sorted(range(m), key=lambda i: pvals[i])
    adj = [None]*m
    prev = 0.0
    for rank, i in enumerate(order):
        a = min(1.0, (m - rank) * pvals[i])
        a = max(a, prev)
        adj[i] = a
        prev = a
    return adj

def ttest_pval(diffs):
    """two-sided paired t p-value for mean(diff)=0."""
    xs = [x for x in diffs if not (isinstance(x,float) and math.isnan(x))]
    n = len(xs)
    if n < 2:
        return float("nan")
    m = st.mean(xs)
    s = st.stdev(xs)
    if s == 0:
        return 0.0 if m != 0 else 1.0
    t = m / (s / math.sqrt(n))
    try:
        from scipy.stats import t as T
        return float(2 * T.sf(abs(t), n-1))
    except Exception:
        # rough normal approx
        return float(2 * math.erfc(abs(t) / math.sqrt(2)))

# ---- parse all unique rundirs referenced by jobs ----
rows = []
fail_parse = 0
seen = set()
for j in JOBS:
    rd = j["rundir"]
    if rd in seen:
        # still emit a logical row pointing at same metrics
        pass
    seen.add(rd)
    p = O / rd / "run.txt"
    meta = {k: j[k] for k in ("camp","host","sc","arm","seed","nAuv","drift","rundir")}
    if not p.exists():
        fail_parse += 1
        rows.append({**meta, "rc": "missing", "wpdr": float("nan")})
        continue
    met = parse_run(p)
    nAuv = j["nAuv"]
    rem = met.get("removed", float("nan"))
    frac = (rem / nAuv) if nAuv and not math.isnan(rem) else float("nan")
    rows.append({**meta, "rc": 0, "relay_removal_frac": frac, **met})

print(f"logical_rows={len(rows)} unique_seen={len(seen)} fail_parse={fail_parse}")

# write per-run CSV
fields = ["camp","host","sc","arm","seed","nAuv","drift","rundir","rc","wpdr","ebit","O_ctrl",
          "hello_tx","removed","relay_removal_frac","beacon_ok",
          "cause_link","cause_periodic","cause_join_or_boot","cause_hoststate",
          "cause_ebeacon","cause_adaptive","cause_sink_beacon","cause_fc_reply",
          "cause_b6","cause_other","cbc_link","cbc_periodic","cbc_bootstrap",
          "cbc_hoststate","cbc_ebeacon","cbc_adaptive","txt_sha"]
with open(O/"t103_per_run.csv","w",newline="") as f:
    w = csv.DictWriter(f, fieldnames=fields, extrasaction="ignore")
    w.writeheader()
    for r in rows:
        w.writerow(r)

# index for pairing: (host,sc,arm,seed,nAuv,drift) -> row
# For main analysis use camp in {C1,C1sat} with nAuv=149 and drift None (locked)
def key(r):
    return (r["host"], r["sc"], r["arm"], int(r["seed"]), int(r["nAuv"]),
            None if r["drift"] is None else float(r["drift"]))

by = {}
for r in rows:
    k = key(r)
    # prefer C1 over C3 for main; first wins if same
    if k not in by:
        by[k] = r

def get(host, sc, arm, seed, nAuv=149, drift=None):
    return by.get((host, sc, arm, seed, nAuv, drift))

SEEDS40 = list(range(20261801, 20261841))
SCS = ["I","II","GM2"]

def pair_metric(host, sc, arm_a, arm_b, metric, seeds=SEEDS40, transform=None):
    diffs = []
    for s in seeds:
        a = get(host, sc, arm_a, s)
        b = get(host, sc, arm_b, s)
        if not a or not b:
            continue
        va, vb = a.get(metric), b.get(metric)
        if va is None or vb is None: continue
        try:
            va, vb = float(va), float(vb)
        except: continue
        if math.isnan(va) or math.isnan(vb): continue
        if transform:
            diffs.append(transform(va, vb))
        else:
            diffs.append(va - vb)
    return diffs

results = {"S1": [], "S1prime": [], "S2": [], "desc_P_vs_B2": [], "desc_means": []}

# S1 family: HELLO-only, P* vs T120, per scenario (3 cells) — PDR pp and O_ctrl relative
# Also B0 vs P* PDR for the "B0 more than 0.5 pp below" descriptive numbers
s1_pdr_pvals = []
s1_entries = []
for sc in SCS:
    d_pdr = pair_metric("hoh", sc, "Pstar", "T120", "wpdr", transform=lambda a,b: 100*(a-b))  # pp
    d_oct = pair_metric("hoh", sc, "Pstar", "T120", "O_ctrl", transform=lambda a,b: (a-b)/b if b else float("nan"))  # relative
    d_oct_abs = pair_metric("hoh", sc, "Pstar", "T120", "O_ctrl")
    ci_p = paired_ci(d_pdr)
    ci_o = paired_ci(d_oct)
    ci_oa = paired_ci(d_oct_abs)
    pv = ttest_pval(d_pdr)
    s1_pdr_pvals.append(pv)
    # B0 - P* PDR
    d_b0 = pair_metric("hoh", sc, "B0", "Pstar", "wpdr", transform=lambda a,b: 100*(a-b))
    ci_b0 = paired_ci(d_b0)
    ent = {"sc": sc, "delta_wpdr_pp_P_minus_T120": ci_p,
           "rel_O_ctrl_P_minus_T120": ci_o,
           "delta_O_ctrl_abs": ci_oa,
           "pct_fewer_bytes_mean": (-ci_o["mean"]*100) if not math.isnan(ci_o["mean"]) else float("nan"),
           "delta_wpdr_pp_B0_minus_P": ci_b0,
           "p_pdr": pv}
    s1_entries.append(ent)

s1_holm = holm([e["p_pdr"] if not math.isnan(e["p_pdr"]) else 1.0 for e in s1_entries])
for e, h in zip(s1_entries, s1_holm):
    e["holm_p_pdr"] = h
results["S1"] = s1_entries

# S1' family: PDR NI + O_ctrl upper<0 + ebit upper<0
s1p = []
for sc in SCS:
    d_pdr = pair_metric("hoh", sc, "Pstar", "T120", "wpdr", transform=lambda a,b: 100*(a-b))
    d_oct = pair_metric("hoh", sc, "Pstar", "T120", "O_ctrl")
    d_eb = pair_metric("hoh", sc, "Pstar", "T120", "ebit")
    s1p.append({
        "sc": sc,
        "delta_wpdr_pp": paired_ci(d_pdr),
        "delta_O_ctrl": paired_ci(d_oct),
        "delta_ebit": paired_ci(d_eb),
        "p_pdr": ttest_pval(d_pdr),
        "p_O_ctrl": ttest_pval(d_oct),
        "p_ebit": ttest_pval(d_eb),
    })
# Holm within S1' across the 9 tests (3 sc x 3 metrics)? Or per metric family?
# A2.7.3: "Holm correction within each family". S1' is one family with 3 scenarios x criteria.
# Apply Holm to the 3 PDR tests, 3 O_ctrl, 3 ebit separately as sub-families, and also report raw.
for metric in ("p_pdr","p_O_ctrl","p_ebit"):
    adj = holm([e[metric] if not math.isnan(e[metric]) else 1.0 for e in s1p])
    for e, a in zip(s1p, adj):
        e[f"holm_{metric}"] = a
results["S1prime"] = s1p

# S2 family: piggyback P* vs B0, per scenario
s2 = []
for sc in SCS:
    d_pdr = pair_metric("piggy", sc, "Pstar", "B0", "wpdr", transform=lambda a,b: 100*(a-b))
    d_eb_rel = pair_metric("piggy", sc, "Pstar", "B0", "ebit",
                           transform=lambda a,b: (a-b)/b if b else float("nan"))
    s2.append({
        "sc": sc,
        "delta_wpdr_pp": paired_ci(d_pdr),
        "rel_ebit": paired_ci(d_eb_rel),
        "p_pdr": ttest_pval(d_pdr),
        "p_ebit_rel": ttest_pval(d_eb_rel),
    })
for metric in ("p_pdr","p_ebit_rel"):
    adj = holm([e[metric] if not math.isnan(e[metric]) else 1.0 for e in s2])
    for e, a in zip(s2, adj):
        e[f"holm_{metric}"] = a
results["S2"] = s2

# Descriptive: P* vs B2* both hosts
desc = []
for host in ("piggy","hoh"):
    for sc in SCS:
        d_pdr = pair_metric(host, sc, "Pstar", "B2star", "wpdr", transform=lambda a,b: 100*(a-b))
        d_oct = pair_metric(host, sc, "Pstar", "B2star", "O_ctrl")
        d_eb = pair_metric(host, sc, "Pstar", "B2star", "ebit",
                           transform=lambda a,b: (a-b)/b if b else float("nan"))
        desc.append({
            "host": host, "sc": sc,
            "delta_wpdr_pp": paired_ci(d_pdr),
            "delta_O_ctrl": paired_ci(d_oct),
            "rel_ebit": paired_ci(d_eb),
        })
results["desc_P_vs_B2"] = desc

# Cell means for main arms (C1)
means = []
for host in ("piggy","hoh"):
    for sc in SCS:
        for arm in ["B0","T20","T40","T60","T120","T5","T10","B2star","Pstar","Pstar_r0","Pstar_r240"]:
            xs_p, xs_e, xs_o, xs_r = [], [], [], []
            for s in SEEDS40:
                r = get(host, sc, arm, s)
                if not r: continue
                for lst, k in ((xs_p,"wpdr"),(xs_e,"ebit"),(xs_o,"O_ctrl"),(xs_r,"relay_removal_frac")):
                    v = r.get(k)
                    try:
                        v = float(v)
                        if not math.isnan(v): lst.append(v)
                    except: pass
            if not xs_p: continue
            means.append({
                "host": host, "sc": sc, "arm": arm, "n": len(xs_p),
                "wpdr_mean": st.mean(xs_p), "wpdr_sd": st.stdev(xs_p) if len(xs_p)>1 else float("nan"),
                "ebit_mean": st.mean(xs_e) if xs_e else float("nan"),
                "O_ctrl_mean": st.mean(xs_o) if xs_o else float("nan"),
                "relay_removal_mean": st.mean(xs_r) if xs_r else float("nan"),
            })
results["desc_means"] = means

# C2 ablation paired vs locked P*
c2 = []
for sc in SCS:
    for arm in ("Pstar_r0","Pstar_r240"):
        d_pdr = pair_metric("hoh", sc, arm, "Pstar", "wpdr", transform=lambda a,b: 100*(a-b))
        d_oct = pair_metric("hoh", sc, arm, "Pstar", "O_ctrl")
        c2.append({"sc": sc, "arm": arm, "delta_wpdr_pp_vs_Pstar": paired_ci(d_pdr),
                   "delta_O_ctrl_vs_Pstar": paired_ci(d_oct)})
results["C2_ablation"] = c2

# C3 summaries (means by nAuv / drift)
c3n = []
for n in [40,70,100,149]:
    for host in ("piggy","hoh"):
        for sc in ("II","GM2"):
            for arm in ("B0","T120","B2star","Pstar"):
                xs_p, xs_o, xs_r = [], [], []
                for s in range(20261801, 20261811):
                    r = get(host, sc, arm, s, nAuv=n)
                    if not r: continue
                    for lst,k in ((xs_p,"wpdr"),(xs_o,"O_ctrl"),(xs_r,"relay_removal_frac")):
                        try:
                            v=float(r.get(k));
                            if not math.isnan(v): lst.append(v)
                        except: pass
                if xs_p:
                    c3n.append({"nAuv":n,"host":host,"sc":sc,"arm":arm,"n":len(xs_p),
                                "wpdr_mean":st.mean(xs_p),"O_ctrl_mean":st.mean(xs_o) if xs_o else float("nan"),
                                "relay_removal_mean":st.mean(xs_r) if xs_r else float("nan")})
results["C3n"] = c3n

c3d = []
for d in [0.1,0.15,0.3,0.5]:
    for host in ("piggy","hoh"):
        for arm in ("B0","T120","B2star","Pstar"):
            xs_p, xs_o, xs_r = [], [], []
            for s in range(20261801, 20261811):
                r = get(host, "II", arm, s, nAuv=149, drift=d)
                if not r: continue
                for lst,k in ((xs_p,"wpdr"),(xs_o,"O_ctrl"),(xs_r,"relay_removal_frac")):
                    try:
                        v=float(r.get(k));
                        if not math.isnan(v): lst.append(v)
                    except: pass
            if xs_p:
                c3d.append({"drift":d,"host":host,"sc":"II","arm":arm,"n":len(xs_p),
                            "wpdr_mean":st.mean(xs_p),"O_ctrl_mean":st.mean(xs_o) if xs_o else float("nan"),
                            "relay_removal_mean":st.mean(xs_r) if xs_r else float("nan")})
results["C3d"] = c3d

(O/"t103_paired.json").write_text(json.dumps(results, indent=2, default=str)+"\n")

# also flat CSVs for means
with open(O/"t103_cell_means.csv","w",newline="") as f:
    w=csv.DictWriter(f, fieldnames=list(means[0].keys()) if means else ["host"])
    w.writeheader(); w.writerows(means)

print("wrote t103_per_run.csv, t103_paired.json, t103_cell_means.csv")
# print compact numeric tables
def fmt_ci(ci, scale=1, places=3):
    if not ci or ci.get("n",0)==0: return "n/a"
    return f"{ci['mean']*scale:.{places}f} [{ci['lo']*scale:.{places}f}, {ci['hi']*scale:.{places}f}] (n={ci['n']})"

print("\n=== S1 (HELLO-only P* vs T120) ===")
for e in results["S1"]:
    print(f"  {e['sc']}: dPDR_pp={fmt_ci(e['delta_wpdr_pp_P_minus_T120'])}  "
          f"rel_Octrl={fmt_ci(e['rel_O_ctrl_P_minus_T120'], places=4)}  "
          f"pct_fewer_mean={e['pct_fewer_bytes_mean']:.2f}%  "
          f"B0-P_pp={fmt_ci(e['delta_wpdr_pp_B0_minus_P'])}")

print("\n=== S1' ===")
for e in results["S1prime"]:
    print(f"  {e['sc']}: dPDR={fmt_ci(e['delta_wpdr_pp'])}  dOctrl={fmt_ci(e['delta_O_ctrl'])}  dEbit={fmt_ci(e['delta_ebit'])}")

print("\n=== S2 (piggy P* vs B0) ===")
for e in results["S2"]:
    print(f"  {e['sc']}: dPDR_pp={fmt_ci(e['delta_wpdr_pp'])}  rel_Ebit={fmt_ci(e['rel_ebit'], places=4)}")
