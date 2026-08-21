# The session ledger — how sessions and the architect actually talk

**Sessions do not message each other. They commit.** This directory is the coordination
medium for the whole project, and it exists because messaging failed three times in two days:

- a session sat **blocked for 19 hours** holding a message to the architect that the channel
  silently dropped;
- an earlier session was lost the same way and had to be replaced;
- two dispatches into an idle session vanished without an error.

`ListAgents` and `SendMessage` cannot see sibling sessions in this setup at all. Git can. So
git is the channel.

## The rule

**One file per session: `docs/status/<area>.md`.** You own yours completely and touch nobody
else's — which is what makes this conflict-free by construction, unlike a shared board.

Write to it whenever your state changes, and **always before you stop**:

- what you finished, with the commit
- what you are doing now
- **what you need from the architect** — a ruling, a seam change, a budget, a decision
- what is blocking you, if anything

Then **push your branch and stop.** Do not try to notify anyone. The architect fetches every
branch on a timer, reads these files, and picks the work up. A need written here is delivered;
a need spoken into the messaging channel is not.

## Why this is better than a message, even when messaging works

- **It survives.** A session can die, be replaced, or run out of context, and its state is
  still in the repo. The one that replaces it reads the file and knows where it stood.
- **It is reconstructible.** The whole project's state can be rebuilt from a fresh clone,
  with no session metadata, no dashboard, and nothing in anyone's memory.
- **It is reviewable.** A status change shows up in a diff next to the code that caused it.
- **It has no delivery step to fail.**

## Status vocabulary

Use exactly these in the `State:` line so the architect can scan them:

| State | Meaning |
|---|---|
| `working` | actively building; nothing needed |
| `ready` | pushed and waiting for the architect to review and merge |
| `blocked` | cannot proceed without a ruling — say precisely what, in `Needs:` |
| `idle` | finished the chartered work; no next job assigned |

`blocked` and `ready` are the two the architect acts on, so be honest about them. A session
marked `working` that is actually stuck is invisible, and invisible is how we lost 19 hours.

## Seam requests

A seam request goes in your status file under `Needs:`, in the `AGENTS.md` §5 format
(SEAM / NEED / BREAKS / PROPOSAL). It does not go in a message, and it does not get
implemented by you while you wait.
