#!/usr/bin/env bash
# Runs every benchmark and the CLI sweeps, writing raw results to data/.
# Usage: scripts/run_benchmarks.sh [build-dir]     (default: build)
set -euo pipefail
cd "$(dirname "$0")/.."
B=${1:-build}
mkdir -p data
# Shared/virtual machines drift; many interleaved repetitions + median keeps that honest.
REPS="--benchmark_repetitions=10 --benchmark_enable_random_interleaving=true"
AGG=--benchmark_report_aggregates_only=true

for bench in order_book_benchmark matching_engine_benchmark queue_benchmark; do
  echo "== $bench"
  "$B/$bench" $REPS $AGG --benchmark_out="data/$bench.json" --benchmark_out_format=json
done

echo "== aquila CLI sweeps -> data/aquila_runs.csv"
rm -f data/aquila_runs.csv
for book in baseline pooled; do
  for n in 10000 100000 1000000; do
    "$B/aquila" --events "$n" --book "$book" --csv data/aquila_runs.csv > /dev/null
  done
done
# Two-thread runs are sensitive to scheduling, so each configuration runs 3 times;
# the analysis script reports the median run.
for rep in 1 2 3; do
  for mode in mutex spsc; do
    "$B/aquila" --events 1000000 --mode "$mode" --csv data/aquila_runs.csv > /dev/null   # saturated
    for rate in 250000 500000 1000000 2000000 4000000; do                              # paced
      "$B/aquila" --events 1000000 --mode "$mode" --rate "$rate" --csv data/aquila_runs.csv > /dev/null
    done
  done
done
"$B/alloc_profile" 1000000 | tee data/alloc_profile.txt
echo "done"
