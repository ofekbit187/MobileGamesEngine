#!/usr/bin/env python3
"""Task 18.2 — combining the three views into one 3D skeleton, and checking whether
that is actually better than using one view.

    python3 tools/mocap/multiview.py --reference assets/mocap/walk_reference_tracking.json.gz

WHAT THIS ANSWERS, AND WHY IT IS ASKED AT ALL. The obvious reading of 18.2 is
"triangulate, get a better skeleton, hand it to 18.3". That reading contains an
assumption — that combining views beats one view — and 18.3 is expensive enough
that the assumption is worth an hour of measurement before it is worth a day of
code. It does not survive: see `docs/research/multiview-triangulation.md`.

THE VALIDATION PROTOCOL, which is the durable part of this file.

Hold out one view. Build a 3D skeleton from the other two. Then fit a camera for
the held-out view and measure how well the skeleton reprojects into an image it
never saw. A method that has genuinely recovered 3D structure predicts the third
view; a method that has merely smoothed two estimates together does not.

Three properties make it trustworthy, and all three were arrived at by getting
them wrong first:

  * THE CAMERA IS FITTED FRESH FOR EVERY ARM. An earlier version of this
    comparison reused one global alignment fitted with `front` as its reference,
    which handed the `front` fold a rotation chosen to explain exactly the
    mapping being scored. It made triangulation look 2.5x better than a single
    view. With every arm scored identically, that margin disappeared entirely.
    A benchmark whose arms are calibrated differently measures the calibration.

  * ROTATION IS FITTED, NOT ASSUMED. Scoring a view's own 3D against its own
    image WITHOUT fitting the rotation reported a 27-34% error and looked like a
    damning noise floor for the estimator. Fitting it gives 3.1-5.1%. The
    estimator was never the problem; the harness was.

  * MULTI-START, BECAUSE SCALED-ORTHOGRAPHIC HAS A DEPTH REFLECTION AMBIGUITY.
    A point set and its depth mirror project identically, so the fit has (at
    least) two minima and a single start lands in whichever one it began nearest.
    Eight starts per fit.

Errors are reported as a percentage of the subject's torso length in that view's
image, so the three panels — 1045x620, 330x620 and 1405x385 — are comparable.
"""

import argparse
import gzip
import json

import numpy as np
from scipy.optimize import least_squares
from scipy.spatial.transform import Rotation as Rot

# The landmarks scored. Face points are excluded: they are clustered within a few
# centimetres, so they dominate a landmark count while carrying almost no
# information about the pose our rig has to reproduce.
BODY = np.array([11, 12, 13, 14, 15, 16, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32])
HIP = [23, 24]
SHOULDER = [11, 12]
MULTISTART = 8


def load(path, window):
    with gzip.open(path, "rt") as f:
        d = json.load(f)
    out = {}
    for v, vd in d["views"].items():
        a = np.array([r["img"] for r in vd["data"]])
        _, _, w, h = d["panels"][v]
        px = np.stack([a[:, :, 0] * w, a[:, :, 1] * h], -1)  # y DOWN, as MediaPipe world is
        sl = slice(*window)
        out[v] = {
            "image": px[sl],
            "vis": np.maximum(a[:, :, 2][sl], 1e-3),
            "world": np.array([r["world"] for r in vd["data"]])[sl],
            "torso": float(np.median(np.linalg.norm(
                px[sl][:, SHOULDER].mean(1) - px[sl][:, HIP].mean(1), axis=1))),
        }
    return out, d


def fit_camera_error(points3d, view):
    """RMS reprojection error of `points3d` into `view`'s image, as % of torso.

    The camera is a scaled-orthographic one: a rotation shared across the take
    (the cameras are static), with per-frame scale and offset solved in closed
    form inside the residual. Per-frame scale is not a fudge — the subject walks
    toward the front camera, so its pixels-per-metre genuinely changes through
    the take, measured at 18% against the side view's 5.8%.
    """
    X = points3d[:, BODY]
    U = view["image"][:, BODY]
    vis = view["vis"][:, BODY]
    W = vis / vis.sum(1, keepdims=True)

    def residual(p):
        R = Rot.from_rotvec(p).as_matrix()
        P = np.einsum("ij,fnj->fni", R[:2], X)
        Pm = (W[:, :, None] * P).sum(1, keepdims=True)
        Um = (W[:, :, None] * U).sum(1, keepdims=True)
        Pc, Uc = P - Pm, U - Um
        s = ((W[:, :, None] * Pc * Uc).sum((1, 2))
             / np.maximum((W[:, :, None] * Pc * Pc).sum((1, 2)), 1e-12))
        return (((s[:, None, None] * Pc) - Uc) * vis[:, :, None]).ravel()

    best = np.inf
    for seed in range(MULTISTART):
        p0 = np.zeros(3) if seed == 0 else Rot.random(random_state=seed).as_rotvec()
        r = least_squares(residual, p0, method="lm", max_nfev=400)
        best = min(best, float(np.sqrt(np.mean(r.fun ** 2))))
    return best / view["torso"] * 100.0


def kabsch(P, Q, w):
    """Weighted similarity alignment of P onto Q. Returns (rotation, scale)."""
    w = w / w.sum()
    Pc = P - (w[:, None] * P).sum(0)
    Qc = Q - (w[:, None] * Q).sum(0)
    U, S, Vt = np.linalg.svd((Pc * w[:, None]).T @ Qc)
    D = np.diag([1.0, 1.0, np.sign(np.linalg.det(Vt.T @ U.T))])
    return Vt.T @ D @ U.T, float(S @ np.diag(D).T / (w[:, None] * Pc * Pc).sum())


def mean_rotation(Rs):
    """The rotation closest to a set of them — SVD projection of their mean."""
    U, _, Vt = np.linalg.svd(np.mean(Rs, 0))
    return U @ np.diag([1.0, 1.0, np.sign(np.linalg.det(U @ Vt))]) @ Vt


def hip_centred(view):
    return view["world"] - view["world"][:, HIP].mean(1, keepdims=True)


def align(src, dst, views):
    """Put `src`'s world landmarks into `dst`'s frame, with the take-wide rotation."""
    a, b = hip_centred(views[dst]), hip_centred(views[src])
    Rs, ss = [], []
    for f in range(len(b)):
        w = np.minimum(views[src]["vis"][f, BODY], views[dst]["vis"][f, BODY])
        R, s = kabsch(b[f, BODY], a[f, BODY], w)
        Rs.append(R)
        ss.append(s)
    return np.einsum("ij,fnj->fni", mean_rotation(np.array(Rs)), b * float(np.median(ss)))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--reference", required=True)
    ap.add_argument("--window", type=int, nargs=2, default=(154, 292),
                    help="steady-walk frame range, as found by gait_compare.py")
    args = ap.parse_args()

    views, meta = load(args.reference, args.window)
    names = list(views)
    print(f"frames {args.window[0]}-{args.window[1]}, {len(names)} views, "
          f"source {meta['source_sha256'][:12]}\n")

    print("SELF-CONSISTENCY — does a view's own 3D explain the image it came from?")
    print("  (this is the noise floor every number below is measured against)")
    for v in names:
        print(f"  {v:14s} {fit_camera_error(hip_centred(views[v]), views[v]):5.1f}% of torso")

    print("\nHELD-OUT REPROJECTION — build from two views, predict the third")
    print(f"  {'held out':14s} {'view A':>8s} {'view B':>8s} {'A+B fused':>11s}   verdict")
    wins = {"single": 0, "fused": 0, "tie": 0}
    for held in names:
        a, b = [v for v in names if v != held]
        A = hip_centred(views[a])
        B = align(b, a, views)
        wa, wb = views[a]["vis"][:, :, None], views[b]["vis"][:, :, None]
        fused = (wa * A + wb * B) / (wa + wb)
        ea, eb, ef = (fit_camera_error(A, views[held]), fit_camera_error(B, views[held]),
                      fit_camera_error(fused, views[held]))
        verdict = ("fused wins" if ef < min(ea, eb) - 0.05 else
                   "single wins" if min(ea, eb) < ef - 0.05 else "tie")
        wins["fused" if verdict.startswith("fused") else
             "single" if verdict.startswith("single") else "tie"] += 1
        print(f"  {held:14s} {ea:8.1f} {eb:8.1f} {ef:11.1f}   {verdict}"
              f"   (A={a}, B={b})")

    print(f"\n  fusion beat the best single view on {wins['fused']} of {len(names)} folds.")
    if wins["fused"] == 0:
        print("  Averaging two views lands BETWEEN them rather than beyond them, which is what\n"
              "  happens when the per-view errors are biased rather than independent. Combining\n"
              "  views needs a constraint the average does not have — see\n"
              "  docs/research/multiview-triangulation.md.")


if __name__ == "__main__":
    main()
