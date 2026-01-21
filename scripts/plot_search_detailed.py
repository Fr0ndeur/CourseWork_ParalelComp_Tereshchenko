import argparse
import os
import pandas as pd
import matplotlib.pyplot as plt

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--input", required=True, help="search_summary.csv")
    ap.add_argument("--outdir", required=True, help="output folder")
    ap.add_argument("--title", default="Search benchmark", help="base title")
    ap.add_argument("--dpi", type=int, default=250)
    args = ap.parse_args()

    os.makedirs(args.outdir, exist_ok=True)
    df = pd.read_csv(args.input)

    # ensure numeric
    for c in ["clients","run","rps","p50_ms","p95_ms","p99_ms","ok","fail","total","duration_s","topk"]:
        if c in df.columns:
            df[c] = pd.to_numeric(df[c], errors="coerce")

    df["ok_rate"] = df["ok"] / (df["ok"] + df["fail"]).replace(0, pd.NA)
    df = df.dropna(subset=["clients","rps","p50_ms","p95_ms","p99_ms"])

    g = df.groupby("clients", as_index=False)

    agg = g.agg(
        rps_mean=("rps","mean"),
        rps_std=("rps","std"),
        p50_mean=("p50_ms","mean"),
        p50_std=("p50_ms","std"),
        p95_mean=("p95_ms","mean"),
        p95_std=("p95_ms","std"),
        p99_mean=("p99_ms","mean"),
        p99_std=("p99_ms","std"),
        ok_rate_mean=("ok_rate","mean"),
        ok_rate_std=("ok_rate","std"),
    ).sort_values("clients")

    clients = agg["clients"].tolist()

    # 1) RPS vs clients (mean ± std)
    plt.figure(figsize=(10,6))
    plt.errorbar(agg["clients"], agg["rps_mean"], yerr=agg["rps_std"], marker="o", capsize=4)
    plt.xlabel("Clients")
    plt.ylabel("Requests/sec (mean ± std)")
    plt.title(f"{args.title}: throughput (RPS)")
    plt.grid(True)
    plt.savefig(os.path.join(args.outdir, "search_rps_vs_clients.png"), dpi=args.dpi, bbox_inches="tight")

    # 2) Latency percentiles vs clients (mean ± std)
    plt.figure(figsize=(10,6))
    plt.errorbar(agg["clients"], agg["p50_mean"], yerr=agg["p50_std"], marker="o", capsize=4, label="p50")
    plt.errorbar(agg["clients"], agg["p95_mean"], yerr=agg["p95_std"], marker="o", capsize=4, label="p95")
    plt.errorbar(agg["clients"], agg["p99_mean"], yerr=agg["p99_std"], marker="o", capsize=4, label="p99")
    plt.xlabel("Clients")
    plt.ylabel("Latency (ms, mean ± std across runs)")
    plt.title(f"{args.title}: latency percentiles")
    plt.grid(True)
    plt.legend()
    plt.savefig(os.path.join(args.outdir, "search_latency_vs_clients.png"), dpi=args.dpi, bbox_inches="tight")

    # 3) Boxplot of p95 across runs per clients
    data = []
    labels = []
    for c in clients:
        vals = df.loc[df["clients"] == c, "p95_ms"].dropna().tolist()
        if vals:
            data.append(vals)
            labels.append(str(int(c)))
    if data:
        plt.figure(figsize=(12,6))
        plt.boxplot(data, labels=labels, showfliers=True)
        plt.xlabel("Clients")
        plt.ylabel("p95 latency (ms) distribution across runs")
        plt.title(f"{args.title}: p95 distribution (boxplot)")
        plt.grid(True)
        plt.savefig(os.path.join(args.outdir, "search_p95_boxplot.png"), dpi=args.dpi, bbox_inches="tight")

    # 4) Throughput vs p95 scatter (каждый прогон — точка)
    plt.figure(figsize=(10,6))
    plt.plot(df["rps"], df["p95_ms"], marker="o", linestyle="None")
    plt.xlabel("Requests/sec (per run)")
    plt.ylabel("p95 latency (ms, per run)")
    plt.title(f"{args.title}: throughput-latency tradeoff")
    plt.grid(True)
    plt.savefig(os.path.join(args.outdir, "search_rps_vs_p95_scatter.png"), dpi=args.dpi, bbox_inches="tight")

    # 5) Efficiency: RPS per client
    df["rps_per_client"] = df["rps"] / df["clients"].replace(0, pd.NA)
    agg2 = df.groupby("clients", as_index=False).agg(
        rpc_mean=("rps_per_client","mean"),
        rpc_std=("rps_per_client","std"),
        ok_rate_mean=("ok_rate","mean"),
        ok_rate_std=("ok_rate","std"),
    ).sort_values("clients")

    plt.figure(figsize=(10,6))
    plt.errorbar(agg2["clients"], agg2["rpc_mean"], yerr=agg2["rpc_std"], marker="o", capsize=4)
    plt.xlabel("Clients")
    plt.ylabel("RPS per client (mean ± std)")
    plt.title(f"{args.title}: efficiency (RPS/client)")
    plt.grid(True)
    plt.savefig(os.path.join(args.outdir, "search_efficiency.png"), dpi=args.dpi, bbox_inches="tight")

    # 6) OK rate vs clients
    plt.figure(figsize=(10,6))
    plt.errorbar(agg["clients"], agg["ok_rate_mean"], yerr=agg["ok_rate_std"], marker="o", capsize=4)
    plt.xlabel("Clients")
    plt.ylabel("OK rate (ok/(ok+fail)) mean ± std")
    plt.title(f"{args.title}: success rate")
    plt.grid(True)
    plt.ylim(0, 1.02)
    plt.savefig(os.path.join(args.outdir, "search_ok_rate.png"), dpi=args.dpi, bbox_inches="tight")

    print("Saved plots to:", args.outdir)

if __name__ == "__main__":
    main()
