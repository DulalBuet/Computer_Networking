
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


nr_t, nr_v = read_trace("figure10_cwnd_newreno.tr")
br_t, br_v = read_trace("figure10_cwnd_brtcp.tr")


fig, ax = plt.subplots(figsize=(8, 5))

# New Reno: solid black line (lower, tighter sawtooth)
ax.step(nr_t, nr_v, where='post',
        label="New Reno",
        color='black', linestyle='-', linewidth=0.8)

# BR-TCP: dashed black line (higher swings, climbs to ~200 pkt)
ax.step(br_t, br_v, where='post',
        label="BR-TCP",
        color='black', linestyle='--', linewidth=1.1)

ax.set_xlabel("Time [s]", fontsize=11)
ax.set_ylabel("Congestion window size [packet]", fontsize=11)

ax.set_xlim(0, 40)
ax.set_ylim(0, 200)          # Figure 10 Y-axis cap is 200 (not 300 like Fig 6)

# Paper ticks: 0, 20, 40 on X  and  0, 100, 200 on Y
ax.set_xticks([0, 20, 40, 60])
ax.set_yticks([0, 100, 200, 300])

ax.grid(True, linestyle=':', alpha=0.4)

textstr = (
    "Bandwidth = 40 [Mbps]\n"
    "delay = 25 [ms]\n"
    "packet size = 1500 [byte]"
)
ax.text(
    22, 3, textstr,
    fontsize=9,
    verticalalignment='bottom',
    bbox=dict(facecolor='white', edgecolor='black', boxstyle='square,pad=0.4')
)


def value_at_time(times, values, target_t):
    """Return the last sampled cwnd value at or before target_t."""
    v = values[0] if values else 0
    for t, val in zip(times, values):
        if t <= target_t:
            v = val
        else:
            break
    return v


nr_y = value_at_time(nr_t, nr_v, 10)
br_y = value_at_time(br_t, br_v, 10)

ax.annotate("New Reno",
            xy=(10, nr_y),
            xytext=(12, max(nr_y - 15, 5)),
            fontsize=9,
            arrowprops=dict(arrowstyle='->', color='black', lw=0.7))

ax.annotate("BR-TCP",
            xy=(10, br_y),
            xytext=(12, min(br_y + 15, 190)),
            fontsize=9,
            arrowprops=dict(arrowstyle='->', color='black', lw=0.7))

ax.legend(loc='upper left', fontsize=9)

fig.suptitle(
    "Figure 10: Congestion window comparison — BR-TCP and TCP New Reno\n"
    "(Testbed equivalent: Reserved BW = 40 Mbps, delay = 25 ms, RTT = 58 ms)",
    fontsize=10
)

plt.tight_layout()
plt.savefig("figure10.png", dpi=150, bbox_inches='tight')
print("Saved figure10.png")


def stats(label, values):
    if not values:
        return
    avg = sum(values) / len(values)
    print(f"{label:10s}  avg={avg:.1f} pkts  "
          f"max={max(values):.1f} pkts  min={min(values):.1f} pkts")

print("\nCongestion window statistics:")
stats("New Reno", nr_v)
stats("BR-TCP",   br_v)
print("\nBR-TCP ssthresh = (40e6/12000)*0.058 = "
      f"{(40e6/12000)*0.058:.1f} packets (~193)")
print("BR-TCP cwnd should climb to ~200 pkts before each drop")
print("New Reno cwnd should stay lower (~50-100 pkts) with deeper cuts")

plt.show()