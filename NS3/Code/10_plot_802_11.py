

import matplotlib.pyplot as plt
import matplotlib.gridspec as gridspec
import numpy as np
import os

# =====================================================================
# Configuration
# =====================================================================
NODE_CONFIGS = [20, 40, 60, 80, 100]
SIM_TIME     = 50.0
SEGMENT_SIZE = 1472   # bytes — used for packets conversion in .cc

# Plot style matching paper Figure 4
CWND_STYLE   = dict(color='black', linestyle='-',  linewidth=0.9)
SS_STYLE     = dict(color='black', linestyle='--', linewidth=1.3)

def read_trace(filename):
    """
    .tr file পড়ে (time, value) list return করে।
    File না থাকলে empty list return করে।
    """
    times, values = [], []
    if not os.path.exists(filename):
        print(f"  [WARNING] File not found: {filename}")
        return times, values

    with open(filename, 'r') as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith('#'):
                continue
            parts = line.split('\t')
            if len(parts) >= 2:
                try:
                    t = float(parts[0])
                    v = float(parts[1])
                    if 0 <= t <= SIM_TIME and v >= 0:
                        times.append(t)
                        values.append(v)
                except ValueError:
                    continue
    return times, values


def get_ylim(cwnd_v, ss_v, pad_frac=0.15):
    all_vals = cwnd_v + ss_v
    if not all_vals:
        return 0, 400
    vmax = max(all_vals)
    # Round up to nearest 100
    ymax = max(400, int(np.ceil(vmax / 100.0) * 100))
    return 0, ymax


def plot_one(ax, nNodes, cwnd_t, cwnd_v, ss_t, ss_v):
    """
    একটি subplot এ Figure 4 style plot আঁকে।
    """
    # ssthresh — dashed line আগে plot করা (background)
    if ss_t:
        ax.step(ss_t, ss_v, where='post',
                label="Slow start threshold",
                **SS_STYLE)

    # cwnd — solid line
    if cwnd_t:
        ax.step(cwnd_t, cwnd_v, where='post',
                label="Congestion window",
                **CWND_STYLE)

    # Axes
    ax.set_xlim(0, SIM_TIME)
    _, ymax = get_ylim(cwnd_v, ss_v)
    ax.set_ylim(0, ymax)

    # Ticks — paper style (0, 20, 40 on X; 0, half, max on Y)
    ax.set_xticks([0, 10, 20, 30, 40, 50])
    ytick_step = max(100, (ymax // 4 // 100) * 100)
    ax.set_yticks(range(0, ymax + 1, ytick_step))

    ax.set_xlabel("Time [s]", fontsize=9)
    ax.set_ylabel("Window size [packet]", fontsize=9)
    ax.grid(True, linestyle=':', alpha=0.45)
    ax.legend(loc='upper right', fontsize=8)

    # Parameter annotation box — paper Figure 4 style
    textstr = (
        f"Nodes = {nNodes}\n"
        "Bandwidth = 10 [Mbps]\n"
        "802.11g WiFi (Static)\n"
        "packet size = 1472 [byte]"
    )
    ax.text(
        0.02, 0.04, textstr,
        transform=ax.transAxes,
        fontsize=7.5,
        verticalalignment='bottom',
        bbox=dict(facecolor='white', edgecolor='black',
                  boxstyle='square,pad=0.35')
    )

    ax.set_title(
        f"BR-TCP: cwnd & ssthresh  (N={nNodes} nodes)",
        fontsize=9, pad=4
    )


# =====================================================================
# Main plotting
# =====================================================================
print("=" * 55)
print(" WiFi Static BR-TCP — Figure 4 Style Plots")
print("=" * 55)

# -------------------------------------------------
# Load all 10 trace files
# -------------------------------------------------
all_data = {}
for n in NODE_CONFIGS:
    cwnd_t, cwnd_v = read_trace(f"wifi_n{n}_cwnd.tr")
    ss_t,   ss_v   = read_trace(f"wifi_n{n}_ssthresh.tr")
    all_data[n] = (cwnd_t, cwnd_v, ss_t, ss_v)
    print(f"  n={n:3d}: cwnd={len(cwnd_t):5d} pts,  ssthresh={len(ss_t):5d} pts")

# -------------------------------------------------
# Individual plots — one per node count
# -------------------------------------------------
for n in NODE_CONFIGS:
    cwnd_t, cwnd_v, ss_t, ss_v = all_data[n]

    fig, ax = plt.subplots(figsize=(9, 5))
    plot_one(ax, n, cwnd_t, cwnd_v, ss_t, ss_v)

    fig.suptitle(
        f"Figure 4 Style: BR-TCP cwnd & ssthresh — WiFi 802.11g Static\n"
        f"({n} nodes, 10 flows, 100 pkt/s, 1×Tx coverage)",
        fontsize=10
    )
    plt.tight_layout()

    fname = f"wifi_n{n}_figure4.png"
    plt.savefig(fname, dpi=150, bbox_inches='tight')
    print(f"  Saved: {fname}")
    plt.close()

fig = plt.figure(figsize=(18, 12))
fig.suptitle(
    "BR-TCP: Congestion Window & Slow Start Threshold — WiFi 802.11g Static\n"
    "(Figure 4 Style — 5 node configurations, 10 flows, 100 pkt/s)",
    fontsize=12, y=0.99
)

gs = gridspec.GridSpec(2, 3, figure=fig,
                        hspace=0.45, wspace=0.35)

positions = [
    (0, 0), (0, 1), (0, 2),
    (1, 0), (1, 1),
]

for idx, n in enumerate(NODE_CONFIGS):
    row, col = positions[idx]
    ax = fig.add_subplot(gs[row, col])
    cwnd_t, cwnd_v, ss_t, ss_v = all_data[n]
    plot_one(ax, n, cwnd_t, cwnd_v, ss_t, ss_v)

# 6번째 cell — summary table
ax_tbl = fig.add_subplot(gs[1, 2])
ax_tbl.axis('off')

# Summary statistics table
table_data = [["Nodes", "cwnd avg\n[pkts]", "ssthresh avg\n[pkts]",
               "cwnd max\n[pkts]"]]
for n in NODE_CONFIGS:
    cwnd_t, cwnd_v, ss_t, ss_v = all_data[n]
    cavg = f"{np.mean(cwnd_v):.1f}" if cwnd_v else "N/A"
    savg = f"{np.mean(ss_v):.1f}"   if ss_v   else "N/A"
    cmax = f"{max(cwnd_v):.1f}"     if cwnd_v else "N/A"
    table_data.append([str(n), cavg, savg, cmax])

tbl = ax_tbl.table(
    cellText=table_data[1:],
    colLabels=table_data[0],
    loc='center',
    cellLoc='center'
)
tbl.auto_set_font_size(False)
tbl.set_fontsize(9)
tbl.scale(1.1, 1.6)

# Header row style
for j in range(len(table_data[0])):
    tbl[0, j].set_facecolor('#1a1a2e')
    tbl[0, j].set_text_props(color='white', fontweight='bold')

# Alternating row colours
for i in range(1, len(NODE_CONFIGS) + 1):
    for j in range(len(table_data[0])):
        tbl[i, j].set_facecolor('#f4f4f4' if i % 2 == 0 else 'white')

ax_tbl.set_title("Summary Statistics", fontsize=9, pad=8)

combined_fname = "wifi_all_figure4.png"
plt.savefig(combined_fname, dpi=150, bbox_inches='tight')
print(f"\n  Saved: {combined_fname}")
plt.close()

# -------------------------------------------------
# Console summary
# -------------------------------------------------
print("\n" + "=" * 55)
print(" Summary (avg values over simulation)")
print("=" * 55)
print(f"{'Nodes':>6} {'cwnd_avg':>12} {'ss_avg':>12} {'cwnd_max':>12}")
print("-" * 55)
for n in NODE_CONFIGS:
    cwnd_t, cwnd_v, ss_t, ss_v = all_data[n]
    cavg = np.mean(cwnd_v) if cwnd_v else 0
    savg = np.mean(ss_v)   if ss_v   else 0
    cmax = max(cwnd_v)     if cwnd_v else 0
    print(f"{n:>6} {cavg:>11.2f}p {savg:>11.2f}p {cmax:>11.2f}p")

print("\nOutput files:")
for n in NODE_CONFIGS:
    print(f"  wifi_n{n}_figure4.png")
print(f"  wifi_all_figure4.png  (combined)")
print("=" * 55)