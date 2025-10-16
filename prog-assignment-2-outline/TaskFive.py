import pandas as pd
import matplotlib.pyplot as plt

# === Load and clean data ===
df = pd.read_csv("timeline.csv")

# Convert timestamp to milliseconds relative to start
df["t_ms"] = (df["ts_ns"] - df["ts_ns"].min()) / 1e6

# Pick a few interesting PIDs automatically (or choose manually)
top_pids = df["pid"].value_counts().head(5).index.tolist()
df = df[df["pid"].isin(top_pids)]

# === Prepare Gantt chart ===
fig, ax = plt.subplots(figsize=(10, 6))

colors = {"SWITCH": "tab:blue", "WAKE": "tab:orange", "EXEC": "tab:green", "EXIT": "tab:red"}

for i, pid in enumerate(top_pids):
    pid_data = df[df["pid"] == pid]
    last_ts = None
    for _, row in pid_data.iterrows():
        if row["event"] == "SWITCH":
            # Draw a small run segment representing the task running
            run_duration = row.get("run_prev_ns", 0) / 1e6
            ax.broken_barh([(row["t_ms"], max(run_duration, 0.5))], (i - 0.3, 0.6),
                           facecolors=colors.get("SWITCH", "gray"))
        elif row["event"] == "WAKE":
            ax.scatter(row["t_ms"], i, color=colors.get("WAKE", "orange"), marker="^", s=50)
        elif row["event"] == "EXEC":
            ax.scatter(row["t_ms"], i, color=colors.get("EXEC", "green"), marker="o", s=60)
        elif row["event"] == "EXIT":
            ax.scatter(row["t_ms"], i, color=colors.get("EXIT", "red"), marker="x", s=60)

# === Style ===
ax.set_yticks(range(len(top_pids)))
ax.set_yticklabels([f"PID {pid}" for pid in top_pids])
ax.set_xlabel("Time (ms since start)")
ax.set_ylabel("Tasks (PIDs)")
ax.set_title("Per-Task Timeline (Gantt-style)")
ax.grid(True, axis="x", linestyle="--", alpha=0.5)

# Legend
legend_labels = [plt.Line2D([0], [0], color=v, lw=4, label=k) for k, v in colors.items()]
ax.legend(handles=legend_labels, loc="upper right")

plt.tight_layout()
plt.savefig("timeline_gantt.png")
plt.show()
