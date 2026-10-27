#!/usr/bin/env python3
"""t109 Gate 1: p18 vs p17 bit-identical run.txt (defaults / p17 behaviour)."""
import os, subprocess, hashlib, json
from pathlib import Path

O = Path("${OUT}/t109_p18_20261023/gate1")
O.mkdir(parents=True, exist_ok=True)
NS = "${WORKSPACE}/DA-HELLO/opt/ns-allinone-3.41/ns-3.41/build/lib"
BINS = {
  "p17": ("${WORKSPACE}/DA-HELLO-porta/build_p17/bin_p17/ns3.41-scenario-i-lqarp-optimized",
          "${WORKSPACE}/DA-HELLO-porta/build_p17/lib_p17"),
  "p18": ("${WORKSPACE}/DA-HELLO-porta/build_p18/bin_p18/ns3.41-scenario-i-lqarp-optimized",
          "${WORKSPACE}/DA-HELLO-porta/build_p18/lib_p18"),
}
COMMON = [
  "--dataDir=${WORKSPACE}/DA-HELLO/data/channel_db","--topo=t1","--nAuv=149",
  "--simTime=14400","--warm=900","--drain=300","--quiet=1","--phase=random",
  "--chan=formula","--mac=cs","--disc=heard","--power=s2c","--sinkBeacon=60",
  "--marvPsatScope=top","--marvSinkOverheard=ack","--pktInterval=1200","--tNb=3600",
  "--eddyCx=2000","--eddyCy=2000","--eddyR=2828","--linkOff=1",
  "--scenario=iii","--driftSpeed=0.15","--gmSigma=0.3","--gmZeta=0.85",
  "--seed=20261114","--out=run.txt",
]
CELLS = {
  "qelar_alone": ["--protocol=qelar","--hostFidelity=1"],
  "qelar_da_fix_on": ["--protocol=qelar","--hostFidelity=1","--qelarDaFix=1",
                     "--hostSched=da","--daMargin=0.5","--daImin=10","--daDoublings=5","--daRefresh=0"],
  "qelar_hello_alone": ["--protocol=qelar","--hostFidelity=1","--qelarDaFix=1","--qelarHelloOnly=1"],
  "marv_piggy_Pstar": ["--protocol=marv-dahello-hs","--daMode=2","--bootFix=1",
                      "--daMargin=0.02","--daImin=10","--daRefresh=0","--daDoublings=5","--pollStored=1",
                      "--helloOnlyHost=0"],
  "marv_hello_Pstar": ["--protocol=marv-dahello-hs","--daMode=2","--bootFix=1",
                      "--daMargin=0","--daImin=2","--daRefresh=480","--daDoublings=5","--pollStored=1",
                      "--helloOnlyHost=1"],
}

def run(tag, cell, flags):
  exe, lib = BINS[tag]
  d = O / f"{tag}_{cell}"
  d.mkdir(parents=True, exist_ok=True)
  cmd = [exe] + COMMON[:COMMON.index("--scenario=iii")] + flags + COMMON[COMMON.index("--scenario=iii"):]
  # Fix order: COMMON has scenario at end; put flags before scenario for readability - actually rebuild
  base = [
    "--dataDir=${WORKSPACE}/DA-HELLO/data/channel_db","--topo=t1","--nAuv=149",
    "--simTime=14400","--warm=900","--drain=300","--quiet=1","--phase=random",
    "--chan=formula","--mac=cs","--disc=heard","--power=s2c","--sinkBeacon=60",
    "--marvPsatScope=top","--marvSinkOverheard=ack","--pktInterval=1200","--tNb=3600",
    "--eddyCx=2000","--eddyCy=2000","--eddyR=2828","--linkOff=1",
  ]
  scen = ["--scenario=iii","--driftSpeed=0.15","--gmSigma=0.3","--gmZeta=0.85","--seed=20261114","--out=run.txt"]
  cmd = [exe] + base + flags + scen
  (d/"cmd.txt").write_text(" ".join(cmd)+"\n")
  env = dict(os.environ)
  env["LD_LIBRARY_PATH"] = f"{lib}:{NS}"
  with open(d/"run.log","w") as lo, open(d/"run.err","w") as er:
    rc = subprocess.call(cmd, cwd=str(d), stdout=lo, stderr=er, env=env)
  (d/"rc").write_text(str(rc)+"\n")
  return rc, d

results = {}
any_fail = 0
for cell, flags in CELLS.items():
  for tag in ("p17","p18"):
    print(f"RUNNING {tag} {cell}", flush=True)
    rc, d = run(tag, cell, flags)
    if rc != 0 or not (d/"run.txt").exists():
      print(f"FAIL rc={rc} {d}", flush=True)
      any_fail = 1
      results[f"{tag}_{cell}"] = {"rc": rc, "sha": None}
    else:
      h = hashlib.sha256((d/"run.txt").read_bytes()).hexdigest()
      results[f"{tag}_{cell}"] = {"rc": rc, "sha": h}
      print(f"  sha={h}", flush=True)

summary = []
for cell in CELLS:
  s17 = results.get(f"p17_{cell}",{}).get("sha")
  s18 = results.get(f"p18_{cell}",{}).get("sha")
  ok = s17 is not None and s17 == s18
  if not ok:
    any_fail = 1
  line = f"{cell}: {'PASS' if ok else 'FAIL'} p17={s17} p18={s18}"
  summary.append(line)
  print(line, flush=True)
  (O/f"{cell}.result").write_text(("PASS" if ok else "FAIL")+"\n")
  if s17: (O/f"{cell}.sha").write_text(s17+"\n")

(O/"summary.txt").write_text("\n".join(summary)+f"\nANY_FAIL={any_fail}\n")
(O/"results.json").write_text(json.dumps({"results":results,"any_fail":any_fail}, indent=2)+"\n")
raise SystemExit(any_fail)
