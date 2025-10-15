# Programming Assignment 2

## SchedLab: Observing the Linux Scheduler with eBPF

Welcome! In this lab you’ll use **eBPF + libbpf** to observe how the Linux scheduler behaves on real workloads—**without modifying the kernel**.

You’ve been given two source files:

* `schedlab.bpf.c` — the kernel-resident eBPF program (tracepoints via **tp\_btf**)
* `schedlab_user.c` — the user-space runner that loads, attaches, reads ring-buffer events, and prints or CSV-logs results

The code is **portable across kernels** (CO-RE) and intentionally simple so you can focus on reasoning and analysis.

We will complete **eight tasks**:

1. Understanding the Event Model and Tracepoints
2. Scheduling Latency Distribution
3. Fairness Study
4. Context Switch Overhead Proxy
5. Per-Task Timeline (Gantt-style)
6. Short vs Long Tasks
7. Starvation Detector
8. Modify and Add a Custom Tracepoint (fork)

This document explains the architecture, how to build and run, what each task requires, how to collect data, and what you should deliver.

---

## 1) What the code does

### 1.1 Probes (tp\_btf)

The BPF file attaches to **BTF-typed tracepoints**:

* `tp_btf/sched_wakeup` – a task became runnable. Specifically, it is triggered whenever a process transitions from a **sleeping state** to a **runnable state**, meaning it's ready to be scheduled and run on a CPU
* `tp_btf/sched_switch` – context switch: `prev -> next`. As the name suggests, it is triggerred whenever a context switch happens.
* `tp_btf/sched_process_exec` – a process called `exec()`. It is triggerred every time a new process is executed.
* `tp_btf/sched_process_exit` – a process exited as its name suggests.

From these, the BPF program computes:

* **Wake→Switch latency** (approx. scheduling latency), i.e., how long the process is waiting in the ready queue.
* **Per-slice run time** for the task being switched out.
* **Per-PID aggregates** (total run ns, total wait ns, switches, wakes, migrations)
* and much more...

It emits compact **events** to a **ring buffer** consumed by user space.

### 1.2 User program

`schedlab_user.c`:

* Loads & attaches the BPF object via skeleton
* Sets simple runtime knobs via a `cfg_map` (e.g., `--filter-pid`, `--wait-alert-ms`)
* Prints human-readable logs **or** CSV (with `--csv` and `--csv-header`)
* Maintains lightweight in-memory aggregates for easy on-the-fly summaries

### 1.3 Event model (from BPF → user space)

Each event has:

* Timestamp (ns), `pid`, `comm`, `type`
* For `EV_SWITCH`: `prev_pid`, `next_pid`, `run_ns`, `wait_ns`, and comms
* For `EV_EXEC` / `EV_EXIT`: basic lifecycle markers
* For `EV_WAKE`: wake observed
* For `EV_WAITLONG`: a “wait too long” alert (threshold via `--wait-alert-ms`)

---

## 2) Building & Running
> **Note:** If you do **not** have either (a) a VirtualBox VM with eBPF set up on macOS/Windows, or (b) a native Linux machine with eBPF enabled, please follow the **VirtualBox Setup README**. If you are already on Linux without eBPF enabled, you only need to set up eBPF itself. 
> You **MUST NOT use Docker** for this assignment (Docker often restricts eBPF capabilities).


### 2.1 Prerequisites

* A reasonably recent Linux kernel with **BTF** enabled (5.15+ works well)
* `clang`/`llvm`, `libbpf-dev`, `libelf`, `zlib` (package names vary by distro)
* `bpftool` (used by your Makefile to generate `vmlinux.h` & the skeleton)

### 2.2 Build

Typical minimal flow:

```bash
make
```

Under the hood this should:

* Generate `vmlinux.h` from your kernel’s BTF (first build only)
* Compile `schedlab.bpf.c` → `schedlab.bpf.o`
* `bpftool gen skeleton schedlab.bpf.o > schedlab.skel.h`
* Compile `schedlab_user.c` → `schedlab` (links `-lbpf` etc.)

If your distro requires it, prefer:

```bash
cc -O2 -g schedlab_user.c -o schedlab $(pkg-config --cflags --libs libbpf || echo "-lbpf -lelf -lz")
```

### 2.3 Running

Most commands need `sudo`:

```bash
sudo ./schedlab --mode stream
sudo ./schedlab --mode latency --csv --csv-header > latency.csv
```

Useful flags:

* `--mode {stream|latency|fairness|ctx|timeline|shortlong|iocorr|starvation}`
* `--filter-pid N` (only track one PID; default=off)
* `--wait-alert-ms M` (long-wait alert threshold; default 5ms)
* `--csv` (machine-readable output)
* `--csv-header` (print header once at the start)

Terminate with `Ctrl+C`.

---

## 3) Ground truth & limitations

* **Latency definition:** approximate **scheduling latency** as time from **`sched_wakeup`** to **the task next being scheduled** (`sched_switch` → `next`). This is not the only possible definition (e.g., runnable queue waiting, preemption effects), but it’s a widely used practical proxy for user-space analysis.
* **Run time slice:** For `prev` on `sched_switch`, we approximate run time as `now - last_on_cpu_ts[prev]`. Remember, here `now` is when the `prev` being scheduled out of CPU. and `last_on_cpu_ts[prev]` indicates when it was scheduled in CPU. The difference is the time slice it executes.
* **Thread vs process:** Note that, we mostly report **per PID (tgid)** semantics. In the exit probe, we ignore thread exits (we only log main thread `pid==tid`).
* **Observer effect:** eBPF overhead is low but non-zero; keep recordings short and interpret very small differences carefully.
* **CO-RE:** We read fields via `BPF_CORE_READ` on `task_struct`, avoiding fragile raw ctx layouts.

---

## 4) Workloads (how to generate activity)

To get interesting data, create CPU and I/O load:

* `stress-ng --cpu 2 --io 2 --timeout 10s`
* Launch some interactive shells / editors / short scripts

Keep runs between **10–120 seconds** for manageable logs.

### notes: stress-ng

Stress-ng is a stress testing tool designed to exercise various components of a computer system to assess its stability and performance under heavy load. It can be used to stress-test the CPU, memory, I/O, and other subsystems in a controlled manner. This tool is particularly useful for benchmarking, diagnosing system stability, and testing performance under high stress conditions. 

Stress-ng can generate load on your system by running multiple threads or processes that simulate different types of workloads. For example, it can stress the CPU by running floating-point operations, or it can stress the memory with random access patterns.

Useful Options in Stress-ng:

* --cpu: Stress the CPU by running mathematical operations.
* --fork: Generate multiple processes.
* --timeout: Run the test for a specified time duration.
* --cpu-method: Use different CPU stress methods (e.g., all, matmul).
* --io: Stress the I/O subsystem by performing random reads/writes.

Official stress-ng GitHub Repository: https://github.com/ColinIanKing/stress-ng



---
## 5) Tasks & Deliverables (150 Pts)

For each task below:

* Run the tool with **clear commands** (include them in your report).
* Collect **CSV** when applicable and perform a brief analysis.
* Provide **plots** (PNG or PDF) where requested.
* Write a **short explanation** of what you see and why (2–5 paragraphs).

### Task 1 - Understanding the Event Model and Tracepoints (10 pts)

**Goal:** analyze how events are modeled and how tracepoints are attached to gather data.

**How:**

Read the code/Makefile and try to understand the codebase.

**Objective**
- Review the event types defined in the code (e.g., `EV_WAKE`, `EV_SWITCH`, `EV_EXEC`, `EV_EXIT`). 
- Identify the tracepoints in the code that correspond to these events (e.g., `sched_process_exec`, `sched_wakeup`, `sched_switch`).
- Understand how data flows from the tracepoints to the user space and which maps store the information.

**Deliverables:**
- write a short summary explaining how tracepoints are used to capture the events in the kernel. **(5 pts)**
- explain what data is stored in the BPF maps and how it is retrieved and processed in user space. **(5 pts)**

###
### Task 2 — Scheduling Latency Distribution (25 pts)

**Goal:** Measure the distribution of **wake→switch latency**.

**How:**

```bash
timeout 20s sudo ./schedlab --mode latency --csv --csv-header > latency.csv &
stress-ng --cpu 2 --io 2 --timeout 15s &
wait
```

**Analysis:**

* Compute p50/p90/p99 of `latency_ns` (convert to ms).
* Plot histogram of `latency_ns` (ms).
* Compare idle vs. loaded runs (e.g., with and without `stress-ng`).

notes:
- p50 (50th percentile / median): 50% of observed latencies are below this value. Gives the "typical" latency.
- p90 (90th percentile): 90% of observed latencies are below this value. Shows the "tail latency" — the slowest 10% of cases.
- p99 (99th percentile): 99% of observed latencies are below this value. Captures extreme "long-tail" behavior, e.g., rare but very slow events.

**Deliverables:**

* `latency.csv` **(5 pts)**
* p50/p90/p99 of `latency_ns` (convert to ms). **(5 pts)**
* A plot histogram of `latency_ns` (ms). **(5 pts)**
* Compare idle vs. loaded runs (e.g., with and without `stress-ng`). **(5 pts)**
* Commentary: What affects tail latency? Any outliers? **(5 pts)**

---

###
### Task 3 — Fairness Study (20 pts)

**Goal:** Compare **per-task run time** vs **wait time** to assess fairness.

**How:**

```bash
timeout 25s sudo ./schedlab --mode fairness --csv --csv-header > fairness.csv &
stress-ng --cpu 1 --fork 5 --timeout 20s &
wait
```

Each `EV_SWITCH` updates aggregates for `next_pid`. CSV lines show:

```
pid,run_ms,wait_ms,switches
```

(these are **running totals** per pid over time)

**Analysis:**

* Plot a bar chart of **run\_ms**, **wait\_ms**, **switches** across top 10 PIDs.
* For active PIDs, compute **run\_ms / (run\_ms + wait\_ms)** as a crude CPU share proxy.
* Compare fairness between different workload mixes (e.g., add more CPU workers).

**Deliverables:**

* `fairness.csv` **(5 pts)**
* A plot summarizing top PIDs and their **run\_ms**, **wait\_ms**, **switches**. **(5 pts)**
* Compare fairness between different workload mixes (e.g., add more CPU workers). **(5 pts)**
* Commentary: Does load get shared as expected? Any task “left behind”? **(5 pts)**

---

###
### Task 4 — Context Switch Overhead Proxy (20 pts)

**Goal:** Use **run slice length** (`run_ns` for `prev` at `sched_switch`) as a proxy for context switch overhead pressure—very short slices imply more switching.

**How:**

```bash
# Phase 1: light workload (10s) → schedlab 15s
timeout 15s sudo ./schedlab --mode ctx --csv --csv-header > ctx_light.csv &
stress-ng --cpu 1 --timeout 10s &
wait

# Phase 2: heavy workload (20s) → schedlab 25s
timeout 25s sudo ./schedlab --mode ctx --csv --csv-header > ctx_heavy.csv &
stress-ng --cpu 8 --fork 8 --timeout 20s &
wait
```

CSV columns:

```
ts_ns,prev_pid,next_pid,run_ns
```

**Analysis:**

* Plot distribution of `run_ns` (ms). Very small values indicates frequent switching.
* Compute the estimated context switches per second from the `EV_SWITCH` rate.
* Compare across workloads (light vs heavy).

**Deliverables:**

* `ctx.csv` **(5 pts)**
* Plot(s) of run slice distribution. **(5 pts)**
* Compare estimated context switches across workloads (light vs heavy). **(5 pts)**
* Brief discussion of overhead vs. throughput tradeoff. **(5 pts)**

---

###
### Task 5 — Per-Task Timeline (Gantt-style) (15 pts)

**Goal:** Build a **timeline** for a few PIDs: when they woke, when they ran, and for how long.

**How:**

```bash
timeout 20s sudo ./schedlab --mode timeline --csv --csv-header > timeline.csv &
stress-ng --cpu 2 --timeout 15s &
wait
```

CSV columns:

```
ts_ns,pid,event,wait_ns,run_prev_ns
```

* `WAKE` lines: task woke
* `SWITCH` lines: task scheduled; includes `wait_ns` since last wake and `run_prev_ns` for the previous task
* `EXEC`, `EXIT` lines: lifecycle markers

**Analysis:**

* Convert `ts_ns` to ms relative to experiment start.
* For **selected PIDs (3–5)**, plot a **Gantt chart**: horizontal bars for run regions, markers for wakes.
* Explain bursty behavior, wait times, and how often tasks get CPU.

**Deliverables:**

* `timeline.csv` **(5 pts)**
* A Gantt-style figure for a few PIDs (+ 2–4 paragraphs of interpretation). **(5 pts)**
* Brief explanation of bursty behavior, wait times, and how often tasks get CPU. **(5 pts)**

---

###
### Task 6 — Short vs Long Tasks (15 pts)

**Goal:** Quantify differences between short-lived and long-running tasks.

**How:**

```bash
timeout 25s sudo ./schedlab --mode shortlong --csv --csv-header > shortlong.csv &
(
  stress-ng --cpu 1 --timeout 20s &
  for i in $(seq 1 100); do sleep 0.05 & done
) &
wait
```

On **EXIT**, the tool prints:

```
pid,lifetime_ms,wakes,switches
```

where `lifetime_ms = last_seen - first_exec`.

**Analysis:**

* Split tasks into **short** (e.g., < 200 ms) vs **long**.
* Compare average `switches` and `wakes` per group.
* Discuss why short tasks might have higher relative scheduling latency or more context switching per ms.

**Deliverables:**

* `sl.csv` **(5 pts)**
* A table comparing short vs long groups **(5 pts)**
* A short essay on observed differences. **(5 pts)**

---

###
### Task 7 — Starvation Detector (15 pts)

**Goal:** Detect tasks whose **wait latency** exceeds a threshold (possible starvation).

**How:**

* Choose a threshold (e.g., 20 ms): `--wait-alert-ms 20`
* Run a workload where some tasks can be delayed (e.g., one CPU hog and a low-priority task, or just heavy contention):

```bash
timeout 30s sudo ./schedlab --mode starvation --wait-alert-ms 20 --csv --csv-header > starv.csv &
stress-ng --cpu $(( $(nproc) * 2 )) --timeout 25s &
wait
```

CSV lines for alerts:

```
ts_ns,pid,wait_alert
```

**Analysis:**

* Count alerts per PID; find top offenders.
* Change threshold (e.g., 5 ms vs 20 ms) and compare.
* Explain plausible causes (CPU contention, migrations, cache effects, etc.).

**Deliverables:**

* `starv.csv` **(5 pts)**
* A table containing count under different threshold **(5 pts)**
* Short write-up: which PIDs starved, under what workload, and what you learned. **(5 pts)**

###
### Task 8 - Modify and Add a Custom Tracepoint (30 pts)

**Goal:** add a new tracepoint to the code to capture fork() activity. 

**Objective:** 
- add `sched_process_fork` trace point and finish its code.
- Modify the user-space application to process and visualize the new event.

**Deliverables:**
- add a custom tracepoint handler in `schedlab.bpf.c`. **(5 pts)**
- add processing logic in `schedlab_user.c`. **(5 pts)**
- a line chart of forks/ms and a bar chart ranking parent by no of forks. **(10 pts)**
- explain the modification of the BPF program (both user and kernel) to handle the new tracepoint and commands used to collect data **(10 pts)**

---

## 6) Suggested plotting scripts

You can use Python/MATLAB/R. Example (Python/pandas/matplotlib):

```python
import pandas as pd
import matplotlib.pyplot as plt

lat = pd.read_csv("latency.csv")
lat['lat_ms'] = lat['latency_ns']/1e6
lat['lat_ms'].plot(kind='hist', bins=50)
plt.xlabel("Scheduling latency (ms)")
plt.ylabel("Count")
plt.title("Latency Distribution")
plt.tight_layout()
plt.savefig("latency_hist.png")
```

For timelines, pivot per-PID and draw segments (WAKE markers vs SWITCH intervals).

---

## 7) What’s expected from you

**One PDF report** covering Tasks 1–8:

* **Methods:** workloads used, commands run, parameters (`--wait-alert-ms`, `--filter-pid`, duration).
* **Data:** CSVs (attach or link), plots (PNG/PDF embedded).
* **Findings:** For each task, 2–5 paragraphs answering the questions.
* **Reflection:** What surprised you? What would you measure next?

**Reproducibility:** Include the exact command lines and the kernel version:

```bash
uname -a
bpftool version
```

---

## 8) Grading rubric (guidance)

| Criterion             | Excellent (A)                                                      | Good (B)                        | Fair (C)       | Needs work       |
| --------------------- | ------------------------------------------------------------------ | ------------------------------- | -------------- | ---------------- |
| Correct builds & runs | Clear commands; no missing steps; uses CSV where needed            | Minor omissions                 | Several gaps   | Not reproducible |
| Plots & stats         | Clean plots, p50/p90/p99, labeled axes/units                       | Some plots lack labels or units | Minimal plots  | No plots         |
| Insight & reasoning   | Connects observations to scheduler behavior; considers limitations | Mostly descriptive              | Superficial    | Incorrect claims |
| Task coverage         | All 6 tasks done thoroughly                                        | 1 task light                    | 2+ tasks light | Major gaps       |
| Clarity               | Organized, concise, figures referenced in text                     | Minor clarity issues            | Hard to follow | Disorganized     |


---

## 9) Safety & etiquette

* Don’t run intrusive workloads on multi-user systems without permission.
* Keep captures short (tens of seconds) to avoid large logs.
* The BPF program is read-only (observability only); it **does not** alter scheduling.

---

###
### References
- https://netflixtechblog.com/noisy-neighbor-detection-with-ebpf-64b1f4b3bbdd


Have fun—and be curious!
