#!/usr/bin/env python3
"""Fetch the curated ambientCG (CC0) texture sets listed in tools/texture_sets.json (docs/textures.md).

Python standard library only. Run it isolated:  $TEMP/nmk/py/python.exe -I tools/fetch_ambientcg.py [options]

  (no flag) / --dry-run   query the official API for each id and print the exact file list and the total 1K-JPG
                          download size. Nothing is downloaded or written.
  --download              download the 1K-JPG zips (only those) into the raw folder, outside the repo
                          (default %TEMP%/nmk/ambientcg_raw), keep Color, NormalGL, Roughness, AmbientOcclusion and
                          Displacement, then pack each set into content/textures/<set>/{color,normal,rma}.jpg + meta.json.
  --pack-only             skip the network: pack from the raw folder that a previous --download filled.
  --only a,b              restrict to these set names (or ambientCG ids).
  --packer EXE            NormandyMapmaker.exe used for the pixel work (`--pack-texture <src> <dst>`): the standard
                          library has no JPEG codec, the engine has stb_image / stb_image_write. Default: the newest
                          build found in %TEMP%/nmk/build-*/NormandyMapmaker.exe.

rma.jpg packs R = roughness, G = ambient occlusion, B = height (displacement), all linear.
"""
import argparse, glob, json, os, shutil, subprocess, sys, time, urllib.parse, urllib.request, zipfile

API = "https://ambientcg.com/api/v2/full_json"
ALLOWED_HOSTS = {"ambientcg.com", "www.ambientcg.com"}
ATTRIBUTE = "1K-JPG"
UA = "NormandyMapmaker-texture-fetch/1.0 (curated CC0 set; tools/fetch_ambientcg.py)"
# map suffix in the zip -> name in the raw folder (the first matching member wins)
MAPS = [("_Color.jpg", "color.jpg"), ("_NormalGL.jpg", "normal.jpg"), ("_Roughness.jpg", "roughness.jpg"),
        ("_AmbientOcclusion.jpg", "ao.jpg"), ("_Displacement.jpg", "height.jpg")]

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def load_sets(path, only):
    with open(path, encoding="utf-8") as f:
        cfg = json.load(f)
    sets = cfg["sets"]
    names = [s["set"] for s in sets]
    if len(set(names)) != len(names):
        sys.exit("texture_sets.json: duplicate set names")
    if only:
        want = {w.strip().lower() for w in only.split(",") if w.strip()}
        sets = [s for s in sets if s["set"].lower() in want or s["id"].lower() in want]
    return cfg, sets


def http_json(url):
    req = urllib.request.Request(url, headers={"User-Agent": UA})
    with urllib.request.urlopen(req, timeout=60) as r:
        return json.load(r)


def query(ids):
    """id -> {link, file, size} for the 1K-JPG zip (None when the asset or the attribute is missing)."""
    out = {}
    for i in range(0, len(ids), 20):
        chunk = ids[i:i + 20]
        url = API + "?" + urllib.parse.urlencode({"include": "downloadData", "id": ",".join(chunk), "limit": "100"})
        d = http_json(url)
        for a in d.get("foundAssets", []):
            folders = a.get("downloadFolders") or {}
            df = folders.get("default", {}) if isinstance(folders, dict) else {}
            cats = df.get("downloadFiletypeCategories") or {}
            z = cats.get("zip", {}) if isinstance(cats, dict) else {}
            for f in z.get("downloads", []) or []:
                if f.get("attribute") == ATTRIBUTE:
                    out[a["assetId"].lower()] = {"id": a["assetId"], "link": f["downloadLink"], "file": f["fileName"], "size": int(f.get("size") or 0)}
        time.sleep(0.3)
    return {k: out.get(k.lower()) for k in ids}


def print_plan(sets, meta):
    total, missing = 0, []
    print(f"{'set':<22} {'ambientCG id':<20} {'family':<20} {'file':<34} {'MB':>6}")
    for s in sets:
        m = meta.get(s["id"])
        if not m:
            missing.append(s["id"]); print(f"{s['set']:<22} {s['id']:<20} {s['family']:<20} {'-- not found --':<34}"); continue
        total += m["size"]
        print(f"{s['set']:<22} {m['id']:<20} {s['family']:<20} {m['file']:<34} {m['size'] / 1e6:6.1f}")
    print(f"\n{len(sets) - len(missing)} zips, {ATTRIBUTE} only, total {total / 1e6:.1f} MB ({total / 2**20:.1f} MiB) from {API.split('/api')[0]}")
    if missing:
        print("missing ids:", ", ".join(missing))
    return total, missing


def download(m, raw):
    u = urllib.parse.urlparse(m["link"])
    if u.scheme != "https" or u.hostname not in ALLOWED_HOSTS:
        raise RuntimeError(f"refusing download from {m['link']}")
    dst = os.path.join(raw, os.path.basename(m["file"]))
    if os.path.exists(dst) and (not m["size"] or os.path.getsize(dst) == m["size"]):
        return dst
    tmp = dst + ".part"
    req = urllib.request.Request(m["link"], headers={"User-Agent": UA})
    with urllib.request.urlopen(req, timeout=120) as r, open(tmp, "wb") as f:
        shutil.copyfileobj(r, f, 1 << 20)
    if m["size"] and os.path.getsize(tmp) != m["size"]:
        os.remove(tmp); raise RuntimeError(f"{m['file']}: size {os.path.getsize(tmp)} != {m['size']}")
    os.replace(tmp, dst)
    return dst


def extract(zpath, dst):
    """Copy the five maps out by suffix (no extractall: member paths are never used as output paths)."""
    os.makedirs(dst, exist_ok=True)
    got = []
    with zipfile.ZipFile(zpath) as z:
        names = z.namelist()
        for suf, out in MAPS:
            mem = next((n for n in names if n.endswith(suf) and "/" not in n.strip("/")), None) or next((n for n in names if n.endswith(suf)), None)
            if mem is None:
                continue
            with z.open(mem) as src, open(os.path.join(dst, out), "wb") as f:
                shutil.copyfileobj(src, f)
            got.append(out)
    return got


def find_packer(arg):
    if arg:
        return arg
    tmp = os.environ.get("TEMP") or os.environ.get("TMP") or "."
    c = glob.glob(os.path.join(tmp, "nmk", "build-*", "NormandyMapmaker.exe"))
    return max(c, key=os.path.getmtime) if c else None


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--sets", default=os.path.join(ROOT, "tools", "texture_sets.json"))
    ap.add_argument("--out", default=os.path.join(ROOT, "content", "textures"))
    ap.add_argument("--raw", default=os.path.join(os.environ.get("TEMP", "."), "nmk", "ambientcg_raw"))
    ap.add_argument("--only", default="")
    ap.add_argument("--packer", default="")
    g = ap.add_mutually_exclusive_group()
    g.add_argument("--dry-run", action="store_true")
    g.add_argument("--download", action="store_true")
    g.add_argument("--pack-only", action="store_true")
    a = ap.parse_args()

    cfg, sets = load_sets(a.sets, a.only)
    if not sets:
        sys.exit("no sets selected")
    raw = os.path.abspath(a.raw)
    if os.path.commonpath([raw, ROOT]) == ROOT:
        sys.exit("the raw folder must be outside the repository")

    meta = {}
    if not a.pack_only:
        meta = query([s["id"] for s in sets])
        total, missing = print_plan(sets, meta)
        if not (a.download):
            return 0
        if missing:
            sys.exit("some ids were not found; fix texture_sets.json first")

    packer = find_packer(a.packer)
    if not packer or not os.path.exists(packer):
        sys.exit("no packer: build NormandyMapmaker.exe or pass --packer")
    os.makedirs(raw, exist_ok=True)
    failed = []
    for s in sets:
        src = os.path.join(raw, s["id"])
        try:
            if not a.pack_only:
                z = download(meta[s["id"]], raw)
                got = extract(z, src)
                print(f"{s['id']}: {', '.join(got)}")
            if not os.path.exists(os.path.join(src, "color.jpg")):
                raise RuntimeError("no color map in the raw folder")
            dst = os.path.join(a.out, s["set"])
            os.makedirs(dst, exist_ok=True)
            r = subprocess.run([packer, "--pack-texture", src, dst], capture_output=True, text=True)
            if r.returncode != 0:
                raise RuntimeError(f"packer failed: {r.stdout[-400:]} {r.stderr[-400:]}")
            info = {"set": s["set"], "id": s["id"], "family": s["family"], "size": s["size"],
                    "source": f"https://ambientcg.com/a/{s['id']}", "license": cfg.get("license", "CC0 1.0 Universal"),
                    "credit": "ambientCG.com (CC0)", "resolution": ATTRIBUTE,
                    "maps": sorted(os.listdir(src))}
            with open(os.path.join(dst, "meta.json"), "w", encoding="utf-8") as f:
                json.dump(info, f, indent=1)
            print(f"  -> {os.path.relpath(dst, ROOT)}")
        except Exception as e:   # keep going: one bad set falls back to the procedural pattern in game
            failed.append(s["set"]); print(f"{s['set']} ({s['id']}): FAILED {e}")
    print(f"\npacked {len(sets) - len(failed)}/{len(sets)} sets into {os.path.relpath(a.out, ROOT)}")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
