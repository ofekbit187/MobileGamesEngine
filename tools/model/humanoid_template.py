#!/usr/bin/env python3
"""Authoring pipeline for the humanoid template body (task 8.12, ADR 0005).

Run headless:  python3 tools/model/humanoid_template.py [outdir]
(uses the `bpy` module — Blender as a Python library, no GUI needed)

The body is Blender Studio's **Human Base Meshes** bundle (CC0), object
`GEO-body_male_realistic`: an anatomically modelled, UV-unwrapped, all-quad
male base mesh. THE GEOMETRY IS NOT MODIFIED. This script only:

  1. places it (180 deg yaw so it faces the engine's -Z, uniform scale to
     1.75 m, soles on the ground, centred on the mid-line)
  2. builds the engine's canonical 17-joint rig AT THE MESH'S OWN JOINTS —
     the rig is fitted to the model, never the model bent onto the rig
  3. skins it with bone-heat weights, clamped to four influences and pruned
     of the long-range leakage bone heat leaves behind
  4. derives LOD1/LOD2 by decimation
  5. cuts the shipped garments out of the body's OWN surface, so every layer
     encloses the one beneath it by construction (CHARACTERS.md 5.4)
  6. exports glTF for the engine's import path (P5)

The bundle is a build-time input, not a runtime dependency: the baked
`.mgeskin` assets are committed, so the engine builds and runs without it.
Download (CC0, Blender Studio):
  https://download.blender.org/demo/asset-bundles/human-base-meshes/
Put human_base_meshes_bundle.blend in tools/model/vendor/ or point
MGE_HUMAN_BASE_BLEND at it.

Coordinates below are ENGINE space (Y up, character faces -Z, +X is the
character's left) and converted to Blender space on the way in.
"""

import math
import os
import sys

import bpy
import bmesh
from mathutils import Vector

HEIGHT = 1.75
LOD_TRIANGLES = (2200, 1200, 560)
BASE_OBJECT = "GEO-body_male_realistic"

# The canonical rig, FITTED to the base mesh's own anatomy (measured from the
# untouched model, scaled to 1.75 m). The engine's buildSkeleton() reproduces
# these positions for the template variant — mesh and rig agree by
# construction, and the model keeps its authored A-pose as its bind pose.
JOINTS = [
    ("Hips",       None,        (0.0000, 0.9000, 0.0000)),
    ("Spine",      "Hips",      (0.0000, 1.0800, 0.0000)),
    ("Chest",      "Spine",     (0.0000, 1.2800, -0.0100)),
    ("Neck",       "Chest",     (0.0000, 1.5000, -0.0100)),
    ("Head",       "Neck",      (0.0000, 1.5800, -0.0200)),
    ("UpperArmL",  "Chest",     (0.1850, 1.4500, 0.0000)),
    ("ForearmL",   "UpperArmL", (0.2990, 1.1500, -0.0450)),
    ("HandL",      "ForearmL",  (0.4010, 0.8780, -0.0910)),
    ("UpperArmR",  "Chest",     (-0.1850, 1.4500, 0.0000)),
    ("ForearmR",   "UpperArmR", (-0.2990, 1.1500, -0.0450)),
    ("HandR",      "ForearmR",  (-0.4010, 0.8780, -0.0910)),
    ("ThighL",     "Hips",      (0.1000, 0.8800, 0.0100)),
    ("ShinL",      "ThighL",    (0.1550, 0.4300, -0.0340)),
    ("FootL",      "ShinL",     (0.1770, 0.1150, -0.0550)),
    ("ThighR",     "Hips",      (-0.1000, 0.8800, 0.0100)),
    ("ShinR",      "ThighR",    (-0.1550, 0.4300, -0.0340)),
    ("FootR",      "ShinR",     (-0.1770, 0.1150, -0.0550)),
]

BUNDLE_CANDIDATES = [
    os.environ.get("MGE_HUMAN_BASE_BLEND", ""),
    os.path.join(os.path.dirname(__file__), "vendor", "human_base_meshes_bundle.blend"),
    os.path.expanduser("~/human_base_meshes_bundle.blend"),
]


def to_blender(p):
    """Engine (x, y up, z back) -> Blender (x, y front, z up)."""
    return Vector((p[0], -p[2], p[1]))


def reset_scene():
    bpy.ops.wm.read_factory_settings(use_empty=True)


def new_object(name, data):
    obj = bpy.data.objects.new(name, data)
    bpy.context.collection.objects.link(obj)
    return obj


def activate(obj):
    bpy.ops.object.select_all(action='DESELECT')
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj


def triangles(obj):
    return sum(len(p.vertices) - 2 for p in obj.data.polygons)


def find_bundle():
    for path in BUNDLE_CANDIDATES:
        if path and os.path.isfile(path):
            return path
    raise SystemExit(
        "Human Base Meshes bundle not found. Download it (CC0, Blender Studio)\n"
        "from https://download.blender.org/demo/asset-bundles/human-base-meshes/\n"
        "and put human_base_meshes_bundle.blend in tools/model/vendor/.")


def load_base_body():
    """Appends the CC0 base mesh and PLACES it. No geometry is edited: the
    only transform is a yaw to face the engine's forward axis, a uniform
    scale to the template height, and a translation to stand on the ground."""
    bundle = find_bundle()
    with bpy.data.libraries.load(bundle, link=False) as (src, dst):
        if BASE_OBJECT not in src.objects:
            raise SystemExit("%s not found in %s" % (BASE_OBJECT, bundle))
        dst.objects = [BASE_OBJECT]
    obj = [o for o in bpy.data.objects if o is not None and o.type == 'MESH'][0]
    if obj.name not in bpy.context.collection.objects:
        bpy.context.collection.objects.link(obj)
    for m in list(obj.modifiers):        # multires cage -> base level
        obj.modifiers.remove(m)
    obj.name = "HumanoidTemplate"
    obj.data.name = "HumanoidTemplate"

    obj.location = (0.0, 0.0, 0.0)
    obj.scale = (1.0, 1.0, 1.0)
    obj.rotation_euler = (0.0, 0.0, math.radians(180))   # face the engine's -Z
    activate(obj)
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)

    me = obj.data
    zs = [v.co.z for v in me.vertices]
    xs = [v.co.x for v in me.vertices]
    lo, hi = min(zs), max(zs)
    cx = (min(xs) + max(xs)) * 0.5
    scale = HEIGHT / (hi - lo)
    for v in me.vertices:
        v.co = Vector(((v.co.x - cx) * scale, v.co.y * scale, (v.co.z - lo) * scale))
    me.update()
    activate(obj)
    bpy.ops.object.shade_smooth()
    return obj


def build_armature():
    """The canonical rig at the fitted joint positions."""
    arm_data = bpy.data.armatures.new("HumanoidRig")
    arm = new_object("HumanoidRig", arm_data)
    activate(arm)
    bpy.ops.object.mode_set(mode='EDIT')
    pos = {n: to_blender(p) for n, _, p in JOINTS}
    children = {}
    for n, parent, _ in JOINTS:
        children.setdefault(parent, []).append(n)
    bones = {}
    for n, parent, _ in JOINTS:
        b = arm_data.edit_bones.new(n)
        head = pos[n]
        kids = children.get(n, [])
        if kids:
            tail = pos[kids[0]]
        else:
            base = pos[parent] if parent else head
            d = head - base
            tail = head + (d.normalized() * 0.12 if d.length > 1e-6 else Vector((0, 0, 0.12)))
        if (tail - head).length < 1e-4:
            tail = head + Vector((0, 0, 0.05))
        b.head, b.tail = head, tail
        bones[n] = b
    for n, parent, _ in JOINTS:
        if parent:
            bones[n].parent = bones[parent]
    bpy.ops.object.mode_set(mode='OBJECT')
    return arm


def clamp_influences(obj):
    """Four bones per vertex is the hardware skinning cap every mobile GPU
    agrees on — and the engine's vertex layout. Decimation merges the influence
    sets of the vertices it collapses, so this has to be re-applied after every
    reduction, not just after binding."""
    activate(obj)
    bpy.ops.object.vertex_group_limit_total(limit=4)
    bpy.ops.object.vertex_group_normalize_all(lock_active=False)


def bind(obj, arm):
    bpy.ops.object.select_all(action='DESELECT')
    obj.select_set(True)
    arm.select_set(True)
    bpy.context.view_layer.objects.active = arm
    bpy.ops.object.parent_set(type='ARMATURE_AUTO')   # bone-heat weights
    clamp_influences(obj)


def reduce_to(obj, target):
    tris = triangles(obj)
    if tris <= target:
        return
    mod = obj.modifiers.new("decimate", 'DECIMATE')
    mod.decimate_type = 'COLLAPSE'
    mod.ratio = target / tris
    mod.use_collapse_triangulate = True
    activate(obj)
    bpy.ops.object.modifier_apply(modifier="decimate")


def duplicate_reduced(obj, arm, name, target):
    dup = obj.copy()
    dup.data = obj.data.copy()
    dup.name = name
    bpy.context.collection.objects.link(dup)
    reduce_to(dup, target)
    # Collapsing an edge merges the two vertices' influence sets, so a reduced
    # mesh reacquires both the fifth influence and the leakage the full one was
    # cleaned of. Re-apply both cleanups.
    clamp_influences(dup)
    prune_far_influences(dup, arm)
    activate(dup)
    bpy.ops.object.shade_smooth()
    return dup


# --------------------------------------------------------------- weights ----

# Bone heat is a diffusion solve: it leaks. On this body it leaves the ankle
# ~10% thigh influence, which reads as the foot swimming when the knee bends.
# An influence is kept only if its bone is within MARGIN of the CLOSEST bone
# influencing that vertex — near enough to preserve every real blend band
# (elbow, knee, shoulder), far enough to cut leakage across a joint.
WEIGHT_MARGIN = 0.10 * (HEIGHT / 1.75)


def _segment_distance(p, a, b):
    ab = b - a
    denom = ab.dot(ab)
    t = 0.0 if denom < 1e-12 else max(0.0, min(1.0, (p - a).dot(ab) / denom))
    return (p - (a + ab * t)).length


def prune_far_influences(obj, arm):
    """Drops leaked influences and renormalises. Returns (dropped, groups)."""
    segments = {}
    for bone in arm.data.bones:
        segments[bone.name] = (bone.head_local.copy(), bone.tail_local.copy())
    dropped = 0
    for v in obj.data.vertices:
        entries = []
        for g in v.groups:
            name = obj.vertex_groups[g.group].name
            if name not in segments or g.weight <= 0.0:
                continue
            head, tail = segments[name]
            entries.append([g.group, name, g.weight, _segment_distance(v.co, head, tail)])
        if not entries:
            continue
        nearest = min(e[3] for e in entries)
        keep = [e for e in entries if e[3] <= nearest + WEIGHT_MARGIN]
        dropped += len(entries) - len(keep)
        total = sum(e[2] for e in keep)
        if total <= 0.0:
            keep, total = [min(entries, key=lambda e: e[3])], 1.0
            keep[0][2] = 1.0
        kept_groups = set(e[0] for e in keep)
        for e in entries:
            if e[0] not in kept_groups:
                obj.vertex_groups[e[0]].remove([v.index])
        for e in keep:
            obj.vertex_groups[e[0]].add([v.index], e[2] / total, 'REPLACE')
    return dropped, len(obj.vertex_groups)


def weight_report(obj, arm):
    """Re-checks the invariant the pruning pass establishes and the engine's
    tests assert: every influence on a vertex belongs to a bone essentially as
    close to it as the closest influencing bone. Returns the violation count."""
    segs = {b.name: (b.head_local.copy(), b.tail_local.copy()) for b in arm.data.bones}
    violations, worst, worst_bone = 0, 0.0, ""
    counts = []
    for v in obj.data.vertices:
        entries = []
        for g in v.groups:
            if g.weight <= 0.0:
                continue
            name = obj.vertex_groups[g.group].name
            entries.append((name, _segment_distance(v.co, *segs[name])))
        counts.append(len(entries))
        if not entries:
            continue
        nearest = min(d for _, d in entries)
        for name, d in entries:
            if d > nearest + WEIGHT_MARGIN + 1e-5:
                violations += 1
            if d > worst:
                worst, worst_bone = d, name
    print("   weights: max influences %d, mean %.2f, farthest from its own "
          "bone %.3f m (%s), violations %d" %
          (max(counts), sum(counts) / float(len(counts)), worst, worst_bone, violations))
    return violations


# -------------------------------------------------------------- garments ----

# Which bone owns a vertex decides which body region it is in — the same rule
# the engine's importer uses, so the segmentation the garments are cut along
# is the segmentation the engine masks along.
REGION_OF_BONE = {
    "Head": "Scalp", "Neck": "Neck",
    "Hips": "Torso", "Spine": "Torso", "Chest": "Torso",
    "UpperArmL": "ArmL", "ForearmL": "ArmL", "HandL": "HandL",
    "UpperArmR": "ArmR", "ForearmR": "ArmR", "HandR": "HandR",
    "ThighL": "LegL", "ShinL": "LegL", "FootL": "FootL",
    "ThighR": "LegR", "ShinR": "LegR", "FootR": "FootR",
}

# kind -> (layer, triangle budget, cut rules). A cut rule is a set of body
# regions and the engine-Y band of them to take.
#
# A garment must cover EVERY region the engine masks for it (garmentCoverage),
# or the masked body leaves a hole — so the torso garments take the whole torso
# and the boots the whole foot. What the extra rules add is the part that only
# has to look right: a short sleeve over the deltoid, so the tunic's boundary
# falls on the smooth ring of the upper arm instead of the jagged shoulder
# seam, and a shaft over the ankle so the boot ends on the calf.
GARMENTS = {
    "tunic":      (1, 760, [(("Torso",), -9.0, 9.0), (("ArmL", "ArmR"), 1.33, 9.0)]),
    "armour":     (2, 760, [(("Torso",), -9.0, 9.0), (("ArmL", "ArmR"), 1.30, 9.0)]),
    "trousers":   (1, 720, [(("LegL", "LegR"), -9.0, 9.0)]),
    "boots":      (1, 520, [(("FootL", "FootR"), -9.0, 9.0),
                            (("LegL", "LegR"), -9.0, 0.26)]),
    "hair_short": (2, 360, [(("Scalp",), 1.655, 9.0)]),
    "hair_long":  (2, 480, [(("Scalp",), 1.645, 9.0), (("Neck",), 1.46, 9.0)]),
}
LAYER_OFFSET = (0.004, 0.009, 0.017)   # base / mid / outer, metres


def vertex_regions(obj):
    """Region per vertex, from the bone that moves it most."""
    out = []
    for v in obj.data.vertices:
        best, best_w = None, -1.0
        for g in v.groups:
            if g.weight > best_w:
                best_w, best = g.weight, obj.vertex_groups[g.group].name
        out.append(REGION_OF_BONE.get(best, "Torso"))
    return out


def build_garment(body, arm, name, layer, budget, rules):
    """Cuts the garment out of the body's own surface and pushes it out by the
    layer thickness. It therefore fits the body exactly, shares the body's
    skin weights, and encloses whatever layer sits beneath it."""
    dup = body.copy()
    dup.data = body.data.copy()
    dup.name = "Garment_" + name
    bpy.context.collection.objects.link(dup)

    per_vertex = vertex_regions(dup)
    bm = bmesh.new()
    bm.from_mesh(dup.data)
    bm.verts.ensure_lookup_table()
    doomed = []
    for f in bm.faces:
        y = sum(v.co.z for v in f.verts) / len(f.verts)   # blender Z = engine Y
        inside = False
        for regions, y0, y1 in rules:
            keep = set(regions)
            votes = sum(1 for v in f.verts if per_vertex[v.index] in keep)
            if votes * 2 > len(f.verts) and y0 <= y <= y1:
                inside = True
                break
        if not inside:
            doomed.append(f)
    bmesh.ops.delete(bm, geom=doomed, context='FACES')
    bm.to_mesh(dup.data)
    bm.free()
    if len(dup.data.polygons) == 0:
        raise RuntimeError("garment %s selected no faces" % name)

    # Solidify doubles the shell and adds a rim, so the cut surface is reduced
    # to a little under half the garment's own budget.
    reduce_to(dup, max(48, int(budget * 0.45)))
    clamp_influences(dup)
    prune_far_influences(dup, arm)

    bm = bmesh.new()
    bm.from_mesh(dup.data)
    bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=1e-5)
    bm.to_mesh(dup.data)
    bm.free()

    # Push along the surface normal, then give the shell a thickness so it is
    # closed from every angle (no backfaces at a hem).
    offset = LAYER_OFFSET[min(layer, len(LAYER_OFFSET) - 1)]
    bm = bmesh.new()
    bm.from_mesh(dup.data)
    bm.normal_update()
    for v in bm.verts:
        v.co += v.normal * offset
    bm.to_mesh(dup.data)
    bm.free()

    solid = dup.modifiers.new("solidify", 'SOLIDIFY')
    solid.thickness = 0.004
    solid.offset = -1.0            # grow inward, so the outer surface stays put
    activate(dup)
    bpy.ops.object.modifier_apply(modifier="solidify")
    bpy.ops.object.shade_smooth()

    if not any(m.type == 'ARMATURE' for m in dup.modifiers):
        dup.modifiers.new("armature", 'ARMATURE').object = arm
        dup.parent = arm
    return dup


def export(obj, arm, path):
    bpy.ops.object.select_all(action='DESELECT')
    obj.select_set(True)
    arm.select_set(True)
    bpy.context.view_layer.objects.active = arm
    bpy.ops.export_scene.gltf(
        filepath=path, export_format='GLB', use_selection=True, export_yup=True,
        export_apply=False, export_skins=True, export_animations=False,
        export_materials='NONE', export_normals=True, export_texcoords=True)


def health(obj, tag):
    bm = bmesh.new()
    bm.from_mesh(obj.data)
    boundary = sum(1 for e in bm.edges if len(e.link_faces) == 1)
    nonmanifold = sum(1 for e in bm.edges if len(e.link_faces) > 2)
    volume = bm.calc_volume(signed=True)
    bm.free()
    print("   %-16s %6d tris  volume %.4f m3  holes %d  non-manifold %d" %
          (tag, triangles(obj), volume, boundary, nonmanifold))


def add_preview_scene():
    scene = bpy.context.scene
    scene.render.engine = 'CYCLES'
    scene.cycles.device = 'CPU'
    scene.cycles.samples = 28
    scene.render.resolution_x = 560
    scene.render.resolution_y = 900
    world = bpy.data.worlds.new("W")
    world.use_nodes = True
    world.node_tree.nodes["Background"].inputs[0].default_value = (0.55, 0.62, 0.72, 1)
    world.node_tree.nodes["Background"].inputs[1].default_value = 1.1
    scene.world = world
    light_data = bpy.data.lights.new("key", type='SUN')
    light_data.energy = 3.2
    light_data.angle = math.radians(12)
    light = new_object("key", light_data)
    light.rotation_euler = (math.radians(58), 0, math.radians(38))
    cam_data = bpy.data.cameras.new("cam")
    cam_data.type = 'ORTHO'
    cam_data.ortho_scale = 1.95
    cam = new_object("cam", cam_data)
    scene.camera = cam
    return cam


def render_views(cam, outdir, tag, views=None, ortho=1.95, target=(0, 0, 0.88)):
    scene = bpy.context.scene
    cam.data.ortho_scale = ortho
    tgt = Vector(target)
    views = views or (("front", 0.0), ("threequarter", 40.0), ("side", 90.0))
    for name, angle in views:
        a = math.radians(angle)
        cam.location = tgt + Vector((math.sin(a) * 3.0, -math.cos(a) * 3.0, 0.0))
        cam.rotation_euler = (tgt - cam.location).to_track_quat('-Z', 'Y').to_euler()
        scene.render.filepath = os.path.join(outdir, "blender_%s_%s.png" % (tag, name))
        bpy.ops.render.render(write_still=True)


def main():
    outdir = sys.argv[1] if len(sys.argv) > 1 else "."
    os.makedirs(outdir, exist_ok=True)

    reset_scene()
    body = load_base_body()
    health(body, "base (CC0)")

    arm = build_armature()
    bind(body, arm)
    dropped, groups = prune_far_influences(body, arm)
    print("   pruned %d leaked influences across %d bones" % (dropped, groups))
    leaks = weight_report(body, arm)
    if leaks > 0:
        raise RuntimeError("%d skin influences survived the prune" % leaks)

    exported = []
    lods = [duplicate_reduced(body, arm, "Body_LOD%d" % i, t)
            for i, t in enumerate(LOD_TRIANGLES)]
    for level, lod in enumerate(lods):
        health(lod, "LOD%d" % level)
        if weight_report(lod, arm) > 0:
            raise RuntimeError("LOD%d skin weights leak" % level)
        path = os.path.join(outdir, "humanoid_template_lod%d.glb" % level)
        export(lod, arm, path)
        exported.append((path, triangles(lod)))
        lod.hide_render = True

    garments = []
    for name, (layer, budget, rules) in sorted(GARMENTS.items()):
        g = build_garment(lods[0], arm, name, layer, budget, rules)
        health(g, name)
        path = os.path.join(outdir, "garment_%s.glb" % name)
        export(g, arm, path)
        exported.append((path, triangles(g)))
        g.hide_render = True
        garments.append(g)

    for path, tris in exported:
        print("exported %-34s %5d tris %8d bytes" %
              (os.path.basename(path), tris, os.path.getsize(path)))

    body.hide_render = False
    cam = add_preview_scene()
    render_views(cam, outdir, "body")
    lods[0].hide_render = False
    body.hide_render = True
    render_views(cam, outdir, "lod0")
    print("done")


if __name__ == "__main__":
    main()
