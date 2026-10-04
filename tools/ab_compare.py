#!/usr/bin/env python3
"""Interleaved A/B comparison of two Google Benchmark binaries.

Shared or virtualised machines drift (frequency scaling, noisy neighbours), so
running "all of A, then all of B" can attribute drift to the code change. This
script alternates A and B for several rounds and reports the median, min and max
throughput per benchmark for each binary.

    python3 tools/ab_compare.py OLD_BINARY NEW_BINARY 'BM_Replay<' 7
"""
import collections
import json
import statistics
import subprocess
import sys


def main() -> None:
    if len(sys.argv) != 5:
        sys.exit(__doc__)
    bins = [("A", sys.argv[1]), ("B", sys.argv[2])]
    filt, rounds = sys.argv[3], int(sys.argv[4])
    print(f"A = {bins[0][1]}\nB = {bins[1][1]}\nfilter = {filt!r}, rounds = {rounds}\n")

    results = collections.defaultdict(list)
    for r in range(rounds):
        order = bins if r % 2 == 0 else bins[::-1]  # alternate who runs first
        for tag, binary in order:
            out = subprocess.run([binary, f"--benchmark_filter={filt}", "--benchmark_format=json"],
                                 capture_output=True, text=True, check=True).stdout
            for bm in json.loads(out)["benchmarks"]:
                rate = bm.get("items_per_second") or 1e9 / bm["real_time"]
                results[(bm["name"], tag)].append(rate)

    for (name, tag), v in sorted(results.items()):
        print(f"{name:<48} {tag}  median {statistics.median(v) / 1e6:8.2f} M/s"
              f"   min {min(v) / 1e6:7.2f}  max {max(v) / 1e6:7.2f}")


if __name__ == "__main__":
    main()
