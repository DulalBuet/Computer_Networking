"""
plot_figure6.py
Reproduces Figure 6 from the paper:
  "Congestion window versus elapsed time of BR-TCP and TCP New Reno"
  Reserved bandwidth = 40 Mbps, buffer size = 100 packets,
  delay = 25 ms, RTT = 58 ms, packet size = 1500 byte

This script reads:
  cwnd.tr        -- New Reno congestion window (from sixth.cc / Figure 4 run)
  cwnd_fig5.tr   -- BR-TCP congestion window   (from sixth_fig5.cc / Figure 5 run)

And overlays them on a single plot matching Figure 6 of the paper.
"""

import matplotlib.pyplot as plt
import matplotlib.ticker as ticker


def read_trace(filename):
    times, values = [], []
    with open(filename, 'r') as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            parts = line.split('\t')
            if len(parts) >= 2:
                times.append(float(parts[0]))
                values.append(float(parts[1]))
    return times, values



nr_t, nr_v = read_trace("cwnd.tr")

br_t, br_v = read_trace("cwnd_fig5.tr")


fig, ax = plt.subplots(figsize=(8, 5))

ax.step(nr_t, nr_v, where='post',
        label="New Reno",
        color='black', linestyle='-', linewidth=0.9)


ax.step(br_t, br_v, where='post',
        label="BR-TCP",
        color='black', linestyle='--', linewidth=1.2)


ax.set_xlabel("Time [s]", fontsize=11)
ax.set_ylabel("Window size [packet]", fontsize=11)

ax.set_xlim(0, 40)
ax.set_ylim(0, 300)


ax.set_xticks([0, 20, 40])
ax.set_yticks([0, 100, 200, 300])

ax.grid(True, linestyle=':', alpha=0.4)

textstr = (
    "Bandwidth = 40 [Mbps]\n"
    "delay = 25 [ms]\n"
    "packet size = 1500 [byte]"
)
ax.text(
    22, 10, textstr,
    fontsize=9,
    verticalalignment='bottom',
    bbox=dict(facecolor='white', edgecolor='black', boxstyle='square,pad=0.4')
)

def value_at_time(times, values, target_t):
    """Return the last value at or before target_t."""
    v = values[0]
    for t, val in zip(times, values):
        if t <= target_t:
            v = val
        else:
            break
    return v

nr_y_at25 = value_at_time(nr_t, nr_v, 25)
br_y_at25 = value_at_time(br_t, br_v, 25)

ax.annotate("New Reno",
            xy=(25, nr_y_at25),
            xytext=(26, nr_y_at25 + 20),
            fontsize=9,
            arrowprops=dict(arrowstyle='->', color='black', lw=0.7))

ax.annotate("BR-TCP",
            xy=(25, br_y_at25),
            xytext=(26, br_y_at25 + 20),
            fontsize=9,
            arrowprops=dict(arrowstyle='->', color='black', lw=0.7))

ax.legend(loc='upper left', fontsize=9)

fig.suptitle(
    "Figure 6: Congestion window vs elapsed time — BR-TCP and TCP New Reno\n"
    "(Reserved BW = 40 Mbps, buffer = 100 pkts, delay = 25 ms, RTT = 58 ms)",
    fontsize=10
)

plt.tight_layout()
plt.savefig("figure6_output.png", dpi=150, bbox_inches='tight')
print("Saved figure6_output.png")
plt.show()