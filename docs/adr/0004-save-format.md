# ADR 0004 — Save format v1 (.mgesave)

**Status:** accepted (task 6.1; realizes P7)

## Decision

Saves record **deltas against shipped content**, never world state wholesale:
save size scales with player impact, not world size (the persistence mirror
of P2). Writes are atomic; loads are checksum-gated and version-gated.

```
FileHeader  { magic "MGES", version, schemaVersion,
              payloadSize, payloadChecksum (FNV-1a 64),
              timestampUnix, playtimeSeconds, name[32] }
Payload:
  PlayerRecord   { position, yaw, health }
  DeltaSection   [chunk deltas, only chunks the player changed]
     per chunk:  { chunkIndex,
                   removed shipped placements   (indices),
                   moved shipped placements     (index + new transform),
                   spawned dynamic entities     (asset id + transform + color) }
  Collections    [registered item collections: id + items(assetId,count,color)]
  Characters     [schema v3, task 8.9: per persistent character —
                  persistentId, health/maxHealth, faction, alive, controller,
                  sightRange, inventory items, equipment slots(+layer/sheathed)]
```

- **Atomicity (P3/P7):** a save writes to `<slot>.mgesave.tmp`, fsyncs, then
  renames over `<slot>.mgesave`. Process death at ANY point leaves the
  previous save untouched — the tmp file is garbage-collected on the next
  save. The kill-test suite (task 6.6) injects death at every stage and
  asserts the previous save survives.
- **Checksum:** the loader validates payload checksum before touching any
  state; a torn or bit-rotted file is rejected cleanly, never half-applied.
- **Versioning:** `schemaVersion` gates a migration chain — each migration
  upgrades a deserialized snapshot one version; loading an old save runs the
  chain forward. Unknown (future) versions are rejected.
- **Load = stream + apply (task 6.4):** the delta log is handed to the
  streaming manager; whenever a chunk instantiates, removed placements are
  skipped, moved ones take their saved transforms, and dynamic spawns are
  added. Deltas thus apply lazily with residency — loading a save never
  touches non-resident chunks.
- **Item identity:** saved items carry (assetId, count, color); display
  metadata (localization key) is re-resolved by the game on load, keeping
  strings out of saves.
- **Character identity (v3, task 8.9):** characters persist under a
  game-assigned `persistentId` (0 = transient, never saved). The same records
  serve save files and streaming: when a chunk eviction despawns an NPC's
  entity, its state parks in the same `SavedCharacter` form and is re-applied
  when the game respawns it — nothing a player did to an NPC is lost while
  its chunk is cold, and cold NPCs cost no live character slots (P1/P2).

## Deferred to v2

- Async write on the I/O lane (v1 writes synchronously at a simulation
  boundary; saves are small by design so the stall is bounded)
- Thumbnail capture into slot metadata
- Entity gameplay-state records (health/AI state of NPCs) — with Phase 8
- Cloud/backup slots; save-file compression
