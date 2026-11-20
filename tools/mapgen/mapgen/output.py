"""Writers: the map file (MapDoc JSON, src/mapdoc.cpp mapToJson), the template (content/templates/<id>.json) with
its heightmap block, and a build summary."""
import json
import os


_TREE_ALIASES = None
_REMOVED = set()
MAP_ALIASES = {}   # FL1: template id -> its own aliases, ahead of the shared tables (tree_aliases.json "maps": the desert maps' plants)
_CUR_MAP = None   # VR1: generated rock ids with no model rock yet (rock_aliases.json "removed"): not written


def tree_alias(aid, iid):
    """TR1: a generated tree id -> one of its model-tree variants (content/settings/tree_aliases.json), picked by the
    instance id exactly as the engine does on load (mapdoc.cpp treeAlias)."""
    global _TREE_ALIASES
    if _TREE_ALIASES is None:
        import json, os
        _TREE_ALIASES = {}
        for f in ("tree_aliases.json", "rock_aliases.json"):   # VR1: the generated rocks too
            p = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "..", "content", "settings", f)
            if os.path.exists(p):
                j = json.load(open(p, encoding="utf-8")); _TREE_ALIASES.update(j.get("aliases", {}))
                for m, al in j.get("maps", {}).items(): MAP_ALIASES.setdefault(m, {}).update(al)   # DF1: per-map tables (tools/fl1_flora.py writes them)
                _REMOVED.update(j.get("removed", []))
    al = MAP_ALIASES.get(_CUR_MAP, {}).get(aid) or _TREE_ALIASES.get(aid)
    if not al:
        return aid
    h = (iid * 0x9E3779B97F4A7C15) & 0xFFFFFFFFFFFFFFFF
    h ^= h >> 29
    return al[h % len(al)]


def instance_json(e, cat):
    aid = tree_alias(e["asset"], e["id"])   # TR1: model trees for the generated ones
    a = cat[aid]
    o = dict(e.get("extra") or {})
    o["id"] = e["id"]
    o["asset"] = {"id": aid, "version": a.version}
    o["position"] = [round(e["pos"][0], 3), round(e["pos"][1], 3), round(e["pos"][2], 3)]
    o["yaw"] = round(e["yaw"], 2)
    o["overrides"] = {"interactables": {}}
    return o


def write_map(path, *, name, template_id, template_version, seed, width, depth, builder, scenario_json,
              explore_start=None, camera=None, bookmarks=None, extra=None):
    global _CUR_MAP
    _CUR_MAP = template_id
    from .lines import pieces_to_lines   # LN2: fence / wire / wall / hedge pieces become lines (as the engine migrates)
    from .lines import trench_pieces_to_lines   # LN1: trench pieces become trench lines
    n_tp, n_tl = trench_pieces_to_lines(builder, scenario_json)
    if n_tp:
        print("trench lines: %d pieces -> %d lines" % (n_tp, n_tl))
    n_pieces, n_lines = pieces_to_lines(builder, scenario_json)
    if n_pieces:
        print("lines: %d pieces -> %d lines" % (n_pieces, n_lines))
    j = dict(extra or {})
    j.update({
        "format": "normandy-mapmaker-map", "schemaVersion": 1, "name": name,
        "template": {"id": template_id, "version": template_version, "seed": seed},
        "mapSize": {"width": width, "depth": depth},
        "nextId": builder.next_id,
        "instances": [instance_json(e, builder.cat) for e in builder.inst if tree_alias(e["asset"], e["id"]) not in _REMOVED],
        "removedTemplateVegetation": [],
        "exploreStart": {"position": [round(v, 3) for v in explore_start[0]], "yaw": explore_start[1]} if explore_start else None,
        "routes": builder.routes,
        "scenario": scenario_json,
    })
    if camera:
        j["editorCamera"] = {"position": camera[0], "yaw": camera[1], "pitch": camera[2]}
        if bookmarks:
            j["editorCamera"]["bookmarks"] = bookmarks
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        json.dump(j, f, separators=(",", ":"))
    return j


def write_template(path, *, tid, name, description, order, version, seed, width, depth, margin, heightmap,
                   water_level, map_file, camera, thumbnail=None, thumb_camera=None, playable=True, extra=None):
    j = {"id": tid, "version": version, "order": order, "name": name, "available": True, "seed": seed,
         "description": description, "width": width, "depth": depth, "margin": margin,
         "camera": {"pos": camera[0], "yaw": camera[1], "pitch": camera[2]},
         "heightmap": heightmap, "waterLevel": water_level, "map": map_file, "playable": playable}
    if thumbnail:
        j["thumbnail"] = thumbnail
    if thumb_camera:
        j["thumbnailCamera"] = {"pos": thumb_camera[0], "yaw": thumb_camera[1], "pitch": thumb_camera[2]}
    if extra:
        j.update(extra)
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        json.dump(j, f, indent=2)
    return j


def write_summary(path, data):
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        json.dump(data, f, indent=2)


def asset_usage(builder):
    use = {}
    for e in builder.inst:
        use[e["asset"]] = use.get(e["asset"], 0) + 1
    return use
