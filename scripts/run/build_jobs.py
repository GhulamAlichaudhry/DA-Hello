#!/usr/bin/env python3
"""Build the complete t103 job list (C1+C2+C3). Writes jobs.jsonl + jobs_manifest.txt BEFORE any run."""
import json, hashlib, os
from pathlib import Path

O = Path("${OUT}/t103_eval_20261023")
BIN = "${WORKSPACE}/DA-HELLO-porta/build_p15v2/bin_p15v2/ns3.41-scenario-i-lqarp-optimized"
ADD = "${OUT}/portA_eval_addendum_t103_marv_20261023.md"
REG = "${OUT}/portA_eval_addendum_t103_marv_20261023.sha256"

COMMON = [
  "--dataDir=${WORKSPACE}/DA-HELLO/data/channel_db","--topo=t1",
  "--simTime=14400","--warm=900","--drain=300","--quiet=1","--phase=random",
  "--chan=formula","--mac=cs","--disc=heard","--power=s2c","--sinkBeacon=60",
  "--marvPsatScope=top","--marvSinkOverheard=ack","--pktInterval=1200","--tNb=3600",
  "--eddyCx=2000","--eddyCy=2000","--eddyR=2828",
  "--eval=1", f"--evalAddendum={ADD}", f"--evalAddendumRegistry={REG}",
]

SC = {
  "I":   ["--scenario=i"],
  "II":  ["--scenario=iii","--driftSpeed=0.15","--gmSigma=0.3","--gmZeta=0.85"],
  "GM2": ["--scenario=iii","--driftSpeed=0.3","--gmSigma=0.6","--gmZeta=0.85"],
}

def arm_flags(arm, host):
  hoh = "1" if host == "hoh" else "0"
  if arm == "B0":
    return ["--protocol=marv-ctrl-off","--linkOff=1", f"--helloOnlyHost={hoh}"]
  if arm.startswith("T"):
    return [f"--protocol=marv-{arm}","--linkOff=1","--periodicJitter=1", f"--helloOnlyHost={hoh}"]
  if arm == "B2star":
    return ["--protocol=marv-ctrl-off","--b2Trickle=1","--b2Imin=20","--b2Doublings=8",
            "--b2K=5","--b2Incons=table","--linkOff=1", f"--helloOnlyHost={hoh}"]
  if arm == "Pstar":
    if host == "piggy":
      return ["--protocol=marv-dahello-hs","--daMode=2","--bootFix=1","--linkOff=1",
              "--daMargin=0.02","--daImin=10","--daRefresh=0","--daDoublings=5",
              "--pollStored=1", f"--helloOnlyHost={hoh}"]
    else:
      return ["--protocol=marv-dahello-hs","--daMode=2","--bootFix=1","--linkOff=1",
              "--daMargin=0","--daImin=2","--daRefresh=480","--daDoublings=5",
              "--pollStored=1", f"--helloOnlyHost={hoh}"]
  if arm == "Pstar_r0":
    return ["--protocol=marv-dahello-hs","--daMode=2","--bootFix=1","--linkOff=1",
            "--daMargin=0","--daImin=2","--daRefresh=0","--daDoublings=5",
            "--pollStored=1","--helloOnlyHost=1"]
  if arm == "Pstar_r240":
    return ["--protocol=marv-dahello-hs","--daMode=2","--bootFix=1","--linkOff=1",
            "--daMargin=0","--daImin=2","--daRefresh=240","--daDoublings=5",
            "--pollStored=1","--helloOnlyHost=1"]
  raise ValueError(arm)

def slug(flags):
  parts = []
  for e in flags:
    if e.startswith("--eval") or e.startswith("--evalAddendum") or e.startswith("--dataDir"):
      continue
    parts.append(e.lstrip("-").replace("=", "").replace("/", "_"))
  return "_".join(parts)

def make_job(camp, host, sc, arm, seed, nAuv=149, drift=None, gm_sigma=None, gm_zeta=None):
  # build scenario flags; allow drift override for C3b
  if sc == "I":
    scflags = list(SC["I"])
  elif sc == "II":
    d = 0.15 if drift is None else drift
    sig = 0.3 if gm_sigma is None else gm_sigma
    zet = 0.85 if gm_zeta is None else gm_zeta
    scflags = [f"--scenario=iii", f"--driftSpeed={d}", f"--gmSigma={sig}", f"--gmZeta={zet}"]
  elif sc == "GM2":
    d = 0.3 if drift is None else drift
    sig = 0.6 if gm_sigma is None else gm_sigma
    zet = 0.85 if gm_zeta is None else gm_zeta
    scflags = [f"--scenario=iii", f"--driftSpeed={d}", f"--gmSigma={sig}", f"--gmZeta={zet}"]
  else:
    raise ValueError(sc)
  base = arm_flags(arm, host)
  extra = [f"--nAuv={nAuv}"]
  all_flags = COMMON + scflags + base + extra
  cmd = [BIN] + all_flags + [f"--seed={seed}", "--out=run.txt"]
  # rundir key: content-addressed so C1/C3 overlaps share one physical run
  key = slug(scflags + base + extra + [f"seed={seed}"])
  rundir = f"runs/bykey/{key}"
  return {
    "camp": camp, "host": host, "sc": sc, "arm": arm, "seed": seed,
    "nAuv": nAuv, "drift": drift, "rundir": rundir,
    "cmd": cmd, "cmd_str": " ".join(cmd),
  }

jobs = []
SEEDS40 = list(range(20261801, 20261841))
SEEDS10 = list(range(20261801, 20261811))
HOSTS = ["piggy", "hoh"]
SCS = ["I", "II", "GM2"]
MAIN_ARMS = ["B0", "T20", "T40", "T60", "T120", "B2star", "Pstar"]

# C1 main
for host in HOSTS:
  for sc in SCS:
    for arm in MAIN_ARMS:
      for seed in SEEDS40:
        jobs.append(make_job("C1", host, sc, arm, seed))
# C1 T5/T10 saturation in II only
for host in HOSTS:
  for arm in ["T5", "T10"]:
    for seed in SEEDS40:
      jobs.append(make_job("C1sat", host, "II", arm, seed))

# C2 ablation HELLO-only
for arm in ["Pstar_r0", "Pstar_r240"]:
  for sc in SCS:
    for seed in SEEDS40:
      jobs.append(make_job("C2", "hoh", sc, arm, seed))

# C3a nAuv sweep
for n in [40, 70, 100, 149]:
  for sc in ["II", "GM2"]:
    for arm in ["B0", "T120", "B2star", "Pstar"]:
      for host in HOSTS:
        for seed in SEEDS10:
          jobs.append(make_job("C3n", host, sc, arm, seed, nAuv=n))

# C3b drift sweep (II only, locked gmSigma=0.3)
for d in [0.1, 0.15, 0.3, 0.5]:
  for arm in ["B0", "T120", "B2star", "Pstar"]:
    for host in HOSTS:
      for seed in SEEDS10:
        jobs.append(make_job("C3d", host, "II", arm, seed, nAuv=149, drift=d))

assert len(jobs) == 3040, len(jobs)

# unique physical runs
uniq = {}
for j in jobs:
  uniq.setdefault(j["rundir"], j)
print(f"logical_jobs={len(jobs)} unique_rundirs={len(uniq)}")

# write JSONL (complete logical list)
jpath = O / "jobs_complete.jsonl"
with open(jpath, "w") as f:
  for j in jobs:
    f.write(json.dumps(j, sort_keys=True) + "\n")

# human-readable manifest (arm, host, sc, seed, full command)
mpath = O / "jobs_manifest.txt"
with open(mpath, "w") as f:
  f.write(f"# t103 complete job list: {len(jobs)} logical jobs, {len(uniq)} unique physical runs\n")
  f.write(f"# binary={BIN}\n")
  for i, j in enumerate(jobs, 1):
    f.write(f"{i:04d}\tcamp={j['camp']}\thost={j['host']}\tsc={j['sc']}\tarm={j['arm']}\t"
            f"seed={j['seed']}\tnAuv={j['nAuv']}\tdrift={j['drift']}\trundir={j['rundir']}\n")
    f.write(f"     CMD {j['cmd_str']}\n")

# unique jobs for the runner
upath = O / "jobs_unique.json"
with open(upath, "w") as f:
  json.dump(list(uniq.values()), f)

# sha256 of job list files
def sha(p):
  h = hashlib.sha256()
  h.update(Path(p).read_bytes())
  return h.hexdigest()

meta = {
  "logical_jobs": len(jobs),
  "unique_rundirs": len(uniq),
  "jobs_complete.jsonl_sha256": sha(jpath),
  "jobs_manifest.txt_sha256": sha(mpath),
  "jobs_unique.json_sha256": sha(upath),
  "eval_addendum_sha256": sha(ADD),
  "exe_sha256": "e66b8970b6d4ad91cbe939b77b0ba2fb10e21521d68e08f4853fefee4dff7396",
  "lib_sha256": "ad9cc623ee99eb44def7b243bf1b62e3954364b4f213ac486e7ff1ead3f24395",
}
(O / "jobs_BEFORE_meta.json").write_text(json.dumps(meta, indent=2) + "\n")
print(json.dumps(meta, indent=2))
