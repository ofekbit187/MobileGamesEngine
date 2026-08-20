#!/usr/bin/env python3
"""Repacks the CC0 source's 24-tile UDIM chart into the single [0,1] tile,
halves DISJOINT, one box per body region — the layout ruled in ADR 0010.

Run standalone to measure it without writing anything:
    python3 tools/model/repack_uv.py

Why a repack is mandatory rather than a choice: `SkinVertex` stores UVs as
normalized uint16 (B-3), which has no representation for a multi-tile UDIM
layout, and the hardened importer now correctly REFUSES the raw source.
`docs/research/source-uv-measurement.md` measured the source: 24 occupied
tiles, u 0.0387..8.9487, v 0.0303..3.9667, and — the fact this file leans on —
ZERO faces straddling a tile border.

Nothing here is placed by hand or by eye. The steps are:

  1. COLLAPSE (`collapse_udim_tiles`) — translate every face by the integer
     offset of its tile, bringing the 24 tiles down to TWO, one per body half.
     Safe per-FACE precisely because no face straddles a border: an island's
     faces are edge-connected, so an island spanning two tiles would have to
     straddle one. Every island moves rigidly, nothing is split, no seam is
     cut, and vertex order is untouched — uv-audit D-A's "UV-only" case.

  2. PACK PER REGION (`repack_by_region`) — each body region is packed on its
     own and given its own rectangle of the sheet, sized so that UV area is
     proportional to SURFACE area. That proportionality is what makes texel
     density even across regions rather than a coincidence of how the source
     happened to unwrap; measured, it lands every region within 0% of the
     chart mean.

  3. GUARD BAND (`inset_guard_band`) — one affine shrink so downstream
     processing cannot push a UV out of the tile.

Two things here are load-bearing and were each got wrong first, so they are
measured rather than trusted (see `docs/research/uv-repack.md`):

  * TWO tiles in step 1, not one. The source's halves are an exact mirrored
    pair (the left is the right translated by +2 in v), so collapsing onto a
    single tile lands them precisely on top of each other and the packer leaves
    coincident islands where they are — 11.7% of the halves' texels shared,
    which is the MIRRORED layout ADR 0010 rejects, arrived at by accident.

  * `merge_overlap=False` in the pack. With True the packer fuses the halves
    and hands back the mirrored layout outright — one scar painting onto both
    cheeks forever. Run both ways on the same source, the measure reads 100.0%
    shared for True and 0.0% for False.

`measure()` reports the shared fraction directly, by rasterising the two halves
into separate coverage grids, and `repack_by_region` refuses its own output if
it climbs.
"""

import math
import os
import sys

import bpy
import bmesh

MARGIN = 0.003
RASTER = 2048          # coverage raster resolution for the disjointness test

# A guard band inset at the tile border, applied to the whole chart as one
# affine shrink after packing. It is not cosmetic. LOD1 and LOD2 are decimated
# out of the LOD0 this chart is packed on, and an edge collapse moves the
# surviving UVs a little: measured, a chart packed flush to the tile drifted its
# u minimum from 0.0015 to 0.0003 and pushed its v maximum past 1.0 by 0.0007. A UV a hair outside the tile is
# not representable in the uint16 vertex format (B-3), so the hardened importer
# refuses the whole body over it — correctly. The guard band is the room that
# drift needs. It costs (1-2*GUARD)^2 = 1% of texel area, which the density
# budget does not notice.
GUARD = 0.005


def _uv_layer(obj):
    return obj.data.uv_layers[0].data


def straddling_faces(obj):
    """Faces whose corners do not all share one integer tile."""
    uvs = _uv_layer(obj)
    n = 0
    for poly in obj.data.polygons:
        tiles = {(math.floor(uvs[i].uv.x), math.floor(uvs[i].uv.y))
                 for i in poly.loop_indices}
        if len(tiles) > 1:
            n += 1
    return n


def collapse_udim_tiles(obj):
    """Step 1 — bring the 24 UDIM tiles down to two, rigidly, per island.

    Not one tile: TWO, one per body half. The source's halves are an exact
    mirrored pair (the left is the right translated by +2 in v), so collapsing
    everything onto tile 0 lands them precisely on top of each other — and the
    packer leaves coincident islands where they are. Measured, that is not
    theoretical: Scalp came back at 118.7% fill and Torso at 110.2% (a region
    cannot exceed 100% unless its islands overlap), and 11.7% of the two
    halves' texels were shared. That is the mirrored layout ADR 0010 rejects,
    arrived at by accident.

    Keeping the halves in separate tiles here means the packer never sees two
    coincident islands, and every later step (prescale, pack, placement) is a
    rigid or uniform transform that preserves the separation.
    """
    straddle = straddling_faces(obj)
    if straddle:
        raise RuntimeError(
            "%d faces straddle a tile border; the per-face collapse would tear "
            "an island. Repack aborted rather than corrupting the chart." % straddle)
    uvs = _uv_layer(obj)
    moved = 0
    for poly in obj.data.polygons:
        ls = list(poly.loop_indices)
        du = math.floor(sum(uvs[i].uv.x for i in ls) / len(ls))
        dv = math.floor(sum(uvs[i].uv.y for i in ls) / len(ls))
        # v < 2 is the body's right half, v >= 2 its left: keep them one tile
        # apart instead of stacking them.
        dv_target = 0 if dv < 2 else 1
        if du or dv != dv_target:
            for i in ls:
                uvs[i].uv.x -= du
                uvs[i].uv.y -= (dv - dv_target)
            moved += 1
    obj.data.update()
    return moved


def inset_guard_band(obj, guard=GUARD):
    """Shrink the whole packed chart toward the tile centre by `guard`.

    One affine transform over every loop: relative layout, island shapes and
    the disjointness of the halves are all untouched — only the scale changes.
    """
    uvs = _uv_layer(obj)
    scale = 1.0 - 2.0 * guard
    for i in range(len(uvs)):
        uvs[i].uv.x = guard + uvs[i].uv.x * scale
        uvs[i].uv.y = guard + uvs[i].uv.y * scale
    obj.data.update()


def uv_extent(obj):
    """Loop UV extent — what the importer's tile gate measures."""
    uvs = _uv_layer(obj)
    n = len(uvs)
    us = [uvs[i].uv.x for i in range(n)]
    vs = [uvs[i].uv.y for i in range(n)]
    return min(us), max(us), min(vs), max(vs)


def assert_inside_tile(obj, tag):
    """Fail in the pipeline, with the mesh named, rather than at import time."""
    lo_u, hi_u, lo_v, hi_v = uv_extent(obj)
    if lo_u < 0.0 or hi_u > 1.0 or lo_v < 0.0 or hi_v > 1.0:
        raise RuntimeError(
            "%s has UVs outside the 0-1 tile after processing "
            "(u %.4f..%.4f, v %.4f..%.4f). The uint16 vertex format cannot "
            "represent them (B-3) and the importer will refuse the asset. "
            "Widen repack_uv.GUARD if decimation drift has grown."
            % (tag, lo_u, hi_u, lo_v, hi_v))


# ------------------------------------------------------- region packing ----
#
# Packing the source's islands as one undifferentiated pile puts pieces of
# every body region all over the sheet. That satisfies ADR 0010's letter (one
# tile, halves disjoint) and still fails the contract: B-27 wants "fixed UV
# islands per region", B-28 wants the scalp cap to own its island, and
# `chart_islands_disjoint` in assets/standards/skin_texture.mgestd tests it the
# way it actually matters — each region's BOUNDING BOX against every other's.
# Measured on an island-wise pack, every one of the 50 region pairs overlapped.
#
# So each region is packed on its own and given its own rectangle of the sheet.
# Region areas are allocated proportional to SURFACE area, which is what makes
# texel density even across regions (B-27, `density_outside_tolerance`) rather
# than a coincidence of how the source happened to unwrap.

GUTTER = 0.004         # blank sheet between one region's box and the next
PACK_ATTEMPTS = 4      # prescale+pack rounds per region; best (tightest) wins


# `BodyRegion` in engine/include/mge/character/body_mesh.h, in enum order —
# which is the order the importer breaks ties in, and therefore the order this
# file has to break them in too.
BODY_REGION_ORDER = ["Scalp", "Face", "Neck", "Torso", "ArmL", "ArmR",
                     "HandL", "HandR", "LegL", "LegR", "FootL", "FootR"]


def face_regions(obj, region_of_bone):
    """Region per face, by the same rule the engine's importer tags triangles:
    dominant bone per vertex, plurality per face, ties to the lowest BodyRegion
    ENUM INDEX.

    The enum index is the whole point. Breaking ties alphabetically instead —
    which is what this pipeline did, believing it matched the importer — sends
    every 2-2 tie at a shoulder or a hip to ArmL/LegL where the engine sends it
    to Torso, because Torso is enum 3 and the limbs are 4 and above. Those few
    triangles are enough: `chart_islands_disjoint` compares region bounding
    boxes, so one triangle in the wrong box fails the region outright. It was
    measured as exactly four failing pairs — Torso/ArmL, Torso/ArmR, Torso/LegL,
    Torso/LegR — which is the tie-break disagreement and nothing else.
    """
    per_vertex = []
    for v in obj.data.vertices:
        best, best_w = None, -1.0
        for g in v.groups:
            if g.weight > best_w:
                best_w, best = g.weight, obj.vertex_groups[g.group].name
        per_vertex.append(region_of_bone.get(best, "Torso"))
    rank = {name: i for i, name in enumerate(BODY_REGION_ORDER)}
    out = []
    for poly in obj.data.polygons:
        votes = {}
        for vi in poly.vertices:
            r = per_vertex[vi]
            votes[r] = votes.get(r, 0) + 1
        out.append(min(votes, key=lambda r: (-votes[r], rank.get(r, len(rank)))))
    return out


def _pack_region(obj, face_indices, margin):
    """Pack ONE region's islands, with the rest of the body hidden.

    Selecting the region is not enough: `uv.pack_islands` rescales every island
    it can see, so a per-region pack done by selection alone shrank each region
    already placed. Measured, that left the first region packed at a 0.000 x
    0.017 box and the last at 0.998 x 0.948 — the layout collapsed to 1 px/m.
    Hiding the other faces is what actually takes them out of the operator's
    view, so each region is packed in isolation and keeps its scale.
    """
    bpy.ops.object.mode_set(mode='OBJECT')
    keep = set(face_indices)
    for poly in obj.data.polygons:
        poly.select = poly.index not in keep
    bpy.ops.object.mode_set(mode='EDIT')
    bpy.context.tool_settings.mesh_select_mode = (False, False, True)
    bpy.context.scene.tool_settings.use_uv_select_sync = False
    bpy.ops.mesh.hide(unselected=False)          # hide everything else
    bpy.ops.mesh.select_all(action='SELECT')     # what is left is the region
    bpy.ops.uv.select_all(action='SELECT')
    bpy.ops.uv.pack_islands(
        udim_source='CLOSEST_UDIM', rotate=True, rotate_method='AXIS_ALIGNED',
        scale=True,
        merge_overlap=False,        # <- ADR 0010: DISJOINT, not mirrored
        margin_method='SCALED', margin=margin, shape_method='CONCAVE')
    bpy.ops.mesh.reveal()
    bpy.ops.object.mode_set(mode='OBJECT')


def _loop_indices(obj, face_indices):
    out = []
    for i in face_indices:
        out.extend(obj.data.polygons[i].loop_indices)
    return out


def _bbox(obj, loops):
    uvs = _uv_layer(obj)
    us = [uvs[i].uv.x for i in loops]
    vs = [uvs[i].uv.y for i in loops]
    return min(us), min(vs), max(us), max(vs)


def _uv_area_of(obj, face_indices):
    """Summed UV area and surface area over a set of faces."""
    me = obj.data
    uvs = _uv_layer(obj)
    uv_area = 0.0
    surface = 0.0
    for i in face_indices:
        poly = me.polygons[i]
        ls = list(poly.loop_indices)
        # fan-triangulate the polygon
        for k in range(1, len(ls) - 1):
            a = uvs[ls[0]].uv
            b = uvs[ls[k]].uv
            c = uvs[ls[k + 1]].uv
            uv_area += abs((b - a).cross(c - a)) * 0.5
        surface += poly.area
    return uv_area, surface


def _shelf_pack(boxes, gutter):
    """Place (w, h) boxes into the unit square, tallest first, in shelves.

    Returns placements or None if they do not fit. Deterministic: the input
    order decides ties and the caller sorts it.
    """
    placed = {}
    x, y, shelf_h = 0.0, 0.0, 0.0
    for key, (w, h) in boxes:
        if x + w > 1.0:                       # new shelf
            x = 0.0
            y += shelf_h + gutter
            shelf_h = 0.0
        if y + h > 1.0 or w > 1.0:
            return None
        placed[key] = (x, y)
        x += w + gutter
        shelf_h = max(shelf_h, h)
    return placed


def _prescale_region(obj, faces, loops, target_fill=0.75):
    """Blow a region's islands up to roughly fill the tile before packing.

    Blender's island packer only ever translates and shrinks — it never scales
    an island UP to use space it has been given. Handed one region's islands at
    their source scale (a few percent of the tile), it therefore just spreads
    them out where they are, and the region's bounding box comes back nearly
    tile-sized around almost no content: measured, 3-19% fill, and the same
    foot packing to 4.4% on one side and 48.2% on the other. Scaling the region
    up first gives the packer something tile-sized to arrange, so what comes
    back is compact and the two sides agree.
    """
    uv_area, _surface = _uv_area_of(obj, faces)
    if uv_area <= 0.0:
        return
    x0, y0, _x1, _y1 = _bbox(obj, loops)
    f = math.sqrt(target_fill / uv_area)
    uvs = _uv_layer(obj)
    for i in loops:
        uvs[i].uv.x = (uvs[i].uv.x - x0) * f
        uvs[i].uv.y = (uvs[i].uv.y - y0) * f
    obj.data.update()


def repack_by_region(obj, region_of_bone, margin=MARGIN, gutter=GUTTER, verbose=True):
    """The whole repack: collapse, pack each region, give each its own box."""
    before_verts = len(obj.data.vertices)
    before_polys = len(obj.data.polygons)

    moved = collapse_udim_tiles(obj)

    regions = face_regions(obj, region_of_bone)
    by_region = {}
    for i, r in enumerate(regions):
        by_region.setdefault(r, []).append(i)
    names = sorted(by_region)

    bpy.ops.object.select_all(action='DESELECT')
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj

    # 1. Pack each region on its own, and measure what came out.
    info = {}
    for name in names:
        faces = by_region[name]
        loops = _loop_indices(obj, faces)
        # Prescale-then-pack is not reliably idempotent: the same arm packed to
        # 49% fill on one side and 11% on the other from identical geometry.
        # Repeating it is cheap and monotone — the packer only ever shrinks —
        # so run it a fixed number of times and keep the tightest result. Fixed
        # count and a name tie-break keep it deterministic (B-14).
        best_fill, best_uv = -1.0, None
        for _attempt in range(PACK_ATTEMPTS):
            _prescale_region(obj, faces, loops)
            _pack_region(obj, faces, margin)
            bx0, by0, bx1, by1 = _bbox(obj, loops)
            area, _surf = _uv_area_of(obj, faces)
            fill = area / max((bx1 - bx0) * (by1 - by0), 1e-9)
            if fill > best_fill:
                uvs_now = _uv_layer(obj)
                best_fill = fill
                best_uv = [(uvs_now[i].uv.x, uvs_now[i].uv.y) for i in loops]
        uvs_now = _uv_layer(obj)
        for k, i in enumerate(loops):
            uvs_now[i].uv.x, uvs_now[i].uv.y = best_uv[k]
        obj.data.update()
        x0, y0, x1, y1 = _bbox(obj, loops)
        uv_area, surface = _uv_area_of(obj, faces)
        info[name] = dict(faces=faces, loops=loops, box=(x0, y0, x1, y1),
                          w=max(x1 - x0, 1e-6), h=max(y1 - y0, 1e-6),
                          uv_area=max(uv_area, 1e-12), surface=surface)
        if verbose:
            w, h = x1 - x0, y1 - y0
            print("      %-8s %5d faces  box %.3f x %.3f  uv %.4f  fill %.1f%%"
                  % (name, len(faces), w, h, uv_area,
                     100.0 * uv_area / max(w * h, 1e-9)))

    # Re-measure every region now that all of them are packed. The boxes taken
    # during the loop describe the sheet as it was mid-way through it; only a
    # fresh pass describes the sheet the placement is actually computed from.
    for name in names:
        d = info[name]
        x0, y0, x1, y1 = _bbox(obj, d['loops'])
        uv_area, surface = _uv_area_of(obj, d['faces'])
        d.update(box=(x0, y0, x1, y1), w=max(x1 - x0, 1e-6), h=max(y1 - y0, 1e-6),
                 uv_area=max(uv_area, 1e-12), surface=surface)

    # 2. Scale each region so UV area is proportional to SURFACE area — even
    #    texel density across regions — then find the largest scale that fits.
    total_surface = sum(info[n]['surface'] for n in names)
    lo, hi = 0.0, 4.0
    best = None
    for _ in range(48):                       # bisection, fixed count = deterministic
        mid = (lo + hi) * 0.5
        boxes = []
        for name in names:
            d = info[name]
            target_uv = mid * d['surface'] / total_surface
            s = math.sqrt(target_uv / d['uv_area'])
            boxes.append((name, (d['w'] * s, d['h'] * s), s))
        order = sorted(boxes, key=lambda b: (-b[1][1], b[0]))
        placed = _shelf_pack([(k, wh) for k, wh, _s in order], gutter)
        if placed is None:
            hi = mid
        else:
            lo = mid
            best = (placed, {k: s for k, _wh, s in boxes})
    if best is None:
        raise RuntimeError("no scale packed the regions into the tile")
    placed, scales = best
    if verbose:
        print("      layout: total uv area %.4f of the tile" % lo)
        for name in names:
            d = info[name]
            print("        %-8s src box %.3f x %.3f fill %.1f%%  scale %.4f -> "
                  "%.3f x %.3f at (%.3f, %.3f)"
                  % (name, d['w'], d['h'],
                     100.0 * d['uv_area'] / max(d['w'] * d['h'], 1e-9),
                     scales[name], d['w'] * scales[name], d['h'] * scales[name],
                     placed[name][0], placed[name][1]))

    # 3. Move each region's UVs into its box.
    uvs = _uv_layer(obj)
    for name in names:
        d = info[name]
        s = scales[name]
        ox, oy = placed[name]
        x0, y0, _x1, _y1 = d['box']
        for i in d['loops']:
            uvs[i].uv.x = ox + (uvs[i].uv.x - x0) * s
            uvs[i].uv.y = oy + (uvs[i].uv.y - y0) * s
    obj.data.update()

    inset_guard_band(obj)
    m = measure(obj)
    m['regions'] = len(names)

    if len(obj.data.vertices) != before_verts or len(obj.data.polygons) != before_polys:
        raise RuntimeError("repack changed topology; it must only move UVs (B-13)")
    if m['outside'] > 0:
        raise RuntimeError("%d uv loops still outside the 0-1 tile (B-3)" % m['outside'])
    if m['shared_frac'] > 0.05:
        raise RuntimeError(
            "%.1f%% of the covered sheet is texels used by more than one "
            "triangle — the halves are SHARING texels, which is the mirrored "
            "layout ADR 0010 rejects. Check merge_overlap=False."
            % (m['shared_frac'] * 100.0))
    if verbose:
        print("   collapsed %d faces off tile 0; packed %d regions into "
              "their own boxes" % (moved, len(names)))
    return m


def measure(obj):
    """Everything the ruling is judged on, measured rather than asserted."""
    me = obj.data
    uvs = _uv_layer(obj)
    n = len(uvs)
    us = [uvs[i].uv.x for i in range(n)]
    vs = [uvs[i].uv.y for i in range(n)]
    outside = sum(1 for i in range(n)
                  if not (-1e-6 <= us[i] <= 1 + 1e-6 and -1e-6 <= vs[i] <= 1 + 1e-6))

    bm = bmesh.new()
    bm.from_mesh(me)
    bmesh.ops.triangulate(bm, faces=bm.faces[:])
    lay = bm.loops.layers.uv[0]
    uv_area = 0.0
    surface = 0.0
    degenerate = 0
    # Coverage raster: which texels any triangle touches. Summing triangle UV
    # area counts overlapped texels twice, so it CANNOT tell disjoint from
    # mirrored; the raster can.
    grid = bytearray(RASTER * RASTER)     # any coverage
    left = bytearray(RASTER * RASTER)     # the body's left half only
    right = bytearray(RASTER * RASTER)    # the body's right half only
    for f in bm.faces:
        a, b, c = [l[lay].uv for l in f.loops]
        area = abs((b - a).cross(c - a)) * 0.5
        uv_area += area
        surface += f.calc_area()
        if area <= 0.0:
            degenerate += 1
            continue
        # Which half of the BODY this triangle is on. The mirrored/disjoint
        # question is only ever about these two sets sharing texels, so that is
        # what gets measured — not per-triangle coverage counts, which
        # double-count every shared edge and would report ~18% on a chart that
        # shares nothing at all.
        mx = (f.verts[0].co.x + f.verts[1].co.x + f.verts[2].co.x) / 3.0
        side = left if mx > 1e-4 else (right if mx < -1e-4 else None)
        lo_x = max(0, int(math.floor(min(a.x, b.x, c.x) * RASTER)))
        hi_x = min(RASTER - 1, int(math.ceil(max(a.x, b.x, c.x) * RASTER)))
        lo_y = max(0, int(math.floor(min(a.y, b.y, c.y) * RASTER)))
        hi_y = min(RASTER - 1, int(math.ceil(max(a.y, b.y, c.y) * RASTER)))
        d = (b - a).cross(c - a)
        if d == 0.0:
            continue
        for py in range(lo_y, hi_y + 1):
            fy = (py + 0.5) / RASTER
            for px in range(lo_x, hi_x + 1):
                fx = (px + 0.5) / RASTER
                w0 = ((b.x - a.x) * (fy - a.y) - (b.y - a.y) * (fx - a.x)) / d
                w1 = ((c.x - b.x) * (fy - b.y) - (c.y - b.y) * (fx - b.x)) / d
                w2 = ((a.x - c.x) * (fy - c.y) - (a.y - c.y) * (fx - c.x)) / d
                if w0 >= 0 and w1 >= 0 and w2 >= 0:
                    k = py * RASTER + px
                    grid[k] = 1
                    if side is not None:
                        side[k] = 1
    bm.free()

    # Counting coverage rather than flagging it is what separates "disjoint"
    # from "mirrored". Comparing summed triangle area against covered area
    # cannot: the raster samples texel centres, so it undercounts slivers and
    # reports a ratio near 1.15 even on a chart that shares nothing. Texels
    # covered TWICE are unambiguous — they are shared texels.
    cells = float(RASTER * RASTER)
    covered = sum(grid) / cells
    both = sum(1 for i in range(len(grid)) if left[i] and right[i])
    either = sum(1 for i in range(len(grid)) if left[i] or right[i])
    shared = both / cells
    shared_frac = (both / float(either)) if either else 0.0
    density = 1024.0 * math.sqrt(uv_area / surface) if surface > 0 else 0.0
    return dict(loops=n, min_u=min(us), max_u=max(us), min_v=min(vs), max_v=max(vs),
                outside=outside, uv_area=uv_area, surface=surface, covered=covered,
                density=density, degenerate=degenerate, shared=shared,
                shared_frac=shared_frac,
                overlap=(uv_area / covered) if covered > 0 else 0.0)


def report(tag, m):
    print("%s" % tag)
    print("  u extent            : %.4f .. %.4f" % (m['min_u'], m['max_u']))
    print("  v extent            : %.4f .. %.4f" % (m['min_v'], m['max_v']))
    print("  outside the 0-1 tile: %d of %d loops" % (m['outside'], m['loops']))
    print("  uv area (summed)    : %.4f tiles" % m['uv_area'])
    print("  covered (rasterised): %.4f tiles  -> utilisation %.1f%%"
          % (m['covered'], m['covered'] * 100.0))
    print("  left/right shared   : %.4f tiles  -> %.1f%% of the two halves' texels "
          "(0%% = disjoint, ~100%% = mirrored)"
          % (m['shared'], m['shared_frac'] * 100.0))
    print("  surface area        : %.4f m2" % m['surface'])
    print("  zero-area triangles : %d" % m['degenerate'])
    print("  texel density       : %.0f px/m at 1024^2" % m['density'])


def main():
    """Measure the repack on its own, on the real path.

    The rig and the skin weights are built first because they are not optional
    scenery: which region a face belongs to is read from the bone that moves
    it, and the whole layout is per region.
    """
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import humanoid_template as HT

    HT.reset_scene()
    body = HT.load_base_body()
    print("source: %d vertices, %d polygons\n"
          % (len(body.data.vertices), len(body.data.polygons)))
    report("BEFORE — the pristine CC0 chart", measure(body))

    arm = HT.build_armature()
    HT.bind(body, arm)
    HT.prune_far_influences(body, arm)
    lod0 = HT.duplicate_reduced(body, arm, "Body_LOD0", HT.LOD_TRIANGLES[0],
                                morphs=False)
    print("")
    m = repack_by_region(lod0, HT.REGION_OF_BONE)
    print("")
    report("AFTER — repacked per ADR 0010 (one tile, halves disjoint)", m)


if __name__ == "__main__":
    main()
