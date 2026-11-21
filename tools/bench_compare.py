"""Compare two benchmark runs (tools/bench_views.sh .jsonl files) by the corpus's regression rule (01 s8.3):
a metric regressed when  new > base * (1 + threshold)  and  new - base > 3 * sigma,  sigma being the spread of the
base run's laps (needs path / bots runs, which record per-lap values). Thresholds: GPU P50 3 %, GPU P99 and CPU P99 8 %.
Usage: python tools/bench_compare.py docs/perf/bench_<base>.jsonl docs/perf/bench_<new>.jsonl
Prints a Markdown table; exits 1 when anything regressed."""
import json
import statistics
import sys

METRICS = [("GPU P50", lambda r: r["gpuMs"]["p50"], "gpuP50PerLap", 0.03),
           ("GPU P99", lambda r: r["gpuMs"]["p99"], "gpuP99PerLap", 0.08),
           ("CPU P99", lambda r: r["cpuNoSwapMs"]["p99"], "cpuP99PerLap", 0.08)]


def load(path):
    out = {}
    with open(path, encoding="utf-8") as f:
        for line in f:
            if line.strip():
                r = json.loads(line)
                out[(r.get("kind", "views"), r["map"], r["view"], r["preset"])] = r
    return out


def verdict(base, new, laps, thr):
    sigma = statistics.pstdev(laps) if laps and len(laps) > 1 else 0.0
    if new > base * (1 + thr) and new - base > 3 * sigma:
        return "regressed", sigma
    if new < base * (1 - thr) and base - new > 3 * sigma:
        return "faster", sigma
    return "", sigma


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        return 2
    base, new = load(sys.argv[1]), load(sys.argv[2])
    worse = 0
    print("| Map | Run | Preset | Metric | Base | New | Change | 3 sigma (base laps) | Verdict |")
    print("|---|---|---|---|---:|---:|---:|---:|---|")
    for key in sorted(set(base) & set(new)):
        b, n = base[key], new[key]
        for name, get, lapKey, thr in METRICS:
            bv, nv = get(b), get(n)
            v, sigma = verdict(bv, nv, b.get(lapKey, []), thr)
            worse += v == "regressed"
            change = (nv - bv) / bv * 100 if bv else 0.0
            print(f"| {key[1]} | {key[2]} | {key[3]} | {name} | {bv:.2f} | {nv:.2f} | {change:+.1f} % | {3 * sigma:.2f} | {v} |")
    missing = sorted(set(base) ^ set(new))
    if missing:
        print("\nIn one run only: " + ", ".join(" ".join(k[1:]) for k in missing))
    return 1 if worse else 0


def selftest():
    assert verdict(10.0, 11.0, [10.0, 10.1, 9.9], 0.08)[0] == "regressed"   # +10 %, sigma 0.08
    assert verdict(10.0, 10.5, [10.0, 10.1, 9.9], 0.08)[0] == ""            # under the threshold
    assert verdict(10.0, 11.0, [8.0, 10.0, 12.0], 0.08)[0] == ""            # over the threshold but inside 3 sigma
    assert verdict(10.0, 8.0, [10.0, 10.1, 9.9], 0.08)[0] == "faster"


if __name__ == "__main__":
    selftest()
    sys.exit(main())
