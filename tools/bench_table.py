"""One table from a combined benchmark run (tools/bench_views.sh .jsonl files with NMK_BENCH_AB configs).
Per view and preset: the baseline (every optimisation on) GPU P50 / P99 and CPU P99, then what each config changes
against it, and the run-to-run noise (the "#2" repeat of the baseline). Run names are "<view>-<config>".
Usage: python tools/bench_table.py --base "^probecap" --target 15 docs/perf/bench_<tag>_*.jsonl > table.md"""
import argparse
import json


def load(paths):
    rows = []
    for p in paths:
        with open(p, encoding="utf-8") as f:
            rows += [json.loads(l) for l in f if l.strip()]
    return rows


def split(view):
    v, _, cfg = view.partition("-")
    return v, cfg or "none"


def label(cfg, base):
    """What a config changes against the baseline, in words."""
    b = set(x for x in base.split("+") if x and x != "none")
    c = set(x for x in cfg.split("+") if x and x != "none")
    off = sorted(c - b)
    gone = sorted(x[1:] for x in b - c if x.startswith("^"))
    parts = [f"{x} off" if not x.startswith("^") and x not in ("msaa4",) and not x.startswith("scale") else x for x in off if not x.startswith("#")]
    parts += [f"{x} off" for x in gone]
    return ", ".join(parts) or ("repeat" if any(x.startswith("#") for x in off) else "baseline")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--base", default="none")
    ap.add_argument("--target", type=float, default=15.0)
    ap.add_argument("files", nargs="+")
    a = ap.parse_args()
    rows = load(a.files)
    groups = {}
    for r in rows:
        v, cfg = split(r["view"])
        groups.setdefault((r["map"], v, r["preset"], r.get("kind", "views")), {})[cfg] = r
    print(f"| Map | Run | Preset | Baseline GPU P50 / P99 | CPU P99 | vs {a.target:g} ms | Noise (repeat) | Config: GPU P99 change (ms), CPU P99 change |")
    print("|---|---|---|---|---:|---:|---:|---|")
    for (m, v, p, k), cfgs in sorted(groups.items()):
        b = cfgs.get(a.base)
        if not b:
            continue
        g99, g50, c99 = b["gpuMs"]["p99"], b["gpuMs"]["p50"], b.get("cpuNoSwapMs", b.get("cpuFrameMs", {})).get("p99", 0)
        rep = next((r for c, r in cfgs.items() if c != a.base and label(c, a.base) == "repeat"), None)
        noise = f"{abs(rep['gpuMs']['p99'] - g99):.2f}" if rep else "-"
        cells = []
        for c, r in sorted(cfgs.items()):
            if c == a.base or (rep is not None and r is rep):
                continue
            d = r["gpuMs"]["p99"] - g99
            dc = r.get("cpuNoSwapMs", {}).get("p99", 0) - c99
            cells.append(f"{label(c, a.base)}: {d:+.2f}, {dc:+.2f}")
        frame = max(g99, c99)
        print(f"| {m} | {v} | {p} | {g50:.2f} / {g99:.2f} | {c99:.2f} | {frame - a.target:+.2f} | {noise} | {'; '.join(cells)} |")


def selftest():
    assert label("^probecap+viscull", "^probecap") == "viscull off"
    assert label("", "^probecap") == "probecap off"
    assert label("^probecap+#2", "^probecap") == "repeat"
    assert label("^probecap+msaa4", "^probecap") == "msaa4"


if __name__ == "__main__":
    selftest()
    main()
