# climbmod v1 - old-school piolets + hinged bent-bar crampon. Blender 4.2: blender -b -P build.py
import bpy, bmesh, math
from mathutils import Vector, Matrix
OUT="/workspace/climbmod"
V=Vector
def reset(): bpy.ops.wm.read_factory_settings(use_empty=True)
def link(o): bpy.context.collection.objects.link(o); return o

# ---------- materials ----------
def M_wood():
    m=bpy.data.materials.new("ash_wood"); m.use_nodes=True; nt=m.node_tree; b=nt.nodes["Principled BSDF"]
    w=nt.nodes.new("ShaderNodeTexWave"); w.inputs["Scale"].default_value=3; w.inputs["Distortion"].default_value=1.5
    w.bands_direction='Z'; tc=nt.nodes.new("ShaderNodeTexCoord"); mp=nt.nodes.new("ShaderNodeMapping"); mp.inputs["Scale"].default_value=(30,30,2)
    nt.links.new(tc.outputs["Object"],mp.inputs[0]); nt.links.new(mp.outputs[0],w.inputs["Vector"])
    r=nt.nodes.new("ShaderNodeValToRGB"); r.color_ramp.elements[0].color=(0.30,0.17,0.08,1); r.color_ramp.elements[1].color=(0.55,0.36,0.18,1)
    nt.links.new(w.outputs["Fac"],r.inputs[0]); nt.links.new(r.outputs[0],b.inputs["Base Color"])
    b.inputs["Roughness"].default_value=0.55; m.diffuse_color=(0.5,0.32,0.16,1); return m
def M_steel():
    m=bpy.data.materials.new("worn_steel"); m.use_nodes=True; nt=m.node_tree; b=nt.nodes["Principled BSDF"]
    n=nt.nodes.new("ShaderNodeTexNoise"); n.inputs["Scale"].default_value=180; n.inputs["Detail"].default_value=8
    r=nt.nodes.new("ShaderNodeValToRGB"); r.color_ramp.elements[0].position=0.4; r.color_ramp.elements[0].color=(0.22,0.22,0.23,1); r.color_ramp.elements[1].color=(0.62,0.61,0.58,1)
    nt.links.new(n.outputs["Fac"],r.inputs[0]); nt.links.new(r.outputs[0],b.inputs["Base Color"])
    r2=nt.nodes.new("ShaderNodeMapRange"); r2.inputs[3].default_value=0.55; r2.inputs[4].default_value=0.25
    nt.links.new(n.outputs["Fac"],r2.inputs[0]); nt.links.new(r2.outputs[0],b.inputs["Roughness"])
    b.inputs["Metallic"].default_value=1.0; m.diffuse_color=(0.5,0.5,0.5,1); return m
def M_leather():
    m=bpy.data.materials.new("leather"); m.use_nodes=True; nt=m.node_tree; b=nt.nodes["Principled BSDF"]
    n=nt.nodes.new("ShaderNodeTexNoise"); n.inputs["Scale"].default_value=60
    r=nt.nodes.new("ShaderNodeValToRGB"); r.color_ramp.elements[0].color=(0.13,0.06,0.025,1); r.color_ramp.elements[1].color=(0.28,0.14,0.06,1)
    nt.links.new(n.outputs["Fac"],r.inputs[0]); nt.links.new(r.outputs[0],b.inputs["Base Color"])
    b.inputs["Roughness"].default_value=0.7; m.diffuse_color=(0.25,0.12,0.05,1); return m

# ---------- geometry helpers ----------
def tube(name,pts,r,mat,res_u=8,bev_res=3,radii=None,cyclic=False,scale_y=1.0,handles='AUTO'):
    cu=bpy.data.curves.new(name,'CURVE'); cu.dimensions='3D'; cu.bevel_depth=r; cu.bevel_resolution=bev_res
    cu.resolution_u=res_u; cu.use_fill_caps=True
    sp=cu.splines.new('BEZIER'); sp.bezier_points.add(len(pts)-1); sp.use_cyclic_u=cyclic
    for i,(bp,p) in enumerate(zip(sp.bezier_points,pts)):
        bp.co=p; bp.handle_left_type=bp.handle_right_type=handles
        if radii: bp.radius=radii[i]
    o=link(bpy.data.objects.new(name,cu)); to_mesh(o); o.data.materials.append(mat)
    if scale_y!=1.0: o.scale.y=scale_y; apply(o)
    return o
def to_mesh(o):
    bpy.ops.object.select_all(action='DESELECT'); o.select_set(True); bpy.context.view_layer.objects.active=o
    bpy.ops.object.convert(target='MESH')
def apply(o):
    bpy.ops.object.select_all(action='DESELECT'); o.select_set(True); bpy.context.view_layer.objects.active=o
    bpy.ops.object.transform_apply(location=True,rotation=True,scale=True)
def strip(name,centers,normals,width,thick,mat,cyclic=False):
    """flat strap: centers along path, normals = face normal of strap"""
    me=bpy.data.meshes.new(name); bm=bmesh.new(); rings=[]; n=len(centers)
    for i,c in enumerate(centers):
        a=centers[(i+1)%n] if (cyclic or i<n-1) else c; b=centers[i-1] if (cyclic or i>0) else c
        t=(V(a)-V(b)).normalized(); nn=V(normals[i]).normalized(); s=t.cross(nn).normalized()
        c=V(c); rings.append([bm.verts.new(c+s*sx*width/2+nn*nz*thick/2) for sx,nz in ((-1,-1),(1,-1),(1,1),(-1,1))])
    rng=range(n) if cyclic else range(n-1)
    for i in rng:
        A,B=rings[i],rings[(i+1)%n]
        for k in range(4): bm.faces.new((A[k],A[(k+1)%4],B[(k+1)%4],B[k]))
    if not cyclic: bm.faces.new(rings[0][::-1]); bm.faces.new(rings[-1])
    bmesh.ops.recalc_face_normals(bm,faces=bm.faces); bm.to_mesh(me); bm.free()
    o=link(bpy.data.objects.new(name,me)); o.data.materials.append(mat); return o
def extrude(name,pts,thick,mat,plane='XZ',bevel=0.0015,segs=2,loc=(0,0,0)):
    me=bpy.data.meshes.new(name); bm=bmesh.new()
    if plane=='XZ': vs=[bm.verts.new((x,-thick/2,z)) for x,z in pts]; d=V((0,thick,0))
    else: vs=[bm.verts.new((x,y,-thick/2)) for x,y in pts]; d=V((0,0,thick))
    f=bm.faces.new(vs); r=bmesh.ops.extrude_face_region(bm,geom=[f])
    for v in [e for e in r['geom'] if isinstance(e,bmesh.types.BMVert)]: v.co+=d
    bmesh.ops.recalc_face_normals(bm,faces=bm.faces); bm.to_mesh(me); bm.free()
    o=link(bpy.data.objects.new(name,me)); o.location=loc; o.data.materials.append(mat)
    if bevel:
        md=o.modifiers.new("b",'BEVEL'); md.width=bevel; md.segments=segs; md.limit_method='ANGLE'
        bpy.context.view_layer.objects.active=o; bpy.ops.object.modifier_apply(modifier="b")
    return o
def arc(p0,p1,n_pts):  # smooth helper (catmull-ish) - returns list
    return [V(p0).lerp(V(p1),i/(n_pts-1)) for i in range(n_pts)]
def prim(kind,mat,bevel=0.0,**kw):
    getattr(bpy.ops.mesh,"primitive_"+kind+"_add")(**kw); o=bpy.context.object; o.data.materials.append(mat)
    if bevel:
        md=o.modifiers.new("b",'BEVEL'); md.width=bevel; md.segments=2; md.limit_method='ANGLE'; bpy.ops.object.modifier_apply(modifier="b")
    apply(o); return o
def finish(name):
    objs=[o for o in bpy.context.scene.objects if o.type=='MESH']
    bpy.ops.object.select_all(action='DESELECT')
    for o in objs: o.select_set(True)
    bpy.context.view_layer.objects.active=objs[0]; bpy.ops.object.join(); o=bpy.context.object; o.name=name
    bpy.ops.object.mode_set(mode='EDIT'); bpy.ops.mesh.select_all(action='SELECT')
    bpy.ops.uv.smart_project(angle_limit=math.radians(60),island_margin=0.01); bpy.ops.object.mode_set(mode='OBJECT')
    bpy.ops.object.shade_smooth_by_angle(angle=math.radians(40))
    tris=sum(len(p.vertices)-2 for p in o.data.polygons)
    bpy.ops.wm.obj_export(filepath=f"{OUT}/models/{name}.obj",export_selected_objects=True,export_materials=True,export_triangulated_mesh=True)
    bpy.ops.export_scene.fbx(filepath=f"{OUT}/models/{name}.fbx",use_selection=True,apply_unit_scale=True,mesh_smooth_type='FACE',use_triangles=True)
    bpy.ops.wm.save_as_mainfile(filepath=f"{OUT}/models/{name}.blend")
    print(f"RESULT {name} tris={tris} dims_m={[round(v,3) for v in o.dimensions]}"); return o

# ---------- render ----------
def studio(objs,name,view=(1.0,-1.25,0.7),lens=55,res=1400,pad=1.25):
    sc=bpy.context.scene; sc.render.engine='CYCLES'; sc.cycles.device='CPU'; sc.cycles.samples=64; sc.cycles.use_denoising=True
    sc.render.resolution_x=res; sc.render.resolution_y=res; sc.view_settings.view_transform='AgX'; sc.view_settings.look='AgX - Base Contrast'
    w=bpy.data.worlds.new("w"); sc.world=w; w.use_nodes=True; w.node_tree.nodes["Background"].inputs[0].default_value=(0.16,0.17,0.19,1); w.node_tree.nodes["Background"].inputs[1].default_value=0.35
    mn=V((1e9,)*3); mx=V((-1e9,)*3)
    for o in objs:
        for c in o.bound_box: wc=o.matrix_world@V(c); mn=V(map(min,mn,wc)); mx=V(map(max,mx,wc))
    ctr=(mn+mx)/2; size=(mx-mn).length
    # ground
    bpy.ops.mesh.primitive_plane_add(size=size*20,location=(ctr.x,ctr.y,mn.z-0.001)); g=bpy.context.object
    gm=bpy.data.materials.new("ground"); gm.use_nodes=True; gm.node_tree.nodes["Principled BSDF"].inputs["Base Color"].default_value=(0.2,0.21,0.23,1); gm.node_tree.nodes["Principled BSDF"].inputs["Roughness"].default_value=0.9; g.data.materials.append(gm)
    def light(loc,e,s):
        ld=bpy.data.lights.new("l",'AREA'); ld.energy=e*size*size*1.6; ld.size=s*size; lo=link(bpy.data.objects.new("l",ld))
        lo.location=ctr+V(loc)*size; lo.rotation_euler=(ctr-lo.location).to_track_quat('-Z','Y').to_euler()
    light((1.2,-1.0,1.6),60,1.5); light((-1.5,-0.8,0.6),18,2.0); light((-0.3,1.5,1.2),35,1.0)
    cd=bpy.data.cameras.new("c"); cd.lens=lens; c=link(bpy.data.objects.new("c",cd)); sc.camera=c
    d=V(view).normalized(); fov=2*math.atan(18/lens); dist=size*pad/2/math.tan(fov/2)
    c.location=ctr+d*dist; c.rotation_euler=(-d).to_track_quat('-Z','Y').to_euler()
    sc.render.filepath=f"{OUT}/previews/{name}.png"; bpy.ops.render.render(write_still=True)

# =============== PIOLET (Z up, spike tip at z=0, ~0.62 m) ===============
def piolet(variant):
    reset(); wood=M_wood(); steel=M_steel(); lea=M_leather()
    # wooden shaft, oval 34x24mm, slight taper toward head, very slight curve
    tube("shaft",[(0,0,0.085),(0.004,0,0.33),(0,0,0.575)],0.017,wood,res_u=14,bev_res=4,radii=[1.0,0.97,0.9],scale_y=0.72)
    # bottom ferrule + spike
    prim("cylinder",steel,bevel=0.0015,vertices=24,radius=0.0185,depth=0.085,location=(0,0,0.095)).scale
    o=bpy.context.object; o.scale.y=0.76; apply(o)
    tube("spike",[(0,0,0.054),(0,0,0.028),(0,0,0.0)],0.0175,steel,res_u=6,bev_res=4,radii=[1.0,0.65,0.05],scale_y=0.8)
    for z in (0.075,0.12):
        for s in (-1,1): prim("uv_sphere",steel,segments=10,ring_count=6,radius=0.0035,location=(0,s*0.0138,z))
    # head collar (langets) + rivets
    prim("cylinder",steel,bevel=0.0015,vertices=24,radius=0.019,depth=0.075,location=(0,0,0.54)); o=bpy.context.object; o.scale.y=0.78; apply(o)
    for z in (0.515,0.555):
        for s in (-1,1): prim("uv_sphere",steel,segments=10,ring_count=6,radius=0.0035,location=(0,s*0.0146,z))
    # forged head eye block
    prim("cylinder",steel,bevel=0.003,vertices=24,radius=0.022,depth=0.045,location=(0,0,0.598)); o=bpy.context.object; o.scale.y=0.8; apply(o)
    # pick: gently drooped, tapering, 4 teeth under tip -> toward -X
    top=[(-0.015,0.618),(-0.06,0.616),(-0.10,0.608),(-0.14,0.594),(-0.175,0.576),(-0.205,0.556),(-0.222,0.542)]
    bot=[(-0.226,0.536),(-0.214,0.538)]
    # teeth
    for i in range(4):
        x=-0.205+i*0.018; z=0.544+i*0.007
        bot+= [(x,z-0.008),(x+0.009,z)]
    bot+=[(-0.12,0.568),(-0.07,0.58),(-0.03,0.585),(-0.015,0.585)]
    extrude("pick",top+bot,0.010,steel,bevel=0.0018)
    if variant=="adze":
        # broad adze, plan view trapezoid with rounded edge, drooped slightly
        pl=[(0.012,-0.010),(0.06,-0.014),(0.10,-0.024),(0.13,-0.030),(0.142,-0.022),(0.146,0.0),(0.142,0.022),(0.13,0.030),(0.10,0.024),(0.06,0.014),(0.012,0.010)]
        a=extrude("adze",pl,0.007,steel,plane='XY',bevel=0.0018,loc=(0,0,0.610))
        a.rotation_euler=(0,math.radians(14),0); a.location=(0.004,0,0.611); apply(a)
        # neck reinforcement
        tube("adze_neck",[(0.0,0,0.606),(0.04,0,0.604),(0.07,0,0.598)],0.008,steel,res_u=6,bev_res=3,radii=[1.2,0.9,0.4],scale_y=1.4)
    else:
        tube("hammer_neck",[(0.0,0,0.598),(0.035,0,0.598)],0.011,steel,res_u=4,bev_res=3,scale_y=0.9)
        prim("cube",steel,bevel=0.004,size=1,location=(0.052,0,0.598),scale=(0.032,0.028,0.028))
    # sliding wrist ring + leather loop
    zr=0.36
    prim("torus",steel,major_radius=0.0235,minor_radius=0.0028,major_segments=20,minor_segments=6,location=(0,0,zr))
    o=bpy.context.object; o.scale.y=0.78; apply(o)
    # loop: closed teardrop of doubled leather; both ends gathered in the wrap at the ring
    C=[];N=[]; n=40; cx,cz,ax,az=0.030,zr-0.072,0.024,0.068
    for i in range(n):
        t=i/n*2*math.pi
        w=1-0.85*max(math.cos(t),0)**4
        x=cx+ax*math.sin(t)*w; z=cz+az*math.cos(t)
        C.append((x,0.0,z))
    for i,(x,y,z) in enumerate(C):
        nx,nz=x-cx,(z-cz)*0.35
        N.append((nx,0,nz) if abs(nx)+abs(nz)>1e-5 else (0,0,1))
    strip("wrist_loop",C,N,0.015,0.003,lea,cyclic=True)
    prim("cylinder",lea,vertices=12,radius=0.0045,depth=0.02,location=(cx,0,zr-0.012),rotation=(0,0,0))
    # leather stitched wrap where the loop meets the ring
    prim("cylinder",lea,vertices=12,radius=0.006,depth=0.022,location=(0.027,0,zr-0.002),rotation=(math.pi/2,0,0))
    return finish(f"piolet_{variant}")

def render_single(name,view):
    bpy.ops.wm.open_mainfile(filepath=f"{OUT}/models/{name}.blend")
    studio([bpy.data.objects[name]],name,view=view)

# =============== CRAMPON (X fwd, sole plane z=0, boot 0.28 m: x -0.14..0.14) ===============
def crampon():
    reset(); steel=M_steel(); lea=M_leather(); r=0.0042
    zb=0.004
    # front section: bent-bar U around toe
    F=[(-0.005,-0.043,zb),(0.06,-0.046,zb),(0.115,-0.042,zb),(0.145,-0.022,zb),(0.152,0,zb),(0.145,0.022,zb),(0.115,0.042,zb),(0.06,0.046,zb),(-0.005,0.043,zb)]
    tube("front_frame",F,r,steel,res_u=4,bev_res=1,scale_y=1)
    tube("front_cross",[(-0.005,-0.043,zb),(0.0,0,zb+0.004),(-0.005,0.043,zb)],r*0.9,steel,res_u=4,bev_res=1)
    # heel section U
    H=[(-0.03,-0.040,zb),(-0.085,-0.041,zb),(-0.125,-0.030,zb),(-0.142,0,zb),(-0.125,0.030,zb),(-0.085,0.041,zb),(-0.03,0.040,zb)]
    tube("heel_frame",H,r,steel,res_u=4,bev_res=1)
    tube("heel_cross",[(-0.03,-0.040,zb),(-0.035,0,zb+0.004),(-0.03,0.040,zb)],r*0.9,steel,res_u=4,bev_res=1)
    # hinged link bar with forged eyes
    tube("link_bar",[(-0.044,0,zb+0.003),(-0.017,0,zb+0.009),(0.012,0,zb+0.003)],r*0.8,steel,res_u=5,bev_res=1)
    for x in (-0.035,0.0):
        prim("torus",steel,major_radius=0.0065,minor_radius=0.0022,major_segments=16,minor_segments=6,location=(x,0,zb+0.004),rotation=(0,math.pi/2,0))
    # points: tapered, slightly curved, hand-forged
    def point(base,tipd,length,curve=(0,0,0)):
        b=V(base); t=b+V(tipd).normalized()*length; m=(b+t)/2+V(curve)
        tube("pt",[tuple(b),tuple(m),tuple(t)],0.0048,steel,res_u=4,bev_res=1,radii=[1.0,0.65,0.08])
    for s in (-1,1):
        for x,y in ((0.115,0.042),(0.065,0.046),(0.01,0.044)):
            point((x,s*y,zb-0.001),(0.08,s*0.12,-1),0.036,(0.004,0,0))
        for x,y in ((-0.075,0.041),(-0.122,0.031)):
            point((x,s*y,zb-0.001),(-0.08,s*0.12,-1),0.034,(-0.003,0,0))
        # secondary (angled forward-down)
        point((0.140,s*0.026,zb-0.001),(0.8,s*0.2,-1),0.034,(0.003,0,0.002))
        # front points: forward, curving down
        b=V((0.150,s*0.010,zb)); tube("front_pt",[tuple(b),tuple(b+V((0.028,s*0.004,-0.004))),tuple(b+V((0.046,s*0.006,-0.022)))],0.0052,steel,res_u=5,bev_res=1,radii=[1.0,0.7,0.08])
    # posts + eyelets: vertical rings on the side bars
    eyes=[]
    for s in (-1,1):
        for x,yy in ((0.095,0.044),(-0.07,0.041)):
            y=s*(yy+0.001); post_top=zb+0.020
            tube("post",[(x,y,zb),(x,y*1.02,zb+0.010),(x,y*1.04,post_top)],0.0032,steel,res_u=4,bev_res=2)
            prim("torus",steel,major_radius=0.0085,minor_radius=0.0023,major_segments=12,minor_segments=5,location=(x,y*1.05,post_top+0.0085),rotation=(math.pi/2,0,0))
            eyes.append((x,y*1.05,post_top+0.0085))
    # toe strap: passes through both front eyelets and wraps over the toe (boot toe ~7cm tall)
    xt=0.095; ey=0.0473; ez=zb+0.0285
    C=[];N=[]; cy,cz=0.0,ez
    for i in range(15):
        a=math.pi*i/14  # from -y side over the top to +y
        y=-math.cos(a)*(ey); z=ez+math.sin(a)*0.052
        C.append((xt-0.004*math.sin(a),y,z)); N.append((0,y-cy,z-cz+0.0001))
    strip("toe_strap",C,N,0.02,0.0035,lea)
    # ankle strap: up from heel eyelets, around the back of the ankle and over the instep
    xa=-0.07; zA=0.115; C=[];N=[]
    # side risers
    for s in (-1,1):
        pts=[]; nrm=[]
        for i in range(10):
            t=i/9; y=s*(0.0442+0.006*t); z=ez+(zA-ez)*t; pts.append((xa,y,z)); nrm.append((0,s,0))
        strip("ankle_riser",pts,nrm,0.018,0.0035,lea)
    # ankle loop (ellipse around leg, centre x=-0.045)
    C=[];N=[]; cx=-0.045
    for i in range(24):
        a=2*math.pi*i/24
        x=cx+math.cos(a)*0.068; y=math.sin(a)*0.051
        C.append((x,y,zA)); N.append((x-cx,y,0))
    strip("ankle_loop",C,N,0.02,0.0035,lea,cyclic=True)
    # buckle on the instep side
    prim("torus",steel,major_radius=0.012,minor_radius=0.0018,major_segments=4,minor_segments=6,location=(cx+0.0705,0,zA),rotation=(0,math.pi/2,math.pi/4))
    return finish("crampon_hinged_12pt")

for v in ("adze","hammer"): piolet(v)
crampon()
render_single("piolet_adze",(1.0,-1.4,0.45))
render_single("piolet_hammer",(1.0,-1.4,0.45))
render_single("crampon_hinged_12pt",(1.0,-1.1,0.8))
# combined set
reset()
names=["piolet_adze","piolet_hammer","crampon_hinged_12pt"]
for n in names:
    with bpy.data.libraries.load(f"{OUT}/models/{n}.blend") as (src,dst): dst.objects=[n]
for o in bpy.data.objects: link(o)
a,h,c=[bpy.data.objects[n] for n in names]
a.location=(-0.05,0.06,0); h.location=(0.08,0.10,0); h.rotation_euler=(0,0,math.radians(160))
c.location=(-0.02,-0.16,0.035); c.rotation_euler=(0,0,0)
c2=c.copy(); c2.data=c.data; link(c2); c2.scale.y=-1; c2.location=(-0.02,-0.31,0.035); c2.rotation_euler=(0,0,0)
bpy.context.view_layer.update()
studio([a,h,c,c2],"full_set",view=(1.0,-1.5,0.75),pad=1.0)
