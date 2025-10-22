# Generate the "choose" and "split" figures only (no steps).
from pathlib import Path
import math
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import Rectangle, Patch
import numpy as np

# -------- geometry --------
def union_rect(a, b):
    return [min(a[0], b[0]), min(a[1], b[1]), max(a[2], b[2]), max(a[3], b[3])]

def area(r):
    # robust to inverted or degenerate rects
    w = max(0.0, float(r[2]) - float(r[0]))
    h = max(0.0, float(r[3]) - float(r[1]))
    return w * h

def enlargement(mbr, newr):
    # Correct definition: area(union) - area(mbr)
    u = union_rect(mbr, newr)
    return area(u) - area(mbr), u

def rp(ax, r, **kw):
    x1,y1,x2,y2 = map(float, r)
    p = Rectangle((x1,y1), x2-x1, y2-y1, **kw)
    ax.add_patch(p)
    return p

def set_bounds(ax, rects):
    xs = [x for r in rects for x in (r[0], r[2])]
    ys = [y for r in rects for y in (r[1], r[3])]
    if not xs or not ys:
        ax.set_aspect("equal", adjustable="box")
        return
    pad_x = max(1.0, 0.08*(max(xs)-min(xs)))
    pad_y = max(1.0, 0.08*(max(ys)-min(ys)))
    ax.set_xlim(min(xs)-pad_x, max(xs)+pad_x)
    ax.set_ylim(min(ys)-pad_y, max(ys)+pad_y)
    ax.set_aspect("equal", adjustable="box")

def color_cycle(n):
    cmap = plt.get_cmap("tab20")
    return [cmap(i % 20) for i in range(n)]

# -------- choose (two children) --------
def draw_choose_quadratic(children, newr, outpath):
    # children: [(id, rect), ...]
    colors = {cid: col for cid, col in zip([c[0] for c in children], color_cycle(len(children)))}
    scored = []
    for cid, m in children:
        enl, U = enlargement(m, newr)
        scored.append((cid, m, enl, U, area(m)))
    # choose least enlargement then smallest child area
    scored.sort(key=lambda t: (t[2], t[4]))
    winner = scored[0][0]

    fig, ax = plt.subplots(figsize=(6.2, 6.2), dpi=140)
    set_bounds(ax, [newr] + [m for _, m, _, _, _ in scored])
    ax.set_title("Choose (quadratic R-Tree): hatched = enlargement (union − child)")

    # new rect (distinct color: neutral black/gray)
    rp(ax, newr, facecolor=(0,0,0,0.08), edgecolor="black", linewidth=1.8)
    ax.text(newr[0], newr[1], "NEW", va="top", fontsize=10, color="black")

    # draw candidates
    handles = []
    for cid, m, enl, U, child_area in scored:
        c = colors[cid]
        # union area (hatched, using child's color)
        rp(ax, U, facecolor=c, alpha=0.18, edgecolor=c, linewidth=0.8, hatch='//')
        # mask child area
        rp(ax, m, facecolor="white", edgecolor="none")
        # outline child
        rp(ax, m, fill=False, edgecolor=c, linewidth=3.0 if cid == winner else 1.8)
        ax.text(m[0], m[1],
                f"child#{cid}  +enl={enl:.2f}" +
                ("  ← WIN" if cid == winner else ""),
                va="bottom", fontsize=9, color=c)
        handles.append(Patch(facecolor=c, edgecolor=c, label=f"child#{cid}", alpha=0.5))

    proxy = Patch(facecolor=(0,0,0,0.15), hatch='//', edgecolor='gray', label='enlargement')
    ax.legend(handles=[proxy] + handles, loc="upper center", ncol=3, framealpha=0.95)
    ax.grid(True, alpha=0.2)
    fig.savefig(outpath, bbox_inches="tight")
    plt.close(fig)

# -------- quadratic split only --------
def pick_seeds_quadratic(rects):
    # seeds maximize wasted area
    worst, si, sj = -1.0, 0, 1
    n = len(rects)
    for i in range(n):
        for j in range(i+1, n):
            w = area(union_rect(rects[i], rects[j])) - area(rects[i]) - area(rects[j])
            if w > worst:
                worst, si, sj = w, i, j
    return si, sj

def distribute_quadratic(rects, seeds):
    s0, s1 = seeds
    G0, G1 = {s0}, {s1}
    cov0, cov1 = rects[s0][:], rects[s1][:]
    remaining = [i for i in range(len(rects)) if i not in (s0, s1)]
    order = []
    while remaining:
        best_idx = None; best_diff = -1.0; best_group = 0
        for i in remaining:
            g0 = area(union_rect(cov0, rects[i])) - area(cov0)
            g1 = area(union_rect(cov1, rects[i])) - area(cov1)
            diff = abs(g0 - g1)
            if g0 < g1: group = 0
            elif g1 < g0: group = 1
            else:
                a0 = area(union_rect(cov0, rects[i]))
                a1 = area(union_rect(cov1, rects[i]))
                if a0 < a1: group = 0
                elif a1 < a0: group = 1
                else: group = 0 if len(G0) <= len(G1) else 1
            if diff > best_diff:
                best_diff = diff; best_idx = i; best_group = group
        order.append((best_idx, best_group, best_diff))
        if best_group == 0:
            G0.add(best_idx); cov0 = union_rect(cov0, rects[best_idx])
        else:
            G1.add(best_idx); cov1 = union_rect(cov1, rects[best_idx])
        remaining.remove(best_idx)
    return G0, G1, cov0, cov1, order

def draw_split_only(rects, labels, outpath):
    colors = color_cycle(len(rects))
    s0, s1 = pick_seeds_quadratic(rects)
    G0, G1, cov0, cov1, _ = distribute_quadratic(rects, (s0, s1))

    fig, axes = plt.subplots(1, 2, figsize=(12, 5), dpi=140)
    # initial
    ax = axes[0]
    set_bounds(ax, rects)
    ax.set_title("Initial entries (colors per rectangle)")
    for i, r in enumerate(rects):
        rp(ax, r, facecolor=colors[i], edgecolor=colors[i], alpha=0.35, linewidth=1.2)
        ax.text(r[0], r[1], labels[i], fontsize=9, va="bottom", color=colors[i])
    # seeds
    for s in (s0, s1):
        x = (rects[s][0] + rects[s][2]) / 2
        y = (rects[s][1] + rects[s][3]) / 2
        ax.plot([x], [y], marker='o', markersize=9, color='black')

    # result
    ax = axes[1]
    set_bounds(ax, [*rects, cov0, cov1])
    ax.set_title(f"Quadratic split result (seeds E{s0}, E{s1})")
    rp(ax, cov0, fill=False, edgecolor="black", linewidth=2.4)
    rp(ax, cov1, fill=False, edgecolor="black", linewidth=2.4, linestyle="--")
    for idx in sorted(G0):
        r = rects[idx]; c = colors[idx]
        rp(ax, r, facecolor=c, edgecolor=c, alpha=0.35, hatch='///', linewidth=1.2)
        ax.text(r[0], r[1], labels[idx], fontsize=9, va="bottom", color=colors[idx])
    for idx in sorted(G1):
        r = rects[idx]; c = colors[idx]
        rp(ax, r, facecolor=c, edgecolor=c, alpha=0.35, hatch='\\\\\\', linewidth=1.2)
        ax.text(r[0], r[1], labels[idx], fontsize=9, va="bottom", color=colors[idx])

    ax.grid(True, alpha=0.2)
    fig.suptitle("R-Tree Quadratic Split — seeds and final distribution")
    fig.tight_layout()
    fig.savefig(outpath, bbox_inches="tight")
    plt.close(fig)

# -------- demo data --------
# Choose: exactly two children; NEW is clearly closer to child#0 → smaller enlargement
children_demo = [
    (0, [1, 1, 4, 4]),
    (1, [9, 3, 10, 4]),
]
new_rect_demo = [2, 2, 5, 5]

# Split: two obvious spatial clusters (left-bottom vs right/top)
split_entries_demo = [
    (0, [1,1,2,2], "E0"),
    (1, [2,1,3,2.5], "E1"),
    (2, [1.5,2,2.5,3], "E2"),
    (3, [6.5,5.0,7.5,6.0], "E3"),
    (4, [7.2,6.2,8.2,7.3], "E4"),
    (5, [8.0,1.2,9.0,2.2], "E5"),
    (6, [6.5,8.5,7.5,9.5], "E6"),
    (7, [7.8,7.2,8.8,8.2], "E7"),
    (8, [6.8,6.8,7.8,7.8], "E8"),
]

# -------- run (no steps) --------
choose_out = Path("choose.png")
draw_choose_quadratic(children_demo, new_rect_demo, choose_out)

split_out = Path("quadratic_split.png")
rects = [r for _, r, *_ in split_entries_demo]
labels = [ (e[2] if len(e)>2 else f"E{i}") for i, e in enumerate(split_entries_demo) ]
draw_split_only(rects, labels, split_out)

print("Wrote:")
print(str(choose_out))
print(str(split_out))
