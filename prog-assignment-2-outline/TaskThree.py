#!/usr/bin/env python3
# Task 3 fairness: read fairness.csv and produce plots/tables
# Usage: python3 task3_fairness.py fairness.csv
# Outputs:
#  - fairness_top10_run_ms.png
#  - fairness_top10_wait_ms.png
#  - fairness_top10_switches.png
#  - fairness_top10_cpu_share.png
#  - fairness_top10_table.csv   (pid, run_ms, wait_ms, switches, cpu_share)

import sys
import pandas as pd
import matplotlib.pyplot as plt
from pathlib import Path

def main():
    if len(sys.argv) < 2:
        print("Usage: python3 task3_fairness.py fairness.csv")
        sys.exit(1)

    in_csv = sys.argv[1]
    df = pd.read_csv(in_csv)  # expects columns: pid,run_ms,wait_ms,switches

    # The CSV contains running totals over time; keep the final totals per PID
    agg = (df.groupby("pid", as_index=False)
             .agg(run_ms=("run_ms", "max"),
                  wait_ms=("wait_ms", "max"),
                  switches=("switches", "max")))

    # CPU share proxy: run_ms / (run_ms + wait_ms), guard div-by-zero
    total_ms = (agg["run_ms"] + agg["wait_ms"]).replace(0, pd.NA)
    agg["cpu_share"] = (agg["run_ms"] / total_ms).fillna(0.0)

    # Pick "top 10 active" by total time observed (run + wait)
    agg["active_ms"] = agg["run_ms"] + agg["wait_ms"]
    top10 = agg.sort_values(["active_ms", "switches"], ascending=[False, False]).head(10)

    # Save a compact table for the report
    top10[["pid", "run_ms", "wait_ms", "switches", "cpu_share"]].to_csv(
        "fairness_top10_table.csv", index=False
    )

    # Helper to make a simple bar chart (one chart per figure)
    def bar_chart(series, title, ylabel, outfile):
        plt.figure()
        # Cast PID to string for nicer x labels
        xlabels = top10["pid"].astype(str)
        plt.bar(xlabels, series)
        plt.xlabel("PID (top 10 by activity)")
        plt.ylabel(ylabel)
        plt.title(title)
        plt.xticks(rotation=45, ha="right")
        plt.tight_layout()
        plt.savefig(outfile, dpi=150)
        plt.close()

    # Individual charts
    bar_chart(top10["run_ms"],      "Task 3: Run time by PID (top 10)",      "run_ms",      "fairness_top10_run_ms.png")
    bar_chart(top10["wait_ms"],     "Task 3: Wait time by PID (top 10)",     "wait_ms",     "fairness_top10_wait_ms.png")
    bar_chart(top10["switches"],    "Task 3: Switches by PID (top 10)",      "switches",    "fairness_top10_switches.png")
    bar_chart(top10["cpu_share"],   "Task 3: CPU share proxy by PID (top 10)","run_ms/(run_ms+wait_ms)","fairness_top10_cpu_share.png")

    # Quick console summary
    print("Wrote:")
    for f in [
        "fairness_top10_run_ms.png",
        "fairness_top10_wait_ms.png",
        "fairness_top10_switches.png",
        "fairness_top10_cpu_share.png",
        "fairness_top10_table.csv",
    ]:
        print(" -", f)

if __name__ == "__main__":
    main()
