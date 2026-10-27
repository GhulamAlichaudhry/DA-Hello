# DA-HELLO manuscript (LaTeX)

IEEE Internet of Things Journal draft:
**DA-HELLO: State-Carrier-Dependent HELLO Scheduling for Q-Learning Routing in Underwater Sensor Networks**

## Contents

| Path | Role |
|------|------|
| `main.tex` | Root document (IEEEtran journal) |
| `authors.tex`, `abstract.tex`, `keywords.tex` | Front matter |
| `sec1_introduction.tex` … `sec5_conclusion.tex` | Sections I–V + conclusion |
| `fig_network.tex`, `fig_carriers.tex`, `fig_flow.tex` | Figure wrappers |
| `tab1.tex` … `tab4.tex` | Tables |
| `fig1_network.pdf`, `fig3_flow.pdf`, `fig4_deploy_top.pdf` … `fig8_scale.pdf` | Figure PDFs |
| `refs.bib` | Bibliography |
| `Makefile` / `build.sh` | Build |
| `BUILD_INFO.txt` | Verified page count and SHA256 |

System packages expected: `texlive` with `IEEEtran` class and `IEEEtran.bst` (e.g. `texlive-publishers`).

## Build

```bash
./build.sh
# or
make
```

Produces `main.pdf`.

## Verified build (t185, 2026-10-26 11:35:04 CST)

- **Pages: 12**
- **main.pdf SHA256:** `f855075daa8912e1c1231fd16282031ea4493411d6f480d3d814db88b1ad5c04`
- Fig. 1 = `fig1_network.pdf` via `fig_network.tex`
- Fig. 3 = `fig3_flow.pdf` via `fig_flow.tex` (Ali PDF; TikZ removed)
- Conclusion text as of t184

## Notes

Manuscript-only tree. Do not run simulations from this package.
