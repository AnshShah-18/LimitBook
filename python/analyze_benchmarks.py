#!/usr/bin/env python3
"""Summarise LimitBook benchmark output.

Inputs (produced by scripts/run_benchmarks.sh):
  data/order_book_benchmark.json        Google Benchmark, order-book micro-benchmarks
  data/matching_engine_benchmark.json   Google Benchmark, end-to-end replay
  data/queue_benchmark.json             Google Benchmark, mutex vs SPSC queue
  data/limitbook_runs.csv               ./limitbook CLI runs (single-threaded + pipeline sweeps)

Outputs:
  data/benchmark_results.csv            one flat table of every Google Benchmark median
  data/plots/*.png                      charts (requires matplotlib; skipped if missing)
  summary tables on stdout

Python is only used for analysis; nothing here runs the engine.

    python3 python/analyze_benchmarks.py [--data-dir data] [--no-plots]
"""
from __future__ import annotations

import argparse
import csv
import json
import re
import statistics
from pathlib import Path

# Two-series palette (validated for colour-vision deficiency). Colour follows the
# entity: the "before" implementation is always blue, the "after" always orange.
BEFORE, AFTER = "#2a78d6", "#eb6834"
INK, MUTED, GRID = "#0b0b0b", "#52514e", "#e4e3df"

NAME_RE = re.compile(r"^(?P<bench>\w+)<(?P<impl>.+)>(?:/(?P<arg>\d+))?(?:/.*)?$")


# ---------------------------------------------------------------- loading
def load_gbench(path: Path, suite: str) -> list[dict]:
    """Flatten the median aggregate of every benchmark in a Google Benchmark JSON file."""
    if not path.exists():
        print(f"  (missing {path}, skipped)")
        return []
    rows = []
    for b in json.loads(path.read_text())["benchmarks"]:
        if b.get("run_type") == "aggregate" and b.get("aggregate_name") != "median":
            continue
        name = b.get("run_name", b["name"])
        m = NAME_RE.match(name)
        if not m:  # non-templated benchmarks such as BM_ClockOverhead
            m = re.match(r"^(?P<bench>\w+)", name)
            bench, impl, arg = m["bench"], "", None
        else:
            bench, impl, arg = m["bench"], m["impl"], m["arg"]
        unit = {"ns": 1, "us": 1e3, "ms": 1e6, "s": 1e9}[b.get("time_unit", "ns")]
        rows.append({
            "suite": suite,
            "benchmark": bench,
            "impl": impl,
            "arg": int(arg) if arg else "",
            "real_time_ns": b["real_time"] * unit,
            "items_per_second": b.get("items_per_second", ""),
            "mean_ns": b.get("mean_ns", ""),
            "p50_ns": b.get("p50_ns", ""),
            "p95_ns": b.get("p95_ns", ""),
            "p99_ns": b.get("p99_ns", ""),
            "p999_ns": b.get("p999_ns", ""),
        })
    return rows


def load_cli(path: Path) -> list[dict]:
    """Load ./limitbook runs; repeated runs of one configuration collapse to the median
    run (per metric), since two-thread runs are sensitive to scheduling noise."""
    if not path.exists():
        print(f"  (missing {path}, skipped)")
        return []
    with path.open() as f:
        raw = [{k: (v if k in ("mode", "book") else float(v)) for k, v in r.items()}
               for r in csv.DictReader(f)]
    groups: dict[tuple, list[dict]] = {}
    for r in raw:
        groups.setdefault((r["mode"], r["book"], r["events"], r["rate"]), []).append(r)
    out = []
    for runs in groups.values():
        merged = dict(runs[0])
        for k, v in merged.items():
            if isinstance(v, float):
                merged[k] = statistics.median(r[k] for r in runs)
        merged["runs"] = len(runs)
        out.append(merged)
    return out


# ---------------------------------------------------------------- helpers
def pick(rows, bench, impl=None, arg=None):
    for r in rows:
        if r["benchmark"] == bench and (impl is None or r["impl"] == impl) and \
                (arg is None or r["arg"] == arg):
            return r
    return None


def mps(x) -> str:
    return f"{x / 1e6:6.2f} M/s" if x != "" else "     n/a"


def ns(x) -> str:
    if x == "":
        return "n/a"
    x = float(x)
    if x < 1e3:
        return f"{x:.0f} ns"
    if x < 1e6:
        return f"{x / 1e3:.1f} us"
    return f"{x / 1e6:.1f} ms"


def table(title: str, header: list[str], rows: list[list[str]]) -> None:
    widths = [max(len(str(c)) for c in col) for col in zip(header, *rows)]
    print(f"\n{title}")
    print("  " + "  ".join(h.ljust(w) for h, w in zip(header, widths)))
    print("  " + "  ".join("-" * w for w in widths))
    for r in rows:
        print("  " + "  ".join(str(c).rjust(w) for c, w in zip(r, widths)))


# ---------------------------------------------------------------- summaries
def summarise(gb: list[dict], cli: list[dict]) -> None:
    rows = []
    for n in (10_000, 100_000, 1_000_000):
        b = pick(gb, "BM_Replay", "BaselineEngine", n)
        p = pick(gb, "BM_Replay", "PooledEngine", n)
        if b and p:
            rows.append([f"{n:,}", mps(b["items_per_second"]), mps(p["items_per_second"]),
                         f"{p['items_per_second'] / b['items_per_second']:.2f}x"])
    table("Engine throughput by workload (Google Benchmark, median of 10 interleaved repetitions)",
          ["events", "baseline", "pooled", "speedup"], rows)

    rows = []
    for impl in ("BaselineEngine", "PooledEngine"):
        r = pick(gb, "BM_ReplayLatency", impl, 1_000_000)
        if r:
            rows.append([impl, ns(r["mean_ns"]), ns(r["p50_ns"]), ns(r["p95_ns"]),
                         ns(r["p99_ns"]), ns(r["p999_ns"])])
    clock = pick(gb, "BM_ClockOverhead")
    table("Per-event engine latency, 1M events (each sample includes one clock read"
          + (f" ~{clock['real_time_ns']:.0f} ns)" if clock else ")"),
          ["engine", "mean", "p50", "p95", "p99", "p99.9"], rows)

    rows = []
    for n in (100, 1_000, 10_000):
        b = pick(gb, "BM_ReplayBookSize", "BaselineEngine", n)
        p = pick(gb, "BM_ReplayBookSize", "PooledEngine", n)
        if b and p:
            rows.append([f"{n:,}", mps(b["items_per_second"]), mps(p["items_per_second"]),
                         f"{p['items_per_second'] / b['items_per_second']:.2f}x"])
    table("100K-event replay vs resting-book size", ["~live orders", "baseline", "pooled",
                                                     "speedup"], rows)

    rows = []
    for bench in ("BM_AddThenCancel", "BM_CancelRandomThenReAdd"):
        for n in (100, 1_000, 10_000):
            b = pick(gb, bench, "MapListOrderBook", n)
            p = pick(gb, bench, "PooledOrderBook", n)
            if b and p:
                rows.append([bench.removeprefix("BM_"), f"{n:,}",
                             f"{b['real_time_ns']:.1f} ns", f"{p['real_time_ns']:.1f} ns"])
    table("Order-book operation cost (time per add+cancel pair)",
          ["operation", "book size", "map+list", "pooled"], rows)

    rows = []
    for r in gb:
        if r["suite"] == "queue":
            rows.append([f"{r['benchmark'].removeprefix('BM_')}<{r['impl']}>",
                         f"{r['arg']:,}", mps(r["items_per_second"])])
    rows.sort()
    table("Queue experiment: mutex+condvar vs SPSC ring", ["benchmark", "capacity",
                                                          "throughput"], rows)

    sat = [r for r in cli if r["mode"] != "single" and r["rate"] == 0]
    paced = sorted((r for r in cli if r["mode"] != "single" and r["rate"] > 0),
                   key=lambda r: (r["mode"], r["rate"]))
    if sat:
        table("Pipeline max throughput (unpaced producer, 1M events)",
              ["queue", "events/sec", "p50 latency"],
              [[r["mode"], f"{r['events_per_sec']:,.0f}", ns(r["p50_ns"])] for r in sat])
    if paced:
        table("Pipeline latency vs offered load (enqueue -> processed, median of runs)",
              ["queue", "offered/sec", "achieved/sec", "p50", "p99", "p99.9"],
              [[r["mode"], f"{r['rate']:,.0f}", f"{r['events_per_sec']:,.0f}", ns(r["p50_ns"]),
                ns(r["p99_ns"]), ns(r["p999_ns"])] for r in paced])


# ---------------------------------------------------------------- plots
def plots(gb: list[dict], cli: list[dict], out: Path) -> None:
    try:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
        import matplotlib.ticker  # noqa: F401  (used for log-axis tick control)
    except ImportError:
        print("\nmatplotlib not installed; skipping plots")
        return
    out.mkdir(parents=True, exist_ok=True)
    plt.rcParams.update({
        "font.size": 10, "axes.edgecolor": MUTED, "axes.labelcolor": MUTED,
        "xtick.color": MUTED, "ytick.color": MUTED, "text.color": INK,
        "axes.spines.top": False, "axes.spines.right": False, "axes.grid": True,
        "axes.axisbelow": True, "grid.color": GRID, "grid.linewidth": 0.8,
        "legend.frameon": False, "figure.dpi": 130, "savefig.bbox": "tight",
    })

    def grouped(ax, labels, before, after, names, fmt):
        x = range(len(labels))
        w = 0.36
        for off, vals, color, name in ((-w / 2 - 0.01, before, BEFORE, names[0]),
                                       (w / 2 + 0.01, after, AFTER, names[1])):
            bars = ax.bar([i + off for i in x], vals, w, color=color, label=name)
            ax.bar_label(bars, labels=[fmt(v) for v in vals], padding=2, fontsize=8, color=MUTED)
        ax.set_xticks(list(x), labels)
        ax.grid(axis="x", visible=False)
        ax.legend(loc="upper left", ncols=2)

    def save(fig, name):
        fig.savefig(out / name)
        plt.close(fig)
        print(f"  wrote {out / name}")

    print()
    # 1. throughput by workload
    sizes = (10_000, 100_000, 1_000_000)
    b = [pick(gb, "BM_Replay", "BaselineEngine", n) for n in sizes]
    p = [pick(gb, "BM_Replay", "PooledEngine", n) for n in sizes]
    if all(b) and all(p):
        fig, ax = plt.subplots(figsize=(6.4, 3.6))
        grouped(ax, ["10K", "100K", "1M"], [r["items_per_second"] / 1e6 for r in b],
                [r["items_per_second"] / 1e6 for r in p], ("baseline (map+list)", "pooled"),
                lambda v: f"{v:.1f}")
        ax.set_title("Matching-engine throughput by workload", loc="left")
        ax.set_xlabel("events replayed")
        ax.set_ylabel("million events / sec")
        ax.margins(y=0.18)
        save(fig, "throughput_by_workload.png")

    # 2. latency percentiles
    b = pick(gb, "BM_ReplayLatency", "BaselineEngine", 1_000_000)
    p = pick(gb, "BM_ReplayLatency", "PooledEngine", 1_000_000)
    if b and p:
        keys = ("p50_ns", "p95_ns", "p99_ns", "p999_ns")
        fig, ax = plt.subplots(figsize=(6.4, 3.6))
        grouped(ax, ["p50", "p95", "p99", "p99.9"], [b[k] for k in keys], [p[k] for k in keys],
                ("baseline (map+list)", "pooled"), lambda v: f"{v:.0f}")
        ax.set_title("Per-event engine latency, 1M events", loc="left")
        ax.set_ylabel("nanoseconds")
        ax.margins(y=0.18)
        save(fig, "latency_percentiles.png")

    # 3. book size
    sizes = (100, 1_000, 10_000)
    b = [pick(gb, "BM_ReplayBookSize", "BaselineEngine", n) for n in sizes]
    p = [pick(gb, "BM_ReplayBookSize", "PooledEngine", n) for n in sizes]
    if all(b) and all(p):
        fig, ax = plt.subplots(figsize=(6.4, 3.6))
        grouped(ax, ["~100", "~1K", "~10K"], [r["items_per_second"] / 1e6 for r in b],
                [r["items_per_second"] / 1e6 for r in p], ("baseline (map+list)", "pooled"),
                lambda v: f"{v:.1f}")
        ax.set_title("Throughput vs resting-book size (100K events)", loc="left")
        ax.set_xlabel("live orders in book")
        ax.set_ylabel("million events / sec")
        ax.margins(y=0.18)
        save(fig, "throughput_by_book_size.png")

    # 4. latency vs offered load
    paced = [r for r in cli if r["mode"] in ("mutex", "spsc") and r["rate"] > 0]
    if paced:
        fig, ax = plt.subplots(figsize=(6.4, 3.8))
        for mode, color, label in (("mutex", BEFORE, "mutex + condvar"),
                                   ("spsc", AFTER, "SPSC ring buffer")):
            rs = sorted((r for r in paced if r["mode"] == mode), key=lambda r: r["rate"])
            if not rs:
                continue
            ax.plot([r["rate"] / 1e6 for r in rs], [r["p50_ns"] for r in rs], color=color,
                    linewidth=2, marker="o", markersize=6, label=f"{label} p50")
            ax.plot([r["rate"] / 1e6 for r in rs], [r["p99_ns"] for r in rs], color=color,
                    linewidth=2, linestyle="--", marker="s", markersize=5, label=f"{label} p99")
            # Points where the consumer could not keep up with the offered load.
            sat = [r for r in rs if r["events_per_sec"] < 0.95 * r["rate"]]
            if sat:
                first = sat[0]
                ax.annotate(f"{mode} saturated\n(achieved {first['events_per_sec'] / 1e6:.1f}M/s)",
                            (first["rate"] / 1e6, first["p50_ns"]), textcoords="offset points",
                            xytext=(-150, -6), ha="right", va="center", fontsize=8, color=MUTED,
                            arrowprops={"arrowstyle": "-", "color": MUTED, "linewidth": 0.8})
        ax.set_xscale("log")
        ax.set_yscale("log")
        ax.set_xticks([0.25, 0.5, 1, 2, 4], ["0.25M", "0.5M", "1M", "2M", "4M"])
        ax.xaxis.set_minor_locator(matplotlib.ticker.NullLocator())
        ax.set_title("Pipeline latency vs offered load (1M events)", loc="left")
        ax.set_xlabel("offered load, events / sec")
        ax.set_ylabel("enqueue -> processed latency, ns (log)")
        ax.legend(loc="upper center", bbox_to_anchor=(0.5, -0.2), ncols=2, fontsize=8)
        save(fig, "pipeline_latency_vs_load.png")


# ---------------------------------------------------------------- main
def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawTextHelpFormatter)
    ap.add_argument("--data-dir", default=Path(__file__).resolve().parent.parent / "data", type=Path)
    ap.add_argument("--no-plots", action="store_true")
    args = ap.parse_args()
    d: Path = args.data_dir

    print(f"Reading benchmark output from {d}")
    gb = (load_gbench(d / "order_book_benchmark.json", "order_book")
          + load_gbench(d / "matching_engine_benchmark.json", "matching_engine")
          + load_gbench(d / "queue_benchmark.json", "queue"))
    cli = load_cli(d / "limitbook_runs.csv")

    if gb:
        out = d / "benchmark_results.csv"
        with out.open("w", newline="") as f:
            w = csv.DictWriter(f, fieldnames=list(gb[0].keys()))
            w.writeheader()
            w.writerows(gb)
        print(f"  wrote {out} ({len(gb)} rows)")

    summarise(gb, cli)
    if not args.no_plots:
        plots(gb, cli, d / "plots")


if __name__ == "__main__":
    main()
