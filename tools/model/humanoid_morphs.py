#!/usr/bin/env python3
"""The variation scope's SHAPE half: morph targets on the template body.

A variant is realized two ways (CHARACTERS.md 4.1). Proportions — height,
limb ratios, breadths — are skeleton scaling, and the engine already has them:
they move joints, so the mesh follows through the skinning palette and so does
every garment. Everything scaling cannot say is a **morph delta**: a stack of
ribs is not a longer bone, and neither is a heavy brow.

This module authors those deltas as Blender **shape keys**, which glTF carries
natively as morph targets and `mge_asset_import --skinned` bakes into the
`.mgeskin`. There is no bespoke authoring format and no side-car file.

Each target is defined PARAMETRICALLY, not sculpted: a region of the body
measured against landmarks found on the model itself, and a displacement over
it. That makes them deterministic (the repo's rule for content), diffable, and
re-derivable if the base mesh is ever upgraded — a hand sculpt would have to be
redone. It also means every target is smooth by construction: the region weight
is a continuous function of position, so a morph can never tear the mesh.

A target stores ONE direction. The engine applies a weight in [-1, +1] and a
negative weight negates the delta, so "narrow jaw" costs nothing beyond
"square jaw".

Coordinates here are ENGINE space (Y up, the character faces -Z, +X is the
character's left).
"""

import math

from mathutils import Vector

# The canonical order. It is the engine's `Morph` enum, the file order and the
# weight order — appending is safe, reordering is not.
MORPH_NAMES = [
    "body.chest", "body.belly", "body.seat", "body.muscle", "body.neck",
    "face.skull", "face.brow", "face.cheeks", "face.jawWidth", "face.chin",
    "face.noseLength", "face.noseWidth", "face.mouth", "face.eyes", "face.ears",
]


def smoothstep(edge0, edge1, x):
    if edge0 == edge1:
        return 1.0 if x >= edge1 else 0.0
    t = (x - edge0) / (edge1 - edge0)
    t = 0.0 if t < 0.0 else (1.0 if t > 1.0 else t)
    return t * t * (3.0 - 2.0 * t)


def band(value, low, high, feather):
    """1 inside [low, high], falling smoothly to 0 over `feather` outside it."""
    return (smoothstep(low - feather, low, value) *
            (1.0 - smoothstep(high, high + feather, value)))


# --------------------------------------------------------------- landmarks --

LIMB_BONES = ("UpperArmL", "ForearmL", "UpperArmR", "ForearmR",
              "ThighL", "ShinL", "ThighR", "ShinR")


def _segment_parameter(p, a, b):
    """Where p projects onto segment ab (0..1) and how far off it lies."""
    ab = b - a
    denom = ab.dot(ab)
    t = 0.0 if denom < 1e-12 else max(0.0, min(1.0, (p - a).dot(ab) / denom))
    closest = a + ab * t
    return t, (p - closest), (p - closest).length


class Landmarks:
    """Measured on the model, never assumed.

    The base mesh could be swapped for a taller or differently proportioned
    one and every target below would still land on the right feature, because
    all of them are expressed against these."""

    def __init__(self, points, bones=None):
        # Limb axes, so "muscle" can grow a limb around the bone inside it
        # rather than along a world axis. Supplied by the caller because the
        # rig is the template script's to define.
        self.bones = bones or {}
        head = [p for p in points if p.y > 1.40]
        self.crown = max(p.y for p in head)
        # The face is the front of the head; the nose is its most forward point.
        self.nose = min(head, key=lambda p: p.z)
        self.ear_x = max(abs(p.x) for p in head if p.y > self.crown - 0.16)
        # The jaw runs down into the neck: the chin is the lowest FRONT point.
        chin_candidates = [p for p in head if p.z < self.nose.z + 0.09]
        self.chin = min(chin_candidates, key=lambda p: p.y)
        self.head_height = self.crown - self.chin.y
        # Classical head proportions, anchored to the two points measured above:
        # eyes halfway up the head, mouth a third of the way from chin to nose.
        self.eye_y = self.chin.y + self.head_height * 0.55
        self.brow_y = self.chin.y + self.head_height * 0.66
        self.mouth_y = self.chin.y + self.head_height * 0.24
        self.jaw_y = self.chin.y + self.head_height * 0.30
        self.skull_centre = Vector((0.0, self.chin.y + self.head_height * 0.62, 0.0))
        self.neck_y = self.chin.y - 0.02

        torso = [p for p in points if 0.75 < p.y < 1.45]
        self.chest_y = 1.31
        self.waist_y = 1.07
        self.seat_y = 0.90
        self.torso_depth = max(p.z for p in torso) - min(p.z for p in torso)

    def report(self):
        return ("landmarks: crown %.3f  chin %.3f  head %.3f m  nose (%.3f,%.3f,%.3f)  "
                "eyes %.3f  brow %.3f  mouth %.3f  ear |x| %.3f" %
                (self.crown, self.chin.y, self.head_height, self.nose.x, self.nose.y,
                 self.nose.z, self.eye_y, self.brow_y, self.mouth_y, self.ear_x))


# ----------------------------------------------------------------- targets --

def _scale_about(point, centre, factors, weight):
    """Displacement that scales `point` about `centre` — the shape of almost
    every anatomical change: a broader skull, a deeper chest, a bigger ear."""
    delta = point - centre
    return Vector((delta.x * factors[0], delta.y * factors[1], delta.z * factors[2])) * weight


def _sign(v):
    return -1.0 if v < 0.0 else 1.0


def morph_offset(name, p, L):
    """The displacement `name` applies at engine-space point `p`. Returns the
    zero vector outside the feature the parameter owns."""

    # ------------------------------------------------------------- body ----
    if name == "body.chest":
        w = band(p.y, 1.22, 1.42, 0.08) * (1.0 - smoothstep(0.11, 0.20, abs(p.x)))
        if w <= 0.0:
            return Vector((0, 0, 0))
        # A deep chest grows forward far more than it grows sideways.
        return Vector((p.x * 0.10, 0.0, p.z * 0.16 - 0.010)) * w

    if name == "body.belly":
        w = band(p.y, 0.96, 1.22, 0.09)
        if w <= 0.0:
            return Vector((0, 0, 0))
        front = 1.0 if p.z < 0.0 else 0.45     # a belly is a front-of-body event
        return Vector((p.x * 0.16, 0.0, p.z * 0.34 * front - 0.012 * front)) * w

    if name == "body.seat":
        w = band(p.y, 0.80, 1.02, 0.07)
        if w <= 0.0:
            return Vector((0, 0, 0))
        back = 1.0 if p.z > 0.0 else 0.3
        return Vector((p.x * 0.13, 0.0, p.z * 0.30 * back + 0.008 * back)) * w

    if name == "body.muscle":
        best = None
        for bone in LIMB_BONES:
            segment = L.bones.get(bone)
            if segment is None:
                continue
            t, radial, distance = _segment_parameter(p, segment[0], segment[1])
            if best is None or distance < best[2]:
                best = (t, radial, distance)
        if best is None or best[2] < 1e-4 or best[2] > 0.13:
            return Vector((0, 0, 0))
        t, radial, distance = best
        # A muscle belly is thickest mid-bone and vanishes at both joints, so
        # the elbow and the knee keep their shape and the skinning stays clean.
        w = math.sin(math.pi * t) ** 1.4
        return radial.normalized() * (distance * 0.22 * w)

    if name == "body.neck":
        w = band(p.y, L.neck_y - 0.06, L.neck_y + 0.03, 0.035)
        if w <= 0.0:
            return Vector((0, 0, 0))
        return Vector((p.x * 0.20, 0.0, (p.z + 0.01) * 0.20)) * w

    # ------------------------------------------------------------- face ----
    if name == "face.skull":
        w = smoothstep(L.eye_y - 0.02, L.brow_y + 0.02, p.y)
        if w <= 0.0:
            return Vector((0, 0, 0))
        # Round and broad, or narrow and long: one axis grows as another shrinks.
        return _scale_about(p, L.skull_centre, (0.13, -0.07, 0.05), w)

    if name == "face.brow":
        w = (band(p.y, L.brow_y - 0.014, L.brow_y + 0.018, 0.012) *
             smoothstep(L.nose.z + 0.10, L.nose.z + 0.045, p.z) *
             (1.0 - smoothstep(0.045, 0.075, abs(p.x))))
        return Vector((0.0, -0.002, -0.009)) * w

    if name == "face.cheeks":
        w = (band(p.y, L.eye_y - 0.030, L.eye_y + 0.008, 0.020) *
             band(abs(p.x), 0.040, L.ear_x - 0.010, 0.016) *
             smoothstep(0.04, -0.02, p.z))
        return Vector((_sign(p.x) * 0.009, 0.004, -0.005)) * w

    if name == "face.jawWidth":
        w = (band(p.y, L.chin.y - 0.01, L.jaw_y + 0.030, 0.022) *
             smoothstep(0.012, 0.045, abs(p.x)))
        return Vector((_sign(p.x) * 0.011, 0.0, 0.0)) * w

    if name == "face.chin":
        w = (band(p.y, L.chin.y - 0.005, L.chin.y + 0.032, 0.018) *
             (1.0 - smoothstep(0.024, 0.050, abs(p.x))) *
             smoothstep(0.0, -0.05, p.z))
        return Vector((0.0, -0.007, -0.011)) * w

    if name == "face.noseLength":
        w = (band(p.y, L.nose.y - 0.030, L.nose.y + 0.048, 0.016) *
             (1.0 - smoothstep(0.014, 0.032, abs(p.x))) *
             smoothstep(L.nose.z + 0.075, L.nose.z + 0.015, p.z))
        return Vector((0.0, -0.004, -0.012)) * w

    if name == "face.noseWidth":
        w = (band(p.y, L.nose.y - 0.026, L.nose.y + 0.014, 0.012) *
             band(abs(p.x), 0.006, 0.032, 0.010) *
             smoothstep(L.nose.z + 0.070, L.nose.z + 0.020, p.z))
        return Vector((_sign(p.x) * 0.007, 0.0, 0.002)) * w

    if name == "face.mouth":
        w = (band(p.y, L.mouth_y - 0.016, L.mouth_y + 0.016, 0.012) *
             (1.0 - smoothstep(0.030, 0.052, abs(p.x))) *
             smoothstep(L.nose.z + 0.085, L.nose.z + 0.030, p.z))
        return Vector((_sign(p.x) * 0.008, 0.0, 0.0)) * w

    if name == "face.eyes":
        w = (band(p.y, L.eye_y - 0.012, L.eye_y + 0.020, 0.012) *
             band(abs(p.x), 0.016, 0.052, 0.012) *
             smoothstep(L.nose.z + 0.105, L.nose.z + 0.045, p.z))
        return Vector((_sign(p.x) * 0.007, 0.0, 0.0)) * w

    if name == "face.ears":
        w = smoothstep(L.ear_x - 0.028, L.ear_x - 0.008, abs(p.x)) * \
            band(p.y, L.eye_y - 0.030, L.brow_y + 0.010, 0.022)
        if w <= 0.0:
            return Vector((0, 0, 0))
        root = Vector((_sign(p.x) * (L.ear_x - 0.028), L.eye_y, 0.0))
        return _scale_about(p, root, (0.30, 0.30, 0.30), w)

    raise KeyError("unknown morph %s" % name)


# ------------------------------------------------------------- authoring ----

def to_engine(co):
    """Blender (x, y front, z up) -> engine (x, y up, z back)."""
    return Vector((co.x, co.z, -co.y))


def to_blender_delta(d):
    return Vector((d.x, -d.z, d.y))


def add_shape_keys(obj, bones=None, verbose=True):
    """Adds one shape key per morph, generated from the object's own geometry.

    Must run AFTER decimation: Blender cannot apply a decimate modifier to a
    mesh that already has shape keys, and a target authored on the full-
    resolution body would not transfer to a reduced one anyway. Because the
    targets are parametric, re-deriving them per LOD is free and each LOD gets
    deltas that actually match its own vertices."""
    mesh = obj.data
    points = [to_engine(v.co) for v in mesh.vertices]
    landmarks = Landmarks(points, bones)
    if verbose:
        print("   " + landmarks.report())

    if mesh.shape_keys is None:
        obj.shape_key_add(name="Basis", from_mix=False)

    summary = []
    for name in MORPH_NAMES:
        key = obj.shape_key_add(name=name, from_mix=False)
        moved, largest = 0, 0.0
        for index, point in enumerate(points):
            offset = morph_offset(name, point, landmarks)
            if offset.length < 1e-6:
                continue
            key.data[index].co = mesh.vertices[index].co + to_blender_delta(offset)
            moved += 1
            largest = max(largest, offset.length)
        summary.append((name, moved, largest))
        if moved == 0:
            raise RuntimeError("morph %s moves nothing — its region missed the model" % name)

    if verbose:
        for name, moved, largest in summary:
            print("      %-16s %4d verts, up to %5.1f mm" % (name, moved, largest * 1000.0))
    return summary
