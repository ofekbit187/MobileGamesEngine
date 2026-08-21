# Character asset pipeline

**Session:** session_01PeC37FrSvon7V7BMAViaJS
**Branch:** `claude/character-asset-pipeline-v3`
**State:** working
**Updated:** 2026-08-21 by the architect (seeding the ledger; this session updates it from now on)

## Now
Task **13.7a** — `B-9`'s authored hairline loop, funded by ADR 0012's LOD0 cap raise to 2 400, carrying the `body_mesh_has_human_proportions` fix. Then **13.7b**, the stretch waiver mechanism (recorded seam exception). Then 13.8, then 13.9.

## Needs from the architect
Nothing. ADR 0012 answered all three open questions.

## Last landed
**13.7** (`8dd2403`) — real `Face` shell on all three LODs, twelve of twelve regions owning geometry, body hash `c4f91ac3fa2fcff5`. Closed the B-8 debt. Found and fixed a morph-target duplication bug that would have moved half a jaw, invisibly, on any multi-material body.
