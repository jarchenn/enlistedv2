"""Writers: the map file (MapDoc JSON, src/mapdoc.cpp mapToJson), the template (content/templates/<id>.json) with
its heightmap block, and a build summary."""
import json
import os


def instance_json(e, cat):
    a = cat[e["asset"]]
    o = dict(e.get("extra") or {})
    o["id"] = e["id"]
    o["asset"] = {"id": e["asset"], "version": a.version}
    o["position"] = [round(e["pos"][0], 3), round(e["pos"][1], 3), round(e["pos"][2], 3)]
    o["yaw"] = round(e["yaw"], 2)
    o["overrides"] = {"interactables": {}}
    return o


def write_map(path, *, name, template_id, template_version, seed, width, depth, builder, scenario_json,
              explore_start=None, camera=None, bookmarks=None, extra=None):
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
        "instances": [instance_json(e, builder.cat) for e in builder.inst],
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
