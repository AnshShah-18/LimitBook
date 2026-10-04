# Profiling artifacts

- `callgrind_base.txt`, `callgrind_step1.txt`, `callgrind_step2.txt`: flat callgrind
  profiles of `MatchingEngine::process()` only (`--toggle-collect`), 300K events,
  `--branch-sim=yes`. Columns: Ir = instructions, Bc/Bcm = conditional branches /
  mispredicted, Bi/Bim = indirect branches / mispredicted.
  - base = `MapListOrderBook`
  - step1 = `PooledOrderBook` with `std::lower_bound` level search
  - step2 = `PooledOrderBook` with a linear scan from the best price (current code)
- `ab_step1_vs_step2_*.txt`: interleaved A/B Google Benchmark runs of the step-1 and
  step-2 binaries (`tools/ab_compare.py`).

Reproduce a profile:

    valgrind --tool=callgrind --branch-sim=yes \
      --toggle-collect='limitbook::BasicMatchingEngine<limitbook::PooledOrderBook>::process*' \
      ./limitbook --events 300000 --book pooled
    callgrind_annotate --inclusive=no callgrind.out.<pid>
