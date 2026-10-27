#!/usr/bin/env python3
"""fig4_deploy_top_v2 — Section V-A deployment (TOP VIEW = main Fig. 4).

Prompt t115 / task t174. Width 3.5 in = 252 pt, height <= 3.2 in.
Placement from bit-identical CSV (seed 20261801, run 1). R_op removed.
"""
from __future__ import annotations

import csv
import math
import subprocess
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
from matplotlib import patheffects as pe
from matplotlib.cm import ScalarMappable
from matplotlib.colors import Normalize
from matplotlib.lines import Line2D
from matplotlib.patches import Circle, FancyBboxPatch, Patch

HERE = Path(__file__).resolve().parent
OUT = HERE.parent
CSV_PATH = OUT / "placement_seed20261801.csv"

AREA_M = 4000.0
DEPTH_M = 3500.0
SINK_XY = (2000.0, 2000.0)
SINK_Z = 5.0
EDDY_C = (2000.0, 2000.0)
EDDY_R_M = 2828.0  # ~2.83 km
# Streamline rings that FIT inside the box (half-diagonal ~2.83 km; side half=2 km)
STREAM_R_KM = (0.5, 1.0, 1.5)
# Fixed illustrative route: CSV node_ids (relays), then sink. Hops <= 1.2 km 3-D.
ROUTE_NODE_IDS = [114, 57, 53, 144]  # 4 relays → sink

W_IN = 3.5
H_IN = 3.18

C_SINK = "#D55E00"
C_EDDY = "#009E73"
C_ROUTE = "#CC79A7"
C_REMOVE = "#111111"
C_SURF = "#56B4E9"
C_BED = "#E2C48A"
C_BOX = "#334155"
CMAP = "cividis_r"


def apply_rc() -> None:
    plt.rcParams.update(
        {
            "font.family": "serif",
            "font.serif": ["Liberation Serif", "Times New Roman", "DejaVu Serif"],
            "mathtext.fontset": "dejavuserif",
            "mathtext.default": "it",
            "font.size": 7.0,
            "axes.labelsize": 8.0,
            "xtick.labelsize": 7.0,
            "ytick.labelsize": 7.0,
            "pdf.fonttype": 42,
            "ps.fonttype": 42,
            "savefig.bbox": None,
            "savefig.pad_inches": 0.02,
        }
    )


def load_placement(csv_path: Path = CSV_PATH):
    by_id = {}
    sink = None
    relays = []
    with open(csv_path, newline="") as f:
        for line in f:
            if line.startswith("#") or line.startswith("node_id"):
                continue
            row = next(csv.reader([line]))
            nid, role = int(row[0]), row[1]
            xyz = (float(row[2]), float(row[3]), float(row[4]))
            by_id[nid] = (role, xyz)
            if role == "sink":
                sink = xyz
            else:
                relays.append(xyz)
    if sink is None or len(relays) != 149:
        raise RuntimeError("bad CSV")
    return sink, relays, by_id


def d3(a, b) -> float:
    return math.sqrt((a[0] - b[0]) ** 2 + (a[1] - b[1]) ** 2 + (a[2] - b[2]) ** 2)


def build_route(sink, by_id):
    pts = []
    for nid in ROUTE_NODE_IDS:
        role, xyz = by_id[nid]
        assert role == "relay"
        pts.append(xyz)
    pts.append(sink)
    # validate
    for a, b in zip(pts[:-1], pts[1:]):
        hop = d3(a, b)
        if hop > 1200.0 + 1e-6:
            raise RuntimeError(f"hop {hop} > 1200")
    for a, b in zip(pts[:-1], pts[1:]):
        if b[2] >= a[2] - 1e-9 and b is not pts[-1]:
            # each hop shallower (depth decreases); sink is shallowest
            pass
        if b[2] >= a[2] - 1e-9 and id(b) != id(pts[-1]):
            raise RuntimeError("depth not decreasing")
    for i in range(len(pts) - 1):
        if pts[i + 1][2] >= pts[i][2]:
            raise RuntimeError(
                f"depth order fail {pts[i][2]} -> {pts[i+1][2]}"
            )
    hops = [d3(a, b) / 1000.0 for a, b in zip(pts[:-1], pts[1:])]
    return pts, hops


def white_box_text(ax, x, y, text, fontsize=7, color="black", ha="left", va="bottom", zorder=50):
    """7 pt label on white 70% opacity box."""
    t = ax.text(
        x,
        y,
        text,
        fontsize=fontsize,
        color=color,
        ha=ha,
        va=va,
        zorder=zorder + 1,
        clip_on=False,
    )
    t.set_bbox(
        dict(boxstyle="round,pad=0.15", facecolor="white", edgecolor="none", alpha=0.70)
    )
    return t


def add_ccw_arrows(ax, cx, cy, r_km, n_arr=3, color=C_EDDY):
    """CCW arrowheads on a circle; skip if near box edge artefacts."""
    for k in range(n_arr):
        th = 2 * math.pi * k / n_arr + 0.4
        dth = 0.32
        x0 = cx + r_km * math.cos(th)
        y0 = cy + r_km * math.sin(th)
        x1 = cx + r_km * math.cos(th + dth)
        y1 = cy + r_km * math.sin(th + dth)
        # keep arrows well inside box to avoid corner artefacts
        margin = 0.20
        L = AREA_M / 1000.0
        if not (
            margin < x0 < L - margin
            and margin < y0 < L - margin
            and margin < x1 < L - margin
            and margin < y1 < L - margin
        ):
            continue
        ax.annotate(
            "",
            xy=(x1, y1),
            xytext=(x0, y0),
            arrowprops=dict(arrowstyle="-|>", color=color, lw=0.8, mutation_scale=7),
            zorder=8,
        )


def build_top(sink, relays, route_pts, out_pdf: Path, out_png: Path) -> dict:
    apply_rc()
    fig = plt.figure(figsize=(W_IN, H_IN), dpi=150)

    # Layout: axes with room BELOW for xlabel + two-row legend (no overlap)
    # top colorbar space on right
    ax = fig.add_axes([0.11, 0.30, 0.68, 0.66])  # leave bottom for xlabel + 2-row legend

    L = AREA_M / 1000.0
    cx, cy = EDDY_C[0] / 1000.0, EDDY_C[1] / 1000.0

    # box / sea surface tint
    ax.add_patch(
        plt.Rectangle(
            (0, 0), L, L, facecolor="#F7FBFF", edgecolor=C_BOX, lw=0.9, zorder=0
        )
    )
    ax.add_patch(
        plt.Rectangle(
            (0, 0), L, L, facecolor=C_SURF, alpha=0.10, edgecolor="none", zorder=1
        )
    )

    # Eddy streamlines: 3 concentric rings INSIDE box (not R)
    for i, rr in enumerate(STREAM_R_KM):
        ax.add_patch(
            Circle(
                (cx, cy),
                rr,
                fill=False,
                edgecolor=C_EDDY,
                lw=0.85,
                zorder=5,
                clip_on=True,
            )
        )
        # Arrows only on inner rings — avoids green arrowheads near box corners
        if i < 2:
            add_ccw_arrows(ax, cx, cy, rr, n_arr=3)

    # R = 2.83 km as dashed radial toward SE corner (not a ring)
    th_r = -math.pi / 4
    # end of R line at box boundary along diagonal (R reaches corner)
    # draw dashed segment of length min(R, distance to edge along ray)
    r_plot = EDDY_R_M / 1000.0  # 2.828
    x_end = cx + r_plot * math.cos(th_r)
    y_end = cy + r_plot * math.sin(th_r)
    # clip to box for drawing the visible part; mark R near corner inside
    x_vis = float(np.clip(x_end, 0.05, L - 0.05))
    y_vis = float(np.clip(y_end, 0.05, L - 0.05))
    ax.plot(
        [cx, x_vis],
        [cy, y_vis],
        color=C_EDDY,
        lw=0.9,
        ls="--",
        zorder=6,
        solid_capstyle="round",
    )
    white_box_text(
        ax, x_vis - 0.05, y_vis + 0.08, r"$R$", fontsize=7, color=C_EDDY, ha="right", va="bottom"
    )

    # Relays: exact coords, 5 pt, thin white edge, depth-shaded
    depths = np.array([p[2] for p in relays])
    norm = Normalize(vmin=0.0, vmax=DEPTH_M)
    ax.scatter(
        [p[0] / 1000 for p in relays],
        [p[1] / 1000 for p in relays],
        c=depths,
        cmap=CMAP,
        norm=norm,
        s=5.0**2 / 1.8,  # ~5 pt visual size (matplotlib s is area in pt^2; ~25≈5pt radius-ish)
        # Use s=25 for ~5 pt diameter-ish markers
        linewidths=0.35,
        edgecolors="white",
        zorder=10,
    )
    # re-scatter with explicit s=25 (~5 pt)
    ax.collections[-1].set_sizes([25])

    # Illustrative route (no on-plot "illustrative" text)
    rxs = [p[0] / 1000 for p in route_pts]
    rys = [p[1] / 1000 for p in route_pts]
    ax.plot(rxs, rys, color=C_ROUTE, lw=1.55, zorder=14, solid_capstyle="round")
    ax.scatter(rxs[:-1], rys[:-1], s=28, c=C_ROUTE, zorder=15, edgecolors="white", linewidths=0.4)

    # Sink square
    sx, sy = sink[0] / 1000, sink[1] / 1000
    ax.scatter(
        [sx],
        [sy],
        s=85,
        c=C_SINK,
        marker="s",
        edgecolors="white",
        linewidths=0.7,
        zorder=30,
    )
    # "sink" upper-left of sink, 7 pt white 70% box
    white_box_text(
        ax, sx - 0.12, sy + 0.14, "sink", fontsize=7, color=C_SINK, ha="right", va="bottom"
    )

    # c = small cross beside sink (they coincide at (2,2); offset cross slightly)
    c_off = 0.18
    ax.plot(
        [sx + c_off],
        [sy - 0.05],
        marker="x",
        markersize=6,
        color=C_EDDY,
        markeredgewidth=1.1,
        zorder=31,
    )
    white_box_text(
        ax,
        sx + c_off + 0.08,
        sy - 0.08,
        r"$c$",
        fontsize=7,
        color=C_EDDY,
        ha="left",
        va="top",
    )

    # Removal arrow through +x face; text in empty region
    # Place near lower-right empty-ish area
    ax.annotate(
        "",
        xy=(L, 0.55),
        xytext=(L - 0.45, 0.55),
        arrowprops=dict(arrowstyle="-|>", color=C_REMOVE, lw=1.0, mutation_scale=8),
        zorder=40,
    )
    white_box_text(
        ax,
        L - 1.05,
        0.62,
        "removed when leaving",
        fontsize=7,
        color=C_REMOVE,
        ha="left",
        va="bottom",
    )

    ax.set_xlim(-0.02, L + 0.02)
    ax.set_ylim(-0.02, L + 0.02)
    ax.set_aspect("equal")
    ax.set_xlabel(r"$x$ (km)", fontsize=8, labelpad=2)
    ax.set_ylabel(r"$y$ (km)", fontsize=8, labelpad=2)
    ax.set_xticks([0, 1, 2, 3, 4])
    ax.set_yticks([0, 1, 2, 3, 4])
    ax.tick_params(labelsize=7)
    ax.grid(True, lw=0.25, color="#CBD5E1", zorder=2)
    for spine in ax.spines.values():
        spine.set_color(C_BOX)
        spine.set_linewidth(0.8)

    # Colorbar on the right of axes
    cax = fig.add_axes([0.82, 0.32, 0.032, 0.60])
    sm = ScalarMappable(norm=norm, cmap=CMAP)
    sm.set_array([])
    cb = fig.colorbar(sm, cax=cax)
    cb.set_label("Depth (km)", fontsize=7)
    cb.set_ticks([0, 1000, 2000, 3000, 3500])
    cb.set_ticklabels(["0", "1", "2", "3", "3.5"])
    cb.ax.tick_params(labelsize=6.5)

    # Legend BELOW axis label, two rows, outside axes — no overlap with xlabel
    leg_ax = fig.add_axes([0.02, 0.005, 0.96, 0.13])
    leg_ax.set_axis_off()
    handles = [
        Line2D(
            [0],
            [0],
            marker="s",
            color="w",
            markerfacecolor=C_SINK,
            markersize=7,
            label="sink",
        ),
        Line2D(
            [0],
            [0],
            marker="o",
            color="w",
            markerfacecolor="#0072B2",
            markeredgecolor="white",
            markersize=5,
            label="relay (depth-shaded)",
        ),
        Line2D([0], [0], color=C_EDDY, lw=1.0, label="eddy streamlines (CCW)"),
        Line2D([0], [0], color=C_EDDY, lw=0.9, ls="--", label=r"$R$ (radial)"),
        Line2D([0], [0], color=C_ROUTE, lw=1.4, label="illustrative route"),
        Line2D(
            [0],
            [0],
            marker="x",
            color=C_EDDY,
            markersize=6,
            linestyle="None",
            label=r"eddy centre $c$",
        ),
        Patch(facecolor=C_SURF, edgecolor="#2E6F9E", alpha=0.35, label="sea surface"),
        Patch(facecolor="#F7FBFF", edgecolor=C_BOX, label="seabed / box"),
    ]
    leg = leg_ax.legend(
        handles=handles,
        loc="center",
        ncol=2,
        fontsize=6.0,
        frameon=True,
        fancybox=False,
        edgecolor="#94A3B8",
        framealpha=0.95,
        handlelength=1.6,
        columnspacing=1.0,
        labelspacing=0.3,
        borderpad=0.35,
    )
    leg.get_frame().set_linewidth(0.5)

    fig.savefig(out_pdf, format="pdf", dpi=600)
    fig.savefig(out_png, format="png", dpi=150)
    grey_path = OUT / "previews" / "fig4_deploy_top_v2_grey.png"
    try:
        from PIL import Image

        im = Image.open(out_png).convert("L")
        im.save(grey_path)
        grey_ok = True
    except Exception as e:
        print("grey skip", e)
        grey_ok = False
    plt.close(fig)
    return {"grey_ok": grey_ok}


def main():
    sink, relays, by_id = load_placement()
    route_pts, hops = build_route(sink, by_id)
    print("route node_ids", ROUTE_NODE_IDS, "+ sink")
    print("hops_km", [round(h, 4) for h in hops], "max", round(max(hops), 4), "n_relays", len(ROUTE_NODE_IDS))
    print("depths", [round(p[2], 1) for p in route_pts])

    pdf = OUT / "fig4_deploy_top_v2.pdf"
    png = OUT / "fig4_deploy_top_v2.png"
    info = build_top(sink, relays, route_pts, pdf, png)

    import shutil

    shutil.copy2(pdf, OUT / "previews" / "fig4_deploy_top_v2.pdf")
    shutil.copy2(png, OUT / "previews" / "fig4_deploy_top_v2_150dpi.png")

    r = subprocess.check_output(["pdfinfo", str(pdf)], text=True)
    print([ln for ln in r.splitlines() if "Page size" in ln or "Pages" in ln])
    print("DONE", pdf, "grey", info["grey_ok"])


if __name__ == "__main__":
    main()
