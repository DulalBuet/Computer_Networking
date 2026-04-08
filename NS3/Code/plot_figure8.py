
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

nr_t, nr_v = read_trace("figure8_throughput_newreno.tr")
br_t, br_v = read_trace("figure8_throughput_brtcp.tr")


fig, ax = plt.subplots(figsize=(8, 5))

# New Reno: solid black line
ax.step(nr_t, nr_v, where='post',
        label="New Reno",
        color='black', linestyle='-', linewidth=0.9)

# BR-TCP: dashed black line (sits higher and is more stable)
ax.step(br_t, br_v, where='post',
        label="BR-TCP",
        color='black', linestyle='--', linewidth=1.2)

ax.axhline(y=40, color='gray', linestyle=':', linewidth=0.8, alpha=0.6)
ax.text(0.5, 40.5, "Link capacity = 40 Mbps",
        fontsize=8, color='gray', va='bottom')

ax.set_xlabel("Time [s]", fontsize=11)
ax.set_ylabel("Throughput [Mbps]", fontsize=11)

ax.set_xlim(0, 40)
ax.set_ylim(0, 40)

# Paper ticks: 0, 20, 40 on X  and  0, 20, 40 on Y
ax.set_xticks([0, 20, 40])
ax.set_yticks([0, 20, 40])

ax.grid(True, linestyle=':', alpha=0.4)

textstr = (
    "Bandwidth = 40 [Mbps]\n"
    "delay = 25 [ms]\n"
    "packet size = 1500 [byte]"
)
ax.text(
    22, 1, textstr,
    fontsize=9,
    verticalalignment='bottom',
    bbox=dict(facecolor='white', edgecolor='black', boxstyle='square,pad=0.4')
)

def value_at_time(times, values, target_t):
    """Return the last sampled value at or before target_t."""
    v = values[0] if values else 0
    for t, val in zip(times, values):
        if t <= target_t:
            v = val
        else:
            break
    return v

nr_y = value_at_time(nr_t, nr_v, 30)
br_y = value_at_time(br_t, br_v, 30)

ax.annotate("New Reno",
            xy=(30, nr_y),
            xytext=(31, max(nr_y - 6, 2)),
            fontsize=9,
            arrowprops=dict(arrowstyle='->', color='black', lw=0.7))

ax.annotate("BR-TCP",
            xy=(30, br_y),
            xytext=(31, min(br_y + 4, 38)),
            fontsize=9,
            arrowprops=dict(arrowstyle='->', color='black', lw=0.7))

ax.legend(loc='upper left', fontsize=9)

fig.suptitle(
    "Figure 8: Throughput of BR-TCP and TCP New Reno\n"
    "(Reserved BW = 40 Mbps, buffer = 100 pkts, delay = 25 ms, RTT = 58 ms)",
    fontsize=10
)

plt.tight_layout()
plt.savefig("figure8.png", dpi=150, bbox_inches='tight')
print("Saved figure8.png")

def stats(label, values):
    if not values:
        return
    avg = sum(values) / len(values)
    print(f"{label:10s}  avg={avg:.2f} Mbps  "
          f"max={max(values):.2f} Mbps  min={min(values):.2f} Mbps")
    print(f"  (Paper: New Reno avg ~20 Mbps, BR-TCP avg ~27 Mbps)")

stats("New Reno", nr_v)
stats("BR-TCP",   br_v)

plt.show()