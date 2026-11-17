"""PR1: download Poly Haven models (CC0) for the close-up props, with the user's go-ahead.

For each id: the public API's file list (https://api.polyhaven.com/files/<id>), the glTF at the chosen resolution with
its buffers and textures, into <dest>/<id>/ (each model in its own folder: downloads are data, opened only by Blender
background scripts). Metadata (name, authors, categories, dimensions) from https://api.polyhaven.com/info/<id>.
Already-downloaded files are kept (cached); one request at a time with a pause between them. A model whose download
fails is recorded as failed in the manifest, never replaced silently.

    python -I tools/props/fetch_polyhaven.py <dest dir> <manifest.json> <res: 1k|2k> <id> [<id> ...]
"""
import json, os, sys, time, urllib.request

UA = {"User-Agent": "NormandyMapmaker-asset-fetch/1.0"}


def get(url, binary=False, tries=3):
    for k in range(tries):
        try:
            with urllib.request.urlopen(urllib.request.Request(url, headers=UA), timeout=120) as r:
                d = r.read()
                return d if binary else json.loads(d.decode("utf-8"))
        except Exception as e:  # noqa: BLE001 - network: retry, then report
            err = e
            time.sleep(2 + 3 * k)
    raise RuntimeError("%s: %s" % (url, err))


def main():
    dest, man, res, ids = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4:]
    manifest = {}
    if os.path.exists(man):
        with open(man, encoding="utf-8") as f: manifest = json.load(f)
    for pid in ids:
        rec = {"id": pid, "url": "https://polyhaven.com/a/" + pid, "licence": "CC0 1.0", "res": res}
        try:
            info = get("https://api.polyhaven.com/info/" + pid); time.sleep(0.4)
            rec.update({"name": info.get("name"), "authors": sorted((info.get("authors") or {}).keys()), "categories": info.get("categories"),
                        "dimensions_mm": info.get("dimensions"), "polycount": info.get("polycount")})
            files = get("https://api.polyhaven.com/files/" + pid); time.sleep(0.4)
            g = files["gltf"].get(res) or files["gltf"].get("1k")
            entry = g["gltf"]; todo = [(os.path.basename(entry["url"]), entry["url"], entry.get("size", 0))]
            todo += [(rel, v["url"], v.get("size", 0)) for rel, v in (entry.get("include") or g.get("include") or {}).items()]
            d = os.path.join(dest, pid); total = 0
            for rel, url, size in todo:
                p = os.path.normpath(os.path.join(d, rel))
                if not p.startswith(os.path.normpath(d)): raise RuntimeError("path escapes the folder: " + rel)
                os.makedirs(os.path.dirname(p), exist_ok=True)
                if not (os.path.exists(p) and size and os.path.getsize(p) == size):
                    data = get(url, binary=True); time.sleep(0.3)
                    with open(p, "wb") as f: f.write(data)
                total += os.path.getsize(p)
            rec.update({"status": "ok", "gltf": os.path.basename(entry["url"]), "files": len(todo), "bytes": total})
            print("ok    %-34s %3d files %7.1f MB  %s" % (pid, len(todo), total / 1e6, ", ".join(rec["authors"])))
        except Exception as e:  # noqa: BLE001
            rec.update({"status": "failed", "error": str(e)[:300]})
            print("FAIL  %-34s %s" % (pid, str(e)[:200]))
        manifest[pid] = rec
        with open(man, "w", encoding="utf-8") as f: json.dump(manifest, f, indent=1, ensure_ascii=False)


if __name__ == "__main__":
    main()
