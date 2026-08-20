#!/usr/bin/env python3
"""Measures the PRISTINE CC0 source mesh's UV chart, before anything touches it.

Run:  python3 tools/model/measure_source_uv.py

This answers one question the owner asked directly: did the Blender Studio base
mesh ship with a sane unwrap, or did our pipeline destroy it in transit? The
committed body has 89.7 % of its triangles at zero UV area and 1 241 vertices
piled on exactly u = 1.0 (docs/research/uv-audit.md), which is the fingerprint
of a clamp. A clamp only destroys what was outside the tile to begin with, so
the source's true extent decides between two very different stories:

  * source inside 0-1  -> our processing moved the UVs; the fix is our pipeline
  * source outside 0-1 -> the CC0 asset is multi-tile; the fix is a repack, and
                          the importer's new refusal was the correct gate

It reads the .blend and NOTHING ELSE — no placement, no scaling, no rig, no
decimation. Any transform at all would make the measurement describe our
pipeline rather than the source, which is the exact confusion being resolved.
"""

import os
import sys

import bpy

BASE_OBJECT = "GEO-body_male_realistic"
BUNDLE_CANDIDATES = [
    os.environ.get("MGE_HUMAN_BASE_BLEND", ""),
    os.path.join(os.path.dirname(__file__), "vendor", "human_base_meshes_bundle.blend"),
    os.path.expanduser("~/human_base_meshes_bundle.blend"),
]


def find_bundle():
    for path in BUNDLE_CANDIDATES:
        if path and os.path.isfile(path):
            return path
    raise SystemExit(
        "Human Base Meshes bundle not found. Download it (CC0, Blender Studio)\n"
        "from https://download.blender.org/demo/asset-bundles/human-base-meshes/\n"
        "and put human_base_meshes_bundle.blend in tools/model/vendor/.")


def main():
    bundle = find_bundle()
    bpy.ops.wm.read_factory_settings(use_empty=True)
    with bpy.data.libraries.load(bundle, link=False) as (src, dst):
        if BASE_OBJECT not in src.objects:
            raise SystemExit("%s not found in %s" % (BASE_OBJECT, bundle))
        dst.objects = [BASE_OBJECT]
    obj = bpy.data.objects[BASE_OBJECT]
    mesh = obj.data

    print("source bundle : %s" % bundle)
    print("object        : %s" % BASE_OBJECT)
    print("vertices      : %d" % len(mesh.vertices))
    print("polygons      : %d (%d triangles)"
          % (len(mesh.polygons), sum(len(p.vertices) - 2 for p in mesh.polygons)))
    print("uv layers     : %s" % ([l.name for l in mesh.uv_layers] or "NONE"))
    if not mesh.uv_layers:
        raise SystemExit("the source has no UV layer at all")

    for layer in mesh.uv_layers:
        us = [d.uv[0] for d in layer.data]
        vs = [d.uv[1] for d in layer.data]
        outside = sum(1 for d in layer.data
                      if not (0.0 <= d.uv[0] <= 1.0 and 0.0 <= d.uv[1] <= 1.0))
        print("")
        print("layer '%s' — %d uv loops" % (layer.name, len(layer.data)))
        print("  u extent : %.6f .. %.6f" % (min(us), max(us)))
        print("  v extent : %.6f .. %.6f" % (min(vs), max(vs)))
        print("  outside the 0-1 tile : %d of %d loops (%.1f%%)"
              % (outside, len(layer.data), 100.0 * outside / max(1, len(layer.data))))

        # The clamp fingerprint the audit found: a pile of coordinates sitting
        # on exactly 1.0. If the SOURCE already has that, the damage predates
        # us; if it does not, the pile was manufactured downstream.
        pinned = sum(1 for d in layer.data if abs(d.uv[0] - 1.0) < 1e-6)
        print("  loops at exactly u = 1.0 : %d (the clamp fingerprint)" % pinned)

        # How much of the tile the chart actually uses, measured as the area of
        # its UV triangles. This is the number that says whether the chart is
        # usable, independently of where it sits.
        area = 0.0
        degenerate = 0
        tris = 0
        for poly in mesh.polygons:
            loops = list(poly.loop_indices)
            for k in range(1, len(loops) - 1):
                a = layer.data[loops[0]].uv
                b = layer.data[loops[k]].uv
                c = layer.data[loops[k + 1]].uv
                t = abs((b[0] - a[0]) * (c[1] - a[1]) - (c[0] - a[0]) * (b[1] - a[1])) * 0.5
                area += t
                tris += 1
                if t < 1e-12:
                    degenerate += 1
        print("  uv area covered : %.4f tiles over %d triangles" % (area, tris))
        print("  zero-area triangles : %d (%.1f%%)"
              % (degenerate, 100.0 * degenerate / max(1, tris)))

        # If the layout is multi-tile, WHICH tiles it occupies decides whether a
        # repack is an affine transform per tile or a real re-layout. A UDIM
        # grid packs cleanly; scattered islands straddling tile borders do not.
        tiles = {}
        straddling = 0
        for poly in mesh.polygons:
            uvs = [layer.data[i].uv for i in poly.loop_indices]
            cells = {(int(uv[0]), int(uv[1])) for uv in uvs}
            if len(cells) > 1:
                straddling += 1
            cell = (int(sum(uv[0] for uv in uvs) / len(uvs)),
                    int(sum(uv[1] for uv in uvs) / len(uvs)))
            tiles[cell] = tiles.get(cell, 0) + 1
        print("  tiles occupied : %d" % len(tiles))
        for cell in sorted(tiles, key=lambda c: (c[1], c[0])):
            print("    tile u%d v%d : %5d faces" % (cell[0], cell[1], tiles[cell]))
        print("  faces straddling a tile border : %d" % straddling)

    print("")
    if all(0.0 <= d.uv[0] <= 1.0 and 0.0 <= d.uv[1] <= 1.0
           for l in mesh.uv_layers for d in l.data):
        print("VERDICT: the source chart is INSIDE the 0-1 tile.")
    else:
        print("VERDICT: the source chart extends OUTSIDE the 0-1 tile.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
