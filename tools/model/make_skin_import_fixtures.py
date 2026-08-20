#!/usr/bin/env python3
"""Generates the skinned-glTF fixtures `tests/test_skin_import.cpp` imports.

Run:  python3 tools/model/make_skin_import_fixtures.py

Two files, identical in every respect except their UV coordinates, so a test
that passes on one and fails on the other has isolated exactly one variable:

  tests/data/skinned_quad.gltf              u,v inside the 0-1 tile
  tests/data/skinned_quad_uv_outside.gltf   the same chart shifted so a second
                                            island sits past u = 1, which is
                                            the shape of the real defect
                                            (docs/research/uv-audit.md D-A)

The mesh is deliberately tiny — two quads, eight vertices — because the thing
under test is the importer's refusal, not its geometry handling. The rig is the
full canonical 17 joints because the importer checks the rig before it looks at
anything else, and a fixture that trips the rig gate would never reach the UV
gate at all.

Buffers are embedded as base64 data URIs so each fixture is one committed file
with no side-car .bin to keep in sync.
"""

import base64
import json
import os
import struct

# The canonical rig, in the order `Joint` declares it. Positions are not
# meaningful here (nothing is posed); what matters is that all 17 names are
# present, because the importer matches them by name.
JOINTS = [
    "Hips", "Spine", "Chest", "Neck", "Head",
    "UpperArmL", "ForearmL", "HandL",
    "UpperArmR", "ForearmR", "HandR",
    "ThighL", "ShinL", "FootL",
    "ThighR", "ShinR", "FootR",
]

# Two separate quads, so the "outside" fixture can move one island out of the
# tile and leave the other where it is — exactly what a two-UDIM layout does.
POSITIONS = [
    (-0.5, 0.0, 0.0), (0.5, 0.0, 0.0), (-0.5, 1.0, 0.0), (0.5, 1.0, 0.0),
    (-0.5, 1.0, 0.0), (0.5, 1.0, 0.0), (-0.5, 2.0, 0.0), (0.5, 2.0, 0.0),
]
NORMALS = [(0.0, 0.0, 1.0)] * 8
INDICES = [0, 1, 2, 2, 1, 3, 4, 5, 6, 6, 5, 7]
# Vertex 0-3 ride Hips, 4-7 ride Spine. Single influence, full weight.
JOINT_IDS = [(0, 0, 0, 0)] * 4 + [(1, 0, 0, 0)] * 4
WEIGHTS = [(1.0, 0.0, 0.0, 0.0)] * 8

UV_INSIDE = [
    (0.00, 0.00), (0.45, 0.00), (0.00, 1.00), (0.45, 1.00),
    (0.55, 0.00), (1.00, 0.00), (0.55, 1.00), (1.00, 1.00),
]
# The second island pushed into a second tile: u runs to 1.9825, which is what
# the audit found piled against the clamp edge on the real body.
UV_OUTSIDE = [
    (0.00, 0.00), (0.45, 0.00), (0.00, 1.00), (0.45, 1.00),
    (1.5325, 0.00), (1.9825, 0.00), (1.5325, 1.00), (1.9825, 1.00),
]


def pack(fmt, rows):
    return b"".join(struct.pack(fmt, *r) if isinstance(r, tuple) else struct.pack(fmt, r)
                    for r in rows)


def build(uvs):
    blobs = [
        ("pos", pack("<3f", POSITIONS), 5126, "VEC3", len(POSITIONS), 34962),
        ("nrm", pack("<3f", NORMALS), 5126, "VEC3", len(NORMALS), 34962),
        ("uv", pack("<2f", uvs), 5126, "VEC2", len(uvs), 34962),
        ("jnt", pack("<4H", JOINT_IDS), 5123, "VEC4", len(JOINT_IDS), 34962),
        ("wgt", pack("<4f", WEIGHTS), 5126, "VEC4", len(WEIGHTS), 34962),
        ("idx", pack("<H", INDICES), 5123, "SCALAR", len(INDICES), 34963),
        # Identity inverse-bind matrices, one per joint.
        ("ibm", pack("<16f", [tuple(1.0 if i % 5 == 0 else 0.0 for i in range(16))] * len(JOINTS)),
         5126, "MAT4", len(JOINTS), None),
    ]

    data = b""
    views, accessors = [], []
    for _name, blob, ctype, atype, count, target in blobs:
        while len(data) % 4:          # accessors must start 4-byte aligned
            data += b"\x00"
        view = {"buffer": 0, "byteOffset": len(data), "byteLength": len(blob)}
        if target is not None:
            view["target"] = target
        accessors.append({"bufferView": len(views), "componentType": ctype,
                          "count": count, "type": atype})
        views.append(view)
        data += blob

    # Position needs min/max; the spec requires it and cgltf leans on it.
    accessors[0]["min"] = [min(p[i] for p in POSITIONS) for i in range(3)]
    accessors[0]["max"] = [max(p[i] for p in POSITIONS) for i in range(3)]

    # A flat joint list: the importer maps joints BY NAME and never reads the
    # hierarchy, so a fixture that invented a parent chain would be asserting
    # something the test does not check.
    nodes = [{"name": "Body", "mesh": 0, "skin": 0}]
    nodes += [{"name": name, "translation": [0.0, 0.1 * i, 0.0]}
              for i, name in enumerate(JOINTS)]

    return {
        "asset": {"version": "2.0",
                  "generator": "MobileGamesEngine tools/model/make_skin_import_fixtures.py"},
        "scene": 0,
        "scenes": [{"nodes": list(range(len(nodes)))}],
        "nodes": nodes,
        "meshes": [{"name": "BodyMesh", "primitives": [{
            "attributes": {"POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2,
                           "JOINTS_0": 3, "WEIGHTS_0": 4},
            "indices": 5, "mode": 4}]}],
        "skins": [{"name": "Rig", "inverseBindMatrices": 6,
                   "joints": list(range(1, len(nodes)))}],
        "accessors": accessors,
        "bufferViews": views,
        "buffers": [{"byteLength": len(data),
                     "uri": "data:application/octet-stream;base64," +
                            base64.b64encode(data).decode("ascii")}],
    }


def main():
    root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    out = os.path.join(root, "tests", "data")
    for name, uvs in (("skinned_quad.gltf", UV_INSIDE),
                      ("skinned_quad_uv_outside.gltf", UV_OUTSIDE)):
        path = os.path.join(out, name)
        with open(path, "w") as f:
            json.dump(build(uvs), f, indent=1, sort_keys=True)
            f.write("\n")
        print("wrote", path)


if __name__ == "__main__":
    main()
