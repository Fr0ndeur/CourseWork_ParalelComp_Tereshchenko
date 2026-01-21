import argparse
import os
import pandas as pd
import matplotlib.pyplot as plt

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--input", required=True, help="build_summary.csv")
    ap.add_argument("--outdir", required=True, help="output folder")
    ap.add_argument("--title", default="Build benchmark", help="base title")
    ap.add_argument("--dpi", type=int, default=250)
    ap.add_argument("--logy", action="store_true", help="log scale for elapsed plots")
    args = ap.parse_args()

    os.makedirs(args.outdir, exist_ok=True)
    df = pd.read_csv(args.input)

    # numeric
    for c in ["threads","run","elapsed_ms","scanned","indexed","skipped","errors"]:
        if c in df.columns:
            df[c] = pd.to_numeric(df[c], errors="coerce")

    df = df.dropna(subset=["threads","elapsed_ms"])
    df = df[df["threads"] > 0]
    df = df[df["elapsed_ms"] > 0]

    # aggregate per threads
    agg = df.groupby("threads", as_index=False).agg(
        mean_ms=("elapsed_ms", "mean"),
        std_ms=("elapsed_ms", "std"),
        min_ms=("elapsed_ms", "min"),
        max_ms=("elapsed_ms", "max"),
        runs=("elapsed_ms", "count"),
    ).sort_values("threads")

    # baseline = mean at smallest threads (usually 1)
    base_threads = int(agg["threads"].iloc[0])
    base_ms = float(agg["mean_ms"].iloc[0])

    agg["speedup"] = base_ms / agg["mean_ms"]
    agg["efficiency"] = agg["speedup"] / agg["threads"]

    # 1) elapsed vs threads (mean ± std)
    plt.figure(figsize=(10,6))
    plt.errorbar(agg["threads"], agg["mean_ms"], yerr=agg["std_ms"], marker="o", capsize=4)
    plt.xlabel("Threads")
    plt.ylabel("Elapsed (ms) mean ± std")
    plt.title(f"{args.title}: elapsed vs threads (baseline={base_threads})")
    plt.grid(True)
    if args.logy:
        plt.yscale("log")
    plt.savefig(os.path.join(args.outdir, "build_elapsed_vs_threads.png"), dpi=args.dpi, bbox_inches="tight")

    # 2) speedup vs threads
    plt.figure(figsize=(10,6))
    plt.plot(agg["threads"], agg["speedup"], marker="o")
    plt.xlabel("Threads")
    plt.ylabel(f"Speedup (T{base_threads} / Tn)")
    plt.title(f"{args.title}: speedup vs threads")
    plt.grid(True)
    plt.savefig(os.path.join(args.outdir, "build_speedup_vs_threads.png"), dpi=args.dpi, bbox_inches="tight")

    # 3) efficiency vs threads
    plt.figure(figsize=(10,6))
    plt.plot(agg["threads"], agg["efficiency"], marker="o")
    plt.xlabel("Threads")
    plt.ylabel("Efficiency = speedup / threads")
    plt.title(f"{args.title}: parallel efficiency")
    plt.grid(True)
    plt.ylim(0, max(1.05, float(agg["efficiency"].max()) * 1.1))
    plt.savefig(os.path.join(args.outdir, "build_efficiency_vs_threads.png"), dpi=args.dpi, bbox_inches="tight")

    # 4) boxplot elapsed per threads (distribution across runs)
    data = []
    labels = []
    for th in agg["threads"].tolist():
        vals = df.loc[df["threads"] == th, "elapsed_ms"].dropna().tolist()
        if vals:
            data.append(vals)
            labels.append(str(int(th)))

    if data:
        plt.figure(figsize=(12,6))
        plt.boxplot(data, labels=labels, showfliers=True)
        plt.xlabel("Threads")
        plt.ylabel("Elapsed (ms) distribution across runs")
        plt.title(f"{args.title}: elapsed distribution (boxplot)")
        plt.grid(True)
        if args.logy:
            plt.yscale("log")
        plt.savefig(os.path.join(args.outdir, "build_elapsed_boxplot.png"), dpi=args.dpi, bbox_inches="tight")

    # 5) scatter: elapsed per run (видно разброс)
    plt.figure(figsize=(10,6))
    plt.plot(df["threads"], df["elapsed_ms"], marker="o", linestyle="None")
    plt.xlabel("Threads")
    plt.ylabel("Elapsed (ms) per run")
    plt.title(f"{args.title}: raw points (each run)")
    plt.grid(True)
    if args.logy:
        plt.yscale("log")
    plt.savefig(os.path.join(args.outdir, "build_elapsed_scatter.png"), dpi=args.dpi, bbox_inches="tight")

    # save aggregated table too
    agg.to_csv(os.path.join(args.outdir, "build_aggregated.csv"), index=False)

    print("Saved plots to:", args.outdir)
    print("Saved aggregated:", os.path.join(args.outdir, "build_aggregated.csv"))

if __name__ == "__main__":
    main()
