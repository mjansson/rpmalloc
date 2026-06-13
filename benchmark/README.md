# Benchmark results and graphs

This directory holds the captured benchmark results and the script that turns them into the
graphs shown in the [README](../README.md) and [BENCHMARKS](../BENCHMARKS.md) documents.

- `results/rptest-threads.csv` - rptest throughput sweep (random `[16, 8000]` bytes, linear
  falloff, cross-thread frees) for 1 to 16 threads, one row per allocator and thread count.
  The exact rptest command is recorded as a comment on the first line.
- `results/mimalloc-bench-allt.csv` - the [mimalloc-bench](https://github.com/daanx/mimalloc-bench)
  `allt` suite results, in the mimalloc-bench `benchres.csv` format
  (`benchmark allocator elapsed rss user sys page-faults page-reclaims`).
- `images/` - the generated PNG graphs.
- `plot.py` - regenerates the graphs from the result files. Requires `matplotlib`.

Both result files are complete and unfiltered. For readability the graphs omit allocators
that are more than 4x slower than rpmalloc on a given benchmark, and drop allocators that
failed many benchmarks entirely; this filtering happens in `plot.py`, not in the data. In the
`allt` data a benchmark an allocator crashed on is recorded with a zero or blank elapsed time,
or with zero user+sys CPU time when it died before doing any work; `plot.py` treats both as
failures rather than instant results.

```
python3 plot.py
```

See [BENCHMARKS.md](../BENCHMARKS.md) for the machine configuration, the allocator list and
versions, and instructions for regenerating the underlying data.
