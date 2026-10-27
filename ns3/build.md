# Building the evaluation binaries

Upstream: **ns-3.41** (see `BASE_VERSION.txt`). This repository ships only the
DA-HELLO / host-router module sources under `src_modules/{p15v2,p18}/` and the
out-of-tree build scripts `build_p15v2.sh` / `build_p18.sh` (scrubbed copies).

## Legacy names (required for bit-identical rebuild against the original box)

Original evaluation used the module / library name **`elqtp`** and paths under
`E-LQTP-portA/`. In this public tree those identifiers are renamed to **`dahello`**
/ **DA-HELLO** (see `rename_map.csv`). CLI flags used in locked jobs
(`--topo`, `--protocol`, `--helloOnlyHost`, `--qelarDaFc`, …) are unchanged.
Scenario executable basename remains `ns3.41-scenario-i-lqarp-optimized`.

To rebuild against a stock ns-3.41:
1. Install ns-3.41 and its dependencies.
2. Point `NS3_ROOT` at the ns-3.41 directory.
3. Place `src_modules/p15v2/src` (or p18) as the module tree expected by the build script
   (or adapt the script’s `SRC` path).
4. Run `bash build_p15v2.sh` or `bash build_p18.sh`.
5. Confirm exe SHA256 against `BINARIES.md` (will match only if sources + toolchain match the freeze).

Do **not** commit compiled `bin_*` / `lib_*` / `obj_*` artifacts.
