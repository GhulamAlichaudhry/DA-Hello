#!/usr/bin/env python3
"""A2.12.3 Phase2 job list. Write BEFORE first Phase2 run."""
import json, hashlib
from pathlib import Path

O = Path("${OUT}/t109_p18_20261023")
EXE = "${WORKSPACE}/DA-HELLO-porta/build_p18/bin_p18/ns3.41-scenario-i-lqarp-optimized"
SEEDS = list(range(20261841, 20261881))  # 40 eval seeds
assert len(SEEDS)==40
dec = json.loads((O/"phase1_selection.json").read_text())
margin = dec["chosen_margin"]  # 10.0
include_fc_off = dec["include_piggy_da_fc_off_in_phase2"]

SC = {
  "I":   ["--scenario=i"],
  "II":  ["--scenario=iii","--driftSpeed=0.15","--gmSigma=0.3","--gmZeta=0.85"],
  "GM2": ["--scenario=iii","--driftSpeed=0.3","--gmSigma=0.6","--gmZeta=0.85"],
}
COMMON = [
  "--dataDir=${WORKSPACE}/DA-HELLO/data/channel_db","--topo=t1","--nAuv=149",
  "--simTime=14400","--warm=900","--drain=300","--quiet=1","--phase=random",
  "--chan=formula","--mac=cs","--disc=heard","--power=s2c","--sinkBeacon=60",
  "--marvPsatScope=top","--marvSinkOverheard=ack","--pktInterval=1200","--tNb=3600",
  "--eddyCx=2000","--eddyCy=2000","--eddyR=2828","--linkOff=1",
  "--protocol=qelar","--hostFidelity=1","--qelarDaFix=1",
]

def hello_arms():
  return {
    "B0": [],
    "T60": ["--hostSched=periodic","--tCtrl=60","--periodicJitter=1"],
    "T120": ["--hostSched=periodic","--tCtrl=120","--periodicJitter=1"],
    "trickle": ["--hostSched=trickle"],
    "DA480": ["--hostSched=da","--daMargin=0","--daImin=2","--daDoublings=5","--daRefresh=480"],
    "DA0": ["--hostSched=da","--daMargin=0","--daImin=2","--daDoublings=5","--daRefresh=0"],
  }

def piggy_arms():
  arms = {
    "alone": [],
    "T60": ["--hostSched=periodic","--tCtrl=60","--periodicJitter=1"],
    "T120": ["--hostSched=periodic","--tCtrl=120","--periodicJitter=1"],
    "trickle": ["--hostSched=trickle"],
    "DA_FCon_m0.5": ["--qelarDaFc=1","--hostSched=da","--daMargin=0.5","--daImin=10","--daDoublings=5","--daRefresh=0"],
  }
  if include_fc_off:
    # chosen margin from Phase1; FC off
    m = margin
    # format without trailing .0 if int
    mstr = str(int(m)) if float(m)==int(m) else str(m)
    arms[f"DA_FCoff_m{mstr}"] = [
      "--qelarDaFc=0","--hostSched=da",f"--daMargin={m}",
      "--daImin=10","--daDoublings=5","--daRefresh=0"]
  return arms

jobs=[]
# HELLO-only panel
for arm, afl in hello_arms().items():
  for sc, sfl in SC.items():
    for seed in SEEDS:
      rundir=f"runs/phase2/hello/{arm}/{sc}/s{seed}"
      cmd=[EXE]+COMMON+["--qelarHelloOnly=1","--qelarDaFc=1"]+sfl+afl+[f"--seed={seed}","--out=run.txt","--recWriteOut=recwrites.txt"]
      jobs.append({"phase":2,"family":"hello","arm":arm,"scen":sc,"seed":seed,
                   "rundir":rundir,"cmd":cmd,"helloOnly":1,"qelarDaFc":1})

# Piggyback panel
for arm, afl in piggy_arms().items():
  # default FC on unless arm specifies qelarDaFc=0
  fc = 0 if "FCoff" in arm else 1
  for sc, sfl in SC.items():
    for seed in SEEDS:
      rundir=f"runs/phase2/piggy/{arm}/{sc}/s{seed}"
      # ensure qelarDaFc present
      flags=list(afl)
      if not any(a.startswith("--qelarDaFc=") for a in flags):
        flags = [f"--qelarDaFc={fc}"] + flags
      cmd=[EXE]+COMMON+["--qelarHelloOnly=0"]+sfl+flags+[f"--seed={seed}","--out=run.txt"]
      jobs.append({"phase":2,"family":"piggy","arm":arm,"scen":sc,"seed":seed,
                   "rundir":rundir,"cmd":cmd,"helloOnly":0,"qelarDaFc":fc})

# counts
from collections import Counter
c=Counter((j["family"],j["arm"],j["scen"]) for j in jobs)
print("cells", len(c), "jobs", len(jobs))
for k in sorted(c):
  print(k, c[k])
assert all(j["seed"] in range(20261841,20261881) for j in jobs)
assert not any("--eval" in a for j in jobs for a in j["cmd"])
expected = 720 + (720 if include_fc_off else 600)
# piggy arms: 5 or 6
n_piggy_arms = len(piggy_arms())
assert len(jobs) == 6*3*40 + n_piggy_arms*3*40

(O/"phase2_jobs.json").write_text(json.dumps(jobs, indent=2)+"\n")
(O/"phase2_jobs.jsonl").write_text("\n".join(json.dumps(j,separators=(',',':')) for j in jobs)+"\n")
h=hashlib.sha256((O/"phase2_jobs.json").read_bytes()).hexdigest()
hl=hashlib.sha256((O/"phase2_jobs.jsonl").read_bytes()).hexdigest()
(O/"phase2_jobs.json.sha256").write_text(h+"\n")
meta={
  "n_jobs": len(jobs),
  "jobs.json_sha256": h,
  "jobs.jsonl_sha256": hl,
  "include_piggy_da_fc_off": include_fc_off,
  "chosen_margin": margin,
  "n_hello_arms": 6,
  "n_piggy_arms": n_piggy_arms,
  "seeds": SEEDS,
  "scenarios": ["I","II","GM2"],
}
(O/"phase2_jobs_meta.json").write_text(json.dumps(meta, indent=2)+"\n")
print("SHA", h)
print("n_jobs", len(jobs))
