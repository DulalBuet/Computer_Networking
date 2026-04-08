
import matplotlib.pyplot as plt
import matplotlib.ticker as ticker
from datetime import datetime, timedelta

def read_trace(filename):
    sim_times, byte_vals, elapsed_s = [], [], []
    with open(filename, 'r') as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            parts = line.split('\t')
            if len(parts) >= 2:
                sim_times.append(float(parts[0]))
                byte_vals.append(float(parts[1]))
                elapsed_s.append(float(parts[2]) if len(parts) >= 3
                                  else float(parts[0]) - 1.0)
    return sim_times, byte_vals, elapsed_s


sim_t, byte_v, elap_s = read_trace("figure11_throughput_newreno.tr")

# Use only data within the 40-second sender window (sim time 2–41s)
filtered = [(s, b, e) for s, b, e in zip(sim_t, byte_v, elap_s)
            if 2.0 <= s <= 41.0]
if filtered:
    sim_t, byte_v, elap_s = zip(*filtered)
    sim_t   = list(sim_t)
    byte_v  = list(byte_v)
    elap_s  = list(elap_s)

BASE_CLOCK = datetime(2024, 1, 1, 16, 30, 37)

clock_times  = elap_s                        # numeric positions (seconds)
clock_labels = [
    (BASE_CLOCK + timedelta(seconds=e)).strftime("%H:%M:%S")
    for e in elap_s
]

total_elapsed = clock_times[-1] if clock_times else 40.0

# Three X-tick positions: start, middle, end  (paper shows 3 timestamps)
tick_positions = [0.0, total_elapsed / 2.0, total_elapsed]

def nearest_label(pos, times, labels):
    """Find the clock label closest to a given elapsed-time position."""
    if not times:
        return ""
    idx = min(range(len(times)), key=lambda i: abs(times[i] - pos))
    return labels[idx]

tick_labels = [nearest_label(p, clock_times, clock_labels)
               for p in tick_positions]

Y_MAX   = 5_000_000        # 5M bytes/s = 40 Mbps
Y_TICKS = [0, 1e6, 2e6, 3e6, 4e6, 5e6]

fig, ax = plt.subplots(figsize=(10, 4.5))

# Single solid black line
ax.step(clock_times, byte_v, where='post',
        color='black', linewidth=0.9)

ax.set_xlabel("Time", fontsize=11)
ax.set_ylabel("Throughput [byte]", fontsize=11)

ax.set_xlim(clock_times[0] if clock_times else 0,
            clock_times[-1] if clock_times else 40)
ax.set_ylim(0, Y_MAX)

ax.set_yticks(Y_TICKS)
ax.yaxis.set_major_formatter(
    ticker.FuncFormatter(
        lambda x, _: f"{int(x / 1e6)}M" if x >= 1e6 else "0"
    )
)

# X ticks: only 3 clock labels (start, mid, end)
ax.set_xticks(tick_positions)
ax.set_xticklabels(tick_labels, fontsize=9)

ax.grid(True, linestyle=':', alpha=0.35)

fig.text(0.13, 0.02, "East Gateway", ha='center', fontsize=9)
fig.text(0.90, 0.02, "West Gateway", ha='center', fontsize=9)

fig.suptitle(
    "Figure 11: Throughput of TCP New Reno (Testbed equivalent)\n"
    "(Reserved BW = 40 Mbps, delay = 25 ms, RTT = 58 ms, "
    "packet size = 1500 byte)",
    fontsize=10
)

plt.tight_layout()
plt.subplots_adjust(bottom=0.15)
plt.savefig("figure11.png", dpi=150, bbox_inches='tight')
print("Saved figure11.png")

if byte_v:
    avg_b   = sum(byte_v) / len(byte_v)
    peak_b  = max(byte_v)
    # Sanity check in Mbps
    avg_mbps  = avg_b  * 8 / 1e6
    peak_mbps = peak_b * 8 / 1e6
    print(f"\nSample window     : 1.0 s")
    print(f"Data points       : {len(byte_v)}")
    print(f"Avg  throughput   : {avg_b/1e6:.3f} M bytes/s  ({avg_mbps:.2f} Mbps)")
    print(f"Peak throughput   : {peak_b/1e6:.3f} M bytes/s  ({peak_mbps:.2f} Mbps)")
    print(f"Link capacity     : {40e6/8/1e6:.1f} M bytes/s  (40 Mbps)")
    print(f"\nExpected (paper)  : peak ~4-5M bytes/s, avg ~2-3M bytes/s")
    print(f"  New Reno sawtooth: cwnd halved on each loss → periodic dips to ~0")

plt.show()