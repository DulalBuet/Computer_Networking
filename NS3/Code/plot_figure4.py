import matplotlib.pyplot as plt

def read_trace(filename):
    times, values = [], []
    with open(filename, 'r') as f:
        for line in f:
            t, v = line.strip().split('\t')
            times.append(float(t))
            values.append(float(v))
    return times, values

c_t, c_v = read_trace("figure4_cwnd.tr")
s_t, s_v = read_trace("figure4_ssthresh.tr")

plt.figure(figsize=(10, 6))


plt.step(c_t, c_v, where='post', label="Congestion window", color='black', linewidth=1)
plt.step(s_t, s_v, where='post', label="Slow start threshold", color='black', linestyle='--', linewidth=1.5)

# Decoration
plt.xlabel("Time [s]")
plt.ylabel("Window size [packet]")
plt.title("Reproduction of Figure 4: TCP New Reno")
plt.xlim(0, 50)
plt.ylim(0, 400)
plt.grid(True, linestyle=':', alpha=0.6)
plt.legend(loc='upper right')


textstr = "Bandwidth = 40 [Mbps]\ndelay = 25 [ms]\npacket size = 1500 [byte]"
plt.text(5, 300, textstr, bbox=dict(facecolor='white', edgecolor='black'))

plt.savefig("figure4.png")
plt.show()