import numpy as np
import matplotlib.pyplot as plt
from scipy.interpolate import make_interp_spline

# =========================================================
# Load data
# =========================================================
def load_data(filename):
    try:
        data = np.loadtxt(filename)
    except Exception as e:
        print(f"Error loading {filename}: {e}")
        return np.array([]), np.array([])

    # Handle empty file
    if data.size == 0:
        print(f"{filename} is empty!")
        return np.array([]), np.array([])

    # Handle single line case
    if data.ndim == 1:
        if len(data) < 2:
            print(f"{filename} has insufficient data!")
            return np.array([]), np.array([])
        return np.array([data[0]]), np.array([data[1]])

    # Normal case
    return data[:, 0], data[:, 1]

# =========================================================
# Smooth curve using spline
# =========================================================
def smooth_curve(x, y, points=500):
    # Remove duplicate x values (important!)
    x_unique, idx = np.unique(x, return_index=True)
    y_unique = y[idx]

    if len(x_unique) < 4:
        return x, y  # not enough points

    x_new = np.linspace(x_unique.min(), x_unique.max(), points)

    spline = make_interp_spline(x_unique, y_unique, k=3)
    y_smooth = spline(x_new)

    return x_new, y_smooth


# =========================================================
# Plot function
# =========================================================
def plot_metric(file, title, ylabel, output):
    x, y = load_data(file)

    # smoothing
    xs, ys = smooth_curve(x, y)

    plt.figure(figsize=(8, 5))

    # Raw (light)
    plt.plot(x, y, alpha=0.3, linewidth=1, label="Raw")

    # Smooth (main)
    plt.plot(xs, ys, linewidth=2.5, label="Smoothed")

    plt.title(title, fontsize=14)
    plt.xlabel("Time (s)", fontsize=12)
    plt.ylabel(ylabel, fontsize=12)

    plt.grid(True, linestyle="--", alpha=0.5)
    plt.legend()

    plt.tight_layout()
    plt.savefig(output)
    plt.show()


# =========================================================
# MAIN
# =========================================================
if __name__ == "__main__":

    # CWND (packets)
    plot_metric(
        "figure_improved_cwnd.tr",
        "Congestion Window (cwnd)",
        "cwnd (packets)",
        "figure_improved_cwnd.png"
    )

    # SSTHRESH (packets)
    plot_metric(
        "figure_improved_ssthresh.tr",
        "Slow Start Threshold (ssthresh)",
        "ssthresh (packets)",
        "figure_improved_ssthresh.png"
    )

    # RTT (seconds)
    plot_metric(
        "figure_improved_rtt.tr",
        "RTT over Time",
        "RTT (s)",
        "figure_improved_rtt.png"
    )