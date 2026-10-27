#!/usr/bin/env python3
"""t172 / Prompt t113 — Section V result figures (DA-HELLO), IEEE IoT-J.
All numbers from retained Port-A CSVs only. No sims, no hand-typed means.
Regenerate: python3 make_figs_v2.py
"""
from __future__ import annotations

import csv
import math
import os
import sys
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
from matplotlib.lines import Line2D
from matplotlib.patches import FancyArrowPatch
from matplotlib.ticker import FixedFormatter, FixedLocator, NullLocator
from scipy import stats

# ---------------------------------------------------------------------------
# Paths & versions
# ---------------------------------------------------------------------------
OUT = Path("${OUT}/t172_secV_figs_20261025")
OUT.mkdir(parents=True, exist_ok=True)
os.chdir(OUT)

T103 = "${OUT}/t103_eval_20261023_unz/t103_eval_20261023/t103_per_run.csv"
T109 = "${OUT}/t109_p18_20261023/per_run.csv"
SRC_MARV = "t103_per_run.csv"
SRC_QELAR = "t109_p18/per_run.csv (phase==2)"

PACKAGE_VERSIONS = {
    "python": sys.version.split()[0],
    "matplotlib": matplotlib.__version__,
    "numpy": np.__version__,
    "scipy": __import__("scipy").__version__,
    "pandas": __import__("pandas").__version__,
}

# ---------------------------------------------------------------------------
# Style (t113)
# ---------------------------------------------------------------------------
plt.rcParams.update({
    "font.family": "serif",
    "font.serif": ["Liberation Serif", "Times New Roman", "DejaVu Serif", "Times"],
    "mathtext.fontset": "stix",
    "font.size": 7,
    "axes.labelsize": 8,
    "axes.titlesize": 8,
    "legend.fontsize": 7,
    "xtick.labelsize": 7,
    "ytick.labelsize": 7,
    "pdf.fonttype": 42,
    "ps.fonttype": 42,
    "axes.linewidth": 0.6,
    "lines.linewidth": 1.0,
    "axes.grid": True,
    "grid.linewidth": 0.3,
    "grid.alpha": 0.4,
    "grid.color": "#b0b0b0",
    "axes.spines.top": False,
    "axes.spines.right": False,
    "figure.dpi": 120,
    "savefig.bbox": None,
    "savefig.pad_inches": 0.0,
})

# Arm styles: colour-blind + greyscale via marker/linestyle
# Periodics dark→light blue as T increases (T20 dark … T120 light)
C_B0 = "#555555"
C_TR = "#2ca02c"          # green
C_DA = "#c0392b"          # red
C_DA0 = "#c0392b"
PER = {
    "T5": "#08306b",
    "T10": "#08519c",
    "T20": "#2171b5",
    "T40": "#4292c6",
    "T60": "#6baed6",
    "T120": "#9ecae1",
}
SCN_ORDER = ["I", "II", "GM2"]
SCN_LABEL = {"I": "I", "II": "II", "GM2": "GM2"}
SCN_COLOR = {"I": "#4d4d4d", "II": "#2c7fb8", "GM2": "#d95f0e"}
SCN_MK = {"I": "o", "II": "s", "GM2": "^"}

# ---------------------------------------------------------------------------
# Data loaders / stats
# ---------------------------------------------------------------------------

def load_csv(path: str):
    with open(path, newline="") as fh:
        return list(csv.DictReader(fh))


def fnum(x):
    try:
        return float(x)
    except Exception:
        return float("nan")


def mean_ci(vals):
    """Two-sided 95% Student-t CI over seeds. Returns (mean, half-width, n, lo, hi)."""
    v = np.asarray([x for x in vals if np.isfinite(x)], dtype=float)
    n = len(v)
    if n == 0:
        return float("nan"), 0.0, 0, float("nan"), float("nan")
    m = float(v.mean())
    if n > 1:
        h = float(stats.t.ppf(0.975, n - 1) * v.std(ddof=1) / math.sqrt(n))
    else:
        h = 0.0
    return m, h, n, m - h, m + h


def cell(rows, key, **flt):
    sel = [r for r in rows if all(str(r.get(k)) == str(v) for k, v in flt.items())]
    out = {}
    for r in sel:
        out[str(r["seed"])] = fnum(r[key])
    return out


def agg(rows, key, **flt):
    d = cell(rows, key, **flt)
    return mean_ci(list(d.values()))


def paired_diff(rows, key, arm_a, arm_b, **flt):
    """Paired mean of (arm_a - arm_b) by seed."""
    da = cell(rows, key, arm=arm_a, **flt)
    db = cell(rows, key, arm=arm_b, **flt)
    common = sorted(set(da) & set(db))
    diffs = [da[s] - db[s] for s in common]
    return mean_ci(diffs)


def paired_diff_cross(rows_a, rows_b, key, arm_a, arm_b, flt_a, flt_b):
    """Paired diff when arms live in different camps (e.g. Pstar C1 vs Pstar_r0 C2)."""
    da = cell(rows_a, key, arm=arm_a, **flt_a)
    db = cell(rows_b, key, arm=arm_b, **flt_b)
    common = sorted(set(da) & set(db))
    return mean_ci([da[s] - db[s] for s in common])


A_ALL = load_csv(T103)
B_ALL = load_csv(T109)
# Phase 2 eval only: seeds 20261841–80
B = [r for r in B_ALL if r.get("phase") == "2" and 20261841 <= int(r["seed"]) <= 20261880]
A_C1 = [r for r in A_ALL if r["camp"] == "C1"]
A_C2 = [r for r in A_ALL if r["camp"] == "C2"]
A_C1sat = [r for r in A_ALL if r["camp"] == "C1sat"]
A_C3n = [r for r in A_ALL if r["camp"] == "C3n"]
A_C3d = [r for r in A_ALL if r["camp"] == "C3d"]

DATA_ROWS = []  # accumulate for figN_data.csv


def record(fig, arm, scenario, n, mean, ci_lo, ci_hi, source, column, filt, **extra):
    row = {
        "figure": fig,
        "arm": arm,
        "scenario": scenario,
        "n": n,
        "mean": mean,
        "ci_lo": ci_lo,
        "ci_hi": ci_hi,
        "source_file": source,
        "column": column,
        "filter": filt,
    }
    row.update(extra)
    DATA_ROWS.append(row)


def write_data_csv(fig_tag):
    path = OUT / f"{fig_tag}_data.csv"
    rows = [r for r in DATA_ROWS if r["figure"] == fig_tag]
    if not rows:
        return path
    keys = list(rows[0].keys())
    for r in rows:
        for k in r:
            if k not in keys:
                keys.append(k)
    with open(path, "w", newline="") as fh:
        w = csv.DictWriter(fh, fieldnames=keys, extrasaction="ignore")
        w.writeheader()
        for r in rows:
            out = {k: ("" if v is None or (isinstance(v, float) and not np.isfinite(v)) else v)
                   for k, v in r.items()}
            w.writerow(out)
    return path


FIG_SIZE = {
    "fig5": (7.16, 3.5),
    "fig6": (7.16, 2.4),
    "fig7": (3.5, 3.2),
    "fig8": (3.5, 3.0),
    "figS1": (3.5, 3.0),
    "figS2": (3.5, 3.0),
    "figS3": (3.5, 3.0),
}

FIG_SIZE = {
    "fig5": (7.16, 3.5),
    "fig6": (7.16, 2.4),
    "fig7": (3.5, 3.2),
    "fig8": (3.5, 3.0),
    "figS1": (3.5, 3.0),
    "figS2": (3.5, 3.0),
    "figS3": (3.5, 3.0),
}


def save_fig(fig, stem, dpi_png=150):
    pdf = OUT / f"{stem}.pdf"
    png = OUT / f"{stem}.png"
    wh = FIG_SIZE.get(stem)
    if wh:
        fig.set_size_inches(wh[0], wh[1], forward=True)
    # Force exact MediaBox = figsize (override any tight default)
    fig.savefig(pdf, dpi=300, bbox_inches=None, pad_inches=0)
    fig.savefig(png, dpi=dpi_png, bbox_inches=None, pad_inches=0)
    plt.close(fig)
    return pdf, png



def panel_tag(ax, letter):
    ax.text(
        0.02, 0.98, f"({letter})", transform=ax.transAxes,
        fontsize=8, fontweight="bold", va="top", ha="left",
        zorder=10,
    )


# ---------------------------------------------------------------------------
# Fig. 5 — delivery vs O_ctrl, HELLO-only
# ---------------------------------------------------------------------------

def fig5():
    fig, axs = plt.subplots(2, 3, figsize=(7.16, 3.5), sharex=False)
    marv_arms = [
        ("B0", "B0", C_B0, "x", 5.5),
        ("T20", "T20", PER["T20"], "o", 4.5),
        ("T40", "T40", PER["T40"], "o", 4.5),
        ("T60", "T60", PER["T60"], "o", 4.5),
        ("T120", "T120", PER["T120"], "o", 4.5),
        ("B2star", "Trickle (tuned)", C_TR, "^", 5.0),
        ("Pstar", "DA-HELLO", C_DA, "*", 8.5),
    ]
    qel_arms = [
        ("B0", "B0", C_B0, "x", 5.5),
        ("T60", "T60", PER["T60"], "o", 4.5),
        ("T120", "T120", PER["T120"], "o", 4.5),
        ("trickle", "Trickle (default)", C_TR, "^", 5.0),
        ("DA480", "DA-HELLO", C_DA, "*", 8.5),
        ("DA0", "DA0", C_DA0, "*", 7.5),  # hollow
    ]

    def draw_panel(ax, rows, arms, sc, hostflt, source, family_tag):
        peri_xy = []
        da_x = None
        t120_xy = None
        b0_y = None
        plotted_ys = []
        for arm, lab, col, mk, ms in arms:
            m_y, h_y, n, lo, hi = agg(rows, "wpdr", arm=arm, sc=sc, **hostflt)
            m_x, h_x, _, _, _ = agg(rows, "O_ctrl", arm=arm, sc=sc, **hostflt)
            filt = f"{family_tag};arm={arm};sc={sc};" + ";".join(f"{k}={v}" for k, v in hostflt.items())
            record("fig5", lab, sc, n, m_y, lo, hi, source, "wpdr", filt,
                   O_ctrl_mean=m_x, metric="PDR")
            record("fig5", lab, sc, n, m_x, m_x - h_x, m_x + h_x, source, "O_ctrl", filt,
                   metric="O_ctrl")
            if not np.isfinite(m_y):
                continue
            if arm == "B0":
                b0_y = m_y
                continue  # off-scale annotation
            hollow = arm == "DA0"
            ax.errorbar(
                m_x, m_y, yerr=h_y,
                fmt=mk, ms=ms, color=col,
                mfc="none" if hollow else col,
                mec=col if hollow else ("k" if mk == "*" else col),
                mew=0.9 if hollow else (0.55 if mk == "*" else 0.35),
                elinewidth=0.7, capsize=1.5, capthick=0.55,
                zorder=2, ecolor="#666666",
            )
            # redraw marker on top of error bar
            ax.plot(
                m_x, m_y, marker=mk, ms=ms, color=col, ls="",
                mfc="none" if hollow else col,
                mec=col if hollow else ("k" if mk == "*" else col),
                mew=0.9 if hollow else (0.55 if mk == "*" else 0.35),
                zorder=5,
            )
            plotted_ys.extend([m_y - h_y, m_y + h_y])
            if arm.startswith("T") and arm[1:].isdigit():
                peri_xy.append((int(arm[1:]), m_x, m_y))
            if arm in ("Pstar", "DA480"):
                da_x = m_x
            if arm == "T120":
                t120_xy = (m_x, m_y)

        # connect periodics by T order
        if len(peri_xy) >= 2:
            peri_xy.sort()
            ax.plot([p[1] for p in peri_xy], [p[2] for p in peri_xy],
                    color="#6baed6", lw=0.7, ls="-", zorder=1, alpha=0.7)

        # dotted vertical at DA-HELLO O_ctrl + % saved vs T120
        if da_x is not None and np.isfinite(da_x):
            ax.axvline(da_x, color=C_DA, ls=":", lw=0.8, alpha=0.7, zorder=1)
        if t120_xy is not None and da_x is not None and t120_xy[0] > 0:
            pct = (1.0 - da_x / t120_xy[0]) * 100.0
            record("fig5", "bytes_saved_vs_T120_pct", sc, n, pct, pct, pct, source,
                   "derived",
                   f"{family_tag};sc={sc};derived=1-O_da/O_T120",
                   metric="pct_saved")
            ax.annotate(
                f"−{pct:.0f}%",
                xy=t120_xy, xytext=(6, 6), textcoords="offset points",
                fontsize=6.5, color="#1f5fa8", ha="left", va="bottom",
            )

        ax.set_xscale("log")
        ax.set_xlim(14, 500)
        ax.xaxis.set_major_locator(FixedLocator([20, 50, 100, 200, 400]))
        ax.xaxis.set_major_formatter(FixedFormatter(["20", "50", "100", "200", "400"]))
        ax.xaxis.set_minor_locator(NullLocator())

        if plotted_ys:
            lo, hi = min(plotted_ys), max(plotted_ys)
            span = max(hi - lo, 0.04)
            ax.set_ylim(lo - 0.18 * span - 0.03, hi + 0.12 * span + 0.02)

        # B0 off-scale note
        if b0_y is not None:
            ax.annotate(
                f"B0: {b0_y:.2f} (off scale) ↓",
                xy=(0.5, 0.02), xycoords="axes fraction",
                fontsize=6.2, color=C_B0, ha="center", va="bottom",
            )

    for j, sc in enumerate(SCN_ORDER):
        draw_panel(axs[0, j], A_C1, marv_arms, sc, dict(host="hoh"), SRC_MARV, "MARV-hoh;camp=C1")
        draw_panel(axs[1, j], B, qel_arms, sc, dict(family="hello"), SRC_QELAR, "QELAR-hello")
        axs[0, j].set_title(SCN_LABEL[sc], fontsize=8, pad=2)
        axs[1, j].set_xlabel("Control overhead (B/s)")
        panel_tag(axs[0, j], "a" if j == 0 else ("b" if j == 1 else "c"))
        panel_tag(axs[1, j], "d" if j == 0 else ("e" if j == 1 else "f"))

    # T5/T10 saturation note (MARV II hoh) — data CSV only, not plotted
    for arm in ("T5", "T10"):
        m_y, h_y, n, lo, hi = agg(A_C1sat, "wpdr", host="hoh", arm=arm, sc="II")
        m_x, h_x, _, _, _ = agg(A_C1sat, "O_ctrl", host="hoh", arm=arm, sc="II")
        record("fig5", arm, "II", n, m_y, lo, hi, SRC_MARV, "wpdr",
               f"MARV-hoh;camp=C1sat;arm={arm};sc=II;NOTE=saturation_not_plotted",
               O_ctrl_mean=m_x, metric="PDR_saturation_note")

    axs[0, 0].set_ylabel("Delivery ratio")
    axs[1, 0].set_ylabel("Delivery ratio")
    # row labels as small text
    axs[0, 0].text(-0.28, 0.5, "MARV\nHELLO-only", transform=axs[0, 0].transAxes,
                   fontsize=7, va="center", ha="right", rotation=90)
    axs[1, 0].text(-0.28, 0.5, "QELAR\nHELLO-only", transform=axs[1, 0].transAxes,
                   fontsize=7, va="center", ha="right", rotation=90)

    # shared legend
    handles = [
        Line2D([], [], marker="x", color=C_B0, ls="", ms=5, label="B0"),
        Line2D([], [], marker="o", color=PER["T20"], ls="", ms=4.5, label="T20"),
        Line2D([], [], marker="o", color=PER["T40"], ls="", ms=4.5, label="T40"),
        Line2D([], [], marker="o", color=PER["T60"], ls="", ms=4.5, label="T60"),
        Line2D([], [], marker="o", color=PER["T120"], ls="", ms=4.5, label="T120"),
        Line2D([], [], marker="^", color=C_TR, ls="", ms=5, label="Trickle (tuned/default)"),
        Line2D([], [], marker="*", color=C_DA, ls="", ms=8, mec="k", mew=0.4, label="DA-HELLO"),
        Line2D([], [], marker="*", color=C_DA, ls="", ms=7, mfc="none", mew=0.9, label="DA0"),
    ]
    fig.legend(
        handles=handles, loc="lower center", ncol=8, frameon=False,
        bbox_to_anchor=(0.5, -0.02), handletextpad=0.3, columnspacing=0.9,
        fontsize=6.5,
    )
    fig.tight_layout(rect=(0.02, 0.07, 1, 1), w_pad=0.45, h_pad=0.55)
    return save_fig(fig, "fig5")


# ---------------------------------------------------------------------------
# Fig. 6 — header hosts, ΔPDR vs B0 / alone
# Decision: omit T20 from plot (≈ −37 pp) so ±1 pp DA differences remain visible;
# T20 values are in fig6_data.csv.
# ---------------------------------------------------------------------------

def fig6():
    """x = scenario; grouped markers per arm. T20 omitted from plot (in CSV)."""
    fig, axs = plt.subplots(1, 2, figsize=(7.16, 2.4))
    offsets_base = None  # set per panel

    # --- panel (a) MARV header ---
    ax = axs[0]
    marv_plot = [
        ("T60", "T60", PER["T60"], "o", False),
        ("T120", "T120", PER["T120"], "o", False),
        ("B2star", "Trickle (tuned)", C_TR, "^", False),
        ("Pstar", "DA-HELLO", C_DA, "*", False),
    ]
    n_arms = len(marv_plot)
    width = 0.18
    offs = (np.arange(n_arms) - (n_arms - 1) / 2.0) * width
    for ai, (arm, lab, col, mk, hollow) in enumerate(marv_plot):
        for si, sc in enumerate(SCN_ORDER):
            m, h, n, lo, hi = paired_diff(A_C1, "wpdr", arm, "B0", host="piggy", sc=sc)
            record("fig6", lab, sc, n, 100 * m, 100 * lo, 100 * hi, SRC_MARV, "wpdr",
                   f"MARV-piggy;camp=C1;arm={arm}-B0;sc={sc};paired",
                   metric="delta_PDR_pp", plotted=1)
            x = si + offs[ai]
            ax.errorbar(
                x, 100 * m, yerr=100 * h, fmt=mk,
                ms=8.0 if mk == "*" else 4.8, color=col,
                mfc="none" if hollow else col,
                mec=("k" if mk == "*" else col),
                mew=0.55 if mk == "*" else 0.35,
                elinewidth=0.7, capsize=1.4, capthick=0.55,
                ecolor="#666666", zorder=2,
            )
            ax.plot(
                x, 100 * m, marker=mk, ms=8.0 if mk == "*" else 4.8, color=col, ls="",
                mfc="none" if hollow else col,
                mec=("k" if mk == "*" else col),
                mew=0.55 if mk == "*" else 0.35, zorder=5,
            )
    for sc in SCN_ORDER:
        m, h, n, lo, hi = paired_diff(A_C1, "wpdr", "T20", "B0", host="piggy", sc=sc)
        record("fig6", "T20", sc, n, 100 * m, 100 * lo, 100 * hi, SRC_MARV, "wpdr",
               f"MARV-piggy;camp=C1;arm=T20-B0;sc={sc};paired;OMITTED_FROM_PLOT",
               metric="delta_PDR_pp", plotted=0)
    ax.axhline(0, color="k", lw=1.0, zorder=1)
    ax.set_xticks(range(len(SCN_ORDER)))
    ax.set_xticklabels(SCN_ORDER)
    ax.set_xlabel("Scenario")
    ax.set_ylabel("Change in delivery ratio (pp)")
    ax.set_ylim(-12, 3)
    panel_tag(ax, "a")
    ax.set_title("MARV header host", fontsize=8, pad=2)

    # --- panel (b) QELAR header ---
    ax = axs[1]
    qel_plot = [
        ("T60", "T60", PER["T60"], "o", False),
        ("T120", "T120", PER["T120"], "o", False),
        ("trickle", "Trickle (default)", C_TR, "^", False),
        ("DA_FCoff_m10", "DA-HELLO (FC off)", C_DA, "*", False),
        ("DA_FCon_m0.5", "DA-HELLO (FC on)", C_DA, "D", True),
    ]
    n_arms = len(qel_plot)
    width = 0.15
    offs = (np.arange(n_arms) - (n_arms - 1) / 2.0) * width
    for ai, (arm, lab, col, mk, hollow) in enumerate(qel_plot):
        for si, sc in enumerate(SCN_ORDER):
            m, h, n, lo, hi = paired_diff(B, "wpdr", arm, "alone", family="piggy", sc=sc)
            record("fig6", lab, sc, n, 100 * m, 100 * lo, 100 * hi, SRC_QELAR, "wpdr",
                   f"QELAR-piggy;arm={arm}-alone;sc={sc};paired",
                   metric="delta_PDR_pp", plotted=1)
            x = si + offs[ai]
            ax.errorbar(
                x, 100 * m, yerr=100 * h, fmt=mk,
                ms=8.0 if mk == "*" else (5.2 if mk == "D" else 4.8), color=col,
                mfc="none" if hollow else col,
                mec=(col if hollow else ("k" if mk == "*" else col)),
                mew=0.9 if hollow else (0.55 if mk == "*" else 0.35),
                elinewidth=0.7, capsize=1.4, capthick=0.55,
                ecolor="#666666", zorder=2,
            )
            ax.plot(
                x, 100 * m, marker=mk,
                ms=8.0 if mk == "*" else (5.2 if mk == "D" else 4.8),
                color=col, ls="",
                mfc="none" if hollow else col,
                mec=(col if hollow else ("k" if mk == "*" else col)),
                mew=0.9 if hollow else (0.55 if mk == "*" else 0.35), zorder=5,
            )
    ax.axhline(0, color="k", lw=1.0, zorder=1)
    ax.set_xticks(range(len(SCN_ORDER)))
    ax.set_xticklabels(SCN_ORDER)
    ax.set_xlabel("Scenario")
    panel_tag(ax, "b")
    ax.set_title("QELAR header host", fontsize=8, pad=2)

    handles = [
        Line2D([], [], marker="o", color=PER["T60"], ls="", ms=4.8, label="T60"),
        Line2D([], [], marker="o", color=PER["T120"], ls="", ms=4.8, label="T120"),
        Line2D([], [], marker="^", color=C_TR, ls="", ms=5, label="Trickle"),
        Line2D([], [], marker="*", color=C_DA, ls="", ms=8, mec="k", mew=0.4, label="DA-HELLO"),
        Line2D([], [], marker="D", color=C_DA, ls="", ms=5, mfc="none", mew=0.9,
               label="DA-HELLO (FC on)"),
    ]
    fig.legend(
        handles=handles, loc="lower center", ncol=5, frameon=False,
        bbox_to_anchor=(0.5, -0.02), fontsize=6.5, columnspacing=0.9, handletextpad=0.3,
    )
    fig.tight_layout(rect=(0, 0.10, 1, 1), w_pad=0.8)
    return save_fig(fig, "fig6")

# ---------------------------------------------------------------------------
# Fig. 7

# ---------------------------------------------------------------------------
# Fig. 7 — slow-refresh ablation
# ---------------------------------------------------------------------------

def fig7():
    fig, axs = plt.subplots(2, 1, figsize=(3.5, 3.2), sharex=False)

    # (a) MARV HELLO-only PDR vs refresh {0,240,480}
    ax = axs[0]
    refresh_arms = [("Pstar_r0", 0, "C2"), ("Pstar_r240", 240, "C2"), ("Pstar", 480, "C1")]
    for sc in SCN_ORDER:
        xs, ys, yerr = [], [], []
        for arm, r, camp in refresh_arms:
            rows = A_C1 if camp == "C1" else A_C2
            m, h, n, lo, hi = agg(rows, "wpdr", host="hoh", arm=arm, sc=sc)
            record("fig7", f"PDR_r{r}", sc, n, m, lo, hi, SRC_MARV, "wpdr",
                   f"MARV-hoh;camp={camp};arm={arm};sc={sc}", metric="PDR", refresh_s=r)
            xs.append(r); ys.append(m); yerr.append(h)
        ax.errorbar(
            xs, ys, yerr=yerr, marker=SCN_MK[sc], ms=4.5, color=SCN_COLOR[sc],
            lw=1.0, capsize=1.4, elinewidth=0.7, capthick=0.55, ecolor="#666666",
            zorder=2, label=SCN_LABEL[sc],
        )
        ax.plot(xs, ys, marker=SCN_MK[sc], ms=4.5, color=SCN_COLOR[sc], ls="-",
                lw=1.0, zorder=5)
    ax.set_xticks([0, 240, 480])
    ax.set_xticklabels(["0", "240", "480"])
    ax.set_xlabel("Refresh period (s)")
    ax.set_ylabel("Delivery ratio")
    panel_tag(ax, "a")
    ax.legend(loc="lower right", frameon=False, fontsize=6.5, title=None)

    # (b) paired ΔPDR: refresh 480 − refresh off
    ax = axs[1]
    # MARV: Pstar (C1) − Pstar_r0 (C2)
    # QELAR: DA480 − DA0 (family=hello)
    groups = [
        ("MARV", A_C1, A_C2, "Pstar", "Pstar_r0",
         dict(host="hoh"), dict(host="hoh"), C_DA, "*", False),
        ("QELAR", B, B, "DA480", "DA0",
         dict(family="hello"), dict(family="hello"), "#e67e22", "s", False),
    ]
    x_sc = {s: i for i, s in enumerate(SCN_ORDER)}
    width = 0.28
    for gi, (name, rows_a, rows_b, arm_a, arm_b, flt_a, flt_b, col, mk, hollow) in enumerate(groups):
        off = -width / 2 if gi == 0 else width / 2
        xs, ys, yerr = [], [], []
        for sc in SCN_ORDER:
            if rows_a is rows_b:
                m, h, n, lo, hi = paired_diff(rows_a, "wpdr", arm_a, arm_b, sc=sc, **flt_a)
            else:
                m, h, n, lo, hi = paired_diff_cross(
                    rows_a, rows_b, "wpdr", arm_a, arm_b,
                    {**flt_a, "sc": sc}, {**flt_b, "sc": sc},
                )
            record("fig7", f"{name}_480_minus_off", sc, n, 100 * m, 100 * lo, 100 * hi,
                   SRC_MARV if name == "MARV" else SRC_QELAR, "wpdr",
                   f"{name};{arm_a}-{arm_b};sc={sc};paired", metric="delta_PDR_pp")
            xs.append(x_sc[sc] + off)
            ys.append(100 * m)
            yerr.append(100 * h)
        ax.errorbar(
            xs, ys, yerr=yerr, fmt=mk, ms=6.5 if mk == "*" else 5.0, color=col,
            mfc="none" if hollow else col, mec=("k" if mk == "*" else col),
            mew=0.5, elinewidth=0.7, capsize=1.5, capthick=0.55,
            ecolor="#666666", zorder=2, label=name,
        )
        ax.plot(xs, ys, marker=mk, ms=6.5 if mk == "*" else 5.0, color=col, ls="",
                mfc="none" if hollow else col, mec=("k" if mk == "*" else col),
                mew=0.5, zorder=5)
    ax.axhline(0, color="k", lw=0.8, zorder=1)
    ax.set_xticks(list(x_sc.values()))
    ax.set_xticklabels(SCN_ORDER)
    ax.set_xlabel("Scenario")
    ax.set_ylabel("Δ delivery ratio (pp)\n(480 s − off)")
    panel_tag(ax, "b")
    ax.legend(loc="upper right", frameon=False, fontsize=6.5)

    fig.tight_layout(h_pad=0.7)
    return save_fig(fig, "fig7")


# ---------------------------------------------------------------------------
# Fig. 8 / S1 — scalability
# ---------------------------------------------------------------------------

def fig_scalability(sc: str, stem: str, fig_tag: str):
    fig, axs = plt.subplots(2, 1, figsize=(3.5, 3.0), sharex=True)
    Ns = [40, 70, 100, 149]
    # arms: B0, T120, Trickle(tuned), DA-HELLO; header solid, hoh dashed
    arm_specs = [
        ("B0", "B0", C_B0, "x", 5.0),
        ("T120", "T120", PER["T120"], "o", 4.5),
        ("B2star", "Trickle (tuned)", C_TR, "^", 5.0),
        ("Pstar", "DA-HELLO", C_DA, "*", 8.0),
    ]

    # (a) O_ctrl vs N log-log
    ax = axs[0]
    exponents = {}
    for host, ls, hostlab in [("piggy", "-", "header"), ("hoh", "--", "HELLO-only")]:
        for arm, lab, col, mk, ms in arm_specs:
            ys, yerr, xs = [], [], []
            for n in Ns:
                rows = [r for r in A_C3n if r["nAuv"] == str(n)]
                m, h, nn, lo, hi = agg(rows, "O_ctrl", host=host, arm=arm, sc=sc)
                record(fig_tag, f"{lab}/{hostlab}", sc, nn, m, lo, hi, SRC_MARV, "O_ctrl",
                       f"C3n;host={host};arm={arm};sc={sc};nAuv={n}", metric="O_ctrl", N=n)
                ys.append(m); yerr.append(h); xs.append(n)
            ax.errorbar(
                xs, ys, yerr=yerr, fmt=mk, ms=ms, color=col, ls=ls, lw=0.9,
                mfc=col, mec=("k" if mk == "*" else col),
                mew=0.5 if mk == "*" else 0.3,
                elinewidth=0.6, capsize=1.2, capthick=0.5, ecolor="#666666", zorder=2,
            )
            ax.plot(xs, ys, marker=mk, ms=ms, color=col, ls=ls, lw=0.9,
                    mfc=col, mec=("k" if mk == "*" else col),
                    mew=0.5 if mk == "*" else 0.3, zorder=5)
            if arm == "Pstar" and all(np.isfinite(ys)) and all(y > 0 for y in ys):
                coef = np.polyfit(np.log(xs), np.log(ys), 1)
                exponents[hostlab] = float(coef[0])
                record(fig_tag, f"DA-HELLO_exponent/{hostlab}", sc, len(xs),
                       exponents[hostlab], exponents[hostlab], exponents[hostlab],
                       SRC_MARV, "derived_loglog_fit", f"C3n;arm=Pstar;sc={sc};host={host}",
                       metric="exponent")

    ax.set_xscale("log")
    ax.set_yscale("log")
    ax.set_xlim(32, 180)
    ax.xaxis.set_major_locator(FixedLocator(Ns))
    ax.xaxis.set_major_formatter(FixedFormatter([str(n) for n in Ns]))
    ax.xaxis.set_minor_locator(NullLocator())
    ax.set_ylabel("Control overhead (B/s)")
    panel_tag(ax, "a")
    # exponent annotation
    exp_txt = "DA-HELLO fit: " + ", ".join(
        rf"{lab} $N^{{{exponents[lab]:.2f}}}$" for lab in ("header", "HELLO-only")
        if lab in exponents
    )
    ax.text(0.03, 0.97, exp_txt, transform=ax.transAxes, fontsize=5.8,
            va="top", ha="left", color="#333")

    # (b) PDR vs N
    ax = axs[1]
    for host, ls, hostlab in [("piggy", "-", "header"), ("hoh", "--", "HELLO-only")]:
        for arm, lab, col, mk, ms in arm_specs:
            ys, yerr, xs = [], [], []
            for n in Ns:
                rows = [r for r in A_C3n if r["nAuv"] == str(n)]
                m, h, nn, lo, hi = agg(rows, "wpdr", host=host, arm=arm, sc=sc)
                record(fig_tag, f"{lab}/{hostlab}", sc, nn, m, lo, hi, SRC_MARV, "wpdr",
                       f"C3n;host={host};arm={arm};sc={sc};nAuv={n}", metric="PDR", N=n)
                ys.append(m); yerr.append(h); xs.append(n)
            ax.errorbar(
                xs, ys, yerr=yerr, fmt=mk, ms=ms, color=col, ls=ls, lw=0.9,
                mfc=col, mec=("k" if mk == "*" else col),
                mew=0.5 if mk == "*" else 0.3,
                elinewidth=0.6, capsize=1.2, capthick=0.5, ecolor="#666666", zorder=2,
            )
            ax.plot(xs, ys, marker=mk, ms=ms, color=col, ls=ls, lw=0.9,
                    mfc=col, mec=("k" if mk == "*" else col),
                    mew=0.5 if mk == "*" else 0.3, zorder=5)
    ax.set_xscale("log")
    ax.set_xlim(32, 180)
    ax.xaxis.set_major_locator(FixedLocator(Ns))
    ax.xaxis.set_major_formatter(FixedFormatter([str(n) for n in Ns]))
    ax.xaxis.set_minor_locator(NullLocator())
    ax.set_xlabel(r"Relays $N$")
    ax.set_ylabel("Delivery ratio")
    ax.set_ylim(0, 1.05)
    panel_tag(ax, "b")

    # legend
    handles = [
        Line2D([], [], marker="x", color=C_B0, ls="-", ms=5, label="B0"),
        Line2D([], [], marker="o", color=PER["T120"], ls="-", ms=4.5, label="T120"),
        Line2D([], [], marker="^", color=C_TR, ls="-", ms=5, label="Trickle (tuned)"),
        Line2D([], [], marker="*", color=C_DA, ls="-", ms=7, mec="k", mew=0.4, label="DA-HELLO"),
        Line2D([], [], color="#333", ls="-", lw=1.0, label="header"),
        Line2D([], [], color="#333", ls="--", lw=1.0, label="HELLO-only"),
    ]
    fig.legend(
        handles=handles, loc="lower center", ncol=3, frameon=False,
        bbox_to_anchor=(0.5, -0.02), fontsize=6.2, columnspacing=0.8, handletextpad=0.3,
    )
    fig.tight_layout(rect=(0, 0.10, 1, 1), h_pad=0.55)
    return save_fig(fig, stem)


# ---------------------------------------------------------------------------
# S2 — drift-speed sweep
# ---------------------------------------------------------------------------

def fig_s2():
    fig, axs = plt.subplots(2, 1, figsize=(3.5, 3.0), sharex=True)
    speeds = sorted({fnum(r["drift"]) for r in A_C3d if r["drift"]})
    arm_specs = [
        ("B0", "B0", C_B0, "x", 5.0),
        ("T120", "T120", PER["T120"], "o", 4.5),
        ("B2star", "Trickle (tuned)", C_TR, "^", 5.0),
        ("Pstar", "DA-HELLO", C_DA, "*", 8.0),
    ]
    for ax, host, title, letter in [
        (axs[0], "piggy", "Header host", "a"),
        (axs[1], "hoh", "HELLO-only host", "b"),
    ]:
        for arm, lab, col, mk, ms in arm_specs:
            xs, ys, yerr = [], [], []
            for v in speeds:
                # drift stored as float; match via string of csv value
                rows = [r for r in A_C3d if abs(fnum(r["drift"]) - v) < 1e-9]
                m, h, n, lo, hi = agg(rows, "wpdr", host=host, arm=arm, sc="II")
                record("figS2", lab, f"V={v}", n, m, lo, hi, SRC_MARV, "wpdr",
                       f"C3d;host={host};arm={arm};sc=II;drift={v}", metric="PDR", drift=v)
                xs.append(v); ys.append(m); yerr.append(h)
            ax.errorbar(
                xs, ys, yerr=yerr, fmt=mk, ms=ms, color=col, ls="-", lw=0.9,
                mfc=col, mec=("k" if mk == "*" else col), mew=0.5 if mk == "*" else 0.3,
                elinewidth=0.6, capsize=1.2, ecolor="#666666", zorder=2,
            )
            ax.plot(xs, ys, marker=mk, ms=ms, color=col, ls="-", lw=0.9,
                    mfc=col, mec=("k" if mk == "*" else col),
                    mew=0.5 if mk == "*" else 0.3, zorder=5)
        ax.set_ylabel("Delivery ratio")
        ax.set_ylim(0, 1.05)
        panel_tag(ax, letter)
        ax.text(0.98, 0.05, title, transform=ax.transAxes, ha="right", va="bottom",
                fontsize=6.5, color="#444")
    axs[1].set_xlabel("Drift speed $V$ (m/s)")
    axs[1].set_xticks(speeds)
    # record exact speeds
    record("figS2", "speeds_in_data", "II", len(speeds), float("nan"), float("nan"),
           float("nan"), SRC_MARV, "drift", f"C3d;unique_speeds={speeds}",
           metric="meta", speeds=str(speeds))

    handles = [
        Line2D([], [], marker="x", color=C_B0, ls="-", ms=5, label="B0"),
        Line2D([], [], marker="o", color=PER["T120"], ls="-", ms=4.5, label="T120"),
        Line2D([], [], marker="^", color=C_TR, ls="-", ms=5, label="Trickle (tuned)"),
        Line2D([], [], marker="*", color=C_DA, ls="-", ms=7, mec="k", mew=0.4, label="DA-HELLO"),
    ]
    fig.legend(
        handles=handles, loc="lower center", ncol=4, frameon=False,
        bbox_to_anchor=(0.5, -0.02), fontsize=6.2, columnspacing=0.7, handletextpad=0.3,
    )
    fig.tight_layout(rect=(0, 0.09, 1, 1), h_pad=0.5)
    return save_fig(fig, "figS2"), speeds


# ---------------------------------------------------------------------------
# S3 — QELAR header request counts
# ---------------------------------------------------------------------------

def fig_s3():
    """QELAR header request counts, Scenario II; label 'requests' not 'HELLOs'."""
    fig, ax = plt.subplots(figsize=(3.5, 3.0))
    kinds = [
        ("hello_fc", "first contact"),
        ("hello_membership", "membership"),
        ("hello_rank", "rank"),
        ("hello_refresh", "refresh"),
    ]
    arms = [
        ("DA_FCoff_m10", "FC off", C_DA, False),
        ("DA_FCon_m0.5", "FC on", C_DA, True),
    ]
    sc = "II"
    x = np.arange(len(kinds), dtype=float)
    width = 0.35
    for ai, (arm, lab, col, hollow) in enumerate(arms):
        vals, yerr = [], []
        for colname, kindlab in kinds:
            m, h, n, lo, hi = agg(B, colname, family="piggy", arm=arm, sc=sc)
            record("figS3", lab, sc, n, m, lo, hi, SRC_QELAR, colname,
                   f"QELAR-piggy;arm={arm};sc={sc};kind={kindlab}",
                   metric="requests", kind=kindlab)
            vals.append(m)
            yerr.append(h)
        xpos = x + (ai - 0.5) * width
        # floor zeros for log axis visibility
        plot_vals = [max(v, 0.5) if v < 0.5 else v for v in vals]
        ax.bar(
            xpos, plot_vals, width=width * 0.9,
            color=("white" if hollow else col),
            edgecolor=col, linewidth=1.0, label=lab, zorder=3,
            hatch="///" if hollow else None,
        )
        ax.errorbar(
            xpos, plot_vals, yerr=yerr, fmt="none", ecolor="#444",
            elinewidth=0.7, capsize=1.5, capthick=0.55, zorder=4,
        )
        for xi, v in zip(xpos, vals):
            if v < 0.5:
                ax.text(xi, 0.7, "0", ha="center", va="bottom", fontsize=6, color=col)

    ax.set_yscale("log")
    ax.set_ylim(0.5, None)
    ax.set_xticks(x)
    ax.set_xticklabels([k[1] for k in kinds], fontsize=6.5)
    ax.set_ylabel("Requests per run")
    ax.set_xlabel("Request kind")
    panel_tag(ax, "a")
    ax.legend(loc="upper right", frameon=False, fontsize=6.5)
    ax.text(0.02, 0.02, "Scenario II", transform=ax.transAxes, fontsize=6.5,
            va="bottom", color="#555")
    fig.tight_layout()
    return save_fig(fig, "figS3")

# ---------------------------------------------------------------------------
# Captions

# ---------------------------------------------------------------------------
# Captions
# ---------------------------------------------------------------------------
CAPTIONS = {
    "fig5": (
        "Delivery ratio versus control overhead for HELLO-only hosts under scenarios "
        "I, II, and GM2 (mean ± 95% Student-t interval over seeds); B0 is annotated "
        "off scale, periodics are connected by period, and the percentage next to T120 "
        "is the control-byte saving of DA-HELLO relative to T120."
    ),
    "fig6": (
        "Paired change in delivery ratio (percentage points) of header-carried hosts "
        "relative to doing nothing (MARV B0; QELAR host alone), mean ± 95% paired "
        "interval; T20 is omitted from the plot because its ≈−37 pp loss would hide "
        "the ±1 pp DA-HELLO differences (values in fig6_data.csv)."
    ),
    "fig7": (
        "Slow-refresh ablation: (a) MARV HELLO-only delivery ratio versus refresh period "
        "{0, 240, 480} s; (b) paired delivery-ratio difference of refresh 480 s minus "
        "refresh off for MARV and QELAR HELLO-only hosts."
    ),
    "fig8": (
        "Scalability under scenario II for N ∈ {40, 70, 100, 149} (n = 10 seeds): "
        "(a) control overhead versus N (log–log) with fitted DA-HELLO exponents; "
        "(b) delivery ratio versus N; solid = header host, dashed = HELLO-only."
    ),
    "figS1": (
        "Same scalability layout as Fig. 8 for scenario GM2."
    ),
    "figS2": (
        "Drift-speed sweep (C3d): delivery ratio versus eddy peak speed V for MARV "
        "header and HELLO-only hosts (n = 10 seeds per cell)."
    ),
    "figS3": (
        "QELAR header-host request counts per run (first contact, membership, rank, "
        "refresh) for DA-HELLO with first-contact OFF versus ON under scenario II "
        "(mean ± 95% interval; log scale)."
    ),
}


def write_captions():
    path = OUT / "CAPTIONS.md"
    with open(path, "w") as fh:
        fh.write("# Figure captions (one sentence, factual)\n\n")
        for k, v in CAPTIONS.items():
            fh.write(f"## {k}\n{v}\n\n")
    return path


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main():
    print("Package versions:", PACKAGE_VERSIONS)
    print("Building Fig. 5…")
    fig5()
    write_data_csv("fig5")
    print("Building Fig. 6…")
    fig6()
    write_data_csv("fig6")
    print("Building Fig. 7…")
    fig7()
    write_data_csv("fig7")
    print("Building Fig. 8…")
    fig_scalability("II", "fig8", "fig8")
    write_data_csv("fig8")
    print("Building Fig. S1…")
    fig_scalability("GM2", "figS1", "figS1")
    write_data_csv("figS1")
    print("Building Fig. S2…")
    _, speeds = fig_s2()
    write_data_csv("figS2")
    print("Building Fig. S3…")
    fig_s3()
    write_data_csv("figS3")
    write_captions()

    # versions file
    with open(OUT / "PACKAGE_VERSIONS.txt", "w") as fh:
        for k, v in PACKAGE_VERSIONS.items():
            fh.write(f"{k}=={v}\n")

    print("speeds in C3d:", speeds)
    print("done →", OUT)


if __name__ == "__main__":
    main()
