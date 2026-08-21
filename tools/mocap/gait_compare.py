#!/usr/bin/env python3
"""Task 18.1 — measure the engine's procedural walk against the tracked human.

    python3 tools/mocap/gait_compare.py \
        --reference assets/mocap/walk_reference_tracking.json.gz \
        --engine build/engine_walk.json \
        --report docs/research/procedural-walk-vs-human.md

WHAT THIS IS FOR. Nobody has ever checked whether the shipped walk is any good.
This makes that a number instead of an opinion, and it does it on a motion the
engine can already produce, so a fault in the tracking-to-comparison path shows
up against a known quantity rather than against novel capture data.

HOW THE TWO SIDES ARE MADE COMPARABLE. Every metric comes from
`gait_metrics.py` and is called once per side. What differs is only the
construction of the canonical skeleton each side hands it:

  ENGINE     `mge_gait_dump` runs the real `LocomotionAnimator` and emits joint
             world positions. The root carries no translation, so ground travel
             is added as speed * t — which is exactly what the runtime does,
             since locomotion is distance-driven and the character controller
             moves the root.

  REFERENCE  Two constructions, because the footage supports different metrics
             to different depths, and pretending otherwise is how a pipeline
             reports confident nonsense:

             * SAGITTAL, from the side view's image landmarks scaled to metres.
               This is the only construction that has real ground travel, so
               stride, cadence, speed, bob, contacts and slide come from here.
               It has no lateral axis at all, and says so rather than reporting
               a step width of zero.
             * PER-VIEW 3D, from each view's metric world landmarks. Hip-origin
               and therefore useless for travel, but valid for joint angles
               (frame-invariant) and for girdle rotation amplitude (invariant
               under any rotation about the vertical, which is why the
               horizontal axis assignment below does not need to be recovered).
               Running it on all three views turns the spread between them into
               a measurement-quality figure instead of a hidden assumption.

Full 3D triangulation is task 18.2 and will replace the per-view construction.
18.1 deliberately does not wait for it: none of the metrics above need it, and
validating them first is what makes 18.2's output checkable when it lands.
"""

import argparse
import gzip
import json
import sys

import numpy as np

sys.path.insert(0, __file__.rsplit("/", 1)[0])
import gait_metrics as gm

# MediaPipe pose landmark indices, named once.
MP = {"shoulder_l": 11, "shoulder_r": 12, "hip_l": 23, "hip_r": 24, "knee_l": 25, "knee_r": 26,
      "ankle_l": 27, "ankle_r": 28, "heel_l": 29, "heel_r": 30, "toe_l": 31, "toe_r": 32}

# Rig joints that correspond to those landmarks. The hip landmark is the femoral
# head, which is what ThighL/R sit on; the knee is ShinL/R's origin; the ankle is
# Foot. `toe` is synthesised by the dump (the rig has no toe joint).
RIG = {"shoulder_l": "UpperArmL", "shoulder_r": "UpperArmR", "hip_l": "ThighL", "hip_r": "ThighR",
       "knee_l": "ShinL", "knee_r": "ShinR", "ankle_l": "FootL", "ankle_r": "FootR"}


# ------------------------------------------------------------- reference ----


def _panel_px(view, data, panels):
    """Panel-normalised landmarks to panel pixels."""
    _, _, w, h = panels[view]
    img = np.array([r["img"] for r in data])
    return np.stack([img[:, :, 0] * w, img[:, :, 1] * h], -1), img[:, :, 2]


def _scale_px_per_m(px, world):
    """Pixels per metre for a view, from the subject's own torso.

    The subject is the calibration target because there is no other one in
    frame — no chequerboard, no known object, and the video itself is not in the
    repo. Torso segments (shoulder to hip, same side) are used rather than limb
    segments because in a side view the legs swing out of the image plane and
    foreshorten, which shows up directly in the measurement: leg-derived scale
    scatters about three times as widely as torso-derived scale on this footage.

    The median over frames is taken, not the mean, so a few badly-tracked frames
    cannot drag the scale.
    """
    ratios = []
    for a, b in (("shoulder_l", "hip_l"), ("shoulder_r", "hip_r")):
        dpx = np.linalg.norm(px[:, MP[a]] - px[:, MP[b]], axis=1)
        dm = np.linalg.norm(world[:, MP[a]] - world[:, MP[b]], axis=1)
        ratios.append(dpx / np.maximum(dm, 1e-6))
    r = np.concatenate(ratios)
    return float(np.median(r)), float(np.std(r) / np.median(r))


def steady_window(hip_forward_m, fps, cycle_frames=45, fraction=0.85):
    """The stretch of constant-speed walking, excluding the start and the stop.

    The footage is a real take: the subject stands, walks, and stops. Averaging
    those ends into the walk would understate cadence and speed together, and
    would do it in a way that looks plausible. Velocity is smoothed over roughly
    one gait cycle so the within-stride speed ripple (which is real, about
    +/-0.2 m/s here) does not chop the window into fragments, then the longest
    run above 85% of the peak is taken.
    """
    v = np.gradient(gm._smooth(hip_forward_m, 15)) * fps
    vs = gm._smooth(v, cycle_frames)
    thr = fraction * float(np.percentile(vs, 90))
    idx = np.where(vs > thr)[0]
    if len(idx) == 0:
        return 0, len(hip_forward_m) - 1
    runs = np.split(idx, np.where(np.diff(idx) > 1)[0] + 1)
    run = max(runs, key=len)
    return int(run[0]), int(run[-1])


def load_reference(path, window_fraction=0.85):
    """Build the sagittal gait and the three per-view 3D gaits from tracking."""
    with gzip.open(path, "rt") as f:
        d = json.load(f)
    fps = d["fps"]
    side = d["views"]["side"]["data"]
    px, vis = _panel_px("side", side, d["panels"])
    world = np.array([r["world"] for r in side])
    pxm, pxm_cv = _scale_px_per_m(px, world)

    # Metres, ground frame. Image y grows downward, so up is negated; the origin
    # is put on the ground so foot heights are heights, not offsets.
    fwd = px[:, :, 0] / pxm
    up = -px[:, :, 1] / pxm

    hip_fwd = 0.5 * (fwd[:, MP["hip_l"]] + fwd[:, MP["hip_r"]])
    a, b = steady_window(hip_fwd, fps, fraction=window_fraction)

    # Put the origin on the ground — and fit the ground as a LINE in forward
    # position, not as a single constant.
    #
    # A constant is what a level, distortion-free camera would justify, and this
    # one is not quite that: over the 4.8 m the subject traverses, the apparent
    # floor drifts by about 20 mm, so the two feet end up with different
    # "lowest points" even though both are standing on the same floor. That is
    # small enough to ignore for anything hip-height, and fatal for a foot-down
    # test with a 20 mm tolerance — measured, it left the left foot registering
    # ONE ground contact in the window where the speed detector found three.
    #
    # So the floor is fitted from the feet's own lower envelope: fit, keep the
    # points at or below the fit, refit. Three passes is enough to converge onto
    # the planted foot and ignore the swinging one.
    foot_idx = [MP["toe_l"], MP["toe_r"], MP["heel_l"], MP["heel_r"]]
    fw = hip_fwd[a : b + 1]
    low = np.min(up[a : b + 1][:, foot_idx], axis=1)
    keep = np.ones(len(fw), dtype=bool)
    fit = np.polyfit(fw[keep], low[keep], 1)
    for _ in range(3):
        keep = low <= np.polyval(fit, fw) + 1e-6
        if keep.sum() < 4:
            break
        fit = np.polyfit(fw[keep], low[keep], 1)
    ground_at = lambda f: np.polyval(fit, f)  # noqa: E731
    ground_drift_mm = float(abs(fit[0]) * (fw.max() - fw.min()) * 1000.0)

    pts = {}
    for name, i in MP.items():
        # Every landmark is levelled against the floor under the HIPS, not under
        # itself: a common reference keeps the skeleton rigid. Levelling each
        # landmark by its own forward position would shear the body.
        pts[name] = np.stack([fwd[:, i], up[:, i] - ground_at(hip_fwd), np.zeros(len(fwd))], -1)
    sagittal = gm.Gait(pts, fps, window=(a, b), label="reference (side view, sagittal)")
    sagittal.lateral_valid = False

    # Per-view 3D from metric world landmarks. MediaPipe world is x right,
    # y down, z toward the camera; canonical is forward, up, left. The
    # horizontal assignment is only correct for the front view, where the
    # subject walks along the camera axis — which is why nothing translation-
    # dependent is computed from these, and the metrics that ARE computed
    # (angles, girdle rotation amplitude) are invariant to it.
    views = {}
    for view, vd in d["views"].items():
        w3 = np.array([r["world"] for r in vd["data"]])
        p = {n: np.stack([-w3[:, i, 2], -w3[:, i, 1], -w3[:, i, 0]], -1) for n, i in MP.items()}
        views[view] = gm.Gait(p, fps, window=(a, b), label=f"reference ({view}, world landmarks)")

    meta = {"px_per_m": pxm, "px_per_m_cv": pxm_cv, "window": (a, b), "fps": fps,
            "ground_drift_mm": ground_drift_mm,
            "frames_total": d["frames"], "source_sha256": d["source_sha256"],
            "visibility_mean": {v: float(np.mean([np.array(r["img"])[:, 2].mean()
                                                  for r in vd["data"]]))
                                for v, vd in d["views"].items()}}
    return sagittal, views, meta


# ---------------------------------------------------------------- engine ----


def load_engine(path):
    """Build the canonical gait from an `mge_gait_dump` file.

    Engine character-local axes are +X left, +Y up, -Z forward (the comment on
    `LocomotionAnimator::samplePose` is the authority: "positive X rotation
    swings the limb forward (-Z)"). Ground travel is added along forward at the
    commanded speed, which is what the runtime does — the animator supplies the
    cycle and the controller supplies the translation.
    """
    with open(path) as f:
        d = json.load(f)
    joints = d["joints"]
    pos = np.array([r["pos"] for r in d["data"]])
    toe = np.array([r["toe"] for r in d["data"]])
    t = np.array([r["t"] for r in d["data"]])
    travel = d["speed"] * t

    def canon(p):
        return np.stack([-p[:, 2] + travel, p[:, 1], p[:, 0]], -1)

    pts = {n: canon(pos[:, joints.index(j)]) for n, j in RIG.items()}
    pts["toe_l"] = canon(toe[:, 0])
    pts["toe_r"] = canon(toe[:, 1])
    g = gm.Gait(pts, d["fps"], label="engine LocomotionAnimator")
    g.lateral_valid = True
    return g, d


# ---------------------------------------------------------------- report ----


def measure(gait, translation=True, rotation=True):
    """Every metric that the given construction actually supports — and no more.

    `translation` gates everything that needs real ground travel; `rotation`
    gates everything that needs a lateral axis. Both are off for constructions
    that cannot support them, rather than left on to return a confident zero.
    """
    out = {}
    if translation:
        out.update(gm.stride(gait))
        out.update(gm.contact_timing(gait))
        out.update(gm.vertical_bob(gait))
        out.update(gm.foot_slide(gait))
    if rotation:
        out.update(gm.axial_rotation(gait))
        out.update(gm.step_width(gait))
    out["angles"] = gm.angle_summary(gm.joint_angle_curves(gait))
    return out


def fmt(v, nd=3):
    if v is None:
        return "n/a"
    if isinstance(v, bool):
        return "yes" if v else "no"
    if isinstance(v, float):
        return f"{v:.{nd}f}"
    return str(v)


def figure(path, sag, views, eng):
    """Cycle-averaged curves for both sides — the evidence, not the summary.

    Range-of-motion numbers say the engine's knee bends a third as far. They do
    not say whether it bends at the right MOMENT, and a curve that peaks in the
    wrong part of the cycle is a different fault with a different fix. So the
    curves go beside each other, resampled onto one normalised gait cycle so
    two different cadences can be laid over each other at all.
    """
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    ref3d = views["side"]
    rs = gm.cycle_starts(sag, "l") or []
    es = [r[0] for r in (gm.geometric_contacts(eng, "l") or [])]
    grid = np.linspace(0, 100, 100, endpoint=False)

    # Reported as FLEXION (0 = straight leg), not as the interior angle, so the
    # curve rises when the knee bends. The interior angle is what the metric
    # computes and what the JSON carries; this is a presentation choice only.
    rk = gm.joint_angle_curves(ref3d).get("knee_l")
    ek = gm.joint_angle_curves(eng).get("knee_l")
    panels = [
        ("knee flexion (deg, 0 = straight)", None if rk is None else 180.0 - rk,
         None if ek is None else 180.0 - ek, None),
        ("foot pitch, knee-ankle-toe (deg)", gm.joint_angle_curves(ref3d).get("ankle_l"),
         gm.joint_angle_curves(eng).get("ankle_l"), None),
        ("pelvis height, de-trended (mm)",
         (lambda h: (h - h.mean()) * 1000)(0.5 * (sag.get("hip_l")[:, 1] + sag.get("hip_r")[:, 1])),
         (lambda h: (h - h.mean()) * 1000)(0.5 * (eng.get("hip_l")[:, 1] + eng.get("hip_r")[:, 1])),
         None),
        ("left toe travel over the ground (mm)",
         (lambda p: (p - p[0]) * 1000)(sag.get("toe_l")[:, 0]),
         (lambda p: (p - p[0]) * 1000)(eng.get("toe_l")[:, 0]), "zero_min"),
    ]

    fig, axes = plt.subplots(2, 2, figsize=(11, 7))
    for ax, (title, ref, en, mode) in zip(axes.ravel(), panels):
        for series, starts, colour, label in ((ref, rs, "#1b6ca8", "human reference"),
                                              (en, es, "#c1440e", "engine procedural")):
            if series is None or len(starts) < 2:
                continue
            # Foot position advances from cycle to cycle, so each cycle is
            # re-zeroed on its own minimum before averaging. A planted foot then
            # draws a flat plateau followed by a step; a sliding foot draws a
            # ramp with no plateau at all, which is the whole comparison.
            got = gm.resample_cycle(np.asarray(series, dtype=float), starts,
                                    zero_each=(mode == "zero_min"))
            if got is None:
                continue
            mean, sd = got
            ax.plot(grid, mean, color=colour, lw=2, label=label)
            ax.fill_between(grid, mean - sd, mean + sd, color=colour, alpha=0.18, lw=0)
        ax.set_title(title, fontsize=10)
        ax.set_xlabel("% of gait cycle (left heel strike to left heel strike)", fontsize=8)
        ax.grid(alpha=0.25)
        ax.legend(fontsize=8)
    fig.suptitle("Task 18.1 — engine procedural walk vs tracked human, both at 1.355 m/s",
                 fontsize=11)
    fig.tight_layout()
    fig.savefig(path, dpi=130)
    print(f"wrote {path}  (human cycles {max(len(rs) - 1, 0)}, engine cycles {max(len(es) - 1, 0)})")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--reference", required=True)
    ap.add_argument("--engine", required=True)
    ap.add_argument("--json", help="write the raw measurements here")
    ap.add_argument("--figure", help="write cycle-averaged curves here (needs matplotlib)")
    ap.add_argument("--window-fraction", type=float, default=0.85,
                    help="steady-window threshold as a fraction of peak speed; "
                         "vary it to check the numbers do not hinge on it")
    args = ap.parse_args()

    sag, views, meta = load_reference(args.reference, window_fraction=args.window_fraction)
    eng, engmeta = load_engine(args.engine)

    # The sagittal construction has no lateral axis, so it is asked only for
    # what a side view can actually answer. The per-view 3D constructions are
    # hip-origin, so they are asked only for angles and girdle rotation. The
    # engine has both and is asked for everything.
    ref = measure(sag, translation=True, rotation=False)
    ref_views = {v: measure(g, translation=False, rotation=True) for v, g in views.items()}
    en = measure(eng, translation=True, rotation=True)

    # Step width is a front-view measurement: it is the one view whose lateral
    # axis is unambiguous, because the subject walks along its camera axis.
    ref["step_width_mean_m"] = ref_views["front"].get("step_width_mean_m")
    ref["step_width_range_m"] = ref_views["front"].get("step_width_range_m")
    for k in ("pelvis_yaw_p2p_deg", "shoulder_yaw_p2p_deg", "girdle_correlation"):
        vals = [rv[k] for rv in ref_views.values() if rv.get(k) is not None]
        if vals:
            ref[k] = float(np.median(vals))

    a, b = meta["window"]
    print(f"reference: {meta['frames_total']} frames at {meta['fps']:.0f} fps; "
          f"steady walk frames {a}-{b} ({(b - a + 1) / meta['fps']:.2f} s)")
    print(f"scale: {meta['px_per_m']:.1f} px/m (cv {meta['px_per_m_cv'] * 100:.1f}%)")
    print(f"engine: commanded {engmeta['speed']:.3f} m/s, "
          f"{engmeta['frames']} frames at {engmeta['fps']:.0f} fps")

    # Comparing two walks at different speeds measures the speed difference, not
    # the walks: stride, cadence and knee flexion all move with speed in both a
    # human and our animator. The tool refuses to be quiet about it.
    human_speed = ref.get("speed_mps")
    if human_speed and abs(engmeta["speed"] - human_speed) / human_speed > 0.05:
        print(f"\n  WARNING: the engine was driven at {engmeta['speed']:.3f} m/s but the "
              f"reference walks at {human_speed:.3f} m/s.\n"
              f"  Re-run: mge_gait_dump --speed {human_speed:.3f}\n"
              f"  Every row below mixes the walk difference with a speed difference.")
    print()

    rows = [
        ("walking speed", "m/s", "speed_mps", 3),
        ("cadence", "steps/min", "cadence_steps_min", 1),
        ("step time", "s", "step_time_s", 3),
        ("stride (from travel)", "m", "stride_from_travel_m", 3),
        ("stride (from contacts)", "m", "stride_from_contacts_m", 3),
        ("stance time L", "s", "stance_time_l_s", 3),
        ("stance time R", "s", "stance_time_r_s", 3),
        ("duty factor", "", "duty_factor", 3),
        ("double support", "share", "double_support_share", 3),
        ("airborne", "share", "airborne_share", 3),
        ("vertical bob p2p", "m", "bob_p2p_m", 4),
        ("vertical bob rate", "Hz", "bob_dominant_hz", 2),
        ("slide, foot down", "m", "down_slide_mean_m", 4),
        ("slide, mid-stance", "m", "down_slide_midstance_m", 4),
        ("min foot speed in contact", "m/s", "down_contact_min_speed_mps", 3),
        ("step width mean", "m", "step_width_mean_m", 4),
        ("step width swing", "m", "step_width_range_m", 4),
        ("pelvis yaw p2p", "deg", "pelvis_yaw_p2p_deg", 1),
        ("shoulder yaw p2p", "deg", "shoulder_yaw_p2p_deg", 1),
        ("girdle correlation", "", "girdle_correlation", 3),
    ]
    print(f"{'metric':26s} {'unit':9s} {'human':>10s} {'engine':>10s}   difference")
    print("-" * 78)
    for label, unit, key, nd in rows:
        h = ref.get(key)
        if h is None:
            h = next((rv.get(key) for rv in ref_views.values() if rv.get(key) is not None), None)
        e = en.get(key)
        diff = ""
        if isinstance(h, float) and isinstance(e, float):
            # A ratio of two correlations, or of anything that legitimately
            # crosses zero, is not a meaningful percentage.
            if key in ("girdle_correlation", "airborne_share", "double_support_share"):
                diff = f"{e - h:+.3f} abs"
            elif abs(h) > 1e-9:
                diff = f"{(e - h) / abs(h) * 100:+.0f}%"
            else:
                diff = f"{e - h:+.3f} abs"
        print(f"{label:26s} {unit:9s} {fmt(h, nd):>10s} {fmt(e, nd):>10s}   {diff}")

    print("\nper-view girdle rotation (world landmarks; spread is measurement quality)")
    for v, rv in ref_views.items():
        print(f"  {v:14s} pelvis {fmt(rv.get('pelvis_yaw_p2p_deg'), 1):>6s} deg   "
              f"shoulder {fmt(rv.get('shoulder_yaw_p2p_deg'), 1):>6s} deg   "
              f"corr {fmt(rv.get('girdle_correlation'), 2):>6s}")

    print("\njoint angle range of motion (deg)")
    print(f"{'angle':10s} {'human side':>12s} {'human 3q':>10s} {'human front':>12s} "
          f"{'engine':>9s}")
    for k in ("knee_l", "knee_r", "ankle_l", "ankle_r"):
        def rom(m):
            return m["angles"].get(k, {}).get("rom_deg")
        print(f"{k:10s} {fmt(rom(ref_views['side']), 1):>12s} "
              f"{fmt(rom(ref_views['threequarter']), 1):>10s} "
              f"{fmt(rom(ref_views['front']), 1):>12s} {fmt(rom(en), 1):>9s}")

    if args.figure:
        figure(args.figure, sag, views, eng)

    if args.json:
        blob = {"meta": meta, "engine_meta": {k: v for k, v in engmeta.items() if k != "data"},
                "reference_sagittal": ref, "reference_views": ref_views, "engine": en}
        with open(args.json, "w") as f:
            json.dump(blob, f, indent=1, default=float)
        print(f"\nwrote {args.json}")


if __name__ == "__main__":
    main()
