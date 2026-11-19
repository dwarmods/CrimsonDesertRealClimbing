import bpy,math
from mathutils import Vector
MO='/workspace/climbmod/rope_slice/models';bpy.ops.wm.read_factory_settings(use_empty=True);sc=bpy.context.scene
sc.render.engine='CYCLES';sc.cycles.device='CPU';sc.cycles.samples=48;sc.cycles.use_denoising=True;sc.render.resolution_x=1200;sc.render.resolution_y=700;sc.view_settings.view_transform='AgX'
w=bpy.data.worlds.new('w');sc.world=w;w.use_nodes=True;w.node_tree.nodes['Background'].inputs[0].default_value=(.5,.5,.52,1);w.node_tree.nodes['Background'].inputs[1].default_value=.5
def app(stem,loc,rot,scale=1):
    bpy.ops.wm.append(filepath=f'{MO}/{stem}.blend/Object/{stem}',directory=f'{MO}/{stem}.blend/Object/',filename=stem);o=bpy.data.objects[stem];o.location=loc;o.rotation_euler=rot;o.scale=(scale,)*3;return o
app('piton_forged_ring',(-0.30,0,0.05),(0,0,math.radians(90)),2.2)
app('carabiner_oval_steel',(-0.05,0,0.14),(0,0,math.radians(20)),2.2)
app('waist_tie_bowline_on_coil',(0.32,0.05,0.05),(math.radians(-25),0,math.radians(-20)),0.9)
app('rope_hemp_segment',(-0.45,-0.12,-0.12),(0,math.radians(90),math.radians(-80)),0.9)
bpy.ops.mesh.primitive_plane_add(size=5,location=(0,0,-0.15));m=bpy.data.materials.new('bg');m.use_nodes=True;m.node_tree.nodes['Principled BSDF'].inputs['Base Color'].default_value=(.32,.30,.27,1);bpy.context.object.data.materials.append(m)
bpy.ops.object.light_add(type='AREA',location=(0.6,-0.8,1.2));l=bpy.context.object;l.data.energy=120;l.data.size=1.2;l.rotation_euler=(Vector((0,0,0))-l.location).to_track_quat('-Z','Y').to_euler()
bpy.ops.object.camera_add(location=(0,-1.25,0.75));c=bpy.context.object;c.rotation_euler=(Vector((0,0,0.02))-c.location).to_track_quat('-Z','Y').to_euler();c.data.lens=50;sc.camera=c
sc.render.filepath='/workspace/climbmod/rope_slice/preview/still_props_old_school.png';bpy.ops.render.render(write_still=True)
