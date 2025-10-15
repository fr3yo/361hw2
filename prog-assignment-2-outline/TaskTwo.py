import pandas as pd
import matplotlib.pyplot as plt

# read csv output from schedlab latency run
df = pd.read_csv("latency.csv")

# convert latency from ns → ms
df["lat_ms"] = df["latency_ns"] / 1e6

# clean up data
df = df[df["lat_ms"] >= 0]

# percentiles
p50 = df["lat_ms"].quantile(0.50)
p90 = df["lat_ms"].quantile(0.90)
p99 = df["lat_ms"].quantile(0.99)
print(f"p50: {p50:.3f} ms, p90: {p90:.3f} ms, p99: {p99:.3f} ms")

# save stats
pd.DataFrame({
    "metric": ["p50","p90","p99"],
    "ms": [p50,p90,p99]
}).to_csv("latency_percentiles.csv", index=False)

# plot histogram
plt.figure()
upper = df["lat_ms"].quantile(0.995)
df["lat_ms"].clip(upper=upper).plot(kind="hist", bins=60)
plt.xlabel("Scheduling latency (ms)")
plt.ylabel("Count")
plt.title("Task 2: Wake→Switch Latency Distribution")
plt.tight_layout()
plt.savefig("latency_hist.png", dpi=150)

print("\nWrote: latency_percentiles.csv and latency_hist.png")
