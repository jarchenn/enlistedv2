"""BF rev 3 phase 0b licence gate (spec 2.1, 9 phase 0b): no reference-only (non-commercial or unverified) source file
reaches shipping content. content/licences.json lists the third-party source files; for every entry with
"shippable": false this fails when
  1. a file under content/ has the same bytes (size pre-filter, then SHA-256), whatever it is called, or
  2. a text file under content/ (json, mjs, txt, md, glsl) names it (the file name or its stem, case-insensitive),
     outside content/licences.json itself.
Exit code 0 = clean, 1 = violations (listed), 2 = bad record. Run before a build or in CI:
  python -I tools/licence_gate.py            (from the repo root)
  python -I tools/licence_gate.py --selftest (plants a fake violation in a temp copy of the check and expects it)"""
import hashlib
import json
import os
import sys
import tempfile

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
TEXT = (".json", ".mjs", ".js", ".txt", ".md", ".glsl", ".csv")


def sha256(p):
    h = hashlib.sha256()
    with open(p, "rb") as f:
        for b in iter(lambda: f.read(1 << 20), b""):
            h.update(b)
    return h.hexdigest()


def gate(content, record):
    try:
        files = json.load(open(record, encoding="utf-8"))["files"]
        banned = [f for f in files if f["shippable"] is False]
        assert all(len(f["sha256"]) == 64 and f["bytes"] > 0 and f["file"] for f in banned)
    except (OSError, ValueError, KeyError, AssertionError) as e:
        print("licence_gate: bad record %s: %s" % (record, e))
        return 2
    sizes = {f["bytes"]: f for f in banned}
    names = [(f["file"].lower(), os.path.splitext(f["file"])[0].lower(), f) for f in banned]
    bad = []
    for d, _, fs in os.walk(content):
        for n in fs:
            p = os.path.join(d, n)
            if os.path.normcase(os.path.abspath(p)) == os.path.normcase(os.path.abspath(record)):
                continue
            try:
                sz = os.path.getsize(p)
            except OSError:
                continue
            f = sizes.get(sz)
            if f and sha256(p) == f["sha256"]:
                bad.append("%s is a copy of %s (%s)" % (os.path.relpath(p, content), f["file"], f["licence"]))
            if n.lower().endswith(TEXT) and sz < 64 << 20:
                t = open(p, encoding="utf-8", errors="ignore").read().lower()
                for full, stem, f in names:
                    if full in t or stem in t:
                        bad.append("%s names %s (%s)" % (os.path.relpath(p, content), f["file"], f["licence"]))
                        break
    for b in bad:
        print("licence_gate: " + b)
    print("licence_gate: %d reference-only files, %s" % (len(banned), "%d violation(s)" % len(bad) if bad else "clean"))
    return 1 if bad else 0


def selftest():
    with tempfile.TemporaryDirectory() as t:
        rec = os.path.join(t, "licences.json")
        blob = b"not really a glb" * 7
        json.dump({"files": [{"file": "nc_thing.glb", "sha256": hashlib.sha256(blob).hexdigest(), "bytes": len(blob), "licence": "CC-BY-NC-4.0", "shippable": False}]}, open(rec, "w"))
        assert gate(t, rec) == 0, "clean tree must pass"
        open(os.path.join(t, "renamed.bin"), "wb").write(blob)
        assert gate(t, rec) == 1, "a renamed copy must fail"
        os.remove(os.path.join(t, "renamed.bin"))
        json.dump({"model": "models/NC_Thing.glb"}, open(os.path.join(t, "asset.json"), "w"))
        assert gate(t, rec) == 1, "a reference by name must fail"
    print("licence_gate selftest: ok")
    return 0


if __name__ == "__main__":
    if "--selftest" in sys.argv:
        sys.exit(selftest())
    c = os.path.join(ROOT, "content")
    sys.exit(gate(c, os.path.join(c, "licences.json")))
