# Wearables & equipment

**Session:** session_01TV2bC4KKZ42EJLh6UdtAun
**Branch:** `claude/wearables-system-research-1h16tq`
**State:** ready
**Updated:** 2026-08-21 — 13.12 done and pushed; one seam request below

## Now

**13.12 is complete.** A wearable is now a `.mgewear` file next to its mesh, in the
`key value` / `#` comment convention `assets/standards/skin_texture.mgestd` established.
`WearableCatalogue` reads the asset directory at load; the three places that defined what a
garment *could be* — `garmentAsset`, `garmentCoverage`, and the bake tool's hardcoded array —
are lookups now instead of `switch` statements and arrays.

**Why I took 13.12 before 13.11**, since you left the choice to me: 13.11 authors four
garments, and against an enum-based catalogue each one would have needed `.cpp` edits that
13.12 would then have to undo — and 13.11's "an artist self-serves" claim would have been
false while the artist still needed an engineer to add an enumerator. It also stands alone
from the pipeline session's 13.8/13.9, which 13.11 does not. So the ordering is: mechanism
first, then content authored through it.

Next, unless you redirect: **8.21** (virtual-model wearables — a placeholder garment from
proportions + description, P5) while 13.8/13.9 are still in flight, then **13.11** when the
hem-loop table and authoring reference land, then **13.5** equip-time chaining.

## Needs from the architect

```
SEAM:     Fitting & masking — `WearableKind` in engine/include/mge/character/humanoid.h
NEED:     A ruling on whether `WearableKind` should eventually be retired in favour of the
          catalogue index/id, and if so, when.
BREAKS:   Removing it touches files this session does not own — app/src/main/cpp/device_game.cpp
          (Platform, 15 uses), tools/body_preview and tests/test_body_mesh.cpp (character asset
          pipeline), tools/humanoid_demo (29 uses), tools/template_game. That is why I did not
          do it: the P12 claim does not require it, so removing it would have been a seam change
          taken for tidiness.
PROPOSAL: Leave it. It now costs one function (`wearableKindId`) and names the first six
          catalogue rows; a NEW garment needs no enumerator, which is what 13.12 asked for.
          Revisit only if a game needs to swap the built-in six themselves, at which point the
          enum stops being a naming convenience and starts being a limit. No action needed now —
          this is registered so the decision is yours rather than mine by default.
```

Nothing is blocking me.

## Last landed

**13.12** (`this push`) — garments as data. The proof is
`a_garment_added_as_pure_data_behaves_like_a_compiled_one`: it writes a surcoat `.mgewear`
into a scratch directory at runtime — no enumerator, no `switch` arm, no rebuild — and
requires it to mask, resolve its mesh and find its baked binding exactly like the shipped
six. A malformed file is refused by name, line and reason, and does not take the rest of the
catalogue down with it. 190 → 199 tests, all green; `steady-state heap allocations: 0`.

**Two things worth carrying forward:**

- **The mask gate caught me.** My first `tunic.mgewear` said `covers torso arms`, copying the
  old *generated* tunic's sleeves. The shipped tunic masks torso only, so the arms were being
  cut away with nothing covering them — the gate refused it with 244 mm of exposed rim before
  it could reach anyone. The gate written in 13.10 paid for itself inside one task, on its
  author. The corrected file says why in a comment.
- **The garment bake is byte-deterministic.** Re-baking all six bindings against body v5
  (`1067c74324b6e091`) reproduced the committed `.mgefit` files exactly — `git status` clean.
  That is B-14's property holding one level below the body, where nothing had asserted it.

**13.10** (`28f0322`) — the seven §9 acceptance gates, six passing and one honestly BLOCKED on
task 13.8. Ratified by ADR 0013 along with both contract corrections.

**Still watching:** the trouser hem at the ankle with no boots leaves 42 mm of rim against a
45 mm allowance — 3 mm of headroom. The first authored trousers that sit higher will fail that
gate, and per your note that is better known now than discovered as a failure.
