#!/usr/bin/env python3
"""Phase1 selection + 5pp gate. Mechanical per A2.12.2."""
import re, json, math, statistics as st, hashlib
from pathlib import Path
from collections import defaultdict

O = Path("${OUT}/t109_p18_20261023")
jobs = json.loads((O/"phase1_jobs.json").read_text())

def num(pat, t):
  m = re.search(pat, t, re.M)
  return float(m.group(1)) if m else float("nan")

def parse(p):
  t = p.read_text(errors="replace")
  d = {}
  d["wpdr"] = num(r"^window gen=\d+ delivered=\d+ pdr=(\S+)", t)
  d["ebit"] = num(r"^ebit_total_mJ_per_bit window=(\S+)", t)
  if math.isnan(d["ebit"]):
    d["ebit"] = num(r"^ebit=(\S+)", t)
    if not math.isnan(d["ebit"]):
      d["ebit"] *= 1000.0  # J/bit -> mJ/bit
  d["O_ctrl"] = num(r"^O_ctrl=(\S+)", t)
  d["total_bytes"] = num(r"OCTRL_SPLIT .* total_bytes=(\S+)", t)
  d["hops"] = num(r"^mean_hops_succ=(\S+)", t)
  d["fc_reply"] = num(r"QELAR_DA .*fc_reply=(\S+)", t)
  d["mem_events"] = num(r"mem_events=(\S+)", t)
  d["rank_events"] = num(r"rank_events=(\S+)", t)
  d["refresh_events"] = num(r"refresh_events=(\S+)", t)
  d["removed"] = num(r"OUT_OF_ENVELOPE nodes_removed=(\d+)", t)
  m = re.search(r"^OCTRL_SPLIT .*$", t, re.M)
  if m:
    for k,v in re.findall(r"(link|periodic|join_or_boot|hoststate|ebeacon|adaptive|sink_beacon|fc_reply|b6|other)=(\S+)", m.group(0)):
      d[f"cause_{k}"] = float(v)
  # hello total excl sink ≈ total_frames - sink_beacon
  d["hello_total_excl_sink"] = (d.get("cause_link",0)+d.get("cause_periodic",0)+d.get("cause_join_or_boot",0)
    +d.get("cause_hoststate",0)+d.get("cause_ebeacon",0)+d.get("cause_adaptive",0)
    +d.get("cause_fc_reply",0)+d.get("cause_b6",0)+d.get("cause_other",0))
  d["txt_sha"] = hashlib.sha256(t.encode()).hexdigest()
  return d

def tcrit(df):
  # two-sided 95% Student-t critical values
  table = {1:12.706,2:4.303,3:3.182,4:2.776,5:2.571,6:2.447,7:2.365,8:2.306,9:2.262,
           10:2.228,11:2.201,12:2.179,13:2.160,14:2.145,15:2.131,19:2.093,20:2.086,
           29:2.045,30:2.042,39:2.023,40:2.021,59:2.001,60:2.000,119:1.980}
  if df in table: return table[df]
  # approx
  if df>=120: return 1.96
  # linear interp between known
  keys=sorted(table)
  for i in range(len(keys)-1):
    if keys[i]<=df<=keys[i+1]:
      a,b=keys[i],keys[i+1]
      return table[a]+(table[b]-table[a])*(df-a)/(b-a)
  return 1.96

def mean_ci(xs):
  xs=[x for x in xs if not math.isnan(x)]
  n=len(xs)
  if n==0: return {"mean":float("nan"),"lo":float("nan"),"hi":float("nan"),"n":0,"sd":float("nan")}
  m=st.mean(xs)
  if n==1: return {"mean":m,"lo":m,"hi":m,"n":1,"sd":0.0}
  sd=st.stdev(xs)
  half=tcrit(n-1)*sd/math.sqrt(n)
  return {"mean":m,"lo":m-half,"hi":m+half,"n":n,"sd":sd}

rows=[]
for j in jobs:
  d=O/j["rundir"]
  met=parse(d/"run.txt") if (d/"run.txt").exists() else {}
  rc=0 if (d/"DONE_rc0").exists() else 1
  rows.append({**{k:j[k] for k in ("phase","family","arm","scen","seed","rundir")}, "rc":rc, **met})

# index alone by (scen,seed)
alone={(r["scen"],r["seed"]):r for r in rows if r["arm"]=="alone" and r["rc"]==0}

# per-arm pooled loss vs alone
arms=["da_m0.02","da_m0.5","da_m10"]
sel=[]
for arm in arms:
  losses=[]  # alone - arm (positive = arm worse)
  hellos=[]
  for r in rows:
    if r["arm"]!=arm or r["rc"]!=0: continue
    a=alone.get((r["scen"],r["seed"]))
    if not a: continue
    losses.append(a["wpdr"]-r["wpdr"])
    hellos.append(r.get("hello_total_excl_sink", float("nan")))
  ci=mean_ci(losses)
  hci=mean_ci(hellos)
  sel.append({"arm":arm,"pooled_loss_mean":ci["mean"],"pooled_loss_lo":ci["lo"],"pooled_loss_hi":ci["hi"],
              "n":ci["n"],"sd":ci["sd"],"hello_mean":hci["mean"]})

# selection: smallest pooled loss; within 0.5 pp tie -> fewer HELLOs
best=min(s["pooled_loss_mean"] for s in sel)
cands=[s for s in sel if (s["pooled_loss_mean"]-best)*100.0 <= 0.5 + 1e-12]
cands.sort(key=lambda s: (s["pooled_loss_mean"], s["hello_mean"]))
chosen=cands[0]
gate_pass = chosen["pooled_loss_mean"] <= 0.05 + 1e-12  # loss > 5pp => drop
decision={
  "chosen_arm": chosen["arm"],
  "chosen_margin": float(chosen["arm"].replace("da_m","")),
  "pooled_loss_mean": chosen["pooled_loss_mean"],
  "pooled_loss_pp": chosen["pooled_loss_mean"]*100,
  "pooled_loss_ci": [chosen["pooled_loss_lo"], chosen["pooled_loss_hi"]],
  "hello_mean": chosen["hello_mean"],
  "gate_5pp_pass": gate_pass,
  "include_piggy_da_fc_off_in_phase2": gate_pass,
  "all_arms": sel,
  "tie_cands": [c["arm"] for c in cands],
}
(O/"phase1_selection.json").write_text(json.dumps(decision, indent=2)+"\n")

# per cell means
cells=[]
for arm in ["alone"]+arms:
  for sc in ["II","GM2"]:
    xs=[r for r in rows if r["arm"]==arm and r["scen"]==sc and r["rc"]==0]
    w=mean_ci([r["wpdr"] for r in xs])
    cells.append({"arm":arm,"sc":sc,"n":w["n"],"wpdr_mean":w["mean"],"wpdr_lo":w["lo"],"wpdr_hi":w["hi"],
                  "hello_mean":mean_ci([r.get("hello_total_excl_sink",float("nan")) for r in xs])["mean"],
                  "fc_reply_mean":mean_ci([r.get("fc_reply",float("nan")) for r in xs])["mean"],
                  "rank_mean":mean_ci([r.get("rank_events",float("nan")) for r in xs])["mean"]})
(O/"phase1_cell_means.json").write_text(json.dumps(cells, indent=2)+"\n")
print(json.dumps(decision, indent=2))
print("--- cells ---")
for c in cells:
  print(f"{c['arm']:10s} {c['sc']:3s} wpdr={c['wpdr_mean']:.6f} hello={c['hello_mean']:.1f} fc={c['fc_reply_mean']:.1f} rank={c['rank_mean']:.1f}")
