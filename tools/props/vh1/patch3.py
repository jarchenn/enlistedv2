# each house's own exterior stone for its inner walls: a hand-picked plain stone square of the original wall (view, along
# range, z range in source units; from the gridded elevations el2/sheet_*), flat true colour, mirrored 2 x 2
import bpy, math, os, json
import numpy as np
from mathutils import Vector
PICK={"vh_cottage_shutters":("h1_box024","S",4.1,6.5,0.4),"vh_cottage_tall":("h2_box030","S",4.1,6.1,0.5),"vh_longhouse_blue":("h3_cube1","S",1.6,4.2,0.6),
      "vh_manor_tower":("h4_zbrush","E",-8.6,-3.6,0.5),"vh_stone_house_l":("h5_maison7","N",0.5,6.5,1.0),"vh_townhouse":("h6_maison","W",2.4,3.9,0.6),
      "vh_timber_house":("h7_poutre","S",-1.5,2.5,3.6),"vh_barn_cottage":("h8_toit","W",3.2,5.9,0.6)}
S=0.29; out={}
for bid,(key,v,a0,a1,z0) in PICK.items():
    bpy.ops.wm.read_factory_settings(use_empty=True); bpy.ops.import_scene.gltf(filepath=r"E:/nmk/village/aligned/%s.glb"%key)
    sc=bpy.context.scene; sc.render.engine='BLENDER_WORKBENCH'; sc.display.shading.light='FLAT'; sc.display.shading.color_type='TEXTURE'; sc.view_settings.view_transform='Standard'
    side=a1-a0; c_a=(a0+a1)/2; c_z=z0+side/2
    ps=[o.matrix_world@Vector(c) for o in bpy.data.objects if o.type=='MESH' for c in o.bound_box]
    lo=Vector([min(p[i] for p in ps) for i in range(3)]); hi=Vector([max(p[i] for p in ps) for i in range(3)])
    cam=bpy.data.objects.new("C",bpy.data.cameras.new("C")); sc.collection.objects.link(cam); sc.camera=cam; cam.data.type='ORTHO'; cam.data.ortho_scale=side
    sc.render.resolution_x=512; sc.render.resolution_y=512
    if v=="S": cam.location=(c_a,lo.y-50,c_z); cam.rotation_euler=(math.radians(90),0,0)
    if v=="N": cam.location=(c_a,hi.y+50,c_z); cam.rotation_euler=(math.radians(90),0,math.radians(180))
    if v=="W": cam.location=(lo.x-50,c_a,c_z); cam.rotation_euler=(math.radians(90),0,math.radians(-90))
    if v=="E": cam.location=(hi.x+50,c_a,c_z); cam.rotation_euler=(math.radians(90),0,math.radians(90))
    fp=r"E:/nmk/village/tex/_pick_%s.png"%bid; sc.render.filepath=fp; bpy.ops.render.render(write_still=True)
    im=bpy.data.images.load(fp); px=np.empty(512*512*4,np.float32); im.pixels.foreach_get(px); tile=px.reshape(512,512,4)[:,:,:3]
    big=np.concatenate([np.concatenate([tile,tile[:,::-1]],1),np.concatenate([tile[::-1],tile[::-1,::-1]],1)],0)
    d=r"E:/nmk/village/tex/%s_stone"%bid; os.makedirs(d,exist_ok=True)
    ni=bpy.data.images.new("patch",1024,1024,alpha=False); ni.pixels.foreach_set(np.concatenate([big,np.ones((1024,1024,1),np.float32)],-1).ravel())
    ni.filepath_raw=d+"/color.jpg"; ni.file_format="JPEG"; ni.save()
    out[bid]={"tile_m":round(2*side*S,3)}; print("PATCH",bid,out[bid])
json.dump(out,open(r"E:/nmk/village/tex/stone_tiles.json","w"),indent=1)
