#!/usr/bin/env python3
# Task 5: Per-task timeline (Gantt)
# Produces: timeline_gantt.png

import pandas as pd
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

# === Load CSV ===
df = pd.read_csv("timeline.csv")

# Trim excessive rows for safety on huge datasets
if len(df) > 100000:
    print(f"Large dataset detected ({len(df)} rows) — sampling 100k rows for plotting.")
    df = df.sample(100000, random_state=42)

# Convert timestamps to ms relative to start
df["t_ms"] = (df["ts_ns"] - df["ts_ns"].min()) / 1e6

# Select top active PIDs
top_pids = df["pid"].value_counts().head(5).index.tolist()
df = df[df["pid"].isin(top_pids)]

# Define colors
colors = {"SWITCH": "tab:blue", "WAKE": "tab:orange", "EXEC": "tab:green", "EXIT": "tab:red"}

# === Build figure ===
plt.figure(figsize=(10, 6))
ax = plt.gca()

# Draw per-PID timelines
for i, pid in enumerate(top_pids):
    pid_data = df[df["pid"] == pid]
    for _, row in pid_data.iterrows():
        event = row["event"]
        if event == "SWITCH":
            dur = max(row.get("run_prev_ns", 0) / 1e6, 0.5)
            ax.broken_barh([(row["t_ms"], dur)], (i - 0.3, 0.6),
                           facecolors=colors.get(event, "gray"))
        elif event in colors:
            ax.scatter(row["t_ms"], i, color=colors[event], s=30)

# === Style ===
ax.set_yticks(range(len(top_pids)))
ax.set_yticklabels([f"PID {pid}" for pid in top_pids])
ax.set_xlabel("Time (ms since start)")
ax.set_ylabel("Tasks (PIDs)")
ax.set_title("Task 5: Per-Task Timeline (Gantt-style)")
ax.grid(True, axis="x", linestyle="--", alpha=0.5)

plt.tight_layout()
plt.savefig("timeline_gantt.png", dpi=150)
plt.close()

print("✅ Wrote: timeline_gantt.png")
