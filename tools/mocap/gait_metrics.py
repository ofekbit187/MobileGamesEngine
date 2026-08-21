#!/usr/bin/env python3
"""Gait metrics — ONE implementation, applied to both sides of task 18.1.

WHY THIS FILE EXISTS SEPARATELY. 18.1 compares the engine's procedural walk
against a real human. That comparison is only honest if both sides are measured
the same way; if the reference used one stride definition and the engine another,
the "difference" reported would partly be a difference in arithmetic. So every
metric lives here exactly once, takes a canonical skeleton, and is called twice.

CANONICAL FRAME. Everything below expects a `Gait`: named landmarks per frame,
in metres, in a GROUND frame — meaning forward travel is included rather than
subtracted out. Axes:

    +X  forward (direction of travel)
    +Y  up
    +Z  the subject's left

Ground frame is not a formality. Foot contact is defined by the foot being
stationary WITH RESPECT TO THE GROUND, and in a hip-relative frame a planted
foot is moving backwards at exactly walking speed. The same detector then
measures foot slide on the engine side, which is the property distance-driven
locomotion exists to protect.

Landmark names are the intersection of what a pose estimator gives and what our
17-joint rig has: hip_l hip_r knee_l knee_r ankle_l ankle_r toe_l toe_r
shoulder_l shoulder_r. Anything a caller cannot supply it simply omits, and the
metrics that need it report None rather than guessing.
"""

import math

import numpy as np

# --------------------------------------------------------------- container --


class Gait:
    """Named landmark trajectories in the canonical ground frame.

    `points` maps a landmark name to an (F, 3) array of metres. `fps` is the
    sample rate. `window` is an optional (first, last) inclusive frame range
    restricting every metric to a steady-state stretch — the reference footage
    contains a standing start and a stop, and averaging those into a walk would
    understate cadence and speed alike.
    """

    def __init__(self, points, fps, window=None, label=""):
        self.points = {k: np.asarray(v, dtype=float) for k, v in points.items()}
        self.fps = float(fps)
        self.label = label
        n = len(next(iter(self.points.values())))
        self.window = (0, n - 1) if window is None else (int(window[0]), int(window[1]))

    def __contains__(self, name):
        return name in self.points

    def get(self, name):
        """Landmark over the analysis window, (W, 3) metres."""
        a, b = self.window
        return self.points[name][a : b + 1]

    @property
    def frames(self):
        a, b = self.window
        return b - a + 1

    @property
    def duration(self):
        return self.frames / self.fps


# ---------------------------------------------------------------- helpers ---


def _smooth(a, k):
    """Moving average with edge padding.

    Edge padding rather than `mode='same'` zero padding: a zero-padded average
    fabricates a huge apparent velocity in the first frames, which on this
    footage looked like a 4.8 m/s lunge out of a standing start.
    """
    if k <= 1:
        return np.asarray(a, dtype=float)
    pad = k // 2
    return np.convolve(np.pad(np.asarray(a, dtype=float), pad, mode="edge"),
                       np.ones(k) / k, mode="valid")[: len(a)]


def _speed(track, fps, k=5):
    """Point speed in m/s, lightly smoothed."""
    sm = np.stack([_smooth(track[:, i], k) for i in range(track.shape[1])], -1)
    return np.linalg.norm(np.stack([np.gradient(sm[:, i]) for i in range(sm.shape[1])], -1),
                          axis=1) * fps


def _runs(mask):
    """Contiguous True runs of a boolean array as (first, last) inclusive."""
    out, start = [], None
    for i, v in enumerate(mask):
        if v and start is None:
            start = i
        elif not v and start is not None:
            out.append((start, i - 1))
            start = None
    if start is not None:
        out.append((start, len(mask) - 1))
    return out


def _angle(a, b, c):
    """Interior angle at b, in degrees, for point triples over frames."""
    u, v = a - b, c - b
    nu = np.linalg.norm(u, axis=-1)
    nv = np.linalg.norm(v, axis=-1)
    cos = np.sum(u * v, axis=-1) / np.maximum(nu * nv, 1e-9)
    return np.degrees(np.arccos(np.clip(cos, -1.0, 1.0)))


def _wrap_deg(a):
    return (np.asarray(a) + 180.0) % 360.0 - 180.0


# ---------------------------------------------------------------- contacts --

# A foot slower than this, relative to the ground, is planted. Chosen from the
# reference's own histogram rather than from taste: stance sits under 0.25 m/s
# and swing peaks near 5 m/s, so the two populations are two orders of magnitude
# apart and any threshold in the gap gives the same answer. Reported in the
# output so a reader can see it was not tuned to a result.
CONTACT_SPEED = 0.35


FOOT_PARTS = ("heel", "toe", "ankle")


def foot_landmarks(gait, side):
    """The foot landmarks this construction actually carries, in contact order."""
    return [f"{p}_{side}" for p in FOOT_PARTS if f"{p}_{side}" in gait]


def contacts(gait, side, speed_threshold=CONTACT_SPEED, min_frames=4):
    """Stance intervals for one foot as (first, last) inclusive frame pairs.

    A foot is planted if ANY of its landmarks is stationary, not if a chosen one
    is. This is not a refinement, it is the difference between a right and a
    wrong answer: the human foot rolls through stance — heel down at strike,
    heel off while the toe is still loaded — so tracking the toe alone ends
    stance early at heel-off and tracking the heel alone ends it early at
    heel-rise. Keying on a single landmark measured this footage at duty factor
    0.49 with ZERO double support, which would make a casual walk a run. Taking
    the minimum over the foot's landmarks recovers both.

    Runs shorter than `min_frames` are dropped as estimator jitter.
    """
    names = foot_landmarks(gait, side)
    if not names:
        return None
    sp = np.min(np.stack([_speed(gait.get(n), gait.fps) for n in names]), axis=0)
    return [r for r in _runs(sp < speed_threshold) if r[1] - r[0] + 1 >= min_frames]


# How close to its own lowest point a foot must be to count as down. Absolute
# rather than relative to y=0, because the two sides put "ground" in different
# places: the reference's origin is the ground plane, while the rig's Foot joint
# is the ANKLE and sits ~0.13 m up even when the sole is flat.
CONTACT_HEIGHT = 0.02


def geometric_contacts(gait, side, tol=CONTACT_HEIGHT, min_frames=4):
    """Stance intervals defined by the foot being DOWN, not by it being still.

    The speed-based detector above answers "is this foot planted?", which is the
    right question for captured motion. It is the wrong question for a walk that
    slides, because a foot skating along the floor is in contact and moving, and
    a speed detector simply reports it as never in contact — which is what the
    engine's procedural cycle does, and it would read as a missing measurement
    rather than as the defect it is.

    So contact is also defined geometrically: the foot is down when its lowest
    landmark is within `tol` of the lowest that landmark ever gets. Slide
    measured over THIS interval is the honest number for both sides.
    """
    names = foot_landmarks(gait, side)
    if not names:
        return None
    h = np.min(np.stack([gait.get(n)[:, 1] for n in names]), axis=0)
    return [r for r in _runs(h < float(np.min(h)) + tol) if r[1] - r[0] + 1 >= min_frames]


def contact_timing(gait):
    """Cadence, stance/swing split and double-support share.

    Everything here is derived from foot contacts rather than from a phase
    parameter, so it applies unchanged to captured motion and to a procedural
    cycle that has no contact concept of its own.
    """
    out = {}
    st = {s: contacts(gait, s) for s in ("l", "r")}
    if st["l"] is None or st["r"] is None:
        return out

    # Cadence from heel strikes (the start of each stance), both feet.
    strikes = sorted([r[0] for r in st["l"]] + [r[0] for r in st["r"]])
    if len(strikes) >= 2:
        step_frames = np.diff(strikes)
        out["step_time_s"] = float(np.mean(step_frames) / gait.fps)
        out["cadence_steps_min"] = float(60.0 * gait.fps / np.mean(step_frames))
        out["step_time_cv"] = float(np.std(step_frames) / max(np.mean(step_frames), 1e-9))
        out["steps_counted"] = int(len(strikes))

    # Duty factor: fraction of the cycle each foot spends on the ground. ~0.6
    # in human walking; below 0.5 for both feet at once is a run, by definition.
    for s in ("l", "r"):
        # Ignore runs clipped by the window edge — a half-seen stance reads short.
        full = [r for r in st[s] if r[0] > 0 and r[1] < gait.frames - 1]
        if full:
            out[f"stance_time_{s}_s"] = float(np.mean([r[1] - r[0] + 1 for r in full]) / gait.fps)
    if "step_time_s" in out and "stance_time_l_s" in out and "stance_time_r_s" in out:
        stance = 0.5 * (out["stance_time_l_s"] + out["stance_time_r_s"])
        out["duty_factor"] = float(stance / (2.0 * out["step_time_s"]))

    # Double support: both feet planted at once. Zero means a flight phase or a
    # cycle that never commits weight — a strong signal about a procedural walk.
    both = np.zeros(gait.frames, dtype=bool)
    lm = np.zeros(gait.frames, dtype=bool)
    rm = np.zeros(gait.frames, dtype=bool)
    for a, b in st["l"]:
        lm[a : b + 1] = True
    for a, b in st["r"]:
        rm[a : b + 1] = True
    both = lm & rm
    out["double_support_share"] = float(both.mean())
    out["airborne_share"] = float((~lm & ~rm).mean())
    return out


def foot_slide(gait):
    """Metres a foot travels while it is supposed to be planted.

    The number distance-driven locomotion exists to keep near zero, and the one
    a capture that ignores ground contact reintroduces (task 18.4). Measured as
    the displacement of the contact landmark across each stance interval.
    """
    out = {}
    for tag, detect in (("", contacts), ("down_", geometric_contacts)):
        per, mid, floor = [], [], []
        for s in ("l", "r"):
            st = detect(gait, s)
            if not st:
                continue
            names = foot_landmarks(gait, s)
            p = gait.get(names[0])
            sp = np.min(np.stack([_speed(gait.get(n), gait.fps) for n in names]), axis=0)
            for a, b in st:
                if a == 0 or b == gait.frames - 1:
                    continue
                seg = p[a : b + 1]
                # Forward slip specifically: a foot creeping along the floor is
                # the failure, and lateral estimator jitter is not.
                per.append(float(np.ptp(seg[:, 0])))
                # Middle half of the interval. The geometric detector opens
                # before the foot has finished landing and closes after it has
                # started leaving, and those transients are real foot motion on
                # BOTH sides — including them flatters whichever walk has the
                # shorter contact. Mid-stance is the part where a foot must be
                # still, so it is the part worth comparing.
                q = (b - a + 1) // 4
                if b - a + 1 >= 8:
                    mids = p[a + q : b - q + 1]
                    mid.append(float(np.ptp(mids[:, 0])))
                # The cleanest question of all, and the one that needs no window
                # fairness argument: DOES THIS FOOT EVER ACTUALLY STOP?
                floor.append(float(np.min(sp[a : b + 1])))
        if per:
            out[f"{tag}slide_mean_m"] = float(np.mean(per))
            out[f"{tag}slide_max_m"] = float(np.max(per))
            out[f"{tag}slides_counted"] = len(per)
        if mid:
            out[f"{tag}slide_midstance_m"] = float(np.mean(mid))
        if floor:
            out[f"{tag}contact_min_speed_mps"] = float(np.mean(floor))
    return out


# ------------------------------------------------------------ stride & bob --


def stride(gait):
    """Stride length, step length and walking speed.

    Two independent estimates of stride, because one is not evidence:

      * `stride_from_travel_m` — hip travel over one full gait cycle. Needs a
        ground frame with real translation.
      * `stride_from_contacts_m` — how far the same foot advances between
        consecutive plants. Needs no speed estimate at all.

    They come from different quantities, so agreement between them is a check
    on the pipeline and disagreement is a flag worth chasing.
    """
    out = {}
    if "hip_l" in gait and "hip_r" in gait:
        hip = 0.5 * (gait.get("hip_l") + gait.get("hip_r"))
        t = np.arange(gait.frames) / gait.fps
        fit = np.polyfit(t, hip[:, 0], 1)
        out["speed_mps"] = float(fit[0])
        out["speed_fit_residual_mm"] = float(np.std(hip[:, 0] - np.polyval(fit, t)) * 1000.0)

    timing = contact_timing(gait)
    if "step_time_s" in timing and "speed_mps" in out:
        out["stride_from_travel_m"] = float(out["speed_mps"] * 2.0 * timing["step_time_s"])

    per = []
    for s in ("l", "r"):
        st = contacts(gait, s)
        if not st or len(st) < 2:
            continue
        name = f"toe_{s}" if f"toe_{s}" in gait else f"ankle_{s}"
        p = gait.get(name)
        # Plant position = mean forward position through the stance, which is
        # steadier than either endpoint when the estimator wobbles at strike.
        plant = [float(np.mean(p[a : b + 1, 0])) for a, b in st]
        per += list(np.diff(plant))
    if per:
        out["stride_from_contacts_m"] = float(np.mean(per))
        out["stride_from_contacts_sd_m"] = float(np.std(per))
        out["strides_counted"] = len(per)

    return out


def step_width(gait):
    """Lateral separation of the ankles, mean and swing.

    Needs a construction with a real lateral axis, so a caller must not hand it
    a sagittal-only one — a side view has no left, and the zero it would report
    is an absence of evidence dressed as a measurement.

    `step_width_range_m` is the more diagnostic of the two: a walk whose feet
    never move laterally has a range of exactly zero, which no body does.
    """
    out = {}
    if not getattr(gait, "lateral_valid", True):
        return out
    if "ankle_l" in gait and "ankle_r" in gait:
        sep = np.abs(gait.get("ankle_l")[:, 2] - gait.get("ankle_r")[:, 2])
        out["step_width_mean_m"] = float(np.mean(sep))
        out["step_width_range_m"] = float(np.max(sep) - np.min(sep))
    return out


def vertical_bob(gait):
    """Peak-to-peak vertical excursion of the pelvis, and its dominant rate.

    Human walking bobs about 40-50 mm peak-to-peak at TWICE step frequency —
    the body rises over each stance leg, so two rises per stride. The rate
    matters as much as the amplitude: a bob at the wrong frequency is a
    different motion, not a smaller one.
    """
    out = {}
    if "hip_l" not in gait or "hip_r" not in gait:
        return out
    hip = 0.5 * (gait.get("hip_l") + gait.get("hip_r"))
    y = _smooth(hip[:, 1], 5)
    y = y - np.polyval(np.polyfit(np.arange(len(y)), y, 1), np.arange(len(y)))
    out["bob_p2p_m"] = float(np.percentile(y, 97.5) - np.percentile(y, 2.5))
    out["bob_rms_m"] = float(np.std(y))
    if len(y) >= 16:
        spec = np.abs(np.fft.rfft(y * np.hanning(len(y))))
        freq = np.fft.rfftfreq(len(y), 1.0 / gait.fps)
        spec[0] = 0.0
        out["bob_dominant_hz"] = float(freq[int(np.argmax(spec))])
    return out


# ------------------------------------------------------- rotation & angles --


def axial_rotation(gait):
    """Pelvis and shoulder yaw about the vertical, and their phase relationship.

    Counter-rotation is what makes a walk read as a walk: the pelvis leads with
    the swinging leg while the shoulders go the other way, roughly 180 degrees
    out of phase. Amplitude alone does not capture it — two girdles rotating
    together by the same amount is a completely different motion — so the phase
    offset is reported beside the amplitudes.
    """
    out = {}

    def yaw(a, b):
        """Girdle angle about the vertical, unwrapped and de-meaned.

        UNWRAPPED FIRST, then de-meaned. Taking the mean of raw atan2 output is
        wrong whenever the axis sits near the +/-180 branch cut, and it fails
        loudly enough to catch: on this footage it reported a pelvis swinging
        through 359 degrees per stride.

        De-meaning is also what makes this metric valid on all three views. Only
        the vertical axis has to be right; any rotation of the horizontal frame
        adds a constant, which the de-mean removes. That is deliberate — the
        walking direction is only unambiguous in the front view, and recovering
        it for the others is task 18.2's job, not this metric's.
        """
        v = gait.get(a) - gait.get(b)
        ang = np.unwrap(np.arctan2(v[:, 0], v[:, 2]))
        return _smooth(np.degrees(ang) - np.mean(np.degrees(ang)), 5)

    p = s = None
    if "hip_l" in gait and "hip_r" in gait:
        p = yaw("hip_l", "hip_r")
        out["pelvis_yaw_p2p_deg"] = float(np.percentile(p, 97.5) - np.percentile(p, 2.5))
    if "shoulder_l" in gait and "shoulder_r" in gait:
        s = yaw("shoulder_l", "shoulder_r")
        out["shoulder_yaw_p2p_deg"] = float(np.percentile(s, 97.5) - np.percentile(s, 2.5))

    if p is not None and s is not None:
        # Phase relationship by normalised cross-correlation at zero lag.
        denom = math.sqrt(float(np.sum(p * p) * np.sum(s * s))) or 1e-9
        out["girdle_correlation"] = float(np.sum(p * s) / denom)
        # -1 is perfect counter-rotation, +1 is the two girdles moving together.
        out["counter_rotation"] = out["girdle_correlation"] < 0.0
    return out


def joint_angle_curves(gait):
    """Knee and ankle angle series, in degrees, per side.

    Knee is the hip-knee-ankle interior angle (180 = straight). Ankle is the
    knee-ankle-toe interior angle (~90 = neutral standing). Both are computed
    from positions and are therefore frame-invariant, which is what lets the
    same function run on captured landmarks and on rig joints.
    """
    out = {}
    for s in ("l", "r"):
        if all(f"{n}_{s}" in gait for n in ("hip", "knee", "ankle")):
            out[f"knee_{s}"] = _angle(gait.get(f"hip_{s}"), gait.get(f"knee_{s}"),
                                      gait.get(f"ankle_{s}"))
        if all(f"{n}_{s}" in gait for n in ("knee", "ankle", "toe")):
            out[f"ankle_{s}"] = _angle(gait.get(f"knee_{s}"), gait.get(f"ankle_{s}"),
                                       gait.get(f"toe_{s}"))
    return out


def angle_summary(curves):
    """Range-of-motion summary per angle series."""
    out = {}
    for k, v in curves.items():
        out[k] = {
            "min_deg": float(np.min(v)),
            "max_deg": float(np.max(v)),
            "rom_deg": float(np.max(v) - np.min(v)),
            "mean_deg": float(np.mean(v)),
        }
    return out


def resample_cycle(series, cycle_starts, n=100, zero_each=False):
    """Average one series over gait cycles, resampled to `n` points.

    Cycle-averaging is how two walks at different cadences are compared at all:
    without it, "our knee bends less" and "our cycle is shorter" are the same
    number. Returns (mean, sd) over the cycles, or None if there are none.

    `zero_each` subtracts each cycle's own minimum BEFORE averaging. It is
    required for any quantity that advances between cycles — foot position over
    the ground being the case that matters here. Without it the spread across
    cycles is dominated by where in the room the subject was, which swamps the
    within-cycle shape entirely: on this footage the band came out +/-2.7 m
    around a signal whose real excursion is 1.4 m.
    """
    if len(cycle_starts) < 2:
        return None
    grid = np.linspace(0.0, 1.0, n, endpoint=False)
    stack = []
    for a, b in zip(cycle_starts[:-1], cycle_starts[1:]):
        if b - a < 4:
            continue
        seg = np.asarray(series[a:b], dtype=float)
        r = np.interp(grid, np.linspace(0.0, 1.0, len(seg), endpoint=False), seg)
        stack.append(r - r.min() if zero_each else r)
    if not stack:
        return None
    stack = np.array(stack)
    return stack.mean(0), stack.std(0)


def cycle_starts(gait, side="l"):
    """Frame indices of each heel strike for one foot — one gait cycle apart."""
    st = contacts(gait, side)
    return [r[0] for r in st] if st else []
