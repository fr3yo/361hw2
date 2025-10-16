#!/usr/bin/env python3
# Task 5: Per-task Gantt timeline (headless, non-interactive)
# Usage: python3 TaskFive.py
# Outputs: timeline_gantt.png

import pandas as pd
import matplotlib
matplotlib.use("Agg")  # non-interactive backend for servers
import matplotlib.pyplot as plt

# === Load and prep data ===
df = pd.read_csv("timeline.csv")

# Convert to ms relative to start
df["t_ms"] = (df["ts_ns"] - df["ts_ns"].min()) / 1e6

# Limit dataset size for performance
if len(df) > 50000:
    print(f"Large dataset detected ({len(df)} rows) — sampling 50k rows.")
    df = df.sample(50000, random_state=42)

# Pick top 5 active PIDs
top_pids = df["pid"].value_counts().head(5).index.tolist()
df = df[df["pid"].isin(top_pids)]

# === Plot ===
fig, ax = plt.subplots(figsize=(10, 6))
colors = {"SWITCH": "tab:blue", "WAKE": "tab:orange", "EXEC": "tab:green", "EXIT": "tab:red"}

# Group by PID once — no row-by-row iteration
for i, (pid, grp) in enumerate(df.groupby("pid")):
    # SWITCH → draw horizontal bar
    switch = grp[grp["event"] == "SWITCH"]
    if not switch.empty:
        ax.broken_barh(
            list(zip(switch["t_ms"], switch["run_prev_ns"].fillna(0) / 1e6 + 0.5)),
            (i - 0.3, 0.6),
            facecolors=colors["SWITCH"],
        )

    # WAKE, EXEC, EXIT → draw scatter markers
    for ev in ["WAKE", "EXEC", "EXIT"]:
        sub = grp[grp["event"] == ev]
        if not sub.empty:
            ax.scatter(sub["t_ms"], [i] * len(sub),
                       color=colors[ev], s=40, label=ev if i == 0 else "")

# === Labels & styling ===
ax.set_yticks(range(len(top_pids)))
ax.set_yticklabels([f"PID {pid}" for pid in top_pids])
ax.set_xlabel("Time (ms since start)")
ax.set_ylabel("Tasks (PIDs)")
ax.set_title("Task 5: Per-Task Timeline (Gantt-style)")
ax.grid(True, axis="x", linestyle="--", alpha=0.4)
plt.legend(loc="upper right")
plt.tight_layout()

# === Save ===
plt.savefig("timeline_gantt.png", dpi=150)
plt.close()
print("✅ Wrote timeline_gantt.png")
