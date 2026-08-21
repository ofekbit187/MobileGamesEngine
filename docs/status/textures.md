# Textures & materials

**Session:** session_01BckkiWsd8dqsjacHidZYPe (to be re-woken)
**Branch:** `claude/textures-artist-research-bkz70u`
**State:** idle
**Updated:** 2026-08-21 by the architect — chartered, awaiting dispatch

## Now
**Chartered by owner dictation (ADR 0014): skin is generated, not painted.** First job is task
**15.1 — one generated face on the shipped body, rendered, for the owner's eye**, before any
system is built.

## Needs from the architect
Nothing. ADR 0014 rules the memory constraint (O(1) in crowd size), the charter question
(a generator is engineering; painting by eye is not), and the sequencing.

## Last landed
`assets/standards/skin_texture.mgestd` — the texture authoring standard, written while the count
of textures was still zero. The UV audit that found the chart was unusable, which set off the
repack that has since made it green.
