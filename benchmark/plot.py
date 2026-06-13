#!/usr/bin/env python3
# Regenerate the benchmark graphs in benchmark/images from the captured
# result files in benchmark/results. Requires matplotlib.
#
#   results/rptest-threads.csv       rptest thread sweep (see README.md)
#   results/mimalloc-bench-allt.csv  mimalloc-bench 'allt' suite results
#
# Allocators that are more than SLOW_FACTOR times slower than rpmalloc on a
# given test (or, for the rptest sweep, on average across thread counts) are
# omitted from the graphs to keep them readable. The captured result files
# always contain the complete unfiltered data.
#
# Usage: python3 plot.py

import csv
import math
import os

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt

SLOW_FACTOR = 4.0
BASELINE = "rp"

basedir = os.path.dirname(os.path.abspath(__file__))
resultdir = os.path.join(basedir, "results")
imagedir = os.path.join(basedir, "images")
os.makedirs(imagedir, exist_ok=True)

# Consistent ordering and styling, rpmalloc drawn last (on top) with emphasis
ALLOC_ORDER = [
    "sys", "rp", "mi", "mi2", "mi3", "je", "tc", "sn", "hd", "tbb", "sm",
    "scudo", "lt", "mng", "iso", "sn-sec", "hml", "hm", "ff", "gd", "sg",
    "fg", "lf", "lp", "mesh", "nomesh", "yal", "rmalloc",
]

ALLOC_LABEL = {
    "sys": "glibc",
    "rp": "rpmalloc",
    "mi": "mimalloc 1",
    "mi2": "mimalloc 2",
    "mi3": "mimalloc 3",
    "je": "jemalloc",
    "tc": "tcmalloc",
    "sn": "snmalloc",
    "hd": "hoard",
    "tbb": "tbbmalloc",
    "sm": "supermalloc",
    "scudo": "scudo",
    "lt": "ltalloc",
    "mng": "mallocng",
    "iso": "isoalloc",
}


def alloc_label(name):
    return ALLOC_LABEL.get(name, name)


def alloc_sort_key(name):
    return (ALLOC_ORDER.index(name) if name in ALLOC_ORDER else 1000, name)


def parse_time(text):
    parts = text.split(":")
    seconds = 0.0
    for part in parts:
        seconds = seconds * 60.0 + float(part)
    return seconds


# --------------------------------------------------------------------
# rptest thread sweep graphs
# --------------------------------------------------------------------

def plot_rptest():
    path = os.path.join(resultdir, "rptest-threads.csv")
    series = {}
    with open(path) as f:
        lines = [l for l in f if not l.startswith("#")]
    for row in csv.DictReader(lines):
        if row["mops_per_cpu_second"]:
            entry = series.setdefault(row["allocator"], {})
            entry[int(row["threads"])] = (
                int(row["mops_per_cpu_second"]),
                int(row["peak_mib"]),
            )

    # Drop allocators whose mean throughput across thread counts is below the
    # baseline / SLOW_FACTOR (i.e. on average more than SLOW_FACTOR times slower)
    def mean_mops(name):
        values = [v[0] for v in series[name].values()]
        return sum(values) / len(values) if values else 0.0

    baseline_mops = mean_mops(BASELINE) if BASELINE in series else 0.0
    threshold = baseline_mops / SLOW_FACTOR
    dropped = [n for n in series if n != BASELINE and mean_mops(n) < threshold]
    for name in dropped:
        del series[name]
    if dropped:
        print("rptest: omitted (>{:g}x slower than {}): {}".format(
            SLOW_FACTOR, BASELINE, ", ".join(sorted(dropped))))

    allocs = sorted(series.keys(), key=alloc_sort_key)
    cmap = plt.get_cmap("tab20")
    colors = {name: cmap(i % 20) for i, name in enumerate(allocs)}
    markers = ["o", "s", "D", "^", "v", "<", ">", "p", "*", "X", "P", "h", "8", "d", "+"]

    for index, ylabel, title, image in (
        (0, "memory ops / CPU second", "rptest performance", "rptest-perf.png"),
        (1, "peak resident memory (MiB)", "rptest peak memory", "rptest-memory.png"),
    ):
        fig, ax = plt.subplots(figsize=(10, 6.5))
        for i, name in enumerate(allocs):
            threads = sorted(series[name].keys())
            values = [series[name][t][index] for t in threads]
            emphasize = name == "rp"
            ax.plot(
                threads,
                values,
                label=alloc_label(name),
                color="black" if emphasize else colors[name],
                marker=markers[i % len(markers)],
                markersize=5 if emphasize else 4,
                linewidth=2.5 if emphasize else 1.2,
                zorder=10 if emphasize else 2,
            )
        ax.set_title(
            title + "\nrandom size [16,8000] linear falloff, cross-thread frees, higher is better"
            if index == 0
            else title + "\nrandom size [16,8000] linear falloff, cross-thread frees, lower is better"
        )
        ax.set_xlabel("threads")
        ax.set_ylabel(ylabel)
        ax.set_xticks(range(1, 17))
        ax.set_xlim(0.5, 16.5)
        ax.set_ylim(bottom=0)
        ax.grid(True, alpha=0.3)
        ax.legend(loc="upper left", bbox_to_anchor=(1.01, 1.0), fontsize=9)
        fig.tight_layout()
        fig.savefig(os.path.join(imagedir, image), dpi=150)
        plt.close(fig)


# --------------------------------------------------------------------
# mimalloc-bench allt graphs
# --------------------------------------------------------------------

def read_allt():
    path = os.path.join(resultdir, "mimalloc-bench-allt.csv")
    results = {}  # test -> alloc -> (time, rss_mib)
    with open(path) as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            fields = line.split()
            if len(fields) < 4:
                continue
            test, alloc, time_text, rss_text = fields[0], fields[1], fields[2], fields[3]
            try:
                seconds = parse_time(time_text)
                rss = float(rss_text) / 1024.0
            except ValueError:
                continue
            results.setdefault(test, {})[alloc] = (seconds, rss)
    return results


def filter_slow(results):
    # Drop, per test, any allocator more than SLOW_FACTOR times slower than the
    # baseline on that test. The baseline itself is always kept.
    dropped = []
    for test, entries in results.items():
        if BASELINE not in entries:
            continue
        limit = entries[BASELINE][0] * SLOW_FACTOR
        for alloc in list(entries.keys()):
            if alloc != BASELINE and entries[alloc][0] > limit:
                dropped.append("{}/{}".format(test, alloc))
                del entries[alloc]
    if dropped:
        print("allt: omitted (>{:g}x slower than {}): {}".format(
            SLOW_FACTOR, BASELINE, ", ".join(sorted(dropped))))
    return results


def plot_allt_grid(results, index, fmt, title, image):
    tests = sorted(results.keys())
    cols = 3
    rows = (len(tests) + cols - 1) // cols
    fig, axes = plt.subplots(rows, cols, figsize=(15, 3.2 * rows))
    for iplot, test in enumerate(tests):
        ax = axes[iplot // cols][iplot % cols]
        entries = sorted(results[test].items(), key=lambda kv: kv[1][index])
        names = [alloc_label(name) for name, _ in entries]
        values = [value[index] for _, value in entries]
        bar_colors = ["#202020" if name == "rp" else "#5a9bd4" for name, _ in entries]
        ax.barh(range(len(names)), values, color=bar_colors)
        ax.set_yticks(range(len(names)))
        ax.set_yticklabels(names, fontsize=7)
        ax.invert_yaxis()
        ax.set_title(test, fontsize=10)
        ax.tick_params(axis="x", labelsize=7)
        ax.grid(True, axis="x", alpha=0.3)
        for ibar, value in enumerate(values):
            ax.text(value, ibar, " " + fmt.format(value), va="center", fontsize=6)
    for iplot in range(len(tests), rows * cols):
        axes[iplot // cols][iplot % cols].axis("off")
    fig.suptitle(title, fontsize=13)
    fig.tight_layout(rect=(0, 0, 1, 0.99))
    fig.savefig(os.path.join(imagedir, image), dpi=150)
    plt.close(fig)


def plot_allt_summary(results):
    # Geometric mean over all tests of the per-test result normalized to the
    # best (lowest) result for that test. Uses the unfiltered data and ranks
    # every allocator that completed all tests, but caps each per-test ratio at
    # SLOW_FACTOR so a single pathological test cannot dominate the mean - this
    # embodies the same "ignore anything beyond SLOW_FACTOR" rule as the grids
    # while keeping every allocator in a single fair head-to-head. Allocators
    # that crashed on one or more tests are excluded (listed at run time).
    tests = list(results.keys())
    test_count = len(tests)
    allocs = {a for entries in results.values() for a in entries}
    complete = sorted((a for a in allocs if all(a in results[t] for t in tests)), key=alloc_sort_key)
    excluded = sorted(a for a in allocs if a not in complete)
    if excluded:
        print("allt summary: excluded (incomplete test coverage): " + ", ".join(excluded))
    norm = {alloc: ([], []) for alloc in complete}
    for test in tests:
        entries = results[test]
        best_time = min(entries[a][0] for a in complete)
        best_rss = min(entries[a][1] for a in complete)
        for alloc in complete:
            seconds, rss = entries[alloc]
            norm[alloc][0].append(min(max(seconds, 0.001) / max(best_time, 0.001), SLOW_FACTOR))
            norm[alloc][1].append(min(max(rss, 0.001) / max(best_rss, 0.001), SLOW_FACTOR))

    def geomean(values):
        return math.exp(sum(math.log(v) for v in values) / len(values))

    fig, axes = plt.subplots(1, 2, figsize=(14, 0.35 * len(norm) + 2))
    for index, (ax, title) in enumerate(
        zip(axes, ("time (normalized to best, geometric mean)", "rss (normalized to best, geometric mean)"))
    ):
        entries = []
        for alloc, values in norm.items():
            entries.append((geomean(values[index]), alloc_label(alloc), alloc))
        entries.sort()
        bar_colors = ["#202020" if alloc == "rp" else "#5a9bd4" for _, _, alloc in entries]
        ax.barh(range(len(entries)), [value for value, _, _ in entries], color=bar_colors)
        ax.set_yticks(range(len(entries)))
        ax.set_yticklabels([label for _, label, _ in entries], fontsize=8)
        ax.invert_yaxis()
        ax.set_title(title, fontsize=11)
        ax.grid(True, axis="x", alpha=0.3)
        for ibar, (value, _, _) in enumerate(entries):
            ax.text(value, ibar, " {:.2f}".format(value), va="center", fontsize=7)
    fig.suptitle(
        "mimalloc-bench allt summary, geometric mean over {} tests normalized to best (lower is better)\n"
        "per-test ratio capped at {:g}x".format(test_count, SLOW_FACTOR),
        fontsize=12,
    )
    fig.tight_layout(rect=(0, 0, 1, 0.96))
    fig.savefig(os.path.join(imagedir, "allt-summary.png"), dpi=150)
    plt.close(fig)


if __name__ == "__main__":
    plot_rptest()
    # Summary uses the complete unfiltered data (with per-test capping); the
    # per-test grids drop allocators beyond SLOW_FACTOR for readability.
    plot_allt_summary(read_allt())
    results = filter_slow(read_allt())
    plot_allt_grid(results, 0, "{:.2f}", "mimalloc-bench allt, elapsed time in seconds (lower is better)", "allt-time.png")
    plot_allt_grid(results, 1, "{:.0f}", "mimalloc-bench allt, peak resident memory in MiB (lower is better)", "allt-rss.png")
    print("graphs written to", imagedir)
