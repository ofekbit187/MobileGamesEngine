#!/usr/bin/env python3
"""Track a multi-view reference video into per-view landmark trajectories.

Run WHERE THE VIDEO IS. The source footage is deliberately NOT committed —
it is third-party reference material of uncertain redistribution licence, and
the derived measurements are what this project actually needs. This script is
how `assets/mocap/*.json.gz` was produced, so the numbers are reproducible
without the repo ever carrying the film.

    pip install opencv-python-headless numpy mediapipe
    curl -sSLO https://storage.googleapis.com/mediapipe-models/pose_landmarker/\
pose_landmarker_full/float16/latest/pose_landmarker_full.task
    python3 tools/mocap/track_video.py <video.mp4> <out.json>

Panels are hard-coded for the walk reference (three synchronised views in one
frame). Edit PANELS for other footage; the output schema does not change.
"""
import cv2, numpy as np, mediapipe as mp, json, hashlib, sys
from mediapipe.tasks import python as mpp
from mediapipe.tasks.python import vision

PANELS = {'threequarter': (30, 25, 1045, 620),
          'front':        (1105, 25, 330, 620),
          'side':         (30, 675, 1405, 385)}
MODEL = "pose_landmarker_full.task"


def track(video_path, out_path, fps=60.0, n_frames=None):
    sha = hashlib.sha256(open(video_path, 'rb').read()).hexdigest()
    probe = cv2.VideoCapture(video_path)
    total = n_frames or int(probe.get(cv2.CAP_PROP_FRAME_COUNT))
    out = {"source_sha256": sha, "fps": fps, "frames": total,
           "panels": {k: list(v) for k, v in PANELS.items()},
           "landmark_model": "mediapipe pose_landmarker_full float16",
           "note": "world_landmarks are metric, hip-origin, per view. "
                   "image xy are panel-normalised.",
           "views": {}}
    opts = vision.PoseLandmarkerOptions(
        base_options=mpp.BaseOptions(model_asset_path=MODEL),
        running_mode=vision.RunningMode.VIDEO, num_poses=1)
    for name, (x, y, w, h) in PANELS.items():
        lmk = vision.PoseLandmarker.create_from_options(opts)
        cap = cv2.VideoCapture(video_path)
        rows, miss = [], 0
        for f in range(total):
            ok, img = cap.read()
            if not ok:
                break
            roi = img[y:y + h, x:x + w]
            r = lmk.detect_for_video(
                mp.Image(image_format=mp.ImageFormat.SRGB,
                         data=cv2.cvtColor(roi, cv2.COLOR_BGR2RGB)),
                int(f * 1000 / fps))
            if not r.pose_landmarks:
                miss += 1
                continue
            P, W = r.pose_landmarks[0], r.pose_world_landmarks[0]
            rows.append({"f": f,
                         "img": [[round(p.x, 5), round(p.y, 5), round(p.visibility, 3)] for p in P],
                         "world": [[round(p.x, 5), round(p.y, 5), round(p.z, 5)] for p in W]})
        out["views"][name] = {"tracked": len(rows), "missed": miss, "data": rows}
        print(f"{name:14s}: {len(rows)}/{total} tracked, {miss} missed")
    json.dump(out, open(out_path, "w"))
    return out


if __name__ == "__main__":
    track(sys.argv[1], sys.argv[2])
