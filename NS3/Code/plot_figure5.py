"""
plot_figure7.py
Reproduces Figure 7 from the paper:
  "RTT of BR-TCP"
  Reserved bandwidth = 40 Mbps, buffer size = 100 packets,
  delay = 25 ms, packet size = 1500 byte

This script reads:
  rtt_fig5.tr  -- RTT trace from the BR-TCP simulation (sixth_fig5.cc)

The RTT is logged in seconds by ns-3; we convert to milliseconds for the plot.

Expected behaviour:
  - Base RTT  = 2 * 25ms propagation = 50 ms
  - Queueing delay pushes RTT up toward ~100 ms when the 100-packet
    buffer fills (100 * 1500 * 8 / 40e6 ≈ 30 ms extra one-way)
  - After each congestion event RTT drops back toward the base,
    producing the oscillating pattern seen in Figure 7.
"""

import matplotlib.pyplot as plt


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


rtt_t, rtt_s = read_trace("figure5_rtt.tr")
rtt_ms = [v * 1000.0 for v in rtt_s]   # seconds -> milliseconds


fig, ax = plt.subplots(figsize=(8, 5))

ax.plot(rtt_t, rtt_ms, color='black', linewidth=0.9, label="BR-TCP RTT")


ax.set_xlabel("Time [s]", fontsize=11)
ax.set_ylabel("Round Trip Time, RTT [ms]", fontsize=11)

ax.set_xlim(0, 40)
ax.set_ylim(0, 100)

# Paper shows ticks at 0, 20, 40 on X and 0, 50, 100 on Y
ax.set_xticks([0, 20, 40])
ax.set_yticks([0, 50, 100])

ax.grid(True, linestyle=':', alpha=0.4)

textstr = (
    "Bandwidth = 40 [Mbps]\n"
    "packet size = 1500 [byte]\n"
    "delay = 25 [ms]"
)
ax.text(
    22, 3, textstr,
    fontsize=9,
    verticalalignment='bottom',
    bbox=dict(facecolor='white', edgecolor='black', boxstyle='square,pad=0.4')
)


ax.axhline(y=50, color='gray', linestyle=':', linewidth=0.8, alpha=0.7)
ax.text(0.5, 51, "Base RTT = 50 ms  (2 x 25 ms propagation)",
        fontsize=8, color='gray', va='bottom')

fig.suptitle(
    "Figure 7: RTT of BR-TCP\n"
    "(Reserved BW = 40 Mbps, buffer = 100 pkts, delay = 25 ms)",
    fontsize=10
)

plt.tight_layout()
plt.savefig("figure7.png", dpi=150, bbox_inches='tight')
print("Saved figure7.png")

if rtt_ms:
    print(f"Min RTT  : {min(rtt_ms):.2f} ms")
    print(f"Max RTT  : {max(rtt_ms):.2f} ms")
    print(f"Mean RTT : {sum(rtt_ms)/len(rtt_ms):.2f} ms")
    print(f"  (Paper reports RTT ~58 ms; base propagation = 50 ms)")

plt.show()