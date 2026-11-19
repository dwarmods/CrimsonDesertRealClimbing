# aligned FBX -> GLB (raw PAC frame via glTF Y-up) with simple PBR material; plus item icons
import bpy, math
from mathutils import Vector as V
OUT="/workspace/climbmod"; T=OUT+"/textures"
def reset(): bpy.ops.wm.read_factory_settings(use_empty=True)
def mat(n):
    m=bpy.data.materials.new(n); m.use_nodes=True; nt=m.node_tree; p=nt.nodes["Principled BSDF"]
    def tex(t,cs):
        x=nt.nodes.new('ShaderNodeTexImage'); x.image=bpy.data.images.load(f"{T}/{n}_{t}.png"); x.image.colorspace_settings.name=cs; return x
    a=tex("albedo","sRGB"); o=tex("orm","Non-Color"); nm=tex("normal","Non-Color")
    nt.links.new(a.outputs[0],p.inputs['Base Color'])
    sp=nt.nodes.new('ShaderNodeSeparateColor'); nt.links.new(o.outputs[0],sp.inputs[0])
    nt.links.new(sp.outputs[1],p.inputs['Roughness']); nt.links.new(sp.outputs[2],p.inputs['Metallic'])
    nmn=nt.nodes.new('ShaderNodeNormalMap'); nt.links.new(nm.outputs[0],nmn.inputs['Color']); nt.links.new(nmn.outputs[0],p.inputs['Normal'])
    return m
JOBS=[("piolet_adze","piolet_adze"),("piolet_hammer","piolet_hammer"),("crampon_pair","crampon_hinged_12pt")]
for fbx,tex in JOBS:
    reset(); bpy.ops.import_scene.fbx(filepath=f"{OUT}/models/aligned/{fbx}.fbx")
    obs=[o for o in bpy.context.selected_objects if o.type=='MESH']
    for o in obs: o.data.materials.clear(); o.data.materials.append(mat(tex)); o.name=fbx
    bpy.ops.export_scene.gltf(filepath=f"{OUT}/models/glb/{fbx}.glb",export_format='GLB',use_selection=True,export_yup=True,export_apply=True)
    # icon
    sc=bpy.context.scene; sc.render.engine='CYCLES'; sc.cycles.samples=48; sc.cycles.use_denoising=True
    sc.render.resolution_x=sc.render.resolution_y=512; sc.render.film_transparent=True; sc.view_settings.view_transform='AgX'
    w=bpy.data.worlds.new("w"); sc.world=w; w.use_nodes=True; w.node_tree.nodes["Background"].inputs[1].default_value=0.25
    mn=V((1e9,)*3); mx=V((-1e9,)*3)
    for o in obs:
        for v in o.data.vertices: c=o.matrix_world@v.co; mn=V(map(min,mn,c)); mx=V(map(max,mx,c))
    ctr=(mn+mx)/2; size=(mx-mn).length
    if fbx.startswith("piolet"): d=V((0.15,-0.25,1.0)).normalized()   # view onto blade plane (thickness = Z)
    else: d=V((0.9,-1.0,0.8)).normalized()
    cd=bpy.data.cameras.new("c"); cd.type='ORTHO'; cd.ortho_scale=size*0.95; c=bpy.data.objects.new("c",cd); sc.collection.objects.link(c); sc.camera=c
    c.location=ctr+d*size*3; c.rotation_euler=(-d).to_track_quat('-Z','Y' if fbx.startswith("piolet") else 'Z').to_euler()
    if fbx.startswith("piolet"): c.rotation_euler.rotate_axis('Z',math.radians(-35))
    for loc,e in (((1,-1,2),120),((-2,-1,0.5),40),((0,2,1),60)):
        ld=bpy.data.lights.new("l",'AREA'); ld.energy=e*size*size; ld.size=size; lo=bpy.data.objects.new("l",ld); sc.collection.objects.link(lo)
        lo.location=ctr+V(loc)*size; lo.rotation_euler=(ctr-lo.location).to_track_quat('-Z','Y').to_euler()
    sc.render.filepath=f"{OUT}/icons/{fbx}.png"; bpy.ops.render.render(write_still=True)
