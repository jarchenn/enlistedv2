# BC2: put a converted character's textures back from its original GLB, material by material (names matched without
# Blender's ".001" suffix). convert_enemy_glb.py keeps only base colour + normal, at most 1024 px, JPEG, opaque; this:
# - base colour and normal: the reference's own images (full resolution, PNG as shipped); a normal the bake dropped is added
# - metallic / roughness factors: the reference's; the game clamps factors to cloth (metal <= 0.05, rough >= 0.75) unless
#   the material has a metallicRoughness texture, so a 4x4 white one is attached where the factors leave that range
# - alpha: a reference material that blends and whose base image carries alpha (hair cards, lashes) becomes MASK 0.5;
#   the rest OPAQUE (the body's BLEND is an exporter default: its opacity maps are solid white)
# - asset.extras.nmkNormals / nmkCutout / nmkNoBC: the game then loads the normal maps, alpha-tests MASK materials and keeps
#   the textures uncompressed (character.cpp)
# Not carried (the character shader has no such terms): KHR_materials_clearcoat (the eyes), blended (sorted) transparency.
# Each base image is checked against the converted one (mean difference, after resizing) so a UV flip cannot slip through.
#   python -I tools/enemies/glb_restore_textures.py <converted.glb> <reference.glb> <out.glb> [--no-metal]
# --no-metal: every metallic factor 0 (roughness kept): the original's 0.25 / 0.35 made her skin read grey in the game
import json, struct, sys, io, re
import numpy as np
from PIL import Image
def load(p):
    b = open(p, 'rb').read(); jl = struct.unpack('<I', b[12:16])[0]; J = json.loads(b[20:20 + jl]); bl = struct.unpack('<I', b[20 + jl:24 + jl])[0]
    return J, b[28 + jl:28 + jl + bl]
def view(J, BIN, k):
    v = J['bufferViews'][k]; o = v.get('byteOffset', 0); return BIN[o:o + v['byteLength']]
def image_bytes(J, BIN, ti): return view(J, BIN, J['images'][J['textures'][ti]['source']]['bufferView'])
NO_METAL = '--no-metal' in sys.argv; BC = '--bc' in sys.argv; sys.argv = [a for a in sys.argv if a not in ('--no-metal', '--bc')]   # --bc (LT1): leave the game's block compression on (no nmkNoBC)   # BC5: the user: the original, but her skin not metallic
G, GB = load(sys.argv[1]); R, RB = load(sys.argv[2]); OUT = sys.argv[3]
rmat = {m['name']: m for m in R['materials']}
NB = bytearray(); views = []; remap = {}
def add(data):
    while len(NB) % 4: NB.append(0)
    views.append({'buffer': 0, 'byteOffset': len(NB), 'byteLength': len(data)}); NB.extend(data); return len(views) - 1
def keep(k):
    if k not in remap:
        v = G['bufferViews'][k]; remap[k] = add(view(G, GB, k)); views[remap[k]].update({x: v[x] for x in ('byteStride', 'target') if x in v})
    return remap[k]
for a in G['accessors']:   # geometry, skin, morph targets and clips: copied as they are (an accessor may be all-zero or sparse)
    if 'bufferView' in a: a['bufferView'] = keep(a['bufferView'])
    for part in ('indices', 'values'):
        if part in a.get('sparse', {}): a['sparse'][part]['bufferView'] = keep(a['sparse'][part]['bufferView'])
images, textures, by_src = [], [], {}
def tex(data, key, mime='image/png'):
    if key not in by_src:
        images.append({'bufferView': add(data), 'mimeType': mime}); textures.append({'source': len(images) - 1}); by_src[key] = len(textures) - 1
    return {'index': by_src[key]}
def png(im): b = io.BytesIO(); im.save(b, 'PNG'); return b.getvalue()
WHITE = png(Image.new('RGB', (4, 4), (255, 255, 255)))
for m in G['materials']:
    name = re.sub(r'\.\d+$', '', m['name'])
    if name not in rmat:   # LT1: a material the reference does not have (a grafted part's): its own maps carried as they are
        pb = m.get('pbrMetallicRoughness', {})
        for slot in [pb.get('baseColorTexture'), pb.get('metallicRoughnessTexture'), m.get('normalTexture')]:
            if slot: slot['index'] = tex(image_bytes(G, GB, slot['index']), ('G', slot['index']), G['images'][G['textures'][slot['index']]['source']].get('mimeType', 'image/png'))['index']
        print('MAT', name, 'kept (not in the reference)'); continue
    r = rmat[name]; rp = r.get('pbrMetallicRoughness', {}); pb = m.setdefault('pbrMetallicRoughness', {})
    if 'baseColorTexture' in rp:
        ti = rp['baseColorTexture']['index']; data = image_bytes(R, RB, ti); ref = Image.open(io.BytesIO(data))
        if ref.mode == 'P': ref = ref.convert('RGBA'); b2 = io.BytesIO(); ref.save(b2, 'PNG'); data = b2.getvalue()   # a palette with tRNS (the lashes): plain RGBA
        if 'baseColorTexture' in pb:
            old = Image.open(io.BytesIO(image_bytes(G, GB, pb['baseColorTexture']['index']))).convert('RGB').resize(ref.size)
            d = lambda im: float(np.abs(np.asarray(im, np.float32) - np.asarray(old, np.float32)).mean())
            same, flip = d(ref.convert('RGB')), d(ref.convert('RGB').transpose(Image.FLIP_TOP_BOTTOM))
            flipped = flip < same; assert min(same, flip) < 12, (name, same, flip)
        else: flipped = False
        if flipped: data = png(ref.transpose(Image.FLIP_TOP_BOTTOM))   # this mesh's v runs the other way (the zip's eyes): the reference flipped to match
        pb['baseColorTexture'] = tex(data, ('R', ti, flipped))
        alpha = 'A' in ref.convert('RGBA').getbands() and ref.convert('RGBA').getchannel('A').getextrema()[0] < 128
    else: alpha = False
    if 'normalTexture' in r:
        nd = image_bytes(R, RB, r['normalTexture']['index'])
        if 'baseColorTexture' in rp and flipped:   # flipped v: the image flipped and its green (v) channel inverted
            a = np.asarray(Image.open(io.BytesIO(nd)).convert('RGB')).copy()[::-1]; a[..., 1] = 255 - a[..., 1]; nd = png(Image.fromarray(a))
        m['normalTexture'] = tex(nd, ('R', r['normalTexture']['index'], 'baseColorTexture' in rp and flipped))
    pb['metallicFactor'] = 0.0 if NO_METAL else rp.get('metallicFactor', 1.0); pb['roughnessFactor'] = rp.get('roughnessFactor', 1.0)
    pb.pop('metallicRoughnessTexture', None)
    if 'metallicRoughnessTexture' in rp and not NO_METAL: pb['metallicRoughnessTexture'] = tex(image_bytes(R, RB, rp['metallicRoughnessTexture']['index']), ('R', rp['metallicRoughnessTexture']['index'], 'mr'))   # LT1: the reference's own map (the chainmail's shine)
    elif pb['metallicFactor'] > 0.05 or pb['roughnessFactor'] < 0.75: pb['metallicRoughnessTexture'] = tex(WHITE, 'white')
    if r.get('alphaMode', 'OPAQUE') != 'OPAQUE' and alpha: m['alphaMode'] = 'MASK'; m['alphaCutoff'] = 0.5
    else: m.pop('alphaMode', None); m.pop('alphaCutoff', None)
    m['doubleSided'] = r.get('doubleSided', m.get('doubleSided', False))
    print('MAT', name, 'v-flipped' if 'baseColorTexture' in rp and flipped else '', 'base', ref.size if 'baseColorTexture' in rp else None, 'nrm', 'normalTexture' in m, 'metal %.2f rough %.2f' % (pb['metallicFactor'], pb['roughnessFactor']),
          'mr' if 'metallicRoughnessTexture' in pb else '', m.get('alphaMode', 'OPAQUE'), 'dropped: ' + ','.join(r.get('extensions', {})) if r.get('extensions') else '')
G['images'], G['textures'], G['bufferViews'] = images, textures, views; G.pop('samplers', None)
G['buffers'] = [{'byteLength': len(NB)}]; G['asset'].setdefault('extras', {}).update(nmkNormals=True, nmkCutout=True, **({} if BC else {'nmkNoBC': True}))
while len(NB) % 4: NB.append(0)
G['buffers'][0]['byteLength'] = len(NB); js = json.dumps(G, separators=(',', ':')).encode()
while len(js) % 4: js += b' '
with open(OUT, 'wb') as f:
    f.write(struct.pack('<III', 0x46546C67, 2, 28 + len(js) + len(NB))); f.write(struct.pack('<II', len(js), 0x4E4F534A)); f.write(js); f.write(struct.pack('<II', len(NB), 0x004E4942)); f.write(NB)
print('wrote', OUT, len(NB) // 1024, 'KB')
