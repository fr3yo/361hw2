import matplotlib
matplotlib.use('Agg')  # use non-GUI backend

import pandas as pd
import matplotlib.pyplot as plt

# === Load and clean data ===
df = pd.read_csv("timeline.csv")

# Limit dataset size for faster rendering
if len(df) > 50000:
    print(f"Large dataset detected ({len(df)} rows) — sampling 50k rows.")
    df = df.sample(50000, random_state=42)

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
    for _, row in pid_data.iterrows():
        if row["event"] == "SWITCH":
            run_duration = row.get("run_prev_ns", 0) / 1e6
            ax.broken_barh([(row["t_ms"], max(run_duration, 0.5))],
                           (i - 0.3, 0.6),
                           facecolors=colors.get("SWITCH", "gray"))
        elif row["event"] in colors:
            ax.scatter(row["t_ms"], i, color=colors[row["event"]], s=40)

# === Style ===
ax.set_yticks(range(len(top_pids)))
ax.set_yticklabels([f"PID {pid}" for pid in top_pids])
ax.set_xlabel("Time (ms since start)")
ax.set_ylabel("Tasks (PIDs)")
ax.set_title("Per-Task Timeline (Gantt-style)")
ax.grid(True, axis="x", linestyle="--", alpha=0.5)

plt.tight_layout()

# === Save image ===
output_file = "timeline_gantt.png"
plt.savefig(output_file, dpi=300)
print(f"✅ Plot saved as '{output_file}'")
