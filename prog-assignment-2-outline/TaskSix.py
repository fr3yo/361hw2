#!/usr/bin/env python3
# Task 6: Short vs Long Tasks
# Usage: python3 TaskSix.py
# Expects: shortlong.csv (or sl.csv) with columns: pid,lifetime_ms,wakes,switches
# Outputs:
#   - shortlong_summary.csv
#   - shortlong_comparison.png

import pandas as pd
import matplotlib
matplotlib.use("Agg")  # headless mode (for servers)
import matplotlib.pyplot as plt

# === Load CSV ===
try:
    df = pd.read_csv("shortlong.csv")
except FileNotFoundError:
    df = pd.read_csv("sl.csv")

# === Split into groups ===
short = df[df["lifetime_ms"] < 200]
long  = df[df["lifetime_ms"] >= 200]

summary = pd.DataFrame({
    "group": ["short (<200ms)", "long (>=200ms)"],
    "count": [len(short), len(long)],
    "avg_wakes": [short["wakes"].mean(), long["wakes"].mean()],
    "avg_switches": [short["switches"].mean(), long["switches"].mean()],
    "avg_lifetime_ms": [short["lifetime_ms"].mean(), long["lifetime_ms"].mean()]
})

summary.to_csv("shortlong_summary.csv", index=False)

# === Plot comparison ===
plt.figure(figsize=(8, 5))

bar_width = 0.35
x = range(2)

plt.bar([i - bar_width/2 for i in x], summary["avg_wakes"], bar_width, label="Avg Wakes")
plt.bar([i + bar_width/2 for i in x], summary["avg_switches"], bar_width, label="Avg Switches")

plt.xticks(x, summary["group"], rotation=15, ha="right")
plt.ylabel("Average Count")
plt.title("Task 6: Short vs Long Tasks (Avg Wakes/Switches)")
plt.legend()
plt.tight_layout()
plt.savefig("shortlong_comparison.png", dpi=150)
plt.close()

print("✅ Wrote shortlong_summary.csv and shortlong_comparison.png")
