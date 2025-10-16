#!/usr/bin/env python3
# Task 5 — Per-Task Timeline (Gantt) generator
#
# Usage examples:
#   python3 task5_timeline.py timeline.csv
#   python3 task5_timeline.py timeline.csv --top 4
#   python3 task5_timeline.py timeline.csv --pids 123,456,789
#   python3 task5_timeline.py timeline_light.csv --label light
#   python3 task5_timeline.py timeline_light.csv timeline_heavy.csv --label light --label heavy
#
# Inputs (supports either schema):
#   Minimal (typical timeline mode):
#     ts_ns, pid, event, wait_ns, run_prev_ns
#   Or richer (if present):
#     ts_ns, prev_pid, next_pid, event, wait_ns, run_prev_ns
#
# What it makes (per input file / label):
#   timeline_<label>_summary.csv     — per-PID totals (run_ms, wakes, runs, avg_run_ms)
#   timeline_<label>_gantt_compare.png
#   timeline_<label>_gantt_pid_<PID>.png  (one image per selected PID)
#
# Selection:
#   - By default, auto-picks top N PIDs (N=4) by total run_ms
#   - Or pass explicit list via --pids
#
# Notes:
#   - A SWITCH row ending at ts_ns with run_prev_ns creates a run-interval for the task that just ran:
#       [ts_ns - run_prev_ns, ts_ns]
#     The PID is taken from prev_pid if available, else row["pid"].
#   - WAKE rows are marked as vertical ticks on each PID’s timeline.

import argparse
from pathlib import Path
from typing import Dict, List, Tuple
import pandas as pd
import numpy as np
import matplotlib.pyplot as plt

def load_and_normalize(csv_path: Path) -> pd.DataFrame:
    df = pd.read_csv(csv_path)
    if "ts_ns" not in df.columns:
        raise ValueError(f"{csv_path} missing ts_ns column")

    # Normalize time to milliseconds since start
    t0 = df["ts_ns"].min()
    df["t_ms"] = (df["ts_ns"] - t0) / 1e6

    # Standardize some columns (may or may not exist)
    for col in ["event", "wait_ns", "run_prev_ns", "pid", "prev_pid", "next_pid"]:
        if col not in df.columns:
            df[col] = np.nan

    # Convert to consistent units
    if df["run_prev_ns"].notna().any():
        df["run_prev_ms"] = df["run_prev_ns"] / 1e6
    else:
        df["run_prev_ms"] = 0.0

    # Make an "event_lc" for filtering
    df["event_lc"] = df["event"].astype(str).str.lower()

    return df

def build_run_intervals(df: pd.DataFrame) -> pd.DataFrame:
    """
    From SWITCH rows, create intervals:
      start_ms = t_ms - run_prev_ms, end_ms = t_ms
      pid_for_run = prev_pid (if present) else pid
    Returns a DataFrame with columns: pid, start_ms, end_ms, dur_ms
    """
    sw = df[df["event_lc"].str.contains("switch", na=False)].copy()
    if sw.empty:
        return pd.DataFrame(columns=["pid","start_ms","end_ms","dur_ms"])

    # Choose which column names hold the PID that just ran
    if "prev_pid" in sw.columns and sw["prev_pid"].notna().any():
        pid_col = "prev_pid"
    else:
        # Fallback: assume 'pid' refers to the task that just ran
        pid_col = "pid"

    sw["start_ms"] = (sw["t_ms"] - sw["run_prev_ms"]).clip(lower=0)
    sw["end_ms"]   = sw["t_ms"]
    sw["dur_ms"]   = (sw["end_ms"] - sw["start_ms"]).clip(lower=0)
    ivals = sw[[pid_col, "start_ms", "end_ms", "dur_ms"]].rename(columns={pid_col: "pid"})
    # drop zero/neg durations, if any
    ivals = ivals[ivals["dur_ms"] > 0].copy()
    # coerce pid to int if possible
    with np.errstate(all="ignore"):
        ivals["pid"] = ivals["pid"].astype("Int64")
    return ivals

def collect_wakes(df: pd.DataFrame) -> pd.DataFrame:
    wk = df[df["event_lc"].str.contains("wake", na=False)].copy()
    if wk.empty:
        return pd.DataFrame(columns=["pid","t_ms"])
    # which column holds the waking pid? prefer explicit pid
    pid_col = "pid" if "pid" in wk.columns else ("next_pid" if "next_pid" in wk.columns else None)
    if pid_col is None:
        return pd.DataFrame(columns=["pid","t_ms"])
    wakes = wk[[pid_col, "t_ms"]].rename(columns={pid_col: "pid"}).copy()
    with np.errstate(all="ignore"):
        wakes["pid"] = wakes["pid"].astype("Int64")
    return wakes

def choose_pids(ivals: pd.DataFrame, top_n: int, explicit_pids: List[int] | None) -> List[int]:
    if explicit_pids:
        return explicit_pids
    if ivals.empty:
        return []
    totals = ivals.groupby("pid", as_index=True)["dur_ms"].sum().sort_values(ascending=False)
    return list(totals.head(top_n).index.astype(int))

def plot_gantt_for_pids(ivals: pd.DataFrame, wakes: pd.DataFrame, pids: List[int], label: str, outdir: Path):
    if not pids:
        return []

    out_paths = []
    # Combined compare figure
    plt.figure()
    ax_all = plt.gca()

    # Per-PID figures
    for i, pid in enumerate(pids):
        segs = ivals[ivals["pid"] == pid]
        wk   = wakes[wakes["pid"] == pid]

        # Individual figure
        plt.figure()
        ax = plt.gca()
        # Use broken_barh style via rectangles (build from intervals)
        for _, row in segs.iterrows():
            ax.broken_barh([(row["start_ms"], row["dur_ms"])], (0.4, 0.6))
        if not wk.empty:
            ax.vlines(wk["t_ms"].values, ymin=0.35, ymax=1.05)  # wake markers

        ax.set_xlabel("Time since start (ms)")
        ax.set_yticks([])
        ax.set_title(f"Task 5: Timeline (PID {pid}, {label})")
        plt.tight_layout()
        fpath = outdir / f"timeline_{label}_gantt_pid_{pid}.png"
        plt.savefig(fpath, dpi=150)
        plt.close()
        out_paths.append(str(fpath))

        # Add to combined plot (stack rows)
        ybase = i  # one row per PID
        for _, row in segs.iterrows():
            ax_all.broken_barh([(row["start_ms"], row["dur_ms"])], (ybase + 0.4, 0.6))
        if not wk.empty:
            ax_all.vlines(wk["t_ms"].values, ymin=ybase + 0.35, ymax=ybase + 1.05)

    ax_all.set_xlabel("Time since start (ms)")
    ax_all.set_yticks([i + 0.7 for i in range(len(pids))])
    ax_all.set_yticklabels([str(pid) for pid in pids])
    ax_all.set_title(f"Task 5: Timeline Gantt (selected PIDs, {label})")
    plt.tight_layout()
    combined = outdir / f"timeline_{label}_gantt_compare.png"
    plt.savefig(combined, dpi=150)
    plt.close()
    out_paths.append(str(combined))
    return out_paths

def write_summary(ivals: pd.DataFrame, wakes: pd.DataFrame, label: str, outdir: Path):
    # Per PID totals
    run_tot = ivals.groupby("pid", as_index=False)["dur_ms"].sum().rename(columns={"dur_ms":"run_ms"})
    runs    = ivals.groupby("pid", as_index=False)["dur_ms"].count().rename(columns={"dur_ms":"num_runs"})
    avg_run = ivals.groupby("pid", as_index=False)["dur_ms"].mean().rename(columns={"dur_ms":"avg_run_ms"})
    if not wakes.empty:
        wk_cnt = wakes.groupby("pid", as_index=False)["t_ms"].count().rename(columns={"t_ms":"wakes"})
    else:
        wk_cnt = pd.DataFrame({"pid": run_tot["pid"], "wakes": 0})

    summary = run_tot.merge(runs, on="pid", how="outer").merge(avg_run, on="pid", how="outer").merge(wk_cnt, on="pid", how="outer")
    summary = summary.fillna(0)
    out = outdir / f"timeline_{label}_summary.csv"
    summary.to_csv(out, index=False)
    return out

def main():
    ap = argparse.ArgumentParser(description="Task 5 timeline Gantt plotter")
    ap.add_argument("csvs", nargs="+", help="timeline CSV file(s)")
    ap.add_argument("--label", action="append", help="label(s) for each CSV (e.g., light, heavy). Repeatable.")
    ap.add_argument("--pids", type=str, default="", help="comma-separated PIDs to include (overrides --top)")
    ap.add_argument("--top", type=int, default=4, help="auto-pick top N PIDs by run_ms (default: 4)")
    ap.add_argument("--outdir", default=".", help="output directory")
    args = ap.parse_args()

    labels = args.label or [Path(p).stem for p in args.csvs]
    if len(labels) != len(args.csvs):
        raise SystemExit("Number of --label entries must match number of CSVs")

    outdir = Path(args.outdir)
    outdir.mkdir(parents=True, exist_ok=True)

    explicit_pids = [int(x) for x in args.pids.split(",") if x.strip().isdigit()] if args.pids else None

    for csv_path, label in zip(args.csvs, labels):
        df = load_and_normalize(Path(csv_path))
        ivals = build_run_intervals(df)
        wakes = collect_wakes(df)

        # Choose which PIDs to draw
        pids = choose_pids(ivals, args.top, explicit_pids)

        # Make images
        made = plot_gantt_for_pids(ivals, wakes, pids, label, outdir)
        # Summary table
        summary_path = write_summary(ivals, wakes, label, outdir)

        print(f"[{label}] Selected PIDs:", pids)
        print(f"[{label}] Wrote:")
        for m in made:
            print(" -", m)
        print(" -", summary_path)

if __name__ == "__main__":
    main()
