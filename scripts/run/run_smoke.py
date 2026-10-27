#!/usr/bin/env python3
"""A2.12.2 smoke: daMargin 0.02 vs 10 with qelarDaFc=0; rank must differ; fc_reply=0."""
import os, subprocess, re
from pathlib import Path
O = Path("${OUT}/t109_p18_20261023/smoke")
O.mkdir(parents=True, exist_ok=True)
EXE = "${WORKSPACE}/DA-HELLO-porta/build_p18/bin_p18/ns3.41-scenario-i-lqarp-optimized"
LIB = "${WORKSPACE}/DA-HELLO-porta/build_p18/lib_p18"
NS = "${WORKSPACE}/DA-HELLO/opt/ns-allinone-3.41/ns-3.41/build/lib"
env = dict(os.environ); env["LD_LIBRARY_PATH"]=f"{LIB}:{NS}"
base = [
  EXE,"--dataDir=${WORKSPACE}/DA-HELLO/data/channel_db","--topo=t1","--nAuv=149",
  "--simTime=14400","--warm=900","--drain=300","--quiet=1","--phase=random",
  "--chan=formula","--mac=cs","--disc=heard","--power=s2c","--sinkBeacon=60",
  "--marvPsatScope=top","--marvSinkOverheard=ack","--pktInterval=1200","--tNb=3600",
  "--eddyCx=2000","--eddyCy=2000","--eddyR=2828","--linkOff=1",
  "--protocol=qelar","--hostFidelity=1","--qelarDaFix=1","--qelarDaFc=0","--qelarHelloOnly=0",
  "--scenario=iii","--driftSpeed=0.15","--gmSigma=0.3","--gmZeta=0.85",
  "--hostSched=da","--daImin=10","--daDoublings=5","--daRefresh=0",
  "--seed=20261114","--out=run.txt",
]
out = {}
for m in (0.02, 10):
  d = O / f"m{m}"
  d.mkdir(parents=True, exist_ok=True)
  cmd = base[:-2] + [f"--daMargin={m}", "--seed=20261114", "--out=run.txt"]
  (d/"cmd.txt").write_text(" ".join(cmd)+"\n")
  with open(d/"run.log","w") as lo, open(d/"run.err","w") as er:
    rc = subprocess.call(cmd, cwd=str(d), stdout=lo, stderr=er, env=env)
  t = (d/"run.txt").read_text() if (d/"run.txt").exists() else ""
  def num(pat):
    mm = re.search(pat, t, re.M)
    return float(mm.group(1)) if mm else float("nan")
  row = {
    "rc": rc,
    "rank_events": num(r"rank_events=(\S+)"),
    "mem_events": num(r"mem_events=(\S+)"),
    "fc_reply": num(r"QELAR_DA .*fc_reply=(\S+)"),
    "fc_reply_octrl": num(r"OCTRL_SPLIT .*fc_reply=(\S+)"),
  }
  out[str(m)] = row
  print(m, row, flush=True)

r02, r10 = out["0.02"], out["10"]
ok = (r02["rc"]==0 and r10["rc"]==0
      and r02["rank_events"] != r10["rank_events"]
      and r02["fc_reply"]==0 and r10["fc_reply"]==0
      and r02["fc_reply_octrl"]==0 and r10["fc_reply_octrl"]==0)
(O/"smoke.json").write_text(__import__("json").dumps({"ok":ok,"rows":out}, indent=2)+"\n")
print("SMOKE", "PASS" if ok else "FAIL", flush=True)
raise SystemExit(0 if ok else 2)
