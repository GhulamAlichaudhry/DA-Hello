#!/usr/bin/env python3
"""t103 evaluation runner. Seeds 20261801-40 ONLY. Requires --eval=1 (already in jobs).
No interim analysis. Stop if >2% fail. Resume-safe: skip rundir with rc=0 marker."""
import os, subprocess, sys, json, time
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path

O = Path("${OUT}/t103_eval_20261023")
LIB = "${WORKSPACE}/DA-HELLO-porta/build_p15v2/lib_p15v2"
NS = "${WORKSPACE}/DA-HELLO/opt/ns-allinone-3.41/ns-3.41/build/lib"
EVAL = set(range(20261801, 20261841))
FAIL_FRAC_STOP = 0.02

def run_one(job):
  seed = int(job["seed"])
  assert seed in EVAL, f"non-eval seed forbidden: {seed}"
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
    # one identical retry on failure
    with open(d / "run.log", "a") as lo, open(d / "run.err", "a") as er:
      lo.write("\n# RETRY identical command\n")
      er.write("\n# RETRY identical command\n")
      rc2 = subprocess.call(cmd, cwd=str(d), stdout=lo, stderr=er, env=env)
    rc = rc2
    if rc == 0 and (d / "run.txt").exists():
      done.write_text("ok_retry\n")
  return str(d), rc, "run"

def main():
  jobs = json.load(open(sys.argv[1]))
  workers = int(sys.argv[2]) if len(sys.argv) > 2 else 6
  total = len(jobs)
  ok = fail = skip = 0
  t0 = time.time()
  milestone = O / "MILESTONE.txt"
  fail_log = open(O / "failures.txt", "a")
  with ThreadPoolExecutor(workers) as ex:
    futs = {ex.submit(run_one, j): j for j in jobs}
    for i, fut in enumerate(as_completed(futs), 1):
      d, rc, how = fut.result()
      if how == "skip":
        skip += 1
        ok += 1
      elif rc == 0:
        ok += 1
      else:
        fail += 1
        fail_log.write(f"rc={rc} {d}\n")
        fail_log.flush()
      if i % 25 == 0 or i == total or fail > 0 and (ok+fail) % 10 == 0:
        elapsed = time.time() - t0
        rate = (ok+fail-skip) / elapsed if elapsed > 1 and (ok+fail-skip) > 0 else 0
        msg = (f"{time.strftime('%Y-%m-%d %H:%M:%S CST')} done={ok+fail}/{total} "
               f"ok={ok} fail={fail} skip={skip} rate={rate:.2f}/s "
               f"eta_s={(total-ok-fail)/rate if rate>0 else -1:.0f}\n")
        milestone.write_text(msg)
        print(msg, end="", flush=True)
      # stopping rule: >2% of TOTAL planned
      if fail / total > FAIL_FRAC_STOP:
        print(f"STOP: fail_frac={fail/total:.4f} > {FAIL_FRAC_STOP}", flush=True)
        # cancel remaining
        for f in futs:
          f.cancel()
        break
  fail_log.close()
  summary = {"ok": ok, "fail": fail, "skip": skip, "total_jobs": total,
             "elapsed_s": time.time()-t0}
  (O / "run_summary.json").write_text(json.dumps(summary, indent=2)+"\n")
  print("SUMMARY", summary, flush=True)
  return 0 if fail / max(total,1) <= FAIL_FRAC_STOP else 2

if __name__ == "__main__":
  sys.exit(main())
