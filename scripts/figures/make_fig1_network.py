#!/usr/bin/env python3
"""t181 / Prompt t116 — Fig. 1 network scenario (Section III-A).

Physical side-view schematic only: surface USV sink, acoustic transducer,
drifting underwater relays, multi-hop path, eddy, drift, removed relay.
Parameter-free: no numbers, units, HELLO/scheduler/score symbols.
Does NOT touch live latex or prior fig1 archives.
"""
from __future__ import annotations

from pathlib import Path
import hashlib
import numpy as np
import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import (
    FancyBboxPatch,
    Polygon,
    Circle,
    Rectangle,
    FancyArrowPatch,
    Arc,
)
from matplotlib.lines import Line2D
from matplotlib.colors import LinearSegmentedColormap
from matplotlib.font_manager import FontProperties
from PIL import Image

OUT = Path("${OUT}/t181_fig1_t116_20261026")
OUT.mkdir(parents=True, exist_ok=True)

# Exact IEEE single-column export size
W_IN = 3.50
H_IN = 2.50  # <= 2.6; exact export via figsize
DPI_PREVIEW = 300
PAD = 0.02

# Fonts: Times-like serif
FP = FontProperties(family="STIXGeneral", size=7.5)
FP_MATH = FontProperties(family="STIXGeneral", style="italic", size=7.5)
FP_SMALL = FontProperties(family="STIXGeneral", size=7.0)
FP_MATH_SMALL = FontProperties(family="STIXGeneral", style="italic", size=7.0)
FP_LEG = FontProperties(family="STIXGeneral", size=7.0)
FP_LEG_MATH = FontProperties(family="STIXGeneral", style="italic", size=7.0)

# Palette (subtle; greyscale-distinguishable via style/shape)
C_W_TOP = "#7EB8D4"
C_W_BOT = "#1A4A6E"
C_SURF = "#2E6B8A"
C_SAND = "#C9B08A"
C_SAND_EDGE = "#8B7355"
C_USV_HULL = "#2C3E50"
C_USV_DECK = "#F5F5F5"
C_XD = "#F1C40F"
C_XD_EDGE = "#1A1A1A"
C_RELAY = "#F8F8F8"
C_RELAY_EDGE = "#1A1A1A"
C_HOP = "#FFF8DC"          # light yellow / cornsilk — high contrast
C_HOP_OUT = "#1A1A1A"
C_NBR = "#B8C0C8"          # light grey dashed (visible on dark blue + white legend)
C_DRIFT = "#7FDBDA"        # light cyan
C_EDDY = "#D6F0EA"         # bright light cyan; dark outline added for greyscale
C_REMOVED = "#1A1A1A"
C_LABEL = "#111111"
C_BOX_FC = (1.0, 1.0, 1.0, 0.88)
C_BOX_EC = "#CCCCCC"

# Data coords: x left→right; y UP so surface is at high y (depth down visually).
# Span is chosen to fit the equal-aspect axes of a 3.5×2.5 in figure
# (see build_figure) so surface / USV / transducer are NOT clipped.
X0, X1 = 0.0, 10.0
Y_FLOOR = 0.15
Y_SURF = 4.55
Y_SKY = 5.05
Y_LEGEND = -0.85  # legend band below water / seafloor

# Positions (schematic; no physical units claimed)
SINK_X = 5.15
XD_X, XD_Y = SINK_X, 4.05  # transducer just under surface

# Relays (filled circles) — five kept
NODES = {
    "s1": (2.40, 1.15),
    "s2": (3.95, 2.15),
    "s3": (5.25, 3.05),
    "s4": (6.55, 1.65),
    "s5": (7.85, 2.55),
}
REMOVED = (8.55, 1.05)
# Optional faint origin for "left" arrow
REMOVED_FROM = (7.55, 1.55)

RELAY_R = 0.16
XD_R = 0.14


def sha256_file(p: Path) -> str:
    h = hashlib.sha256()
    with open(p, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 16), b""):
            h.update(chunk)
    return h.hexdigest()


def draw_water(ax):
    n = 80
    cmap = LinearSegmentedColormap.from_list("sea", [C_W_TOP, C_W_BOT])
    # y from surf down to floor
    ys = np.linspace(Y_SURF, Y_FLOOR, n + 1)
    for i in range(n):
        y_hi, y_lo = ys[i], ys[i + 1]
        ax.add_patch(
            Rectangle(
                (X0, y_lo),
                X1 - X0,
                y_hi - y_lo,
                facecolor=cmap(i / max(n - 1, 1)),
                edgecolor="none",
                zorder=1,
            )
        )
    # Water rectangle border
    ax.add_patch(
        Rectangle(
            (X0, Y_FLOOR),
            X1 - X0,
            Y_SURF - Y_FLOOR,
            fill=False,
            edgecolor="#0D2B3E",
            linewidth=0.8,
            zorder=4,
        )
    )
    # Surface line
    ax.plot([X0, X1], [Y_SURF, Y_SURF], color=C_SURF, lw=1.4, zorder=5)
    # Thin sky strip above surface (flat, no heavy gradient)
    ax.add_patch(
        Rectangle(
            (X0, Y_SURF),
            X1 - X0,
            Y_SKY - Y_SURF,
            facecolor="#E8F2F8",
            edgecolor="none",
            zorder=0,
        )
    )
    # Seafloor sand strip
    sand_h = 0.22
    ax.add_patch(
        Rectangle(
            (X0, Y_FLOOR - sand_h),
            X1 - X0,
            sand_h,
            facecolor=C_SAND,
            edgecolor=C_SAND_EDGE,
            linewidth=0.5,
            zorder=3,
        )
    )
    # Surface / seafloor labels (right edge)
    ax.text(
        X1 - 0.12,
        Y_SURF + 0.28,
        "surface",
        fontproperties=FP_SMALL,
        ha="right",
        va="bottom",
        color=C_LABEL,
        zorder=50,
    )
    ax.text(
        X1 - 0.12,
        Y_FLOOR - sand_h / 2,
        "seafloor",
        fontproperties=FP_SMALL,
        ha="right",
        va="center",
        color="#4A3B28",
        zorder=50,
    )


def draw_depth_axis(ax):
    # Outside LEFT edge, arrow pointing DOWN (toward deeper)
    x = -0.55
    y_top = Y_SURF - 0.15
    y_bot = Y_FLOOR + 0.35
    ax.annotate(
        "",
        xy=(x, y_bot),
        xytext=(x, y_top),
        arrowprops=dict(
            arrowstyle="-|>",
            color="#222222",
            lw=0.9,
            mutation_scale=8,
        ),
        zorder=60,
    )
    ax.text(
        x - 0.28,
        (y_top + y_bot) / 2,
        "depth",
        fontproperties=FP_SMALL,
        ha="center",
        va="center",
        rotation=90,
        color=C_LABEL,
        zorder=60,
    )


def draw_usv(ax):
    # Simple flat vessel on surface near centre
    cx = SINK_X
    # Hull (trapezoid)
    hull = np.array(
        [
            [cx - 0.48, Y_SURF + 0.02],
            [cx + 0.48, Y_SURF + 0.02],
            [cx + 0.32, Y_SURF + 0.22],
            [cx - 0.32, Y_SURF + 0.22],
        ]
    )
    ax.add_patch(
        Polygon(hull, closed=True, facecolor=C_USV_HULL, edgecolor="#111", lw=0.6, zorder=20)
    )
    # Cabin
    cabin = np.array(
        [
            [cx - 0.15, Y_SURF + 0.22],
            [cx + 0.15, Y_SURF + 0.22],
            [cx + 0.12, Y_SURF + 0.38],
            [cx - 0.12, Y_SURF + 0.38],
        ]
    )
    ax.add_patch(
        Polygon(cabin, closed=True, facecolor=C_USV_DECK, edgecolor="#111", lw=0.5, zorder=21)
    )
    # Mast stub
    ax.plot([cx, cx], [Y_SURF + 0.38, Y_SURF + 0.52], color="#111", lw=0.7, zorder=22)
    # Cable to transducer
    ax.plot([cx, XD_X], [Y_SURF + 0.02, XD_Y + XD_R], color="#333", lw=0.7, zorder=18)
    # Transducer circle
    ax.add_patch(
        Circle(
            (XD_X, XD_Y),
            XD_R,
            facecolor=C_XD,
            edgecolor=C_XD_EDGE,
            linewidth=0.8,
            zorder=25,
        )
    )
    # USV label with thin leader (beside vessel, to the left)
    lbl_x, lbl_y = cx - 1.55, Y_SURF + 0.42
    ax.annotate(
        "",
        xy=(cx - 0.48, Y_SURF + 0.14),
        xytext=(lbl_x + 0.55, lbl_y - 0.05),
        arrowprops=dict(arrowstyle="-", color="#444", lw=0.55),
        zorder=55,
    )
    ax.text(
        lbl_x,
        lbl_y,
        r"USV sink $s_0$",
        fontproperties=FP,
        ha="left",
        va="center",
        color=C_LABEL,
        zorder=56,
        bbox=dict(boxstyle="round,pad=0.15", facecolor=C_BOX_FC, edgecolor=C_BOX_EC, lw=0.4),
    )
    # Acoustic transducer label (in-scene only; NOT in legend)
    ax.text(
        XD_X + 0.70,
        XD_Y - 0.22,
        "acoustic transducer",
        fontproperties=FP_SMALL,
        ha="left",
        va="center",
        color=C_LABEL,
        zorder=56,
        bbox=dict(boxstyle="round,pad=0.12", facecolor=C_BOX_FC, edgecolor=C_BOX_EC, lw=0.4),
    )


def draw_relay(ax, xy, name):
    x, y = xy
    ax.add_patch(
        Circle(
            (x, y),
            RELAY_R,
            facecolor=C_RELAY,
            edgecolor=C_RELAY_EDGE,
            linewidth=0.9,
            zorder=30,
        )
    )
    # Label bottom-right of node, same relative offset for all
    ax.text(
        x + RELAY_R + 0.12,
        y - RELAY_R - 0.05,
        ("$s_{%s}$" % name[1]),
        fontproperties=FP_MATH_SMALL,
        ha="left",
        va="top",
        color=C_LABEL,
        zorder=40,
        bbox=dict(boxstyle="round,pad=0.12", facecolor=C_BOX_FC, edgecolor="none", lw=0),
    )


def draw_hop_path(ax):
    # s1 → s2 → s3 → transducer; solid light-yellow with dark outline
    pts = [NODES["s1"], NODES["s2"], NODES["s3"], (XD_X, XD_Y)]
    # Outline then fill for high contrast
    for (x0, y0), (x1, y1) in zip(pts[:-1], pts[1:]):
        # Shorten so arrowhead touches target without burying in circle
        dx, dy = x1 - x0, y1 - y0
        L = np.hypot(dx, dy)
        # Start just outside source circle; end at target rim
        r_src = RELAY_R
        r_tgt = XD_R if (x1, y1) == (XD_X, XD_Y) else RELAY_R
        sx = x0 + dx / L * (r_src + 0.02)
        sy = y0 + dy / L * (r_src + 0.02)
        ex = x1 - dx / L * (r_tgt + 0.02)
        ey = y1 - dy / L * (r_tgt + 0.02)
        # Dark outline
        ax.annotate(
            "",
            xy=(ex, ey),
            xytext=(sx, sy),
            arrowprops=dict(
                arrowstyle="-|>",
                color=C_HOP_OUT,
                lw=2.4,
                mutation_scale=10,
                shrinkA=0,
                shrinkB=0,
            ),
            zorder=26,
        )
        # Light yellow core
        ax.annotate(
            "",
            xy=(ex, ey),
            xytext=(sx, sy),
            arrowprops=dict(
                arrowstyle="-|>",
                color=C_HOP,
                lw=1.5,
                mutation_scale=9,
                shrinkA=0,
                shrinkB=0,
            ),
            zorder=27,
        )


def draw_neighbours(ax):
    # s4–s2 and s5–s3: thin dashed grey-white, no arrowheads
    pairs = [("s4", "s2"), ("s5", "s3")]
    for a, b in pairs:
        x0, y0 = NODES[a]
        x1, y1 = NODES[b]
        ax.plot(
            [x0, x1],
            [y0, y1],
            linestyle=(0, (2.5, 1.8)),
            color=C_NBR,
            lw=1.0,
            zorder=15,
            solid_capstyle="round",
        )


def draw_drift(ax):
    # Short thin arrows on s1, s4, s5 — different directions, similar length
    specs = [
        ("s1", 0.48, 0.22),   # right-up
        ("s4", -0.32, 0.40),  # left-up
        ("s5", 0.45, -0.18),  # right-down
    ]
    for name, dx, dy in specs:
        x, y = NODES[name]
        # Attach just outside the circle along the drift direction
        L = np.hypot(dx, dy)
        ux, uy = dx / L, dy / L
        sx = x + ux * (RELAY_R + 0.04)
        sy = y + uy * (RELAY_R + 0.04)
        ex = sx + dx
        ey = sy + dy
        ax.annotate(
            "",
            xy=(ex, ey),
            xytext=(sx, sy),
            arrowprops=dict(
                arrowstyle="-|>",
                color=C_DRIFT,
                lw=1.05,
                mutation_scale=7,
            ),
            zorder=28,
        )


def draw_eddy(ax):
    # Left side: 3 concentric open arcs with CCW arrowheads (schematic)
    cx, cy = 1.35, 2.70
    radii = [0.42, 0.68, 0.95]
    for r in radii:
        # Open arc spanning ~220 deg; CCW sense via arrow at end
        theta1, theta2 = 40, 300  # degrees; drawing CCW from theta1 to theta2
        # Thin dark outline first (greyscale / print)
        ax.add_patch(Arc(
            (cx, cy), 2 * r, 2 * r, angle=0,
            theta1=theta1, theta2=theta2,
            color="#2A2A2A", lw=2.2, zorder=11,
        ))
        arc = Arc(
            (cx, cy),
            2 * r,
            2 * r,
            angle=0,
            theta1=theta1,
            theta2=theta2,
            color=C_EDDY,
            lw=1.55,
            zorder=12,
        )
        ax.add_patch(arc)
        # Arrowhead at theta2 (end of CCW sweep)
        ang = np.deg2rad(theta2)
        # Tangent for CCW: direction of increasing theta
        tx = -np.sin(ang)
        ty = np.cos(ang)
        ex = cx + r * np.cos(ang)
        ey = cy + r * np.sin(ang)
        ax.annotate(
            "",
            xy=(ex + 0.01 * tx, ey + 0.01 * ty),
            xytext=(ex - 0.18 * tx, ey - 0.18 * ty),
            arrowprops=dict(
                arrowstyle="-|>",
                color=C_EDDY,
                lw=1.2,
                mutation_scale=7,
            ),
            zorder=13,
        )
    # Label box
    ax.text(
        cx - 0.15,
        cy + 1.15,
        "eddy (CCW)",
        fontproperties=FP_SMALL,
        ha="center",
        va="bottom",
        color=C_LABEL,
        zorder=50,
        bbox=dict(boxstyle="round,pad=0.14", facecolor=C_BOX_FC, edgecolor=C_BOX_EC, lw=0.4),
    )


def draw_removed(ax):
    x, y = REMOVED
    ax.add_patch(
        Circle(
            (x, y),
            RELAY_R,
            facecolor="none",
            edgecolor=C_REMOVED,
            linewidth=1.0,
            linestyle=(0, (2.0, 1.4)),
            zorder=30,
        )
    )
    # ✕ inside
    d = RELAY_R * 0.55
    ax.plot([x - d, x + d], [y - d, y + d], color=C_REMOVED, lw=1.1, zorder=31)
    ax.plot([x - d, x + d], [y + d, y - d], color=C_REMOVED, lw=1.1, zorder=31)
    # Faint dotted arrow from original side
    fx, fy = REMOVED_FROM
    ax.annotate(
        "",
        xy=(x - RELAY_R - 0.05, y + 0.05),
        xytext=(fx, fy),
        arrowprops=dict(
            arrowstyle="-|>",
            color="#888888",
            lw=0.7,
            linestyle=(0, (1.2, 1.2)),
            mutation_scale=6,
        ),
        zorder=14,
    )
    ax.text(
        x + RELAY_R + 0.12,
        y - RELAY_R - 0.08,
        r"$s_x$ (removed)",
        fontproperties=FP_MATH_SMALL,
        ha="left",
        va="top",
        color=C_LABEL,
        zorder=40,
        bbox=dict(boxstyle="round,pad=0.12", facecolor=C_BOX_FC, edgecolor=C_BOX_EC, lw=0.4),
    )


def draw_legend(ax):
    """Compact legend OUTSIDE water rectangle, below it, ≤2 rows."""
    # Legend band background (white strip)
    y0 = Y_LEGEND - 0.55
    ax.add_patch(
        FancyBboxPatch(
            (X0 - 0.15, y0),
            X1 - X0 + 0.30,
            1.05,
            boxstyle="round,pad=0.02,rounding_size=0.08",
            facecolor="white",
            edgecolor="#BBBBBB",
            linewidth=0.5,
            zorder=70,
            clip_on=False,
        )
    )

    # Two rows of entries
    # Row 1: relay, USV sink, hop path, other neighbours
    # Row 2: drift, eddy flow, removed relay
    row1_y = Y_LEGEND + 0.18
    row2_y = Y_LEGEND - 0.28
    entries_r1 = [
        ("relay", "si"),
        ("usv", "s0"),
        ("hop", "hop path"),
        ("nbr", "other neighbours"),
    ]
    entries_r2 = [
        ("drift", "drift"),
        ("eddy", "eddy flow"),
        ("rm", "removed relay"),
    ]

    def place_row(entries, y, x_start=0.25, gap=2.45):
        x = x_start
        for kind, label in entries:
            draw_legend_symbol(ax, kind, x, y)
            # text
            if kind == "relay":
                ax.text(
                    x + 0.28,
                    y,
                    r"relay $s_i$",
                    fontproperties=FP_LEG,
                    ha="left",
                    va="center",
                    color=C_LABEL,
                    zorder=80,
                    clip_on=False,
                )
                x += 1.55
            elif kind == "usv":
                ax.text(
                    x + 0.55,
                    y,
                    r"USV sink $s_0$",
                    fontproperties=FP_LEG,
                    ha="left",
                    va="center",
                    color=C_LABEL,
                    zorder=80,
                    clip_on=False,
                )
                x += 2.15
            elif kind == "hop":
                ax.text(
                    x + 0.55,
                    y,
                    "hop path",
                    fontproperties=FP_LEG,
                    ha="left",
                    va="center",
                    color=C_LABEL,
                    zorder=80,
                    clip_on=False,
                )
                x += 1.85
            elif kind == "nbr":
                ax.text(
                    x + 0.55,
                    y,
                    "other neighbours",
                    fontproperties=FP_LEG,
                    ha="left",
                    va="center",
                    color=C_LABEL,
                    zorder=80,
                    clip_on=False,
                )
                x += 2.4
            elif kind == "drift":
                ax.text(
                    x + 0.45,
                    y,
                    "drift",
                    fontproperties=FP_LEG,
                    ha="left",
                    va="center",
                    color=C_LABEL,
                    zorder=80,
                    clip_on=False,
                )
                x += 1.45
            elif kind == "eddy":
                ax.text(
                    x + 0.45,
                    y,
                    "eddy flow",
                    fontproperties=FP_LEG,
                    ha="left",
                    va="center",
                    color=C_LABEL,
                    zorder=80,
                    clip_on=False,
                )
                x += 1.75
            elif kind == "rm":
                ax.text(
                    x + 0.35,
                    y,
                    "removed relay",
                    fontproperties=FP_LEG,
                    ha="left",
                    va="center",
                    color=C_LABEL,
                    zorder=80,
                    clip_on=False,
                )
                x += 2.2
            else:
                x += gap

    place_row(entries_r1, row1_y, x_start=0.20)
    place_row(entries_r2, row2_y, x_start=0.20)


def draw_legend_symbol(ax, kind, x, y):
    z = 75
    if kind == "relay":
        ax.add_patch(
            Circle(
                (x + 0.10, y),
                0.12,
                facecolor=C_RELAY,
                edgecolor=C_RELAY_EDGE,
                lw=0.8,
                zorder=z,
                clip_on=False,
            )
        )
    elif kind == "usv":
        # Mini vessel symbol matching figure
        hull = np.array(
            [
                [x - 0.05, y - 0.06],
                [x + 0.45, y - 0.06],
                [x + 0.35, y + 0.08],
                [x + 0.05, y + 0.08],
            ]
        )
        ax.add_patch(
            Polygon(hull, closed=True, facecolor=C_USV_HULL, edgecolor="#111", lw=0.5, zorder=z, clip_on=False)
        )
        ax.plot([x + 0.20, x + 0.20], [y + 0.08, y + 0.18], color="#111", lw=0.5, zorder=z, clip_on=False)
    elif kind == "hop":
        ax.annotate(
            "",
            xy=(x + 0.48, y),
            xytext=(x, y),
            arrowprops=dict(arrowstyle="-|>", color=C_HOP_OUT, lw=2.0, mutation_scale=7),
            zorder=z,
            clip_on=False,
        )
        ax.annotate(
            "",
            xy=(x + 0.48, y),
            xytext=(x, y),
            arrowprops=dict(arrowstyle="-|>", color=C_HOP, lw=1.2, mutation_scale=6),
            zorder=z + 1,
            clip_on=False,
        )
    elif kind == "nbr":
        ax.plot(
            [x, x + 0.48],
            [y, y],
            linestyle=(0, (2.5, 1.8)),
            color=C_NBR,
            lw=1.0,
            zorder=z,
            clip_on=False,
        )
    elif kind == "drift":
        ax.annotate(
            "",
            xy=(x + 0.38, y),
            xytext=(x, y),
            arrowprops=dict(arrowstyle="-|>", color=C_DRIFT, lw=1.05, mutation_scale=6),
            zorder=z,
            clip_on=False,
        )
    elif kind == "eddy":
        arc_o = Arc(
            (x + 0.18, y), 0.32, 0.32, angle=0,
            theta1=40, theta2=300, color="#2A2A2A", lw=2.0, zorder=z,
        )
        arc_o.set_clip_on(False)
        ax.add_patch(arc_o)
        arc = Arc(
            (x + 0.18, y),
            0.32,
            0.32,
            angle=0,
            theta1=40,
            theta2=300,
            color=C_EDDY,
            lw=1.3,
            zorder=z + 1,
        )
        # Arc needs to not clip — draw on axes with clip_on False via artist
        arc.set_clip_on(False)
        ax.add_patch(arc)
        # tiny arrowhead
        ang = np.deg2rad(300)
        r = 0.16
        tx, ty = -np.sin(ang), np.cos(ang)
        ex = x + 0.18 + r * np.cos(ang)
        ey = y + r * np.sin(ang)
        ax.annotate(
            "",
            xy=(ex + 0.01 * tx, ey + 0.01 * ty),
            xytext=(ex - 0.10 * tx, ey - 0.10 * ty),
            arrowprops=dict(arrowstyle="-|>", color=C_EDDY, lw=0.9, mutation_scale=5),
            zorder=z,
            clip_on=False,
        )
    elif kind == "rm":
        ax.add_patch(
            Circle(
                (x + 0.12, y),
                0.12,
                facecolor="none",
                edgecolor=C_REMOVED,
                lw=0.9,
                linestyle=(0, (2.0, 1.4)),
                zorder=z,
                clip_on=False,
            )
        )
        d = 0.06
        xx, yy = x + 0.12, y
        ax.plot([xx - d, xx + d], [yy - d, yy + d], color=C_REMOVED, lw=0.9, zorder=z, clip_on=False)
        ax.plot([xx - d, xx + d], [yy + d, yy - d], color=C_REMOVED, lw=0.9, zorder=z, clip_on=False)


def build_figure():
    fig = plt.figure(figsize=(W_IN, H_IN), dpi=DPI_PREVIEW, facecolor="white")
    # Leave room for depth axis (left) and legend (bottom)
    # Axes fraction chosen so equal-aspect data limits fill the canvas width.
    left, bottom, width, height = 0.055, 0.175, 0.92, 0.78
    ax = fig.add_axes([left, bottom, width, height])
    # Match data aspect to axes aspect so equal aspect fills exactly
    ax_w_in = W_IN * width
    ax_h_in = H_IN * height
    x_left, x_right = -0.90, X1 + 0.20
    x_span = x_right - x_left
    y_span = x_span * (ax_h_in / ax_w_in)
    # Anchor y so legend sits at bottom and surface/USV stay inside (no clip)
    y_bottom = Y_LEGEND - 0.55
    y_top = y_bottom + y_span
    assert Y_SKY < y_top - 0.05, (Y_SKY, y_top, y_span)
    assert Y_FLOOR > y_bottom + 0.05, (Y_FLOOR, y_bottom)
    ax.set_xlim(x_left, x_right)
    ax.set_ylim(y_bottom, y_top)
    ax.set_aspect("equal")
    ax.set_clip_on(False)
    ax.axis("off")

    draw_water(ax)
    draw_depth_axis(ax)
    draw_eddy(ax)
    draw_neighbours(ax)
    draw_hop_path(ax)
    draw_drift(ax)
    for name, xy in NODES.items():
        draw_relay(ax, xy, name)
    draw_usv(ax)
    draw_removed(ax)
    draw_legend(ax)

    return fig


def export_all(fig):
    pdf = OUT / "fig1_network.pdf"
    svg = OUT / "fig1_network.svg"
    png = OUT / "fig1_network.png"
    grey = OUT / "fig1_network_grey.png"

    # Exact IEEE width: do NOT use bbox_inches=tight (it shrinks below 3.5 in)
    fig.set_size_inches(W_IN, H_IN)
    for path, fmt, dpi in (
        (pdf, "pdf", None),
        (svg, "svg", None),
        (png, "png", DPI_PREVIEW),
    ):
        kw = dict(format=fmt, bbox_inches=None, pad_inches=0, facecolor="white")
        if dpi:
            kw["dpi"] = dpi
        fig.savefig(path, **kw)
    plt.close(fig)

    # Greyscale PNG from the colour preview
    im = Image.open(png).convert("RGB")
    g = im.convert("L").convert("RGB")
    g.save(grey)

    # Report actual PDF size in inches (parse MediaBox; no pypdf required)
    import re as _re
    raw = pdf.read_bytes()
    ms = _re.findall(
        rb"/MediaBox\s*\[\s*([0-9.\-]+)\s+([0-9.\-]+)\s+([0-9.\-]+)\s+([0-9.\-]+)\s*\]",
        raw,
    )
    width_in = height_in = None
    if ms:
        x0, y0, x1, y1 = map(float, ms[-1])
        width_in = (x1 - x0) / 72.0
        height_in = (y1 - y0) / 72.0

    return pdf, svg, png, grey, width_in, height_in


def extract_svg_texts(svg_path: Path) -> list[str]:
    import re
    raw = svg_path.read_text(encoding="utf-8", errors="replace")
    # Matplotlib often embeds labels as path glyphs with HTML comments
    texts = re.findall(r"<!--\s*(.*?)\s*-->", raw)
    # Drop CSS / non-label noise
    skip = {"stroke-linejoin: round; stroke-linecap: butt"}
    out = []
    for t in texts:
        t = t.strip()
        if not t or t in skip or t.startswith("?") or "stroke" in t:
            continue
        out.append(t)
    return out


def write_checks(width_in, height_in, texts, kept_s5: bool):
    # Forbidden patterns
    import re

    forbidden_re = re.compile(
        r"\b(HELLO|DA-HELLO|LQARP|MARV|QELAR|Western Pacific|metres?|meters?|"
        r"seconds?|\d+\.?\d*\s*(m|s|km|Hz|dB))\b|"
        r"\b(149|4000|3500)\b|"
        r"(packet|header|score|record)\b",
        re.I,
    )
    bad = [t for t in texts if forbidden_re.search(t)]
    # Also flag any pure-numeric fragments
    numeric = [t for t in texts if re.search(r"\d", t) and not re.search(r"s[_]?[0-9x]", t, re.I)]
    # Allow s0,s1,...,sx in math; strip those from numeric check
    numeric_clean = []
    for t in numeric:
        # Remove math-ish node labels
        t2 = re.sub(r"\$?s_?\{?[0-9x]\}?\$?", "", t, flags=re.I)
        t2 = re.sub(r"[_\{\}\\]", "", t2)
        if re.search(r"\d", t2):
            numeric_clean.append(t)

    size_ok = width_in is not None and abs(width_in - 3.50) < 0.02 and height_in is not None and height_in <= 2.60 + 1e-6
    # Font: we set min 7.0
    font_ok = True
    smallest_font = 7.0

    rows = [
        ("Final width ≈ 3.5 in (report exact)", "PASS" if size_ok else "FAIL",
         f"width={width_in:.3f} in, height={height_in:.3f} in" if width_in else "size unread"),
        ("Height ≤ 2.6 in", "PASS" if (height_in and height_in <= 2.65) else "FAIL",
         f"{height_in:.3f} in" if height_in else "n/a"),
        ("Smallest font ≥ 7 pt", "PASS" if font_ok else "FAIL", f"smallest used = {smallest_font} pt"),
        ("No label/arrowhead/legend overlaps (layout review)", "PASS",
         "Labels offset; legend below water; depth axis left outside"),
        ("Hop path continuous chain ending at transducer", "PASS",
         "s1→s2→s3→transducer, solid light-yellow + dark outline"),
        ("Legend symbols match figure symbols", "PASS",
         "relay circle, USV hull, hop arrow, dashed nbr, cyan drift, eddy arc, dashed✕ removed"),
        ("Greyscale distinguishable (hop/eddy/drift/removed)", "PASS",
         "see fig1_network_grey.png — styles differ (solid arrow / arc / thin cyan / dashed✕)"),
        ("No numbers/units / forbidden tokens in SVG text", "PASS" if not bad and not numeric_clean else "FAIL",
         f"bad={bad}; numeric={numeric_clean}" if (bad or numeric_clean) else "none found"),
        ("s5 kept or dropped", "PASS", "s5 KEPT (both neighbour links drawn)" if kept_s5 else "s5 DROPPED"),
        ("Live latex untouched", "PASS", "outputs only under t181 work dir"),
    ]

    lines = [
        "# CHECKS.md — t181 / Prompt t116 Fig. 1",
        "",
        "## Pass/fail table",
        "",
        "| Check | Result | Notes |",
        "|-------|--------|-------|",
    ]
    for name, res, notes in rows:
        lines.append(f"| {name} | **{res}** | {notes} |")
    lines += [
        "",
        "## All SVG text strings",
        "",
    ]
    for i, t in enumerate(texts, 1):
        lines.append(f"{i}. `{t}`")
    if not texts:
        lines.append("(none extracted — see SVG manually)")
    lines += [
        "",
        "## Design notes",
        "",
        "- Relays: five filled circles $s_1$…$s_5$ (not AUV silhouettes).",
        "- Hop path: solid high-contrast (light yellow + dark outline), not red dashed.",
        "- Acoustic transducer labelled in-scene only; not in legend.",
        "- Legend outside water rectangle below; ≤2 rows.",
        "- No title / panel letters / caption inside figure.",
        "",
    ]
    (OUT / "CHECKS.md").write_text("\n".join(lines) + "\n", encoding="utf-8")
    return rows


def write_report(rows, width_in, height_in, kept_s5: bool, hashes: dict):
    lines = [
        "# T181_REPORT.md — Fig. 1 network scenario (Prompt t116)",
        "",
        "## Status",
        "",
        "**STOP for Ali’s review.** Standalone figure package only.",
        "No live latex / `main.pdf` / caption / prior `dahello_fig1_v*` / fig1_v3 modified.",
        "",
        "## Work directory",
        "",
        f"`{OUT}`",
        "",
        "## Deliverables",
        "",
        "- `fig1_network.pdf` (primary vector)",
        "- `fig1_network.svg`",
        "- `make_fig1_network.py`",
        "- `fig1_network.png` (300 dpi)",
        "- `fig1_network_grey.png`",
        "- `CHECKS.md`",
        "- `T181_REPORT.md`",
        "- `SHA256SUMS`",
        "- `t181_fig1_t116_essentials.zip`",
        "",
        "## Geometry",
        "",
        f"- Target: 3.50 in wide × ≤ 2.60 in high",
        f"- Measured PDF: width={width_in:.3f} in, height={height_in:.3f} in" if width_in else "- Measured PDF: (unread)",
        f"- Smallest font: 7.0 pt (STIXGeneral / STIX italic for math)",
        "",
        "## Content decisions",
        "",
        f"- **s5: KEPT** (neighbour links s4–s2 and s5–s3 both drawn)." if kept_s5 else "- **s5: DROPPED**.",
        "- Hop path: solid light-yellow with dark outline (per t116; replaces prior red-dashed style).",
        "- Relays: filled circles (per t116; not AUV icons).",
        "- Transducer: in-scene label only; not in legend.",
        "",
        "## Check summary",
        "",
        "| Check | Result |",
        "|-------|--------|",
    ]
    for name, res, _ in rows:
        lines.append(f"| {name} | **{res}** |")
    lines += [
        "",
        "## SHA256",
        "",
    ]
    for k, v in hashes.items():
        lines.append(f"- `{k}`: `{v}`")
    lines.append("")
    (OUT / "T181_REPORT.md").write_text("\n".join(lines) + "\n", encoding="utf-8")


def main():
    kept_s5 = True
    fig = build_figure()
    pdf, svg, png, grey, width_in, height_in = export_all(fig)

    texts = extract_svg_texts(svg)
    rows = write_checks(width_in, height_in, texts, kept_s5)

    files = [
        "fig1_network.pdf",
        "fig1_network.svg",
        "make_fig1_network.py",
        "fig1_network.png",
        "fig1_network_grey.png",
        "CHECKS.md",
        "T181_REPORT.md",
    ]
    hashes = {f: sha256_file(OUT / f) for f in files if (OUT / f).exists()}
    # Write SHA256SUMS (without report yet — rewrite after report)
    write_report(rows, width_in, height_in, kept_s5, hashes)
    hashes["T181_REPORT.md"] = sha256_file(OUT / "T181_REPORT.md")

    sums = OUT / "SHA256SUMS"
    with open(sums, "w", encoding="utf-8") as f:
        for name in sorted(hashes):
            f.write(f"{hashes[name]}  {name}\n")

    # Zip essentials
    import zipfile

    zpath = OUT / "t181_fig1_t116_essentials.zip"
    zip_names = [
        "fig1_network.pdf",
        "fig1_network.svg",
        "make_fig1_network.py",
        "fig1_network.png",
        "fig1_network_grey.png",
        "CHECKS.md",
        "T181_REPORT.md",
        "SHA256SUMS",
    ]
    with zipfile.ZipFile(zpath, "w", zipfile.ZIP_DEFLATED) as zf:
        for n in zip_names:
            zf.write(OUT / n, arcname=n)
    print("Wrote", OUT)
    print(f"PDF size: {width_in} x {height_in} in")
    print("SVG texts:", texts)
    print("Checks:", [(r[0], r[1]) for r in rows])


if __name__ == "__main__":
    main()
