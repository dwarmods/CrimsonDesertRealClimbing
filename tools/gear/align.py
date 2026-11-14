# align our models to the game base meshes exported by CDMW; export models/aligned/*.fbx and compare renders
import bpy, math
from mathutils import Matrix, Vector as V
s=open("/workspace/climbmod/scripts/build.py").read(); exec(s[:s.index('for v in ("adze","hammer")')])
r=open("/workspace/climbmod/scripts/render_baked.py").read(); exec(r[r.index("def baked"):r.index("reset()")])
T="/workspace/climbmod/textures"; BX="/workspace/climbmod/base/export"; AL=f"{OUT}/models/aligned"
GRIP=0.21   # base axe: origin is 0.211 m above the butt end -> hand grip
FOOT_X=0.206; SOLE_Z=-0.008; CR_WIDTH_SCALE=1.2
def load_ours(n):
    with bpy.data.libraries.load(f"{OUT}/models/{n}.blend") as (a,b): b.objects=[n]
    o=link(bpy.data.objects[n]); o.data=o.data.copy(); o.data.materials.clear(); o.data.materials.append(baked(n)); return o
def load_base(n):
    bpy.ops.import_scene.fbx(filepath=f"{BX}/{n}.fbx"); obs=list(bpy.context.selected_objects)
    clay=bpy.data.materials.new("clay"); clay.use_nodes=True; clay.node_tree.nodes["Principled BSDF"].inputs["Base Color"].default_value=(0.55,0.5,0.45,1)
    for o in obs:
        if o.type=='MESH': o.data.materials.clear(); o.data.materials.append(clay)
    return [o for o in obs if o.type=='MESH']
def bake_xform(o,M):
    o.data.transform(M@o.matrix_world); o.matrix_world=Matrix.Identity(4)
def export(objs,name):
    bpy.ops.object.select_all(action='DESELECT')
    for o in objs: o.select_set(True)
    bpy.context.view_layer.objects.active=objs[0]
    bpy.ops.export_scene.fbx(filepath=f"{AL}/{name}.fbx",use_selection=True,apply_unit_scale=True,mesh_smooth_type='FACE',use_triangles=True,path_mode='COPY',embed_textures=False)
def bbox(objs):
    mn=V((1e9,)*3); mx=V((-1e9,)*3)
    for o in objs:
        for v in o.data.vertices: w=o.matrix_world@v.co; mn=V(map(min,mn,w)); mx=V(map(max,mx,w))
    return [round(x,3) for x in mn],[round(x,3) for x in mx]
# ---- axes: ours Z-up spike@0 -> game hand space: shaft +Y, origin at grip, pick toward -X, thickness Z
reset()
AXE=Matrix.Translation((0,-GRIP,0))@Matrix.Rotation(-math.pi/2,4,'X')
ours=[]
for n in ("piolet_adze","piolet_hammer"):
    o=load_ours(n); bake_xform(o,AXE); export([o],n); ours.append(o); print("ALIGN",n,bbox([o]))
base=load_base("cd_phm_01_axe_0001"); print("ALIGN base_axe",bbox(base))
# compare render: stand everything up (+90 X) side by side in X
show=Matrix.Rotation(math.pi/2,4,'X')
for i,o in enumerate(base+ours):
    k=0 if o in base else (1 if o is ours[0] else 2)
    o.matrix_world=Matrix.Translation((k*0.45,0,GRIP+0.02))@show@(o.matrix_world if k==0 else Matrix.Identity(4))
bpy.context.view_layer.update()
studio(base+ours,"base_compare",view=(0.0,-1.0,0.25),lens=50,pad=1.1)
# ---- crampons: ours X fwd -> game +Y (toe), under each boot sole
reset()
cs=[]
for side in (1,-1):
    o=load_ours("crampon_hinged_12pt"); o.name=f"crampon_{'R' if side>0 else 'L'}"
    zb_top=0.0082   # top of frame tube in our model
    M=Matrix.Translation((side*FOOT_X,0,SOLE_Z-zb_top))@Matrix.Rotation(math.pi/2,4,'Z')@Matrix.Diagonal((1,CR_WIDTH_SCALE*(1 if side<0 else -1),1,1))
    bake_xform(o,M)
    if side>0: o.data.flip_normals()
    cs.append(o)
bpy.ops.object.select_all(action='DESELECT')
for o in cs: o.select_set(True)
bpy.context.view_layer.objects.active=cs[0]; bpy.ops.object.join(); cp=bpy.context.object; cp.name="crampon_pair"
export([cp],"crampon_pair"); print("ALIGN crampon_pair",bbox([cp]))
boot=load_base("cd_phm_00_foot_0001"); print("ALIGN boot",bbox(boot))
studio(boot+[cp],"crampon_on_boot",view=(1.0,-0.7,0.45),lens=50,pad=1.05)
