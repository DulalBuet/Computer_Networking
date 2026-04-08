
import matplotlib.pyplot as plt
import numpy as np
from collections import defaultdict

def read_trace(filename):
    data = defaultdict(dict)

    with open(filename, "r") as f:
        for line in f:
            line = line.strip()

            # Skip empty lines
            if not line:
                continue

            # Skip header line
            if line.startswith("rtt_ms"):
                continue

            parts = line.split()

            # Skip malformed lines
            if len(parts) < 5:
                continue

            try:
                rtt = float(parts[0])
                bw  = float(parts[1])
                imp = float(parts[4])
            except ValueError:
                # Skip any non-numeric lines safely
                continue

            data[bw][rtt] = imp

    return data


data = read_trace("figure14_improvement_vs_rtt.tr")
bw_list = sorted(data.keys())

styles = {
    10.0: dict(marker='v'),
    20.0: dict(marker='^'),
    30.0: dict(marker='o'),
    40.0: dict(marker='s'),
}

fig, ax = plt.subplots(figsize=(7, 5))

for bw in bw_list:
    rtt_vals = sorted(data[bw].keys())
    imp_vals = [data[bw][r] for r in rtt_vals]

    st = styles.get(bw, dict(marker='D'))

    ax.plot(
        rtt_vals,
        imp_vals,
        color='black',
        linewidth=1.4,
        linestyle='-',
        marker=st['marker'],
        markersize=7,
        markerfacecolor='white',
        markeredgecolor='black',
        markeredgewidth=1.2
    )

ax.set_xlabel("Round trip time, RTT [ms]", fontsize=11)
ax.set_ylabel("Improvement rate [%]", fontsize=11)

ax.set_xlim(0, 60)
ax.set_ylim(0, 50)

ax.set_xticks([20, 40, 60])
ax.set_yticks([0, 10, 20, 30, 40, 50])

# Light dotted grid like paper
ax.grid(True, linestyle=':', linewidth=0.8, alpha=0.6)

# Thicker frame (paper style)
for spine in ax.spines.values():
    spine.set_linewidth(1.2)

label_offsets = {
    10.0: (2, -2),
    20.0: (2, -4),
    30.0: (-18, 2),
    40.0: (-15, 4),
}

for bw in bw_list:
    rtt_vals = sorted(data[bw].keys())
    x = rtt_vals[-1]
    y = data[bw][x]

    dx, dy = label_offsets.get(bw, (2, 0))

    ax.text(
        x + dx,
        y + dy,
        f"{int(bw)} Mbps",
        fontsize=9
    )

ax.text(
    5, 45,
    "packet size = 1500 [byte]",
    fontsize=9,
    bbox=dict(
        facecolor='white',
        edgecolor='black',
        boxstyle='square,pad=0.4'
    )
)

plt.tight_layout()
plt.savefig("figure14.png", dpi=300)
print("Saved figure14.png")

plt.show()