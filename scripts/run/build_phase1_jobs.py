#!/usr/bin/env python3
import json, hashlib
from pathlib import Path
O = Path("${OUT}/t109_p18_20261023")
EXE = "${WORKSPACE}/DA-HELLO-porta/build_p18/bin_p18/ns3.41-scenario-i-lqarp-optimized"
SEEDS = list(range(20261114, 20261124))
SC = {
  "II":  ["--scenario=iii","--driftSpeed=0.15","--gmSigma=0.3","--gmZeta=0.85"],
  "GM2": ["--scenario=iii","--driftSpeed=0.3","--gmSigma=0.6","--gmZeta=0.85"],
}
COMMON = [
  "--dataDir=${WORKSPACE}/DA-HELLO/data/channel_db","--topo=t1","--nAuv=149",
  "--simTime=14400","--warm=900","--drain=300","--quiet=1","--phase=random",
  "--chan=formula","--mac=cs","--disc=heard","--power=s2c","--sinkBeacon=60",
  "--marvPsatScope=top","--marvSinkOverheard=ack","--pktInterval=1200","--tNb=3600",
  "--eddyCx=2000","--eddyCy=2000","--eddyR=2828","--linkOff=1",
  "--protocol=qelar","--hostFidelity=1","--qelarDaFix=1","--qelarDaFc=0","--qelarHelloOnly=0",
]
ARMS = {
  "alone": [],
  "da_m0.02": ["--hostSched=da","--daMargin=0.02","--daImin=10","--daDoublings=5","--daRefresh=0"],
  "da_m0.5":  ["--hostSched=da","--daMargin=0.5","--daImin=10","--daDoublings=5","--daRefresh=0"],
  "da_m10":   ["--hostSched=da","--daMargin=10","--daImin=10","--daDoublings=5","--daRefresh=0"],
}
jobs=[]
for arm, afl in ARMS.items():
  for sc, sfl in SC.items():
    for seed in SEEDS:
      rundir = f"runs/phase1/{arm}/{sc}/s{seed}"
      cmd = [EXE]+COMMON+sfl+afl+[f"--seed={seed}","--out=run.txt"]
      jobs.append({"phase":1,"family":"piggy_fc0","arm":arm,"scen":sc,"seed":seed,
                   "rundir":rundir,"cmd":cmd,"helloOnly":0,"qelarDaFc":0})
assert len(jobs)==80
(O/"phase1_jobs.json").write_text(json.dumps(jobs, indent=2)+"\n")
(O/"phase1_jobs.jsonl").write_text("\n".join(json.dumps(j,separators=(',',':')) for j in jobs)+"\n")
h=hashlib.sha256((O/"phase1_jobs.json").read_bytes()).hexdigest()
(O/"phase1_jobs.json.sha256").write_text(h+"\n")
print("n=",len(jobs),"sha=",h)
