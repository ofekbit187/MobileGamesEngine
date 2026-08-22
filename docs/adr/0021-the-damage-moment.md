# ADR 0021 — The damage moment lands on `strike`

**Status:** accepted · **Date:** 2026-08-22 · **Ruled by:** the architect, on the owner's
instruction ("fix the damage timing to land on strike")

## The problem

`action/use_held` resolves a Strike **on the button press**. `CharacterSystem::perform` picks a
victim and applies damage in the same call that starts the action, and returns who was hit.

That was correct when nothing moved: the action model (Phase 12) predated the archetype library
(14.2), so there was no motion to be late or early relative to. Since 19.3 the character actually
swings, and the mismatch is now visible: the blow lands at the instant of the press, while the arm
is still winding up. A heavy weapon is worse than a light one, because a maul's wind-up is longer
— the damage arrives further from the moment it looks like it should.

Task 14.3 always said the damage moment should hang on `strike`. This is that.

## The obstacle, which is a real one

The strike moment is a property of the **motion timeline**: `usePhases(motion)` gives wind-up /
strike / recovery as fractions plus a duration in seconds, and the blow lands at the END of the
strike phase. That function lives in `character/use_archetypes.h`.

`CharacterSystem` lives in `framework/`. **`character/` includes `framework/`, never the
reverse** — ADR 0017 measured that and put the archetype *vocabulary* in
`framework/use_archetype.h` precisely to keep it acyclic. So `CharacterSystem` cannot call
`usePhases`, and the fix cannot be "include the header".

Two ways out were considered and rejected:

- **Recompute the timing in `framework/`.** It would work today and drift tomorrow: the phase
  formula and the per-archetype timing table would exist in two places, and the first change to
  either would silently desynchronise the blow from the animation — reintroducing exactly the
  defect this ADR removes, in a harder-to-see form.
- **Move `usePhases` into `framework/`.** It drags the per-archetype timing table with it. That
  table is *how a swing feels*, which is the animation area's judgment, not gameplay's. ADR 0017
  drew this line deliberately: gameplay owns the vocabulary, animation owns what it looks like
  and how long it takes.

## Ruling

**The timing is injected, in the pattern this system already uses for everything else it
needs from outside itself** (`setItemUses`, `setCollision`, `setInteractions`).

`framework/character.h` declares the seam:

```cpp
// Seconds from the start of a use to the instant its blow lands.
using StrikeDelayFn = float (*)(const ItemUse&);
void setStrikeTiming(StrikeDelayFn fn);
```

`character/use_archetypes.h` supplies the implementation — `strikeDelaySeconds(const ItemUse&)`,
one line over `usePhases(motionFromItemUse(use))` — and the composition root wires the two
together. The dependency direction is unchanged: `framework/` declares, `character/` provides,
the game connects.

**When no timing is installed, damage resolves immediately, exactly as before.** That is not a
grace period for laziness; it is what keeps `framework/` honest. A game that never brings the
character pillar still gets a working action model, and every existing tool and test keeps its
behaviour without being touched.

### What changes for callers

`perform(use_held)` on a Strike with timing installed returns `performed = true` and
`useKind = Strike`, but **no target and no damage** — the blow has not happened yet. A new
`ActionResult::pending` says so, so a caller cannot mistake "not yet" for "missed".

The outcome arrives later, and is polled rather than pushed:

```cpp
struct StrikeOutcome { EntityId actor, target; float amount; };
bool consumeStrike(StrikeOutcome& out);   // true once per resolved blow
```

Polling, not a callback, for two reasons: the frame loop already polls everything else, and a
callback into game code from inside a system tick is the kind of re-entrancy that turns a
Phase 12 action model into a Phase 30 debugging session.

### The target is chosen at the strike, not at the press

This follows from the ruling rather than being a separate decision, and it is the more correct
semantic anyway: hit detection happens when the blade is there. A victim who steps out of reach
during the wind-up is missed; one who steps *into* it is hit. Under the old model, both outcomes
were decided before the arm moved.

### Interruption

`cancelStrike(EntityId)` drops a pending blow. A swing that is interrupted must not land, and
`UsePlayer::interrupt` already blends the motion out — but `CharacterSystem` does not know about
`UsePlayer`, so the presentation side that interrupts the motion is what cancels the blow.
**This is a seam with a loose end**: nothing today calls it, because nothing today interrupts a
player's swing. Left deliberately, named here, rather than pretending it is closed.

## Consequences

- A pending strike is per-character state and is saved by nobody — a blow in flight across a save
  is dropped. Correct: reloading mid-swing and taking damage from a swing you no longer remember
  would be worse.
- The AI still calls `damage()` directly (`AiSystem::stepAgent`) and is untouched by this. It
  never drew, never used the item it held, and never played an archetype — a separate gap, raised
  and not fixed here, and gameplay's to close.
- `PracticeYard` reports `connected` on the frame the blow **lands** rather than the frame it was
  requested, which is what the scene should have meant all along.
