# Note: legacy strings in signed provenance documents

Signed gate verdicts, SIGNED addenda, and preregistration files under `docs/` and
`docs/gate_verdicts/` are **path-scrubbed copies** of the locked originals. Their
scientific wording was **not** rewritten, so they may still contain:

- the former project codename (see root `rename_map.csv` / README “Legacy names”);
- metric names of the form `E_bit_*` / `ΔE_bit` used in the preregistration text.

Those are **not** per-run CSV column names. Dataset-of-record CSVs use the column
`ebit`. Do not treat signed-doc metric aliases as CSV headers.
