# each house on its own: turned square (its walls' dominant direction, area-weighted, mod 90), centred, base at z 0; one GLB each
import bpy, math, os, json
from mathutils import Vector, Matrix
bpy.ops.wm.open_mainfile(filepath=r"E:/nmk/village/village_src.blend")
os.makedirs(r"E:/nmk/village/aligned",exist_ok=True)
SETS={"h1_box024":["Box024_2_wire_115115115_0"],"h2_box030":["Box030_1_wire_115115115_0"],"h3_cube1":["Cube_1_Mat_0"],"h4_zbrush":["ZBrushPolyMesh3D_1_defaultMat_0"],
      "h5_maison7":["maison7_Mat_0"],"h6_maison":["maison_Mat_0"],"h7_poutre":["poutre_5_1_Mat.1_0"],"h8_toit":["toit_wire_115115115_0","toit_wire_115115115_0.001"]}
info={}
for key,names in SETS.items():
    objs=[bpy.data.objects[n] for n in names]
    hist=[0.0]*90
    for o in objs:
        M=o.matrix_world; R=M.to_3x3()
        for p in o.data.polygons:
            n=(R@p.normal).normalized()
            if abs(n.z)>0.2: continue
            a=math.degrees(math.atan2(n.y,n.x))%90; area=p.area*abs(R.determinant())**(2/3)
            hist[int(a)%90]+=area
    sm=[sum(hist[(i+k)%90] for k in (-2,-1,0,1,2)) for i in range(90)]; i=max(range(90),key=lambda k:sm[k])
    num=sum(hist[(i+k)%90]*(i+k+0.5) for k in (-2,-1,0,1,2)); den=sum(hist[(i+k)%90] for k in (-2,-1,0,1,2)); th=num/den
    if th>45: th-=90
    for o in bpy.data.objects: o.select_set(False)
    cps=[]
    for o in objs:
        c=o.copy(); c.data=o.data.copy(); bpy.context.scene.collection.objects.link(c); c.parent=None; c.matrix_world=o.matrix_world.copy(); cps.append(c)
    bpy.context.view_layer.update()
    for c in cps:
        bpy.context.view_layer.objects.active=c; c.select_set(True)
    bpy.ops.object.transform_apply(location=True,rotation=True,scale=True)
    Rz=Matrix.Rotation(math.radians(-th),4,'Z')
    for c in cps: c.data.transform(Rz)
    vs=[v.co for c in cps for v in c.data.vertices]
    lo=Vector([min(v[k] for v in vs) for k in range(3)]); hi=Vector([max(v[k] for v in vs) for k in range(3)])
    T=Matrix.Translation(-Vector(((lo.x+hi.x)/2,(lo.y+hi.y)/2,lo.z)))
    for c in cps: c.data.transform(T)
    vs=[v.co for c in cps for v in c.data.vertices]
    lo=Vector([min(v[k] for v in vs) for k in range(3)]); hi=Vector([max(v[k] for v in vs) for k in range(3)])
    info[key]={"theta":round(th,2),"size":[round(hi[k]-lo[k],2) for k in range(3)]}
    for o in bpy.data.objects: o.select_set(False)
    for c in cps: c.select_set(True)
    bpy.ops.export_scene.gltf(filepath=r"E:/nmk/village/aligned/%s.glb"%key,export_format="GLB",use_selection=True,export_yup=True)
    for c in cps: bpy.data.objects.remove(c,do_unlink=True)
json.dump(info,open(r"E:/nmk/village/aligned/info.json","w"),indent=1); print("ALIGN",json.dumps(info))
