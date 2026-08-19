# ADR 0005 — Family tree format (.mgetree)

Status: accepted (Phase 9, Dictation 5)

## Context

People are dictated to be generated as **family trees, not individuals**
(PEOPLE.md §2): every person's name and looks have provenance. Trees must be
compact (P1/P2 — a settlement's population is data, bodies derive on
demand), deterministic (saves store deltas against generated truth, P7), and
robust on disk like every other engine format.

## Decision

```
TreeHeader  { magic "MGET", version, recordSize, count, seed,
              cultureId[16], checksum (FNV-1a 64 over records) }
Records     [count × PersonRecord, raw]

PersonRecord {
  personId            stable id; doubles as the save persistentId (8.9)
  sex, generation, alive, marriedIn
  firstName           index into the culture pack's per-sex name pool
  familyName          CURRENT family name index (marriage ruling applied)
  birthFamilyName     birth name, preserved
  mother/father/spouse  record indices (kNoPerson = none)
  genome              2 haplotypes × 8 traits (dna.h) — the DNA mechanism
  occupationTag, residenceCell, scheduleId   (9.7 stubs, deepen later)
  voiceDesc[96]       the recording brief (PEOPLE.md §6)
}
```

- **Naming rules are the owner's rulings, not data:** first names unique
  within the family (per-sex pools, refuse when exhausted — no duplicates);
  children always take the father's family name; a woman takes her husband's
  family name upon marriage, birth name kept. Culture packs supply POOLS
  only (shipped: `anglo`, `hebrew` — P11).
- **Determinism:** generation is a pure function of (params, culture); the
  header records the seed. The same tree file — or the same seed — always
  yields the same people, so runtime state (deaths, marriages, effects)
  persists as deltas via the save system's character records, never by
  rewriting the tree.
- **Record-only ancestors** (ruling 5): older generations ship dead
  (`alive = 0`) — history that explains names and looks at the cost of rows.
- **Integrity:** `recordSize` gates layout compatibility; the FNV-1a
  checksum rejects torn/corrupted files before anything is read into play.
- Records are written raw (trivially-copyable struct). Both shipped ABIs
  (x86-64 host, arm64 device) agree on the layout; if a future platform
  disagrees, `recordSize` refuses the file rather than misreading it.

## Consequences

- A tree's cold cost is `count × sizeof(PersonRecord)` (~200 B/person) —
  a 500-person town is ~100 KB of identity, zero bodies.
- Bodies, wearables, and voices instantiate from the record on demand
  (`variantOf(genome, sex)`), so heredity costs nothing to store.
- v2 candidates: variable-length string tables for game-authored names,
  per-person authored voice briefs, runtime tree deltas (births/marriages
  during play) — deferred until dictated.
