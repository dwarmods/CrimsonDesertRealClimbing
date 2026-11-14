# bake procedural materials -> albedo / normal / ORM (R=AO,G=rough,B=metal) PNGs. blender -b -P bake.py -- name
import bpy, sys, os
name=sys.argv[sys.argv.index("--")+1]; OUT="/workspace/climbmod/rope_slice"; RES=int(os.environ.get("RES","1024"))
bpy.ops.wm.open_mainfile(filepath=f"{OUT}/models/{name}.blend")
sc=bpy.context.scene; sc.render.engine='CYCLES'; sc.cycles.device='CPU'; sc.cycles.samples=16
ob=bpy.data.objects[name]
for o in bpy.data.objects: o.select_set(o==ob)
bpy.context.view_layer.objects.active=ob
def img(tag,color=True):
    im=bpy.data.images.new(f"{name}_{tag}",RES,RES,alpha=False,float_buffer=False)
    im.colorspace_settings.name='sRGB' if color else 'Non-Color'; return im
def set_target(im):
    for m in ob.data.materials:
        nt=m.node_tree; n=nt.nodes.get("BAKE") or nt.nodes.new('ShaderNodeTexImage'); n.name="BAKE"; n.image=im
        nt.nodes.active=n; n.select=True
def save(im,tag):
    im.filepath_raw=f"{OUT}/textures/{name}_{tag}.png"; im.file_format='PNG'; im.save(); print("SAVED",im.filepath_raw)
bk=sc.render.bake; bk.margin=8; bk.use_selected_to_active=False
# albedo
n=img("normal",False); set_target(n); bk.normal_space='TANGENT'; bpy.ops.object.bake(type='NORMAL'); save(n,"normal")
r=img("rough",False); set_target(r); bpy.ops.object.bake(type='ROUGHNESS'); save(r,"rough")
ao=img("ao",False); set_target(ao); sc.cycles.samples=32; bpy.ops.object.bake(type='AO'); save(ao,"ao")
def emit_bake(sock,tag,color):
    links=[]
    for m in ob.data.materials:
        nt=m.node_tree; p=next((x for x in nt.nodes if x.type=='BSDF_PRINCIPLED'),None); out=next(x for x in nt.nodes if x.type=='OUTPUT_MATERIAL' and x.is_active_output)
        old=out.inputs['Surface'].links[0].from_socket
        em=nt.nodes.new('ShaderNodeEmission'); inp=p.inputs[sock]
        if inp.is_linked: nt.links.new(inp.links[0].from_socket, em.inputs['Color'])
        else:
            v=inp.default_value; em.inputs['Color'].default_value=tuple(v) if hasattr(v,'__len__') else (v,v,v,1)
        nt.links.new(em.outputs[0], out.inputs['Surface']); links.append((nt,old,out,em))
    im=img(tag,color); set_target(im); sc.cycles.samples=1; bpy.ops.object.bake(type='EMIT'); save(im,tag)
    for nt,old,out,em in links: nt.links.new(old,out.inputs['Surface']); nt.nodes.remove(em)
emit_bake('Base Color','albedo',True)
emit_bake('Metallic','metal',False)
