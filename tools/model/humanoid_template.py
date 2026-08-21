#!/usr/bin/env python3
"""Authoring pipeline for the humanoid template body (task 8.12, ADR 0005).

Run headless:  python3 tools/model/humanoid_template.py [outdir]
(uses the `bpy` module — Blender as a Python library, no GUI needed)

The body is Blender Studio's **Human Base Meshes** bundle (CC0), object
`GEO-body_male_realistic`: an anatomically modelled, UV-unwrapped, all-quad
male base mesh. THE GEOMETRY IS NOT MODIFIED. This script only:

  1. places it (180 deg yaw so it faces the engine's -Z, uniform scale to
     1.75 m, soles on the ground, centred on the mid-line)
  1b. repacks its UV chart from the source's 24-tile UDIM layout into the
     single [0,1] tile with the halves DISJOINT (ADR 0010). Mechanical, via
     tools/model/repack_uv.py — islands move as units, nothing is split, no
     seam is cut, vertex order is untouched. This is not a tidy-up: a UDIM
     layout has no representation in the uint16 vertex UV (B-3), so without it
     the hardened importer refuses the body outright.
  2. builds the engine's canonical 17-joint rig AT THE MESH'S OWN JOINTS —
     the rig is fitted to the model, never the model bent onto the rig
  3. skins it with bone-heat weights, clamped to four influences and pruned
     of the long-range leakage bone heat leaves behind
  4. derives LOD1/LOD2 by decimation
  5. cuts the shipped garments out of the body's OWN surface, so every layer
     encloses the one beneath it by construction (CHARACTERS.md 5.4)
  6. authors the variation scope's morph targets on each LOD (shape keys,
     which glTF carries natively) — see humanoid_morphs.py
  7. exports glTF for the engine's import path (P5)

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
from mathutils.bvhtree import BVHTree

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import humanoid_morphs
import repack_uv

HEIGHT = 1.75
# Triangles the hairline loop costs, measured: 74 for the hairline plane and
# 128 for the ear plane, plus a little slack because the exact count depends on
# where the planes fall through the decimated head. LOD0 is decimated to its
# cap MINUS this, so the loop is paid for out of the 200 ADR 0012 added rather
# than out of the body.
FACE_CUT_HEADROOM = 215
LOD_TRIANGLES = (2400 - FACE_CUT_HEADROOM, 1200, 560)   # decimation targets
# B-5's caps, which LOD0's is no longer equal to: ADR 0012 raised LOD0 to 2 400
# to fund B-9's hairline loop, leaving LOD1/LOD2 alone because the crowd draws
# those and they are the budget P1 defends. The body still DECIMATES to 2 200 —
# the extra 200 are the boundary loop, not licence to decimate less carefully.
LOD_BUDGET = (2400, 1300, 650)
BASE_OBJECT = "GEO-body_male_realistic"

# The cap/face boundary (B-9: "the hairline ... is an authored loop, not an
# accident"). ONE constant, because two things depend on it and they must agree:
# the Face/Scalp split below, and where the hair garments cut their cap. If the
# hairline and the hair's edge drift apart, a hairstyle stops degrading to the
# bald cap the contract promises it degrades to.
HAIRLINE_Y = 1.655

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


_JOINT_POS = None
_JOINT_CHILD = None


def _joint_tables():
    """Joint positions and each joint's first child, in ENGINE coordinates —
    what a bone segment is."""
    global _JOINT_POS, _JOINT_CHILD
    if _JOINT_POS is None:
        _JOINT_POS = {n: p for n, _parent, p in JOINTS}
        _JOINT_CHILD = {}
        for n, parent, _p in JOINTS:
            if parent and parent not in _JOINT_CHILD:
                _JOINT_CHILD[parent] = n
    return _JOINT_POS, _JOINT_CHILD


def _bone_head(name):
    return _joint_tables()[0][name]


def _bone_tail(name):
    pos, child = _joint_tables()
    return pos[child[name]] if name in child else pos[name]


def duplicate_reduced(obj, arm, name, target, morphs=True):
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
    # After the reduction, never before: Blender refuses to apply a decimate
    # modifier to a mesh that already carries shape keys. `morphs=False` defers
    # them entirely, which is what lets one LOD be decimated out of another.
    if morphs:
        add_morphs(dup)
    return dup


def add_morphs(obj):
    humanoid_morphs.add_shape_keys(
        obj, bones={name: (Vector(_bone_head(name)), Vector(_bone_tail(name)))
                    for name in humanoid_morphs.LIMB_BONES})


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

def _joint(name):
    return Vector(dict((n, p) for n, _p, p in JOINTS)[name])


def above(y):
    """Keep everything above the plane y = `y` (engine coordinates)."""
    return ((0.0, y, 0.0), (0.0, 1.0, 0.0))


def below(y):
    return ((0.0, y, 0.0), (0.0, -1.0, 0.0))


def behind(z):
    return ((0.0, 0.0, z), (0.0, 0.0, 1.0))


def along_arm(side, distance):
    """A sleeve ends on a ring around the arm, so its cut plane is
    PERPENDICULAR TO THE ARM BONE, `distance` down from the shoulder.

    A horizontal plane cannot do this job. The template's arms hang about 21
    degrees out, and the armpit sits below the shoulder joint: any horizontal
    cut high enough to look like a short sleeve also slices the armpit in two
    and leaves the sleeve hanging off the shoulder as a separate flap, with the
    inside of the garment showing under it."""
    shoulder = _joint("UpperArm" + side)
    elbow = _joint("Forearm" + side)
    axis = (elbow - shoulder).normalized()
    point = shoulder + axis * distance
    return (tuple(point), tuple(-axis))


def inboard(side):
    """Keep the part of an arm that is inboard of the shoulder joint.

    The armpit floor belongs to the arm's bone, but it is 15 cm down the arm
    from the shoulder JOINT, so a sleeve cut by distance-along-the-bone slices
    it off and the tunic ends up open under the arm. Anything inboard of the
    shoulder is body, not sleeve, and stays whatever the sleeve does."""
    x = _joint("UpperArm" + side).x
    return ((x, 0.0, 0.0), (-1.0 if side == "L" else 1.0, 0.0, 0.0))


# kind -> (layer, triangle budget, cut rules, regions the ENGINE masks).
# A cut rule is a set of body regions and the half-spaces to keep of them; a
# face survives if it is in one of the rules' regions and on the inside of all
# that rule's planes.
#
# Two rules govern the cuts, and both had to be learned the hard way:
#
# 1. A garment must contain EVERY region the engine masks for it
#    (`garmentCoverage`), or the masked body leaves a hole. So the covered
#    regions are taken WHOLE — never sliced by a plane.
#
# 2. Every boundary the eye can see must be a PLANE cut, never a region
#    boundary. Region membership is decided per face by which bone owns it, so
#    a region's outline is a zig-zag one face wide, and a hem cut along one
#    looks like the character is dressed in rags. Each garment therefore
#    extends past its covered regions into a neighbouring one and ends on a
#    plane through the middle of it: the tunic's hem crosses the thighs, its
#    sleeves cross the upper arms, its collar crosses the neck.
GARMENTS = {
    "tunic": (1, 900, [
        (("Torso",),         []),
        (("Neck",),          [below(1.53)]),                       # collar
        (("ArmL",),          [along_arm("L", 0.13)]),              # short sleeve
        (("ArmR",),          [along_arm("R", 0.13)]),
        (("ArmL",),          [inboard("L")]),                      # close the armpit
        (("ArmR",),          [inboard("R")]),
        (("LegL", "LegR"),   [above(0.72)]),                       # hem, over the thighs
    ], ("Torso",)),
    "armour": (2, 900, [
        (("Torso",),         []),
        (("Neck",),          [below(1.55)]),
        (("ArmL",),          [along_arm("L", 0.16)]),
        (("ArmR",),          [along_arm("R", 0.16)]),
        (("ArmL",),          [inboard("L")]),
        (("ArmR",),          [inboard("R")]),
        (("LegL", "LegR"),   [above(0.82)]),   # shorter than the tunic under it
    ], ("Torso",)),
    # Trousers are a BASE layer, not a mid one: the tunic's hem comes down over
    # the thighs, and two garments at the same offset interpenetrate — the
    # trousers were punching through the tunic all down the hips.
    "trousers": (0, 800, [
        (("LegL", "LegR"),   []),
        (("Torso",),         [below(1.10)]),                       # waist
        (("FootL", "FootR"), [above(0.115)]),                      # cuff over the ankle
    ], ("LegL", "LegR")),
    "boots": (1, 620, [
        (("FootL", "FootR"), []),
        (("LegL", "LegR"),   [below(0.26)]),                       # shaft
    ], ("FootL", "FootR")),
    "hair_short": (0, 340, [
        (("Scalp",),         [above(HAIRLINE_Y)]),
    ], ()),
    "hair_long": (0, 460, [
        (("Scalp",),         [above(HAIRLINE_Y - 0.010)]),
        (("Neck",),          [above(1.44), behind(-0.005)]),       # down the back only
        (("Torso",),         [above(1.44), behind(0.010)]),
    ], ()),
}

# base / mid / outer, metres. The gaps are wide because a garment is a
# decimated shell: between its vertices it chords across the surface, so two
# layers only a couple of millimetres apart will punch through each other
# wherever the inner one bulges.
LAYER_OFFSET = (0.011, 0.020, 0.028)

# Where a garment's own build says more than its layer does. A boot is not a
# shirt: it has a sole and a toe box, and it has to swallow the toes of a foot
# the shell no longer has the triangles to follow.
OFFSET_OVERRIDE = {
    "boots": 0.019,        # a sole and a toe box, clear of the trousers' cuff
    "hair_short": 0.008,   # hair overlaps nothing, so it is not on the ladder
    "hair_long": 0.010,
}


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


def split_face_shell(obj, arm, landmarks):
    """Cuts the head into the Scalp cap and a real Face shell (B-8, task 13.7).

    The rig cannot answer this. There is one Head joint (B-1 fixes the 17), so
    every head vertex is Scalp by construction and `BodyRegion::Face` has been
    empty since the body was first imported — the debt ADR 0008 logs against v3,
    and what blocks the first mask, visor or face-covering helm. The face is a
    MASKING division of the head, not an articulated one, so it is labelled by
    the asset rather than derived from a bone: this cuts the boundary and paints
    the front with a material named `Face`, which the importer reads.

    Both planes come off measured landmarks, never off a number chosen by eye:

      * the hairline is `HAIRLINE_Y`, the same height the hair garments cut
        their cap at, so the cap the contract promises as the bald fallback is
        exactly the region a hairstyle covers;
      * the sides stop at the coronal plane through the ears (`ear_z`, measured
        off the widest band of the skull), because everything behind it is
        cranium and nape — hair territory, not face.

    The boundary is CUT, then classified — B-9's authored loop (task 13.7a).
    Bisecting the two planes costs about 200 triangles, measured: 74 for the
    hairline and 128 for the ear plane. Task 13.7 shipped without them because
    LOD0 sat exactly on B-5's old 2 200 cap and buying them by decimating first
    collapsed `body_mesh_has_human_proportions`. ADR 0012 funded them by raising
    the LOD0 cap to 2 400 and holding LOD1/LOD2 where they were, so the body
    still decimates to 2 200 and the extra 200 are the loop itself.
    """
    # Cut the boundary before classifying it, so it is a real edge loop rather
    # than a staircase one face wide (B-9). Only the head is cut: the ear plane
    # crosses the whole body, so bisecting globally would slice the torso, both
    # arms and both legs and spend the budget on nothing.
    planes = [((0.0, HAIRLINE_Y, 0.0), (0.0, -1.0, 0.0)),
              ((0.0, 0.0, landmarks.ear_z), (0.0, 0.0, -1.0))]
    for point, normal in planes:
        head = {i for i, r in enumerate(repack_uv.face_regions(obj, REGION_OF_BONE))
                if r == "Scalp"}
        bm = bmesh.new()
        bm.from_mesh(obj.data)
        bm.faces.ensure_lookup_table()
        # bisect_plane rejects a geom list with any element repeated, and
        # neighbouring faces share verts and edges by definition.
        picked = [f for f in bm.faces if f.index in head]
        verts, edges = set(), set()
        for f in picked:
            verts.update(f.verts)
            edges.update(f.edges)
        bmesh.ops.bisect_plane(bm, geom=picked + list(verts) + list(edges), dist=1e-6,
                               plane_co=to_blender(point), plane_no=to_blender_dir(normal))
        bm.to_mesh(obj.data)
        bm.free()

    # The bisect interpolates weights onto the vertices it creates, which can
    # reintroduce both the fifth influence and the long-range leakage this body
    # was cleaned of — measured, one violation. Re-run both cleanups exactly as
    # `duplicate_reduced` does after its own decimation, and BEFORE the regions
    # are read, since pruning changes which bone moves a vertex most.
    clamp_influences(obj)
    prune_far_influences(obj, arm)

    regions = repack_uv.face_regions(obj, REGION_OF_BONE)
    hairline_b = to_blender((0.0, HAIRLINE_Y, 0.0))
    ear_b = to_blender((0.0, 0.0, landmarks.ear_z))
    faces = 0
    for i, poly in enumerate(obj.data.polygons):
        if regions[i] != "Scalp":
            continue
        c = poly.center
        # Blender space: z is up, and +y is the direction the body FACES
        # (engine -z). Forward is therefore the GREATER y, not the lesser.
        if c.z < hairline_b.z and c.y > ear_b.y:
            regions[i] = "Face"
            faces += 1

    # EVERY region is labelled, not just Face.
    #
    # Labelling only the face left the other eleven to the rig on the engine's
    # side while the chart was packed from the rig on this side — two
    # inferences that have to agree, and they did not: `chart_islands_disjoint`
    # failed on Scalp/Neck over a handful of triangles at the seam, the same
    # class of disagreement that cost three attempts during the repack. Region
    # bounding boxes are unforgiving, so "usually agrees" is not good enough.
    #
    # With all twelve labelled there is nothing left to disagree about: the
    # region a triangle is packed into IS the region the engine reads back. It
    # also gives B-25's per-region groups (task 13.8) their door.
    obj.data.materials.clear()
    slot = {}
    for name in repack_uv.BODY_REGION_ORDER:
        slot[name] = len(obj.data.materials)
        mat = bpy.data.materials.get(name) or bpy.data.materials.new(name)
        obj.data.materials.append(mat)
    for i, poly in enumerate(obj.data.polygons):
        poly.material_index = slot[regions[i]]
    obj.data.update()
    print("   face shell: %d of %d head polygons -> Face (hairline %.3f, ear z %.3f)"
          % (faces, sum(1 for r in regions if r in ("Scalp", "Face")),
             HAIRLINE_Y, landmarks.ear_z))
    return regions


def body_bvh(obj):
    me = obj.data
    return BVHTree.FromPolygons([tuple(v.co) for v in me.vertices],
                                [list(p.vertices) for p in me.polygons])


def to_blender_dir(d):
    """Engine direction (x, y up, z back) -> Blender (x, y front, z up)."""
    return Vector((d[0], -d[2], d[1]))


def bisect_at(bm, planes):
    """Inserts a real edge loop at every cut plane the rules use.

    Without this, a face straddling the plane has to be either in or out and
    the boundary steps around it. After it, no face straddles a plane, so the
    hem is a clean ring."""
    for point, normal in planes:
        bmesh.ops.bisect_plane(bm, geom=list(bm.verts) + list(bm.edges) + list(bm.faces),
                               dist=1e-6, plane_co=to_blender(point),
                               plane_no=to_blender_dir(normal))


MIN_OFFSET = 0.0035         # metres — the thinnest a BASE layer may sit off the skin


def min_offset(layer):
    """Where a garment is squeezed, it still has to stack in the right order.

    The armpit gap is about 15 mm. An outer layer wants 28 mm and cannot have
    it, so it is backed off — and if every layer backs off to the same floor,
    the armour ends up level with the tunic and shows through it. Each layer
    therefore keeps its own floor, and the order survives the pinch even though
    the thicknesses do not. (Down there the garments intersect the arm, which
    is drawn over them and hides it.)"""
    return MIN_OFFSET * (1 + 2 * layer)
SHELL_THICKNESS = 0.003     # metres — solidify, grown inward from that surface
ENCLOSE_STEP = 0.002        # metres pushed out per round where skin shows through
ENCLOSE_RADIUS = 0.030      # metres — how far around a bare spot the push reaches


def inside_body(bvh, point, direction=Vector((0.0, 0.0, 1.0))):
    """Crossing parity against a closed mesh: odd means the point is inside."""
    crossings, origin = 0, point.copy()
    for _ in range(24):
        location = bvh.ray_cast(origin + direction * 1e-5, direction, 8.0)[0]
        if location is None:
            break
        crossings += 1
        origin = location + direction * 1e-5
    return crossings % 2 == 1


def bridge_creases(obj, rounds=10, factor=0.5, limit=0.006):
    """Cloth does not follow the body into a valley — it bridges it.

    The armpit is a crease a centimetre deep. A shell cut from the body dives
    into it and back out, and when every vertex is then pushed out along its
    normal the two walls of the crease drive into each other: the sleeve
    self-intersects and tears open. Relaxing only the CONCAVE vertices lifts
    those valleys until the surface spans them, and leaves every convex vertex
    — which is all of the silhouette — exactly where it was."""
    bm = bmesh.new()
    bm.from_mesh(obj.data)
    origin = {v.index: v.co.copy() for v in bm.verts}
    for _ in range(rounds):
        bm.normal_update()
        concave = []
        for v in bm.verts:
            if not v.link_edges or any(e.is_boundary for e in v.link_edges):
                continue   # a hem is a designed edge; smoothing it makes it ragged
            average = Vector((0.0, 0.0, 0.0))
            for e in v.link_edges:
                average += e.other_vert(v).co
            average /= len(v.link_edges)
            if (average - v.co).dot(v.normal) > 1e-5:
                concave.append(v)
        if not concave:
            break
        bmesh.ops.smooth_vert(bm, verts=concave, factor=factor,
                              use_axis_x=True, use_axis_y=True, use_axis_z=True)
        # Capped: the crotch is a valley several centimetres deep, and left
        # uncapped this fills it in — the trousers balloon out there and come
        # through the tunic over them. A crease only has to be bridged enough
        # that offsetting it does not fold the surface.
        for v in bm.verts:
            delta = v.co - origin[v.index]
            if delta.length > limit:
                v.co = origin[v.index] + delta.normalized() * limit
    bm.to_mesh(obj.data)
    bm.free()


def offset_shell(obj, bvh, offset, floor, under=None, clearance=0.008):
    """Lifts the cut shell off the skin by `offset`, ALONG ITS OWN NORMALS.

    The obvious implementation — for each vertex, find the nearest point on the
    body and put the vertex that far outside it — tears the garment apart. At
    the armpit the nearest point on the body for a vertex on the chest is the
    ARM, a centimetre away across the gap, so the vertex is teleported to the
    other side of the crease and the faces around it fold inside out. What
    comes out has holes in it.

    A garment is cut FROM the body, so each of its vertices is already on the
    body and its own normal is the body's normal there. Moving along that
    normal cannot cross a gap. Where the gap is too narrow for the full offset
    — the armpit is about 15 mm, an outer layer wants 17 — the ray hits the
    other wall first and the offset is backed off to fit."""
    bm = bmesh.new()
    bm.from_mesh(obj.data)
    bm.normal_update()
    pinched = 0
    for v in bm.verts:
        normal = v.normal.copy()
        if normal.length < 1e-9:
            continue
        normal.normalize()
        distance = offset
        # Clear every layer already on the body. A fixed ladder of thicknesses
        # is not enough on its own: a lower garment is a decimated shell that
        # bulges between its vertices, and the trousers were coming through the
        # tunic's belly wherever the waistband did.
        if under is not None:
            below = under.ray_cast(v.co + normal * 1e-4, normal, 0.10)
            if below[0] is not None and below[3] is not None:
                distance = max(distance, below[3] + clearance)
        hit = bvh.ray_cast(v.co + normal * 1e-4, normal, distance)
        if hit[0] is not None and hit[3] is not None:
            distance = max(floor, hit[3] - 0.0015)
            pinched += 1
        v.co = v.co + normal * distance
    bm.to_mesh(obj.data)
    bm.free()
    return pinched


def bare_spots(garment, body, covers):
    """Body vertices the engine will MASK that this garment does not actually
    cover: fire the body's own outward normal and see whether it meets the
    garment. These are the holes a dressed character would have."""
    gbvh = body_bvh(garment)
    per_vertex = vertex_regions(body)
    keep = set(covers)
    bare, checked = [], 0
    for v in body.data.vertices:
        if per_vertex[v.index] not in keep:
            continue
        checked += 1
        if gbvh.ray_cast(v.co + v.normal * 1e-4, v.normal, 0.12)[0] is None:
            bare.append((v.co.copy(), v.normal.copy()))
    return bare, checked


def enclose(garment, body, covers, rounds=14):
    """A garment must ENCLOSE what the engine masks for it, or the dressed
    character has a hole where the skin was removed.

    Offsetting every vertex is not enough to guarantee that. A reduced shell is
    a chord across the surface it came from, so a sharp convex feature — the
    toes are the worst — pokes out between the garment's vertices even though
    every one of those vertices is outside the body. Rather than paying for the
    triangles that would resolve the toes, the garment is pushed out only where
    skin still shows through, and relaxed afterwards so the bulge stays a bulge
    instead of a fold."""
    for _ in range(rounds):
        bare, _checked = bare_spots(garment, body, covers)
        if not bare:
            return 0
        bm = bmesh.new()
        bm.from_mesh(garment.data)
        moved = []
        for v in bm.verts:
            if any(e.is_boundary for e in v.link_edges):
                continue      # a hem is a designed edge; moving it makes it ragged
            # Along the SKIN's outward direction at the spot showing through —
            # that is the direction the shell has to open in.
            push = None
            for spot, normal in bare:
                if (v.co - spot).length < ENCLOSE_RADIUS:
                    push = normal if push is None else push + normal
            if push is not None and push.length > 1e-6:
                v.co = v.co + push.normalized() * ENCLOSE_STEP
                moved.append(v)
        if moved:
            bmesh.ops.smooth_vert(bm, verts=moved, factor=0.15,
                                  use_axis_x=True, use_axis_y=True, use_axis_z=True)
        bm.to_mesh(garment.data)
        bm.free()
    return len(bare_spots(garment, body, covers)[0])


def check_layering(built, body):
    """Every layer must enclose the one beneath it (CHARACTERS.md 5.4).

    Measured from the skin outwards: fire the body's own normal and collect
    where it meets each garment. The distances have to come out in layer
    order, or an inner garment is showing through an outer one."""
    # A garment the engine masks away under an outer one is never drawn with
    # it, so their geometry crossing is not a defect (`wearableHidden`).
    sealed = set()
    for inner, (inner_layer, _o) in built.items():
        for outer, (outer_layer, _p) in built.items():
            covers_inner = set(GARMENTS[inner][3])
            covers_outer = set(GARMENTS[outer][3])
            if (inner != outer and outer_layer > inner_layer and covers_inner
                    and covers_inner <= covers_outer):
                sealed.add((inner, outer))
    bvhs = [(layer, name, body_bvh(obj)) for name, (layer, obj) in built.items()]
    problems, pairs, sample = 0, {}, {}
    for v in body.data.vertices:
        hits = []
        for layer, name, gbvh in bvhs:
            hit = gbvh.ray_cast(v.co + v.normal * 1e-4, v.normal, 0.12)
            if hit[0] is not None and hit[3] is not None:
                hits.append((layer, name, hit[3]))
        hits.sort()
        for i in range(1, len(hits)):
            if (hits[i][0] > hits[i - 1][0] and hits[i][2] <= hits[i - 1][2]
                    and (hits[i - 1][1], hits[i][1]) not in sealed):
                key = "%s under %s" % (hits[i - 1][1], hits[i][1])
                pairs[key] = pairs.get(key, 0) + 1
                sample[key] = (v.co.x, v.co.z, -v.co.y)
                problems += 1
                break
    for key in sorted(pairs, key=lambda k: -pairs[k]):
        print("      %-28s %3d  e.g. (%.3f,%.3f,%.3f)" % (key, pairs[key], *sample[key]))
    return problems


def lift_over(obj, under, clearance, step=0.002, rounds=12):
    """Lifts a garment clear of every layer already on the body.

    Offsetting by ray along the body's normal is not enough on its own: a lower
    garment's hem is a rim that sticks out sideways, and a ray fired outwards
    goes straight past it. The trousers' waistband came through the tunic's
    belly exactly there. This asks the question the ray cannot — "am I inside
    what is underneath me, or too close to it?" — and pushes out until the
    answer is no."""
    bm = bmesh.new()
    bm.from_mesh(obj.data)
    bm.normal_update()
    lifted = 0
    for v in bm.verts:
        normal = v.normal.copy()
        if normal.length < 1e-9:
            continue
        normal.normalize()
        for _ in range(rounds):
            nearest = under.find_nearest(v.co)[0]
            if nearest is None:
                break
            if (v.co - nearest).length >= clearance and not inside_body(under, v.co):
                break
            v.co = v.co + normal * step
            lifted += 1
    bm.to_mesh(obj.data)
    bm.free()
    return lifted


def fix_folds(obj, rounds=6):
    """Flattens faces that have folded back on themselves.

    Pushing individual vertices around a shell — to clear the skin, to clear a
    lower layer — can flip a small triangle over its neighbours. It is one
    triangle, but it faces away from the light and reads as a black gash on an
    otherwise clean garment. Relaxing its corners into the surface around it
    removes the fold; collapsing the triangle instead (the obvious fix) tears
    holes in the shell, which is a worse defect than the one being repaired."""
    fixed = 0
    for _ in range(rounds):
        bm = bmesh.new()
        bm.from_mesh(obj.data)
        bm.normal_update()
        folded = set()
        for f in bm.faces:
            neighbours = [g for e in f.edges for g in e.link_faces if g is not f]
            if len(neighbours) < 2:
                continue
            average = Vector((0.0, 0.0, 0.0))
            for g in neighbours:
                average += g.normal
            if average.length < 1e-9:
                continue
            if f.normal.dot(average.normalized()) < -0.2:
                for v in f.verts:
                    if not any(e.is_boundary for e in v.link_edges):
                        folded.add(v)
        if not folded:
            bm.free()
            break
        fixed += len(folded)
        bmesh.ops.smooth_vert(bm, verts=list(folded), factor=0.9,
                              use_axis_x=True, use_axis_y=True, use_axis_z=True)
        bm.to_mesh(obj.data)
        bm.free()
    return fixed


def check_garment(garment, body, bvh, name, covers):
    """The three ways a wearable goes wrong, all measured on the real geometry:
    it sinks into the body, it fails to cover what the engine masks, or its
    own shell is broken."""
    sunk, deepest = 0, 0.0
    for v in garment.data.vertices:
        if not inside_body(bvh, v.co):
            continue
        sunk += 1
        location = bvh.find_nearest(v.co)[0]
        if location is not None:
            deepest = max(deepest, (v.co - location).length)

    bare, checked = ([], 0) if not covers else bare_spots(garment, body, covers)
    samples = ["(%.3f,%.3f,%.3f)" % (c.x, c.z, -c.y) for c, _n in bare[:3]]

    bm = bmesh.new()
    bm.from_mesh(garment.data)
    open_edges = sum(1 for e in bm.edges if len(e.link_faces) == 1)
    nonmanifold = sum(1 for e in bm.edges if len(e.link_faces) > 2)
    volume = bm.calc_volume(signed=True)
    bm.free()
    broken = open_edges + nonmanifold + (1 if volume <= 0.0 else 0)
    slivers = [poly for poly in garment.data.polygons if poly.area < 2e-6]
    sliver = len(slivers)
    for poly in slivers[:2]:
        c = poly.center
        samples.append("sliver(%.3f,%.3f,%.3f)" % (c.x, c.z, -c.y))

    print("   %-11s %4d tris  sunk %d (deepest %.4f m)  uncovered %d/%d  open %d  "
          "non-manifold %d  slivers %d %s" %
          (name, triangles(garment), sunk, deepest, len(bare), checked, open_edges,
           nonmanifold, sliver, " ".join(samples)))
    # A vertex a fraction of a millimetre under the skin is not a defect: the
    # shell is 1.5 mm thick and the skin it hugs is itself a 2200-triangle
    # approximation. A garment sunk far enough to SHOW is.
    return (0 if deepest < 0.0015 else sunk), len(bare), broken, sliver


def cut_shell(source, rules, name):
    """The garment's surface: the body's own faces, restricted by the rules,
    with every visible boundary a real plane cut."""
    dup = source.copy()
    dup.data = source.data.copy()
    dup.name = "Garment_" + name
    bpy.context.collection.objects.link(dup)

    planes = [plane for _regions, rule_planes in rules for plane in rule_planes]
    bm = bmesh.new()
    bm.from_mesh(dup.data)
    bisect_at(bm, planes)
    bm.to_mesh(dup.data)
    bm.free()

    per_vertex = vertex_regions(dup)
    bm = bmesh.new()
    bm.from_mesh(dup.data)
    bm.verts.ensure_lookup_table()
    doomed = []
    for f in bm.faces:
        centre = f.calc_center_median()
        # ONE region per face, by plurality — exactly as the engine's importer
        # tags triangles. Asking each rule "are most of this face's vertices
        # mine?" instead drops every face that straddles two regions: a quad
        # split 2-2 between the torso and the arm belongs to neither, and the
        # whole torso/arm seam comes out as a ring of holes around the armpit.
        # Ties go to the lowest region name, as they do in the importer.
        votes = {}
        for v in f.verts:
            r = per_vertex[v.index]
            votes[r] = votes.get(r, 0) + 1
        region = min(sorted(votes), key=lambda r: -votes[r])
        keep = False
        for regions, rule_planes in rules:
            if region not in regions:
                continue
            if all((centre - to_blender(point)).dot(to_blender_dir(normal)) >= 0.0
                   for point, normal in rule_planes):
                keep = True
                break
        if not keep:
            doomed.append(f)
    if len(doomed) == len(bm.faces):
        raise RuntimeError("garment %s selected no faces" % name)
    bmesh.ops.delete(bm, geom=doomed, context='FACES')
    bm.to_mesh(dup.data)
    bm.free()
    return dup


def build_garment(body, arm, bvh, name, layer, budget, rules, covers, under=None):
    """Cuts the garment out of the body's own surface and lifts it onto the
    layer's offset surface. It therefore fits the body exactly, shares the
    body's skin weights, and encloses whatever layer sits beneath it.

    The body is reduced BEFORE the cut, never after: decimating a cut shell
    chews its boundary back into the zig-zag the plane cuts were there to
    avoid, and a hem is the most visible edge on a garment."""
    target = max(48, int(budget * 0.45))

    probe = cut_shell(body, rules, name + "_probe")
    share = max(1, triangles(probe))
    bpy.data.objects.remove(probe, do_unlink=True)

    source = body.copy()
    source.data = body.data.copy()
    source.name = "GarmentSource_" + name
    bpy.context.collection.objects.link(source)
    # The body carries the variation morphs by now, and decimation refuses a
    # mesh with shape keys. The garment does not want them anyway: a garment
    # follows a morphed body through the fitting pipeline's re-fit (13.4), not
    # by carrying the body's own shape keys.
    if source.data.shape_keys:
        activate(source)
        bpy.ops.object.shape_key_remove(all=True)
    reduce_to(source, max(120, int(triangles(body) * target / float(share))))
    clamp_influences(source)
    prune_far_influences(source, arm)

    dup = cut_shell(source, rules, name)
    bpy.data.objects.remove(source, do_unlink=True)

    bm = bmesh.new()
    bm.from_mesh(dup.data)
    bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=1e-5)
    bm.to_mesh(dup.data)
    bm.free()

    # The OUTER surface is settled completely before the shell is given any
    # thickness. Doing it the other way round moves the two shells
    # independently, and where they cross, the inside of the garment faces the
    # camera and reads as a torn dark patch.
    bridge_creases(dup)
    offset = OFFSET_OVERRIDE.get(name, LAYER_OFFSET[min(layer, len(LAYER_OFFSET) - 1)])
    offset_shell(dup, bvh, offset, min_offset(layer), under)
    if covers:
        enclose(dup, body, covers)
    if under is not None:
        lift_over(dup, under, 0.008)
    # On the OPEN shell, where "boundary" still means the hem. After solidify
    # the outer and inner surfaces are edge-connected around that hem, and
    # relaxing across the join drags one surface through the other — which is
    # exactly the black gash this pass exists to remove.
    fix_folds(dup)

    solid = dup.modifiers.new("solidify", 'SOLIDIFY')
    solid.thickness = SHELL_THICKNESS
    solid.offset = -1.0            # grow inward, so the outer surface stays put
    activate(dup)
    bpy.ops.object.modifier_apply(modifier="solidify")

    bm = bmesh.new()
    bm.from_mesh(dup.data)
    bmesh.ops.dissolve_degenerate(bm, dist=1.8e-3, edges=bm.edges)
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    bmesh.ops.triangulate(bm, faces=bm.faces, quad_method='BEAUTY', ngon_method='BEAUTY')
    bm.to_mesh(dup.data)
    bm.free()
    # A garment is a thin shell. Shaded fully smooth, every rim face averages
    # its normal with the outer surface AND the inner one, which reads as hard
    # dark facets along every hem. Splitting the normals by angle keeps the
    # cloth smooth and the rim its own surface.
    activate(dup)
    bpy.ops.object.shade_auto_smooth(angle=math.radians(40))

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
        export_materials='EXPORT', export_normals=True, export_texcoords=True,
        export_morph=True, export_morph_normal=True, export_morph_tangent=False)


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
        # +Y, not -Y. The body is placed facing Blender +Y (engine -Z, its
        # forward), so a camera on -Y stands BEHIND it: every "front" preview
        # this script has ever written was actually the back. Harmless to the
        # assets, corrosive to the review — these renders are the evidence the
        # owner's aesthetic gate is judged on, and it was being handed the
        # wrong side. Verified against the engine's own body_preview sheet,
        # which renders the same body facing the camera.
        cam.location = tgt + Vector((math.sin(a) * 3.0, math.cos(a) * 3.0, 0.0))
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
    # The chart is packed on LOD0, and LOD1/LOD2 are decimated OUT OF LOD0 so
    # they inherit it.
    #
    # Packing the full-resolution body instead looks equivalent and is not.
    # Which region a triangle belongs to is decided from the bone that moves
    # it, and decimation moves weights: a chart packed on the 21 160-triangle
    # base and measured on the 2 200-triangle LOD0 disagrees about a handful of
    # triangles near every region seam. A handful is enough, because
    # `chart_islands_disjoint` compares region BOUNDING BOXES — one stray
    # triangle stretches a region's box across the sheet. Measured: 0 of 55
    # region pairs overlapped on the base mesh, and 45 of 55 overlapped on the
    # LOD0 imported from it. Packing the mesh the gate actually measures is the
    # only version of this that holds.
    lods = [duplicate_reduced(body, arm, "Body_LOD0", LOD_TRIANGLES[0], morphs=False)]
    # The Face shell is cut BEFORE the chart is packed, so Face is a region the
    # packer sees and gives its own box to — `chart_regions_required` and
    # `chart_islands_disjoint` both read the shipped chart, not the intent.
    lod0_regions = split_face_shell(
        lods[0], arm, humanoid_morphs.Landmarks(
            [humanoid_morphs.to_engine(v.co) for v in lods[0].data.vertices]))
    health(lods[0], "LOD0 + face")
    if triangles(lods[0]) > LOD_BUDGET[0]:
        raise RuntimeError("LOD0 is %d triangles after the face cut, over B-5's %d "
                           "budget" % (triangles(lods[0]), LOD_BUDGET[0]))
    uv = repack_uv.repack_by_region(lods[0], REGION_OF_BONE, regions=lod0_regions)
    print("   uv chart: %d regions, %.1f%% of the tile used, %.0f px/m at "
          "1024^2, %d loops outside, %.1f%% of the halves' texels shared"
          % (uv['regions'], uv['covered'] * 100.0, uv['density'],
             uv['outside'], uv['shared_frac'] * 100.0))
    for level in (1, 2):
        lods.append(duplicate_reduced(lods[level - 1], arm, "Body_LOD%d" % level,
                                      LOD_TRIANGLES[level], morphs=False))
    for lod in lods:
        add_morphs(lod)
    for level, lod in enumerate(lods):
        health(lod, "LOD%d" % level)
        if weight_report(lod, arm) > 0:
            raise RuntimeError("LOD%d skin weights leak" % level)
        # Decimation moves the surviving UVs slightly; the guard band exists so
        # they cannot leave the tile. Checked here so the pipeline names the
        # mesh, instead of the importer refusing the body three steps later.
        repack_uv.assert_inside_tile(lod, "LOD%d" % level)
        path = os.path.join(outdir, "humanoid_template_lod%d.glb" % level)
        export(lod, arm, path)
        exported.append((path, triangles(lod)))
        lod.hide_render = True

    garments = []
    built = {}
    failures = []
    bvh = body_bvh(lods[0])
    # In layer order, so each garment can be told what is already on the body.
    lower_verts, lower_polys = [], []
    current_layer = None
    for name, (layer, budget, rules, covers) in sorted(
            GARMENTS.items(), key=lambda kv: (kv[1][0], kv[0])):
        if layer != current_layer:
            # Garments on the same layer do not stack, so the obstacle set only
            # grows when the layer does.
            under = (BVHTree.FromPolygons(lower_verts, lower_polys)
                     if lower_polys else None)
            current_layer = layer
        g = build_garment(lods[0], arm, bvh, name, layer, budget, rules, covers, under)
        health(g, name)
        sunk, holes, broken, sliver = check_garment(g, lods[0], bvh, name, covers)
        if sunk or holes or broken or sliver:
            failures.append("%s: %d sunk, %d bare, %d broken, %d slivers" %
                            (name, sunk, holes, broken, sliver))
        path = os.path.join(outdir, "garment_%s.glb" % name)
        export(g, arm, path)
        exported.append((path, triangles(g)))
        g.hide_render = True
        garments.append(g)
        built[name] = (layer, g)
        base = len(lower_verts)
        lower_verts.extend(tuple(v.co) for v in g.data.vertices)
        lower_polys.extend([base + i for i in poly.vertices] for poly in g.data.polygons)

    crossings = check_layering(built, lods[0])
    print("   layering: %d places where a garment shows through the one above it"
          % crossings)
    if crossings:
        failures.append("layering: %d crossings" % crossings)

    for path, tris in exported:
        print("exported %-34s %5d tris %8d bytes" %
              (os.path.basename(path), tris, os.path.getsize(path)))

    if failures:
        print("   GARMENT DEFECTS: " + "; ".join(failures))

    body.hide_render = False
    cam = add_preview_scene()
    render_views(cam, outdir, "body")
    lods[0].hide_render = False
    body.hide_render = True
    render_views(cam, outdir, "lod0")
    print("done")


if __name__ == "__main__":
    main()
