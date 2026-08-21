# ADR 0017 — Items declare how they are used; and who acts when an area has no session

**Status:** accepted (architect ruling) · **Date:** 2026-08-21
**Depends on:** ADR 0008 (P12 amendment), `docs/CHARACTERS.md` §6.2
**Raised by:** the animation session (14.5) and the wearables session (`WearableKind`)

## Ruling 1 — `ItemUse` gains the archetype, and the shared header goes in `framework/`

The engine now animates nine use archetypes parameterized by grip, reach and weight, and
**nothing can reach those numbers**: `ItemUse` carries `range`, `power` and a free-form `animKey`.
P12's promise — *model it, declare `swing`, give it a reach and a weight, ship it* — stops one
step short, because declaring it is exactly what an item cannot do. It also blocks hanging the
damage moment on `strike` (14.3) and keeps the six-item catalogue (14.6) living in a demo instead
of in shipped item data.

**Approved as proposed:** four fields on `ItemUse`, defaulted so every existing item is unaffected.

```
UseArchetype archetype = UseArchetype::Swing;
ItemGrip     grip      = ItemGrip::OneHanded;
float        reach     = 1.0f;   // metres, tip to grip — arc radius and lean
float        weight    = 1.4f;   // kg — wind-up/strike/recovery timing
```

**On the include direction, the session offered (a) or (b) and said it had no stake. It is not a
taste call — (a) is impossible.** Measured: `framework/` includes nothing from `character/`, and
`character/humanoid.h` includes `framework/`. The dependency runs **character → framework, one
way**. Making `framework/items.h` include `character/use_archetypes.h` would close that into a
cycle at the directory level.

**Ruled: (b), and specifically the two enums move to `engine/include/mge/framework/use_archetype.h`,
owned by gameplay mechanics.** `character/use_archetypes.h` includes it. This matches the existing
dependency direction *and* the concept: the archetype vocabulary is design language, defined in
`CHARACTERS.md` §6.2 as a gameplay property of an item; the animation area **implements** it. The
`UseMotion` block and `UsePlayer` stay in `character/` where they belong.

**One correction in the session's favour: there is no save-schema bump.** The request assumed one.
`ItemUse` is not serialized — the registry is built at startup and inventories reference items by
`AssetId`. Grepped to confirm. The change is cheaper than proposed.

`animKey` keeps working and narrows to the bespoke-clip escape hatch it was always meant to be.

## Ruling 2 — `WearableKind` stays, with a named trigger

**Ratified exactly as the session proposed, including its reason for not acting.** It costs one
function and names the first six catalogue rows; a *new* garment needs no enumerator, which is
what 13.12 actually promised. Removing it would touch four other areas' files for tidiness alone,
and **"the P12 claim does not require it" is the right test** — it refused a seam change it could
have justified aesthetically. That instinct is worth more than the cleanup.

**Revisit trigger, so this is a decision and not a drift:** when a *game* needs to replace the
built-in six itself. At that point the enum stops being a naming convenience and becomes a limit.

**One thing to protect:** `WearableKind` lives in `character/humanoid.h`, the same header whose box
rig is deprecated. Task 16.3 deletes `buildHumanoidVisual` — **it must not take `WearableKind`
with it.**

## Ruling 3 — an area with no session is not a deadlock

Both requests exposed the same structural gap. `framework/items.h` belongs to **gameplay
mechanics**, which has no live session; the textures standard belonged to an area with no live
session two rulings ago. Under a strict reading of `AGENTS.md` §3, work in an unowned area cannot
proceed at all — which turns an ownership rule meant to prevent collisions into a deadlock.

**Ruled: when an area has no live session, the architect assigns the change to the session that
raised it, recorded in the ADR that rules on it.** Three conditions, all of which held here:

1. **The change is fully specified by the ruling** — not "go design something in someone else's
   area".
2. **It is recorded, never silent.** A future owner of that area reads why their file changed.
3. **It does not extend.** The grant covers the named change and nothing adjacent.

Ownership exists to stop two correct implementations from being incompatible. With no second
implementer, that risk is zero, and the cost of waiting is real. **So 14.5 is assigned to the
animation session**, which designed it and holds the context.

## Consequences

- 14.5 lands, which unblocks 14.3's damage-on-`strike` and moves 14.6's catalogue into item data.
- 14.4's interruption closes via `UsePlayer::interrupt()`.
- The engine still cannot *show* any of it until the joints pass `B-31` (ADR 0016) and held items
  render at all (14.7) — both queued, neither this ruling's business.
- `AGENTS.md` §3 gains the unowned-area rule so the next session does not have to ask.
