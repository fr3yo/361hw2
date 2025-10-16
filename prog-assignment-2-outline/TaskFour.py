#!/usr/bin/env python3
# Task 4 (Context Switch Overhead) — individual + comparison outputs
# Usage:
#   python3 task4_ctx.py ctx_light.csv ctx_heavy.csv --labels light heavy
#   (also works with a single CSV/label)
#
# Input CSV columns: ts_ns,prev_pid,next_pid,run_ns

import argparse
from pathlib import Path
import pandas as pd
import matplotlib.pyplot as plt

def load_ctx(csv_path: Path) -> pd.DataFrame:
    df = pd.read_csv(csv_path)
    need = {"ts_ns","prev_pid","next_pid","run_ns"}
    if not need.issubset(df.columns):
        missing = ", ".join(sorted(need - set(df.columns)))
        raise ValueError(f"Missing columns in {csv_path}: {missing}")
    df["ts_s"] = df["ts_ns"] / 1e9
    df["run_ms"] = df["run_ns"] / 1e6
    df = df[pd.notnull(df["run_ms"]) & (df["run_ms"] >= 0)]
    return df

def save_hist_run_ms(df: pd.DataFrame, label: str, outdir: Path) -> Path:
    plt.figure()
    upper = df["run_ms"].quantile(0.995)
    df["run_ms"].clip(upper=upper).plot(kind="hist", bins=60)
    plt.xlabel("Run slice length (ms)")
    plt.ylabel("Count")
    plt.title(f"Task 4: Run-slice distribution ({label})")
    plt.tight_layout()
    outfile = outdir / f"ctx_{label}_hist.png"
    plt.savefig(outfile, dpi=150)
    plt.close()
    return outfile

def per_second_counts(df: pd.DataFrame) -> pd.DataFrame:
    start = df["ts_s"].min()
    sec = (df["ts_s"] - start).astype(int)
    return sec.value_counts().sort_index().rename("switches").reset_index(names="sec")

def save_rate_bar(per_sec: pd.DataFrame, label: str, outdir: Path) -> Path:
    plt.figure()
    plt.bar(per_sec["sec"], per_sec["switches"])
    plt.xlabel("Time since start (s)")
    plt.ylabel("Context switches per second")
    plt.title(f"Task 4: EV_SWITCH rate ({label})")
    plt.tight_layout()
    outfile = outdir / f"ctx_{label}_switches_per_sec.png"
    plt.savefig(outfile, dpi=150)
    plt.close()
    return outfile

def save_compare_hist(dfs, labels, outdir: Path) -> Path | None:
    if len(dfs) < 2:
        return None
    plt.figure()
    for df, lab in zip(dfs, labels):
        upper = df["run_ms"].quantile(0.995)
        df["run_ms"].clip(upper=upper).plot(kind="hist", bins=60, alpha=0.5, label=lab)
    plt.xlabel("Run slice length (ms)")
    plt.ylabel("Count")
    plt.title("Task 4: Run-slice distribution (comparison)")
    plt.legend()
    plt.tight_layout()
    outfile = outdir / "ctx_compare_hist.png"
    plt.savefig(outfile, dpi=150)
    plt.close()
    return outfile

def save_compare_rate(per_secs, labels, outdir: Path) -> Path | None:
    if len(per_secs) < 2:
        return None
    # align on sec for clean overlay
    merged = None
    for ps, lab in zip(per_secs, labels):
        ps2 = ps.set_index("sec").rename(columns={"switches": lab})
        merged = ps2 if merged is None else merged.join(ps2, how="outer")
    merged = merged.fillna(0).sort_index()

    plt.figure()
    for lab in labels:
        merged[lab].reset_index(drop=True).plot(label=lab)
    plt.xlabel("Time since start (s)")
    plt.ylabel("Context switches per second")
    plt.title("Task 4: EV_SWITCH rate (comparison)")
    plt.legend()
    plt.tight_layout()
    outfile = outdir / "ctx_compare_switches_per_sec.png"
    plt.savefig(outfile, dpi=150)
    plt.close()
    return outfile

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("csvs", nargs="+", help="ctx csv files")
    ap.add_argument("--labels", nargs="+", required=True, help="labels matching CSVs (e.g., light heavy)")
    ap.add_argument("--outdir", default=".", help="output directory")
    args = ap.parse_args()

    if len(args.csvs) != len(args.labels):
        raise SystemExit("Number of CSVs must match number of labels")

    outdir = Path(args.outdir)
    outdir.mkdir(parents=True, exist_ok=True)

    summaries = []
    dfs, per_secs = [], []

    for csv_path, label in zip(args.csvs, args.labels):
        p = Path(csv_path)
        df = load_ctx(p)
        dfs.append(df)

        hist_file = save_hist_run_ms(df, label, outdir)

        ps = per_second_counts(df)
        per_secs.append(ps)
        rate_file = save_rate_bar(ps, label, outdir)

        duration = df["ts_s"].max() - df["ts_s"].min()
        switches = len(df)
        rate = (switches / duration) if duration > 0 else float("nan")

        summaries.append({
            "label": label,
            "csv": str(p),
            "count_switches": int(switches),
            "duration_s": float(duration),
            "switches_per_sec": float(rate),
            "run_ms_p50": float(df["run_ms"].quantile(0.50)),
            "run_ms_p90": float(df["run_ms"].quantile(0.90)),
            "run_ms_p99": float(df["run_ms"].quantile(0.99)),
            "histogram_image": str(hist_file),
            "rate_image": str(rate_file),
        })

    # Combined comparison outputs
    cmp_hist = save_compare_hist(dfs, args.labels, outdir)
    if cmp_hist:
        summaries.append({"label": "comparison", "csv": "", "histogram_image": str(cmp_hist)})

    cmp_rate = save_compare_rate(per_secs, args.labels, outdir)
    if cmp_rate:
        summaries.append({"label": "comparison", "csv": "", "rate_image": str(cmp_rate)})

    pd.DataFrame(summaries).to_csv(outdir / "ctx_summary.csv", index=False)

    print("Wrote:")
    for s in summaries:
        for k in ("histogram_image","rate_image"):
            if k in s:
                print(" -", s[k])
    print(" -", outdir / "ctx_summary.csv")

if __name__ == "__main__":
    main()
