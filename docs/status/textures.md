# Textures & materials

**Session:** session_01BckkiWsd8dqsjacHidZYPe (to be re-woken)
**Branch:** `claude/textures-artist-research-bkz70u`
**State:** idle
**Updated:** 2026-08-21 by the architect — chartered, awaiting dispatch

## Now
**Chartered, then re-aimed the same day by the owner: skin base maps are IMPORTED, not
generated.** Read ADR 0014 *and its reversal at the end of that file* — the reversal is the
operative ruling. First job is **15.0, sourceability measured**, then 15.1 (one imported face on
the shipped body, for the owner's eye).

## Needs from the architect
Nothing. The reversal rules the memory constraint (unchanged: O(1) in crowd size), the source
question (import; never originate — the same rule the mesh side follows), and the sequencing.
If nothing suitable proves sourceable, that comes back here as a finding rather than becoming a
quiet fallback to generating.

## Last landed
`assets/standards/skin_texture.mgestd` — the texture authoring standard, written while the count
of textures was still zero. The UV audit that found the chart was unusable, which set off the
repack that has since made it green.
