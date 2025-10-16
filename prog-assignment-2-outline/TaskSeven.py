#!/usr/bin/env python3
# Task 7: Starvation Detector Analysis
# Usage: python3 TaskSeven.py
# Expects one or more CSVs from different thresholds:
#   starv_5.csv, starv_10.csv, starv_20.csv ... or a single starv.csv
# Outputs:
#   - starv_summary.csv
#   - starv_bar.png

import pandas as pd
import matplotlib
matplotlib.use("Agg")  # for servers
import matplotlib.pyplot as plt
from pathlib import Path

# === Helper to summarize one file ===
def summarize_starv(file, threshold):
    df = pd.read_csv(file)
    pid_counts = df["pid"].value_counts().reset_index()
    pid_counts.columns = ["pid", "alert_count"]
    pid_counts["threshold_ms"] = threshold
    return pid_counts

# === Detect files ===
files = list(Path(".").glob("starv*.csv"))
if not files:
    raise FileNotFoundError("No starv*.csv files found in current directory.")

all_data = []
for f in files:
    # Extract threshold from filename (e.g. starv_20.csv → 20)
    name = f.stem
    parts = name.split("_")
    threshold = None
    for p in parts:
        if p.isdigit():
            threshold = int(p)
            break
    threshold = threshold or 20  # fallback default
    all_data.append(summarize_starv(f, threshold))

combined = pd.concat(all_data, ignore_index=True)

# === Create summary table ===
summary = (combined.groupby(["threshold_ms", "pid"], as_index=False)
                      .agg(alert_count=("alert_count", "sum"))
                      .sort_values(["threshold_ms", "alert_count"], ascending=[True, False]))

summary.to_csv("starv_summary.csv", index=False)

print("✅ Wrote starv_summary.csv")

# === Optional: Bar plot for top offenders ===
top = summary.groupby("pid")["alert_count"].sum().sort_values(ascending=False).head(10)
plt.figure(figsize=(8, 5))
top.plot(kind="bar", color="tab:red")
plt.title("Task 7: Top Starved PIDs (Total Alerts)")
plt.xlabel("PID")
plt.ylabel("Alert Count")
plt.tight_layout()
plt.savefig("starv_bar.png", dpi=150)
plt.close()
print("✅ Wrote starv_bar.png")
