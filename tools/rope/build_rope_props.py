# Old-school rope/anchor props, Blender 4.2 headless. Units metres, Blender Z-up.
# blender -b -P build_rope_props.py   -> models/<name>.blend/.glb/.fbx/.obj
import bpy,bmesh,math,os
from mathutils import Vector,Matrix
OUT='/workspace/climbmod/rope_slice/models'
def reset():
    bpy.ops.wm.read_factory_settings(use_empty=True)
def principled(name,base,rough,metal,bump=None,extra=None):
    m=bpy.data.materials.new(name);m.use_nodes=True;nt=m.node_tree;p=nt.nodes['Principled BSDF']
    p.inputs['Base Color'].default_value=(*base,1);p.inputs['Roughness'].default_value=rough;p.inputs['Metallic'].default_value=metal
    if extra:extra(nt,p)
    return m
def link(ob):bpy.context.scene.collection.objects.link(ob);return ob
def to_mesh(curve_ob):
    bpy.context.view_layer.objects.active=curve_ob;curve_ob.select_set(True);bpy.ops.object.convert(target='MESH');return bpy.context.object
def finish(ob,name):
    for o in bpy.context.scene.objects:o.select_set(False)
    ob.select_set(True);bpy.context.view_layer.objects.active=ob;ob.name=name
    bpy.ops.object.mode_set(mode='EDIT');bpy.ops.mesh.select_all(action='SELECT');bpy.ops.uv.smart_project(angle_limit=math.radians(60),island_margin=0.02);bpy.ops.object.mode_set(mode='OBJECT')
    bpy.ops.object.shade_smooth();ob.data.update()
    ob.modifiers.new('tri','TRIANGULATE');bpy.ops.object.modifier_apply(modifier='tri')
    bpy.ops.wm.save_as_mainfile(filepath=f'{OUT}/{name}.blend')
    bpy.ops.export_scene.gltf(filepath=f'{OUT}/{name}.glb',use_selection=True,export_tangents=True)
    bpy.ops.export_scene.fbx(filepath=f'{OUT}/{name}.fbx',use_selection=True,axis_forward='-Z',axis_up='Y')
    bpy.ops.wm.obj_export(filepath=f'{OUT}/{name}.obj',export_selected_objects=True,export_materials=True)
    print('BUILT',name,len(ob.data.polygons),'tris',tuple(round(x,3) for x in ob.dimensions))
# ---------- materials ----------
def hemp_extra(nt,p):
    # twisted-lay fibre look: wave texture along the strand + fine noise, natural hemp colour
    tc=nt.nodes.new('ShaderNodeTexCoord');wv=nt.nodes.new('ShaderNodeTexWave');wv.wave_type='BANDS';wv.bands_direction='DIAGONAL';wv.inputs['Scale'].default_value=60;wv.inputs['Distortion'].default_value=3
    nz=nt.nodes.new('ShaderNodeTexNoise');nz.inputs['Scale'].default_value=220;nz.inputs['Detail'].default_value=8
    mix=nt.nodes.new('ShaderNodeMix');mix.data_type='RGBA';mix.inputs[6].default_value=(.20,.14,.07,1);mix.inputs[7].default_value=(.42,.32,.18,1)
    nt.links.new(tc.outputs['Object'],wv.inputs['Vector']);nt.links.new(tc.outputs['Object'],nz.inputs['Vector'])
    m2=nt.nodes.new('ShaderNodeMath');m2.operation='MULTIPLY_ADD';m2.inputs[1].default_value=.6;m2.inputs[2].default_value=.25
    nt.links.new(wv.outputs['Fac'],m2.inputs[0]);add=nt.nodes.new('ShaderNodeMath');add.operation='ADD';nt.links.new(m2.outputs[0],add.inputs[0]);nt.links.new(nz.outputs['Fac'],add.inputs[1])
    cl=nt.nodes.new('ShaderNodeClamp');nt.links.new(add.outputs[0],cl.inputs[0]);nt.links.new(cl.outputs[0],mix.inputs[0]);nt.links.new(mix.outputs[2],p.inputs['Base Color'])
    bp=nt.nodes.new('ShaderNodeBump');bp.inputs['Strength'].default_value=.6;bp.inputs['Distance'].default_value=.0008;nt.links.new(add.outputs[0],bp.inputs['Height']);nt.links.new(bp.outputs['Normal'],p.inputs['Normal'])
def forged_extra(nt,p):
    # hand-forged soft iron: dark scale, hammer facets, a little rust in the hollows
    tc=nt.nodes.new('ShaderNodeTexCoord');vo=nt.nodes.new('ShaderNodeTexVoronoi');vo.inputs['Scale'].default_value=90
    nz=nt.nodes.new('ShaderNodeTexNoise');nz.inputs['Scale'].default_value=40;nz.inputs['Detail'].default_value=6
    nt.links.new(tc.outputs['Object'],vo.inputs['Vector']);nt.links.new(tc.outputs['Object'],nz.inputs['Vector'])
    mix=nt.nodes.new('ShaderNodeMix');mix.data_type='RGBA';mix.inputs[6].default_value=(.10,.095,.09,1);mix.inputs[7].default_value=(.30,.16,.08,1)
    ramp=nt.nodes.new('ShaderNodeValToRGB');ramp.color_ramp.elements[0].position=.55;ramp.color_ramp.elements[1].position=.75
    nt.links.new(nz.outputs['Fac'],ramp.inputs['Fac']);nt.links.new(ramp.outputs['Color'],mix.inputs[0]);nt.links.new(mix.outputs[2],p.inputs['Base Color'])
    rr=nt.nodes.new('ShaderNodeMapRange');rr.inputs['To Min'].default_value=.45;rr.inputs['To Max'].default_value=.8;nt.links.new(ramp.outputs['Color'],rr.inputs['Value']);nt.links.new(rr.outputs[0],p.inputs['Roughness'])
    mm=nt.nodes.new('ShaderNodeMapRange');mm.inputs['To Min'].default_value=.85;mm.inputs['To Max'].default_value=.2;nt.links.new(ramp.outputs['Color'],mm.inputs['Value']);nt.links.new(mm.outputs[0],p.inputs['Metallic'])
    bp=nt.nodes.new('ShaderNodeBump');bp.inputs['Strength'].default_value=.35;bp.inputs['Distance'].default_value=.0006;nt.links.new(vo.outputs['Distance'],bp.inputs['Height']);nt.links.new(bp.outputs['Normal'],p.inputs['Normal'])
def steel_extra(nt,p):
    tc=nt.nodes.new('ShaderNodeTexCoord');nz=nt.nodes.new('ShaderNodeTexNoise');nz.inputs['Scale'].default_value=300;nz.inputs['Detail'].default_value=10
    nt.links.new(tc.outputs['Object'],nz.inputs['Vector'])
    rr=nt.nodes.new('ShaderNodeMapRange');rr.inputs['To Min'].default_value=.22;rr.inputs['To Max'].default_value=.42;nt.links.new(nz.outputs['Fac'],rr.inputs['Value']);nt.links.new(rr.outputs[0],p.inputs['Roughness'])
    bp=nt.nodes.new('ShaderNodeBump');bp.inputs['Strength'].default_value=.08;nt.links.new(nz.outputs['Fac'],bp.inputs['Height']);nt.links.new(bp.outputs['Normal'],p.inputs['Normal'])
HEMP=lambda:principled('hemp_rope',(.36,.27,.15),.88,0,extra=hemp_extra)
IRON=lambda:principled('forged_iron',(.12,.11,.10),.6,.7,extra=forged_extra)
STEEL=lambda:principled('oval_steel',(.62,.62,.60),.3,1.0,extra=steel_extra)
# ---------- rope geometry ----------
def strand_curve(name,length,radius,pitch,phase,res=9,y0=0.0):
    cu=bpy.data.curves.new(name,'CURVE');cu.dimensions='3D';sp=cu.splines.new('POLY');n=max(8,int(length/pitch*res))
    sp.points.add(n-1)
    for i in range(n):
        t=i/(n-1);y=y0+t*length;a=phase+2*math.pi*y/pitch;sp.points[i].co=(radius*math.cos(a),y,radius*math.sin(a),1)
    return cu
def rope_mesh(name,length,R=0.0058,pitch=0.034,ring_res=8):
    # three-strand hawser lay, each strand itself slightly twisted; R = strand radius (rope ~11.5mm)
    obs=[]
    for k in range(3):
        cu=strand_curve(f'{name}_s{k}',length,R*1.0,pitch,2*math.pi*k/3);cu.bevel_depth=R*1.08;cu.bevel_resolution=2;cu.use_fill_caps=True
        obs.append(link(bpy.data.objects.new(f'{name}_s{k}',cu)))
    meshes=[to_mesh(o) for o in obs]
    for o in bpy.context.scene.objects:o.select_set(o in meshes)
    bpy.context.view_layer.objects.active=meshes[0];bpy.ops.object.join();ob=bpy.context.object
    return ob
def build_rope_segment():
    reset();ob=rope_mesh('rope_hemp_segment',1.0);ob.data.materials.append(HEMP());finish(ob,'rope_hemp_segment')
# ---------- piton ----------
def build_piton():
    reset();bm=bmesh.new()
    # blade: soft-iron horizontal blade, 105mm long, tapering 16->6mm high, 4->2mm thick, along +Y (into the wall)
    L=0.105;segs=10
    rings=[]
    for i in range(segs+1):
        t=i/segs;y=0.022+t*L;h=0.016*(1-t)+0.006*t;w=0.0042*(1-t)+0.0018*t
        rings.append([bm.verts.new((sx*w/2,y,sz*h/2)) for sx,sz in((-1,-1),(1,-1),(1,1),(-1,1))])
    for a,b in zip(rings,rings[1:]):
        for j in range(4):bm.faces.new((a[j],a[(j+1)%4],b[(j+1)%4],b[j]))
    bm.faces.new(list(reversed(rings[-1])))
    # forged head: upset, slightly mushroomed striking block 0..22mm
    head=bmesh.ops.create_cube(bm,size=1.0)['verts']
    for v in head:v.co=Vector((v.co.x*0.012,0.011+v.co.y*0.022,v.co.z*0.02))
    bmesh.ops.bevel(bm,geom=[e for e in bm.edges if all(v in head for v in e.verts)],offset=0.0018,segments=2,affect='EDGES')
    me=bpy.data.meshes.new('piton');bm.to_mesh(me);ob=link(bpy.data.objects.new('piton_body',me))
    # eye: forged loop below the head, and the welded ring through it
    bpy.ops.mesh.primitive_torus_add(major_radius=0.009,minor_radius=0.0032,major_segments=20,minor_segments=8,location=(0,0.011,-0.017),rotation=(0,math.pi/2,0));eye=bpy.context.object
    bpy.ops.mesh.primitive_torus_add(major_radius=0.019,minor_radius=0.0028,major_segments=28,minor_segments=8,location=(0,0.011,-0.035),rotation=(math.pi/2,0,0));ring=bpy.context.object
    for o in bpy.context.scene.objects:o.select_set(o in(ob,eye,ring))
    bpy.context.view_layer.objects.active=ob;bpy.ops.object.join();ob=bpy.context.object;ob.data.materials.append(IRON())
    # local frame: origin = striking face centre, +Y = into the wall, ring hangs toward -Z
    finish(ob,'piton_forged_ring')
# ---------- carabiner ----------
def build_carabiner():
    reset()
    # steel oval, 102 x 60 mm outside, 10 mm rod; long axis along -Z from origin (origin = top inner bend, where it hangs)
    cu=bpy.data.curves.new('kar','CURVE');cu.dimensions='3D';sp=cu.splines.new('POLY');n=96;a,b=0.025,0.046;sp.points.add(n-1)
    for i in range(n):
        t=2*math.pi*i/n;sp.points[i].co=(a*math.cos(t),0,-b-b*math.sin(t)+0.0,1)
    sp.use_cyclic_u=True;cu.bevel_depth=0.005;cu.bevel_resolution=3
    ob=link(bpy.data.objects.new('kar',cu));ob=to_mesh(ob)
    # gate: straight bar on the +X side with hinge barrel and nose notch
    bpy.ops.mesh.primitive_cylinder_add(radius=0.0062,depth=0.012,location=(a,0,-b-0.03),vertices=16);hinge=bpy.context.object
    bpy.ops.mesh.primitive_cylinder_add(radius=0.0058,depth=0.01,location=(a,0,-b+0.032),vertices=16);nose=bpy.context.object
    bpy.ops.mesh.primitive_cylinder_add(radius=0.0016,depth=0.014,location=(a,0,-b-0.03),rotation=(math.pi/2,0,0),vertices=10);rivet=bpy.context.object
    for o in bpy.context.scene.objects:o.select_set(True)
    bpy.context.view_layer.objects.active=ob;bpy.ops.object.join();ob=bpy.context.object;ob.data.materials.append(STEEL())
    finish(ob,'carabiner_oval_steel')
# ---------- waist tie-in ----------
def build_waist_tie():
    reset()
    # bowline-on-a-coil: four wraps of hemp rope round the waist (elliptical, slightly staggered) + bowline knot at the front
    obs=[];rx,rz=0.172,0.132   # model x half-width, model z half-depth  (Blender: x, y)
    for k in range(4):
        cu=bpy.data.curves.new(f'coil{k}','CURVE');cu.dimensions='3D';sp=cu.splines.new('POLY');n=72;sp.points.add(n-1)
        for i in range(n):
            t=2*math.pi*i/n;sp.points[i].co=((rx+0.006*k)*math.sin(t),-(rz+0.005*k)*math.cos(t),0.024*k-0.036+0.004*math.sin(3*t+k),1)
        sp.use_cyclic_u=True;cu.bevel_depth=0.0058;cu.bevel_resolution=2;obs.append(to_mesh(link(bpy.data.objects.new(f'coil{k}',cu))))
    # bowline at the front centre (Blender -Y is the belly side): a nipped loop, the collar round the standing part and the tail
    def path(name,pts,depth=0.0058):
        cu=bpy.data.curves.new(name,'CURVE');cu.dimensions='3D';sp=cu.splines.new('NURBS');sp.points.add(len(pts)-1)
        for i,p in enumerate(pts):sp.points[i].co=(*p,1)
        sp.use_endpoint_u=True;sp.order_u=4;cu.resolution_u=10;cu.bevel_depth=depth;cu.bevel_resolution=2;return to_mesh(link(bpy.data.objects.new(name,cu)))
    f=-0.138
    obs.append(path('bowline_loop',[(-0.03,f,-0.03),(-0.04,f-0.02,0.0),(-0.01,f-0.03,0.03),(0.025,f-0.02,0.02),(0.03,f-0.012,-0.01),(0.0,f-0.03,-0.035),(-0.02,f-0.025,-0.005),(0.01,f-0.035,0.015)]))
    obs.append(path('bowline_collar',[(0.0,f-0.02,0.045),(-0.018,f-0.035,0.05),(-0.02,f-0.045,0.03),(0.0,f-0.045,0.02),(0.018,f-0.035,0.035),(0.012,f-0.02,0.05)]))
    obs.append(path('bowline_tail',[(0.01,f-0.03,0.0),(0.03,f-0.04,-0.02),(0.045,f-0.035,-0.06),(0.05,f-0.025,-0.09)]))
    obs.append(path('live_end_root',[(0.0,f-0.04,0.03),(0.0,f-0.05,0.06),(0.0,f-0.055,0.09)]))
    for o in bpy.context.scene.objects:o.select_set(o in obs)
    bpy.context.view_layer.objects.active=obs[0];bpy.ops.object.join();ob=bpy.context.object;ob.data.materials.append(HEMP())
    # local frame: origin = waist centre at tie height, -Y = belly side (model -Z), live rope leaves the knot upward at (0,-0.193,0.09)
    finish(ob,'waist_tie_bowline_on_coil')
os.makedirs(OUT,exist_ok=True)
build_rope_segment();build_piton();build_carabiner();build_waist_tie()
