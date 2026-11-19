"""TX2K: Poly Haven (CC0) texture sets at 2K, packed like tools/fetch_ambientcg.py does (docs/textures.md).

For each "<set>=<polyhaven id>": the 2k JPGs of Diffuse, nor_gl, Rough, AO and Displacement from the public files API
(https://api.polyhaven.com/files/<id>), into <raw>/<id>/ under the packer's names, then
`<packer> --pack-texture <raw>/<id> content/textures/<set> 2048`; the set's meta.json keeps its size and family.
Run it isolated:  python -I tools/fetch_polyhaven_tex.py <raw dir outside the repo> <packer exe> <set>=<id> [...]
"""
import json, os, subprocess, sys, time, urllib.parse, urllib.request

UA = {"User-Agent": "NormandyMapmaker-texture-fetch/1.0"}
MAPS = [("Diffuse", "color.jpg"), ("nor_gl", "normal.jpg"), ("Rough", "roughness.jpg"), ("AO", "ao.jpg"), ("Displacement", "height.jpg")]
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def get(url, binary=False):
    u = urllib.parse.urlparse(url)
    if u.scheme != "https" or not (u.hostname or "").endswith("polyhaven.org") and u.hostname != "api.polyhaven.com":
        raise RuntimeError("refusing " + url)
    with urllib.request.urlopen(urllib.request.Request(url, headers=UA), timeout=120) as r:
        d = r.read()
    time.sleep(0.4)
    return d if binary else json.loads(d.decode("utf-8"))


def main():
    raw, packer, pairs = os.path.abspath(sys.argv[1]), sys.argv[2], sys.argv[3:]
    if os.path.splitdrive(raw)[0].lower() == os.path.splitdrive(ROOT)[0].lower() and os.path.commonpath([raw, ROOT]) == ROOT:
        sys.exit("the raw folder must be outside the repository")
    bad = 0
    for p in pairs:
        name, pid = p.split("=", 1)
        src, dst = os.path.join(raw, pid), os.path.join(ROOT, "content", "textures", name)
        os.makedirs(src, exist_ok=True)
        files = get("https://api.polyhaven.com/files/" + pid)
        got = []
        for key, out in MAPS:
            f = (files.get(key) or {}).get("2k", {}).get("jpg")
            if not f:
                continue
            q = os.path.join(src, out)
            if not (os.path.exists(q) and os.path.getsize(q) == f.get("size", -1)):
                with open(q, "wb") as fh:
                    fh.write(get(f["url"], True))
            got.append(out)
        if "color.jpg" not in got:
            print(f"{name} ({pid}): FAILED no 2k Diffuse jpg"); bad += 1; continue
        r = subprocess.run([packer, "--pack-texture", src, dst, "2048"], capture_output=True, text=True)
        if r.returncode != 0:
            print(f"{name}: packer FAILED {r.stdout[-300:]}"); bad += 1; continue
        mp = os.path.join(dst, "meta.json")
        m = json.load(open(mp, encoding="utf-8")) if os.path.exists(mp) else {"set": name, "id": pid, "source": "https://polyhaven.com/a/" + pid,
                                                                               "license": "CC0 1.0 Universal", "credit": "Poly Haven (CC0)", "size": [2.0, 2.0]}
        m["resolution"] = "2K (Poly Haven 2k JPG release; OpenGL normals)"
        m["maps"] = sorted(got)
        with open(mp, "w", encoding="utf-8") as fh:
            json.dump(m, fh, indent=1)
        print(f"{name} ({pid}): {', '.join(got)} -> {os.path.relpath(dst, ROOT)}")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
