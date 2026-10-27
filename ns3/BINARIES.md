# Evaluation binaries (not shipped — hashes only)

| Binary | Role | Scenario exe basename | exe SHA256 | Notes |
|---|---|---|---|---|
| **p15v2** | MARV t103 DoR | `ns3.41-scenario-i-lqarp-optimized` | `e66b8970b6d4ad91cbe939b77b0ba2fb10e21521d68e08f4853fefee4dff7396` | Legacy lib name `libns3.41-elqtp-optimized.so` SHA `ad9cc623ee99eb44def7b243bf1b62e3954364b4f213ac486e7ff1ead3f24395` |
| **p18** | QELAR t109 DoR | `ns3.41-scenario-i-lqarp-optimized` | `c57ce18e9a580655818bf06e9ecab73cefcbdb870e7d2e5f41eeb55581152f56` | See `docs/p18_BUILD_RECORD.md` |

Sources frozen in `src_modules/{p15v2,p18}/` from the t164 implementation audit.
Original box paths used module name `elqtp`; this repo renames identifiers to `dahello` (see root `rename_map.csv` and README “Legacy names”).
