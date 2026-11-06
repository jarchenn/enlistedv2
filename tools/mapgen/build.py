"""Build a default map:  <python> -I tools/mapgen/build.py <map> [--content DIR] [--out DIR]

<map> is a module in tools/mapgen/maps/ (omaha_easy_red, mont_castre, ...) exposing build(ctx). It writes
content/templates/<id>.json, <id>.height.png and <id>.nmap.json, and prints a summary (also written to
tools/mapgen/out/<id>.summary.json, not committed)."""
import argparse
import importlib
import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

from mapgen import Catalog  # noqa: E402


class Ctx:
    def __init__(self, repo, content, out):
        self.repo, self.content, self.out = repo, content, out
        self.templates = os.path.join(content, "templates")
        self.catalog = Catalog(content)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("map")
    repo = os.path.normpath(os.path.join(HERE, "..", ".."))
    ap.add_argument("--content", default=os.path.join(repo, "content"))
    ap.add_argument("--out", default=os.path.join(HERE, "out"))
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    mod = importlib.import_module("maps." + a.map)
    t = time.time()
    mod.build(Ctx(repo, a.content, a.out))
    print("built %s in %.1f s" % (a.map, time.time() - t))


if __name__ == "__main__":
    main()
