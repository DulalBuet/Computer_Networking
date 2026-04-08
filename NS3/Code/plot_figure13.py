
import matplotlib.pyplot as plt

def read_trace(filename):
    rtt_v, nr_v, br_v = [], [], []
    with open(filename, 'r') as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith('#'):
                continue
            parts = line.split('\t')
            if len(parts) >= 3:
                rtt_v.append(float(parts[0]))
                nr_v.append(float(parts[1]))
                br_v.append(float(parts[2]))
    return rtt_v, nr_v, br_v


rtt_v, nr_v, br_v = read_trace("figure13_throughput_vs_rtt.tr")

fig, ax = plt.subplots(figsize=(7, 5))

# --- New Reno: SOLID line + triangle markers (△) ---
ax.plot(rtt_v, nr_v,
        color='black',
        linestyle='-',          # solid line
        linewidth=1.4,
        marker='^',             # triangle up
        markersize=8,
        markerfacecolor='white',
        markeredgecolor='black',
        markeredgewidth=1.3,
        label="New Reno",
        zorder=3)

# --- BR-TCP: DOTTED line + square markers (□) ---
ax.plot(rtt_v, br_v,
        color='black',
        linestyle=':',          # dotted line  ·····
        linewidth=1.8,          # slightly thicker so dots are visible
        marker='s',             # square
        markersize=8,
        markerfacecolor='white',
        markeredgecolor='black',
        markeredgewidth=1.3,
        label="BR-TCP",
        zorder=4)

# ------------------------------------------------------------------
# Axes — match paper Figure 13
# ------------------------------------------------------------------
ax.set_xlabel("Round trip time, RTT [ms]", fontsize=11)
ax.set_ylabel("Throughput [Mbps]", fontsize=11)

# Determine Y-axis range dynamically
all_vals = nr_v + br_v
y_max = max(30, max(all_vals) + 2) if all_vals else 30

ax.set_xlim(0, 65)
ax.set_ylim(0, y_max)

# Paper X-ticks: 20, 40, 60  (no 0 shown on X axis)
ax.set_xticks([20, 40, 60])

# Y-ticks: every 10 Mbps up to y_max
import numpy as np
ax.set_yticks(np.arange(0, y_max + 1, 10))

ax.grid(True, linestyle=':', alpha=0.4)

textstr = "Bandwidth = 40 [Mbps]\npacket size = 1500 [byte]"
ax.text(
    1, 1, textstr,
    fontsize=9,
    verticalalignment='bottom',
    bbox=dict(facecolor='white', edgecolor='black',
              boxstyle='square,pad=0.4')
)

if len(rtt_v) >= 3:
    label_idx = -2   # second-to-last RTT point

    br_label_y = br_v[label_idx]
    nr_label_y = nr_v[label_idx]
    label_x    = rtt_v[label_idx]

    # BR-TCP label above its curve
    ax.annotate("BR-TCP",
                xy=(label_x, br_label_y),
                xytext=(label_x - 14, br_label_y + 2.0),
                fontsize=9,
                arrowprops=dict(arrowstyle='->', color='black', lw=0.7))

    # New Reno label below its curve
    ax.annotate("New Reno",
                xy=(label_x, nr_label_y),
                xytext=(label_x - 17, nr_label_y - 3.5),
                fontsize=9,
                arrowprops=dict(arrowstyle='->', color='black', lw=0.7))

ax.legend(loc='upper right', fontsize=9)

fig.suptitle(
    "Figure 13: Throughput of BR-TCP and TCP New Reno versus RTT\n"
    "(Reserved BW = 40 Mbps, packet size = 1500 bytes)",
    fontsize=10
)

plt.tight_layout()
plt.savefig("figure13.png", dpi=150, bbox_inches='tight')
print("Saved figure13.png")

if rtt_v:
    print(f"\n{'RTT(ms)':>8} {'NewReno(Mbps)':>15} {'BR-TCP(Mbps)':>14} "
          f"{'Diff':>8} {'Improv%':>9}")
    print("-" * 58)
    for r, nr, br in zip(rtt_v, nr_v, br_v):
        diff = br - nr
        imp  = (diff / nr * 100) if nr > 0 else 0
        flag = (" <-- BR-TCP wins" if br > nr + 0.3
                else " (approx equal)")
        print(f"{r:>8.0f} {nr:>15.2f} {br:>14.2f} "
              f"{diff:>+8.2f} {imp:>8.1f}%{flag}")

    print(f"\nPaper expected: both curves decrease from ~28 Mbps at RTT=10ms")
    print(f"                down to ~8 Mbps (NR) / ~15 Mbps (BR) at RTT=60ms")
    if all_vals and max(all_vals) > 35:
        print(f"\n*** VALUES TOO HIGH (~{max(all_vals):.0f} Mbps) ***")
        print(f"    Recompile sixth_fig13.cc — socket buffers must scale with BDP.")
        print(f"    See corrected sixth_fig13.cc for the buffer sizing fix.")

plt.show()