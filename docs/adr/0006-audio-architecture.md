# ADR 0006 — Audio architecture

Status: accepted (Phase 10)

## Context

The engine needs sound: voice lines are already an engine concept (PEOPLE.md
§6 — takes are delivered, validated, and selected, but nothing plays them),
and games need ambience, effects, and music. The pillar must obey the same
laws as everything else: P1 (fixed budgets, allocation-free steady state),
P2 (content streams; the mixer never blocks on I/O), P3 (portable core,
platform sink behind a boundary), and it must be verifiable headlessly —
this environment has no sound device, so correctness is proven by mixing to
buffers/files and asserting on the samples.

## Decision

```
AudioClip      loaded PCM16 (wav), charged to the "audio" budget; refuse on cap
AudioMixer     portable core: fixed voice slots (cap refuses), buses
               (music/sfx/voice under master), per-voice gain/loop,
               linear-resampling to the output rate, positional voices
               (distance attenuation + constant-power stereo pan from the
               listener), music ducking while the voice bus speaks
mix(out, n)    THE interface: fills n interleaved stereo int16 frames,
               allocation-free, from whatever thread the device drives
Device sinks   Android: AAudio callback pulls mix() (app module, P3
               boundary); host: tests/demos pull mix() directly and write
               wavs — real engine output, listenable evidence
```

- **Pull model.** The device callback pulls the mixer; the game thread
  starts/stops voices and moves the listener. A mutex guards the brief slot
  updates (v1); a lock-free command ring replaces it if profiling ever shows
  the audio thread blocked.
- **Fixed everything (P1).** Voice slots, bus count, and the mix scratch
  buffer are fixed at init; `play()` on a full mixer refuses (returns
  invalid), never grows. Mixing performs zero allocations — enforced by the
  host runner's steady-state allocation counter, which now pumps the mixer
  every frame.
- **Clips are owned by the caller** and must outlive their voices; clip
  bytes are charged to the "audio" budget and released on unload. Long
  music streaming from disk (job-lane fed ring) is the planned v2; voice
  lines and effects are short and load whole.
- **Positional v1:** linear distance attenuation between reference and max
  distance; azimuth from the listener's forward → constant-power pan.
  HRTF/occlusion/reverb are content-level upgrades behind the same voice
  params.
- **Ducking:** while any voice-bus slot plays, the music bus gain glides
  toward a configurable ducked level and recovers after — speech is always
  intelligible over music (smoothing avoids clicks).
- **Voice-line playback** composes existing pieces: `pickTake` (random among
  delivered takes) → `loadAudioClip` → `play` on the Voice bus at the
  speaker's position; no take → the line's text is the subtitle. One
  fulfillment pipeline, now audible.

## Consequences

- Everything above the sink is testable in CI: mixed output is asserted
  sample-by-sample (energy, pan asymmetry, ducking depth, clamp safety) and
  rendered to wav files as review-board evidence.
- The AAudio sink is deliberately thin (open stream, pull mix, restart on
  error) and lives in the app module — the engine core never includes
  Android headers, same as rendering.
- On-device listening remains unverified until a physical device session;
  the sink compiles into the APK and the mixer beneath it is fully tested.
