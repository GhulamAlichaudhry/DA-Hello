#!/usr/bin/env python3
"""t109 runner. Resume-safe. Stop if >2% fail."""
import os, subprocess, sys, json, time
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path

O = Path("${OUT}/t109_p18_20261023")
LIB = "${WORKSPACE}/DA-HELLO-porta/build_p18/lib_p18"
NS = "${WORKSPACE}/DA-HELLO/opt/ns-allinone-3.41/ns-3.41/build/lib"
FAIL_FRAC_STOP = 0.02
TUNING = set(range(20261114, 20261124))
EVAL = set(range(20261841, 20261881))
ALLOWED = TUNING | EVAL

def run_one(job):
  seed = int(job["seed"])
  assert seed in ALLOWED, f"forbidden seed {seed}"
  has_eval = any(a == "--eval=1" or a.startswith("--eval=1") for a in job["cmd"])
  if seed in EVAL:
    assert has_eval, f"eval seed {seed} requires --eval=1"
  else:
    assert not has_eval, f"tuning seed {seed} must not set --eval=1"
  d = O / job["rundir"]
  d.mkdir(parents=True, exist_ok=True)
  done = d / "DONE_rc0"
  if done.exists() and (d / "run.txt").exists():
    return str(d), 0, "skip"
  cmd = job["cmd"]
  env = dict(os.environ)
  env["LD_LIBRARY_PATH"] = f"{LIB}:{NS}"
  (d / "cmd.txt").write_text(" ".join(cmd) + "\n")
  with open(d / "run.log", "w") as lo, open(d / "run.err", "w") as er:
    rc = subprocess.call(cmd, cwd=str(d), stdout=lo, stderr=er, env=env)
  if rc == 0 and (d / "run.txt").exists():
    done.write_text("ok\n")
  else:
    with open(d / "run.log", "a") as lo, open(d / "run.err", "a") as er:
      lo.write("\n# RETRY identical command\n")
      er.write("\n# RETRY identical command\n")
      rc2 = subprocess.call(cmd, cwd=str(d), stdout=lo, stderr=er, env=env)
    rc = rc2
    if rc == 0 and (d / "run.txt").exists():
      done.write_text("ok_retry\n")
    else:
      (d / "FAIL_rc").write_text(str(rc) + "\n")
  return str(d), rc, "run"

def main():
  jobs = json.load(open(sys.argv[1]))
  workers = int(sys.argv[2]) if len(sys.argv) > 2 else 6
  milestone_name = sys.argv[3] if len(sys.argv) > 3 else "MILESTONE.txt"
  for j in jobs:
    seed = int(j["seed"])
    assert seed in ALLOWED
    has_eval = any(a == "--eval=1" or a.startswith("--eval=1") for a in j["cmd"])
    if seed in EVAL:
      assert has_eval, f"eval seed {seed} requires --eval=1"
    else:
      assert not has_eval
  total = len(jobs)
  ok = fail = skip = 0
  t0 = time.time()
  milestone = O / milestone_name
  fail_log = open(O / "failures.txt", "a")
  with ThreadPoolExecutor(workers) as ex:
    futs = {ex.submit(run_one, j): j for j in jobs}
    for i, fut in enumerate(as_completed(futs), 1):
      d, rc, how = fut.result()
      if how == "skip":
        skip += 1; ok += 1
      elif rc == 0:
        ok += 1
      else:
        fail += 1
        fail_log.write(f"rc={rc} {d}\n"); fail_log.flush()
      if i % 5 == 0 or i == total or fail > 0:
        elapsed = time.time() - t0
        done_n = ok + fail
        rate = (done_n - skip) / elapsed if elapsed > 1 and (done_n - skip) > 0 else 0
        msg = (f"{time.strftime('%Y-%m-%d %H:%M:%S')} CST done={done_n}/{total} "
               f"ok={ok} fail={fail} skip={skip} rate={rate:.3f}/s "
               f"eta_s={(total-done_n)/rate if rate>0 else -1:.0f}\n")
        milestone.write_text(msg)
        print(msg, end="", flush=True)
      if fail / total > FAIL_FRAC_STOP:
        print(f"STOP: fail_frac={fail/total:.4f} > {FAIL_FRAC_STOP}", flush=True)
        for f in futs: f.cancel()
        break
  fail_log.close()
  summary = {"ok": ok, "fail": fail, "skip": skip, "total_jobs": total,
             "elapsed_s": time.time() - t0, "workers": workers,
             "jobs_file": sys.argv[1]}
  out_sum = O / (sys.argv[4] if len(sys.argv) > 4 else "run_summary.json")
  out_sum.write_text(json.dumps(summary, indent=2) + "\n")
  print("SUMMARY", summary, flush=True)
  return 0 if fail / max(total, 1) <= FAIL_FRAC_STOP else 2

if __name__ == "__main__":
  sys.exit(main())
