import pandas as pd
import matplotlib.pyplot as plt
import numpy as np
import os
import glob

# ─── Settings ─────────────────────────────────────────────
protocol  = "BR-TCP"
standard  = "IEEE 802.11g"
csv_pattern = "*.csv"
out_dir   = "figures"
os.makedirs(out_dir, exist_ok=True)

plt.rcParams.update({
    "font.size": 12,
    "axes.titlesize": 13,
    "axes.labelsize": 12,
    "lines.linewidth": 2,
    "lines.markersize": 8,
    "figure.dpi": 150,
    "grid.alpha": 0.4,
})

# ─── Load CSV ─────────────────────────────────────────────
def load_csv(csv_file):
    try:
        df = pd.read_csv(csv_file)
        if df['PDR'].max() <= 1.0:
            df['PDR'] = df['PDR'] * 100
        if df['DropRatio'].max() <= 1.0:
            df['DropRatio'] = df['DropRatio'] * 100
        return df
    except Exception as e:
        print(f"[!] Failed to read {csv_file}: {e}")
        return None

# ─── Parse Filename ───────────────────────────────────────
# Expected: node20_flow10_packet_200_1Tx.csv
def parse_filename(csv_file):
    base   = os.path.basename(csv_file).replace(".csv", "")
    tokens = base.split("_")
    try:
        nNodes    = int(tokens[0][4:])   # node20  → 20
        nFlows    = int(tokens[1][4:])   # flow10  → 10
        # tokens[2] = "packet" (skip)
        pktPerSec = int(tokens[3])       # 200     → 200
        covMult   = tokens[4]            # 1Tx     → "1Tx"
    except Exception:
        nNodes = nFlows = pktPerSec = covMult = "?"
    return base, nNodes, nFlows, pktPerSec, covMult

# ─── Summary Table Figure ─────────────────────────────────
def plot_summary_table(df, base, nNodes, nFlows, pktPerSec, covMult, out_dir):
    metrics = ["Throughput_Mbps", "Delay_ms", "PDR", "DropRatio"]
    labels  = ["Throughput (Mbps)", "Delay (ms)", "PDR (%)", "Drop Ratio (%)"]

    table_data = []
    for m, lbl in zip(metrics, labels):
        col = df[m]
        table_data.append([
            lbl,
            f"{col.min():.3f}",
            f"{col.max():.3f}",
            f"{col.mean():.3f}",
            f"{col.median():.3f}",
            f"{col.std():.3f}",
        ])

    col_headers = ["Metric", "Min", "Max", "Mean", "Median", "Std Dev"]

    fig, ax = plt.subplots(figsize=(11, 3))
    ax.axis("off")

    title = (f"{protocol} | {standard} Static  —  "
             f"{nNodes} Nodes | {nFlows} Flows | {pktPerSec} pkt/s | {covMult} Coverage")
    fig.suptitle(title, fontsize=12, fontweight="bold", y=1.02)

    tbl = ax.table(
        cellText=table_data,
        colLabels=col_headers,
        cellLoc="center",
        loc="center",
    )
    tbl.auto_set_font_size(False)
    tbl.set_fontsize(11)
    tbl.scale(1.2, 2.0)

    # Style header row
    for col_idx in range(len(col_headers)):
        tbl[0, col_idx].set_facecolor("#2c3e50")
        tbl[0, col_idx].set_text_props(color="white", fontweight="bold")

    # Alternating row colors
    for row_idx in range(1, len(table_data) + 1):
        bg = "#eaf4fb" if row_idx % 2 == 0 else "#ffffff"
        for col_idx in range(len(col_headers)):
            tbl[row_idx, col_idx].set_facecolor(bg)

    fig.tight_layout()
    out_path = os.path.join(out_dir, f"{base}_summary_table.png")
    fig.savefig(out_path, bbox_inches="tight")
    plt.close(fig)
    print(f"  [✓] Table  → {out_path}")

# ─── Metrics Bar Charts ───────────────────────────────────
def plot_metrics(df, base, nNodes, nFlows, pktPerSec, covMult, out_dir):
    flows = df["FlowID"].values
    x     = np.arange(len(flows))
    bar_kw = dict(width=0.6, edgecolor="black", linewidth=0.6)

    fig, axes = plt.subplots(2, 2, figsize=(16, 11))
    fig.suptitle(
        f"{protocol} | {standard} Static\n"
        f"{nNodes} Nodes | {nFlows} Flows | {pktPerSec} pkt/s | {covMult} Coverage",
        fontsize=14, fontweight="bold"
    )

    def bar_plot(ax, col, color, title, ylabel, ylim=None):
        ax.bar(x, df[col], color=color, **bar_kw)
        ax.axhline(df[col].mean(), color="red", linestyle="--", linewidth=1.5,
                   label=f"Mean = {df[col].mean():.2f}")
        ax.set_title(title)
        ax.set_xlabel("Flow ID"); ax.set_ylabel(ylabel)
        ax.set_xticks(x); ax.set_xticklabels(flows)
        if ylim: ax.set_ylim(*ylim)
        ax.legend(); ax.grid(axis="y")

    bar_plot(axes[0,0], "Throughput_Mbps", "steelblue",  "Network Throughput",         "Mbps")
    bar_plot(axes[0,1], "Delay_ms",        "darkorange", "End-to-End Delay",           "Delay (ms)")
    bar_plot(axes[1,0], "PDR",             "seagreen",   "Packet Delivery Ratio (PDR)","PDR (%)",        (0, 105))
    bar_plot(axes[1,1], "DropRatio",       "crimson",    "Packet Drop Ratio",          "Drop Ratio (%)", (0, 105))

    fig.tight_layout()
    out_path = os.path.join(out_dir, f"{base}_metrics.png")
    fig.savefig(out_path, bbox_inches="tight")
    plt.close(fig)
    print(f"  [✓] Charts → {out_path}")

# ─── Summary Bar Graph ────────────────────────────────────
def plot_summary_graph(df, base, nNodes, nFlows, pktPerSec, covMult, out_dir):
    metrics = ["Throughput_Mbps", "Delay_ms", "PDR", "DropRatio"]
    labels  = ["Throughput\n(Mbps)", "Delay\n(ms)", "PDR\n(%)", "Drop\nRatio (%)"]

    means = [df[m].mean() for m in metrics]
    mins  = [df[m].min()  for m in metrics]
    maxs  = [df[m].max()  for m in metrics]

    width = 0.25
    x = np.arange(len(metrics))

    fig, ax = plt.subplots(figsize=(9, 6))
    ax.bar(x - width, mins,  width, label="Min",  color="lightgray",  edgecolor="black", linewidth=0.6)
    ax.bar(x,         means, width, label="Mean", color="steelblue",  edgecolor="black", linewidth=0.6)
    ax.bar(x + width, maxs,  width, label="Max",  color="darkorange", edgecolor="black", linewidth=0.6)

    ax.set_xticks(x); ax.set_xticklabels(labels)
    ax.set_ylabel("Value")
    ax.set_title(
        f"Summary — {nNodes} Nodes | {nFlows} Flows | {pktPerSec} pkt/s | {covMult}"
    )
    ax.legend(); ax.grid(axis="y", linestyle="--", alpha=0.5)

    fig.tight_layout()
    out_path = os.path.join(out_dir, f"{base}_summary_graph.png")
    fig.savefig(out_path, bbox_inches="tight")
    plt.close(fig)
    print(f"  [✓] Graph  → {out_path}")

# ─── Main Loop ────────────────────────────────────────────
csv_files = glob.glob(csv_pattern)
if not csv_files:
    print("[!] No CSV files found.")
else:
    for csv_file in csv_files:
        print(f"\n→ Processing: {csv_file}")
        df = load_csv(csv_file)
        if df is None or df.empty:
            continue
        base, nNodes, nFlows, pktPerSec, covMult = parse_filename(csv_file)
        plot_metrics(df, base, nNodes, nFlows, pktPerSec, covMult, out_dir)
        plot_summary_graph(df, base, nNodes, nFlows, pktPerSec, covMult, out_dir)
        plot_summary_table(df, base, nNodes, nFlows, pktPerSec, covMult, out_dir)

print("\nAll figures saved in ./figures/")