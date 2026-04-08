
import matplotlib.pyplot as plt
import matplotlib.ticker as ticker
from datetime import datetime, timedelta

# ------------------------------------------------------------------
# Helper
# ------------------------------------------------------------------
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


sim_t, byte_v, elap_s = read_trace("figure12_throughput_brtcp.tr")

# Use only data within the 40-second sender window (sim time 2–41s)
filtered = [(s, b, e) for s, b, e in zip(sim_t, byte_v, elap_s)
            if 2.0 <= s <= 41.0]
if filtered:
    sim_t, byte_v, elap_s = zip(*filtered)
    sim_t  = list(sim_t)
    byte_v = list(byte_v)
    elap_s = list(elap_s)

BASE_CLOCK = datetime(2024, 1, 1, 16, 29, 11)

clock_times  = elap_s
clock_labels = [
    (BASE_CLOCK + timedelta(seconds=e)).strftime("%H:%M:%S")
    for e in elap_s
]

total_elapsed = clock_times[-1] if clock_times else 40.0

# Three X-tick positions: start, middle, end
tick_positions = [0.0, total_elapsed / 2.0, total_elapsed]

def nearest_label(pos, times, labels):
    if not times:
        return ""
    idx = min(range(len(times)), key=lambda i: abs(times[i] - pos))
    return labels[idx]

tick_labels = [nearest_label(p, clock_times, clock_labels)
               for p in tick_positions]

Y_MAX   = 10_000_000     # 10M bytes  (paper's Y-axis ceiling)
Y_TICKS = [0, 2e6, 4e6, 6e6, 8e6, 10e6]

fig, ax = plt.subplots(figsize=(10, 4.5))

# Single solid black line — BR-TCP throughput
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

ax.axhline(y=5_000_000, color='gray', linestyle=':', linewidth=0.8, alpha=0.6)
ax.text(1.0, 5_100_000, "Link capacity = 40 Mbps (5M bytes/s)",
        fontsize=8, color='gray', va='bottom')

fig.text(0.13, 0.02, "East Gateway", ha='center', fontsize=9)
fig.text(0.90, 0.02, "West Gateway", ha='center', fontsize=9)
fig.suptitle(
    "Figure 12: Throughput of BR-TCP (Testbed equivalent)\n"
    "(Reserved BW = 40 Mbps, delay = 25 ms, RTT = 58 ms, "
    "packet size = 1500 byte)",
    fontsize=10
)

plt.tight_layout()
plt.subplots_adjust(bottom=0.15)
plt.savefig("figure12.png", dpi=150, bbox_inches='tight')
print("Saved figure12.png")

if byte_v:
    avg_b   = sum(byte_v) / len(byte_v)
    peak_b  = max(byte_v)
    avg_mbps  = avg_b  * 8 / 1e6
    peak_mbps = peak_b * 8 / 1e6

    # Count deep drops (below 10% of link capacity = below 0.5M bytes/s)
    drops = sum(1 for v in byte_v if v < 500_000)

    print(f"\nSample window     : 1.0 s")
    print(f"Data points       : {len(byte_v)}")
    print(f"Avg  throughput   : {avg_b/1e6:.3f} M bytes/s  ({avg_mbps:.2f} Mbps)")
    print(f"Peak throughput   : {peak_b/1e6:.3f} M bytes/s  ({peak_mbps:.2f} Mbps)")
    print(f"Deep drops (<0.5M): {drops} events")
    print(f"Link capacity     : {40e6/8/1e6:.1f} M bytes/s  (40 Mbps)")
    print(f"\nExpected (paper Section 5.2):")
    print(f"  BR-TCP  avg ~3.4M bytes/s, max ~4.5M bytes/s")
    print(f"  New Reno avg ~2.5M bytes/s, max ~4.3M bytes/s")
    print(f"  BR-TCP has fewer deep drops → higher average throughput")

plt.show()