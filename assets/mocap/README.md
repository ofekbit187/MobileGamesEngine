# Tracked motion reference

**What is here:** per-view landmark trajectories measured from reference video.
**What is deliberately NOT here: the video.**

The source footage is third-party animation reference of uncertain
redistribution licence. Using it as reference is ordinary practice; committing
it to a repository and shipping data derived from it are separate questions the
owner has been asked to check. So the film stays out, the measurements come in,
and `tools/mocap/track_video.py` — plus the recorded `source_sha256` — is how
anyone reproduces them from their own copy.

## `walk_reference_tracking.json.gz`

A casual male walk, three synchronised views in one frame (three-quarter,
front, side), white cyclorama, static cameras, 60 fps, 436 frames / 7.27 s.

**436 of 436 frames tracked on every view, zero misses**; mean landmark
visibility 0.97 front, 0.90 three-quarter, 0.82 side.

Per frame per view: 33 image-space landmarks (panel-normalised x, y, plus
visibility) and 33 metric hip-origin world landmarks. Schema is in the file's
own `note` field.

**The three views are simultaneous** — one take, one frame — so temporal
synchronisation across views is free and needs no clapper or alignment pass.
That is the property that makes triangulation tractable here and is usually the
hardest part of markerless capture.
