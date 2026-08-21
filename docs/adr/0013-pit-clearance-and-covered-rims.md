# ADR 0013 — Two contract clauses that were unsatisfiable, corrected against measurement

**Status:** accepted (architect ruling) · **Date:** 2026-08-21
**Depends on:** ADR 0007, ADR 0008, ADR 0012, `docs/BODY_CONTRACT.md`
**Evidence:** `docs/research/pit-measurement.md` · **Raised by:** the wearables session, at task 13.10

## Context

Task 13.10 built the `BODY_CONTRACT.md` §9 acceptance gates. Six of seven pass on the shipped
body; the seventh (hem loops, `B-11`) reports **BLOCKED** rather than passing or failing, naming
task 13.8 as what unblocks it — which is the right third answer for "nothing to check yet".

Building them surfaced that **two clauses could not be satisfied by any correct body.** The
session measured that rather than asserting it, corrected the clauses to state the property its
own examples had always been reaching for, and marked both in the contract with the original
text preserved. **Both corrections are ratified here.**

## Ruling 1 — `B-24`: pit clearance, not a global normal offset

The clause required the body to "tolerate a +35 mm normal offset without self-intersection".

**That fails on every anatomically correct body, and the failure is geometric, not incidental.**
A normal offset folds wherever the concave curvature radius is smaller than the offset, and a
nose, a pair of lips and the web between thumb and palm all are — 82 folded triangles on the face
alone. Satisfying the clause as written would have required a body with no facial features.

The clause's own examples — armpit, crotch, neck/chin, elbow and knee creases — were never about
surface curvature. They are all **pits**: two surfaces far apart *along* the body but near in
space, which is the thing a garment can bridge and must not be trapped inside. **Ratified as
corrected.** Garments bridge concave features rather than conforming to them, so the offset test
was measuring a property the catalogue does not need.

**The measurement method is ratified with it, because it is the part that is easy to get wrong.**
Three approaches were tried and discarded, each for a specific reason worth keeping:

- **Triangle centroids invent gaps that are not there** — they reported an 11.7 mm armpit that is
  really 38.8 mm, because a centroid sits up to half a triangle away from the surface.
- **Measuring between regions returns ~0 mm everywhere**, because neighbouring regions share their
  boundary edges, so the answer is an artifact of the partition rather than of the body.
- **Dijkstra over hop count measures a different thing in every region** — a hop crosses ~5 mm on
  the dense face and ~25 mm on a thigh, and the answer swung between 16 mm and 40 mm on threshold
  choice alone.

What works: weld the mesh, run Dijkstra over **edge length**, exclude a 120 mm geodesic radius,
take the closest remaining triangle.

## Ruling 2 — §9.1: masking leaves a **covered** rim, not "no hole"

The clause asked that masking any combination of regions leave "no hole".

**Unachievable on this body, and the reason is a fact about v3 that the contract predates.** The
imported body is **one shell** whose regions partition its triangles, so removing a region
necessarily opens a boundary. The v2 *generated* body had per-region closed shells and could
satisfy "no hole" by construction; §9.1 was written against that body and silently stopped
applying the moment ADR 0007 v3 replaced it with an artist mesh.

This is the same class of debt ADR 0008 already logged against v3 under "the gap between a body
built to render and a body built to be dressed" — one more item in it, found by building the gate
instead of reading the clause.

**Ratified as corrected: the rim must be _covered by the outfit that opened it_, judged per
outfit.** Per outfit is the load-bearing part. Armour leaves 67 mm of hip rim bare and that is
correct, because armour is an outer layer worn over a tunic; judging it in isolation would fail a
combination the game will never draw.

## Two thin margins, recorded so they are not discovered later

Neither is a defect today. Both are three-to-four millimetres from being one.

- **The trouser hem at the ankle, with no boots, leaves 42 mm of rim against a 45 mm allowance.**
  Three millimetres of headroom. The first authored trousers that sit a little higher will fail
  this gate, and that is the gate working — but whoever authors them should know before, not after.
- **`hips > waist` holds by 4 mm** on this male body. With the sampling defect ADR 0012 Ruling 3
  removed, that is real anatomy rather than an artifact, so the assertion stands untouched. It is
  thin enough that a future shape parameter could cross it legitimately.

## On process — ratified, and the deviation is not a precedent

`docs/BODY_CONTRACT.md` is a **seam** (`AGENTS.md` §4): it changes by architect ruling, recorded
before either side implements. This session implemented first and flagged after, which is the
wrong order.

**I am ratifying rather than reversing, and saying plainly why**, so the reasoning is available
and the exception is bounded:

1. The substance is right, and demonstrated by measurement rather than argued.
2. The alternative was to implement a gate the session had already proven impossible to pass —
   shipping a permanently-red gate, which ADR 0012 Ruling 1 identifies as the silent-clamp
   pathology in another costume.
3. **It had no working channel to ask through.** The session had been blocked for nineteen hours
   on an undeliverable message when this work started, and `docs/status/` did not exist yet. The
   process it violated was, at that moment, not actually available to it.
4. It changed nothing quietly: both clauses carry their original text, and the evidence doc is
   written as the reply to the task.

Point 3 is now fixed — the ledger exists (`AGENTS.md` §4.5), a seam request has a delivery path
that has never lost a message, and **the next such correction goes in `Needs:` and waits.**

## Consequences

- Six of seven §9 gates pass on the shipped body; the seventh is `BLOCKED` on task 13.8, honestly.
- The gates run from one place and are read from two — `tools/wearable_gates` for whoever changed
  the body, `tests/test_wearable_gates.cpp` in CI. P12 is satisfied: an artist gets pass/fail with
  a reason and no engineer in the loop.
- `B-24` and §9.1 now state properties a correct body can actually satisfy, which is the only kind
  of clause worth having in a contract.
