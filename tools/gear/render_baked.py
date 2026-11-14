# re-render full_set using only the baked texture maps (what the game will see)
import bpy, math
s=open("/workspace/climbmod/scripts/build.py").read()
exec(s[:s.index('for v in ("adze","hammer")')])
T="/workspace/climbmod/textures"
def baked(n):
    m=bpy.data.materials.new(n+"_baked"); m.use_nodes=True; nt=m.node_tree; p=nt.nodes["Principled BSDF"]
    def tex(t,cs):
        x=nt.nodes.new('ShaderNodeTexImage'); x.image=bpy.data.images.load(f"{T}/{n}_{t}.png"); x.image.colorspace_settings.name=cs; return x
    a=tex("albedo","sRGB"); o=tex("orm","Non-Color"); nm=tex("normal","Non-Color")
    sp=nt.nodes.new('ShaderNodeSeparateColor'); nt.links.new(o.outputs[0],sp.inputs[0])
    mix=nt.nodes.new('ShaderNodeMix'); mix.data_type='RGBA'; mix.blend_type='MULTIPLY'; mix.inputs[0].default_value=0.6
    nt.links.new(a.outputs[0],mix.inputs[6]); nt.links.new(sp.outputs[0],mix.inputs[7]); nt.links.new(mix.outputs[2],p.inputs['Base Color'])
    nt.links.new(sp.outputs[1],p.inputs['Roughness']); nt.links.new(sp.outputs[2],p.inputs['Metallic'])
    nmn=nt.nodes.new('ShaderNodeNormalMap'); nt.links.new(nm.outputs[0],nmn.inputs['Color']); nt.links.new(nmn.outputs[0],p.inputs['Normal'])
    return m
reset()
names=["piolet_adze","piolet_hammer","crampon_hinged_12pt"]
for n in names:
    with bpy.data.libraries.load(f"{OUT}/models/{n}.blend") as (src,dst): dst.objects=[n]
for o in bpy.data.objects: link(o)
for n in names:
    o=bpy.data.objects[n]; o.data=o.data.copy(); m=baked(n); o.data.materials.clear(); o.data.materials.append(m)
a,h,c=[bpy.data.objects[n] for n in names]
a.location=(-0.05,0.06,0); h.location=(0.08,0.10,0); h.rotation_euler=(0,0,math.radians(160))
c.location=(-0.02,-0.16,0.035)
c2=c.copy(); c2.data=c.data; link(c2); c2.scale.y=-1; c2.location=(-0.02,-0.31,0.035)
bpy.context.view_layer.update()
studio([a,h,c,c2],"full_set",view=(1.0,-1.5,0.75),pad=1.0)
