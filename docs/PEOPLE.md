# People — persons, family trees, heredity, status effects, and voices

Dictated design (Dictation 5). A **person** is a kind of NPC with a real
identity: a name that comes from somewhere, relatives, heritable looks,
abstract status effects (skills and education among them), an occupation, a
place to live, a schedule, and a voice with actual lines. This document
records the dictation faithfully and marks engineering proposals awaiting
the owner's verdict as *[proposal]*.

Related: [`CHARACTERS.md`](CHARACTERS.md) (a person IS a humanoid character —
everything there applies), [`PRINCIPLES.md`](PRINCIPLES.md) (P1 memory, P2
streaming, P5 placeholder-then-fulfill, P9 player parity).

## 1. The Person kind

- A person is a humanoid character with an identity layer on top. All
  universal mechanisms (mortality, factions, inventory, equipment, AI) and
  all humanoid machinery (variants, wearables, animations) apply unchanged.
- Not every NPC is a person: animals, monsters, and nameless creatures stay
  plain characters. Persons are the ones with names, families, and voices.
- *[proposal]* The player can be a person too (P9): name, family, skills —
  the same identity layer, steered by the player controller.

## 2. Family trees are the generation unit

**You don't generate people — you generate family trees.** A person never
exists alone; they are a node in a tree that explains their name and looks.

- Every person has a unique first name and a last name **derived from their
  family tree**.
- Relations per person: **mother, father**, and optionally **siblings,
  children, spouse**.
- *[proposal]* Trees are compact data assets (a `.mgetree` file baked like
  everything else): person records = name refs + genome + relation indices +
  identity fields — bytes, not bodies. Bodies, wearables, and voices
  instantiate on demand when a person's chunk is hot; a whole town's
  population costs almost nothing while cold (P1/P2).
- *[proposal]* Person identity rides the existing `persistentId` mechanism
  from task 8.9 — a `PersonId` is stable across saves, streaming, and
  sessions, and the save system already persists everything a character
  carries.
- *[proposal]* Generation is deterministic: the same tree seed always
  produces the same people, names, and genomes. Saves store only deltas
  against the generated truth, exactly like world deltas (P7).

## 3. DNA and heredity

There is a **DNA mechanism** so visual features are hereditary.

- *[proposal]* The genome is two haplotypes over the trait set that already
  drives the body: the `HumanoidVariant` fields (height, build, widths,
  head shape, skin tone) plus, when the artist face lands, face-feature
  genes. Phenotype resolution (dominance/blending per trait + bounded
  mutation) produces the person's `HumanoidVariant` — children resemble
  parents *by construction* because their variant is literally derived from
  the parents' genomes; siblings differ by recombination seed.
- *[proposal]* DNA also seeds non-visual defaults where the owner wants it
  later (voice timbre description, aptitudes) — the mechanism is general.

## 4. Status effects

Every character carries a **list of assigned status effects** — an abstract
mechanism that can "theoretically do anything," the implementation hook for
a lot of future systems.

- **Skills / education are a special kind of status effect** — every person
  has a list of them.
- *[proposal]* Data model: `{ effectId, tags, magnitude, duration,
  sourceRef, payload }`. Skills/education are permanent effects whose
  magnitude is the rank. The engine owns storage, queries ("does this person
  have an effect tagged X?", "sum of modifiers to Y"), durations, and stat
  modifier hooks (speed, health, sight...); games define effect semantics in
  data. Blessings, diseases, drunkenness, literacy, "wanted by the guards,"
  a guild apprenticeship — all the same record.

## 5. Occupation, residence, schedule

Every person has an **occupation**, a **place of living**, and a
**schedule**. Dictated as mechanisms to be developed further later; v1 holds
their data shape.

- *[proposal]* v1 stubs: occupation = tagged reference; residence =
  interior-cell reference (the P10 interiors people actually live in);
  schedule = time-of-day → place/activity table. Cold people don't tick —
  when their chunk loads, the schedule says where they are *now*, so towns
  look alive without off-stage simulation cost (P1/P2). Deeper simulation
  is a later dictation.

## 6. Voices — text lines

Every person has a **folder of text lines**, with **at least one line**.

- **A text line is a folder.** Inside it, a text file states the text and
  the way it should be said — tone directions plus non-verbal sounds:
  sighs, laughs, and so on.
- **An external agent generates wav file(s) into that folder.** If multiple
  wavs exist for the same line, the engine picks one at random (takes =
  natural variety).
- Every person also has a **description of how their voice sounds** — the
  brief the external agent records to.
- *[proposal]* This is the virtual-models pattern (P5) applied to audio —
  the third fulfillment pipeline (models, then wearables, now voices):

  ```
  people/<person-id>/voice.txt              # voice description (the brief)
  people/<person-id>/lines/<line-id>/line.txt   # text + delivery directions
  people/<person-id>/lines/<line-id>/*.wav      # agent-delivered takes
  ```

  The engine is fully playable with zero wavs recorded: unvoiced lines play
  as subtitles (through the localization/RTL text stack — P11 applies to
  speech text too). A manifest export lists every unrecorded line with its
  text, directions, and the person's voice description — exactly like the
  model manifest — and re-export shrinks as takes are delivered. Delivered
  wavs are picked up under the same line id with zero content edits.
- *[proposal]* Delivery notation in `line.txt`: plain text with bracketed
  marks inline — `[sigh]`, `[laugh]`, `[pause]` — plus header fields for
  tone (e.g. `tone: weary, warm`). Human-writable, agent-readable.
- Dependency note: *playing* a wav needs the audio pillar (not yet built).
  The line folders, manifests, and subtitle path can land first; a minimal
  wav playback sink joins with the audio system.

## 7. Open questions for the owner

1. **Name uniqueness scope.** Globally unique first names collapse at scale.
   Proposal: full name (first + last) unique within a person's community;
   the stable `PersonId` is the true identity.
2. **Family-name rules.** Does a spouse take the other's family name (and
   which)? Do children take the father's, mother's, or a game-configured
   rule? Proposal: a per-game *culture pack* (name pools + naming rules);
   shipped medieval default: children take the father's family name, spouses
   keep their birth name.
3. **Status-effect authority.** Engine owns storage/queries/stat-modifier
   hooks, games define semantics in data — acceptable split?
4. **Voice-line languages.** One language per game, or per-language text +
   take sets under each line folder (Hebrew games need Hebrew lines — P11)?
5. **Tree generation depth.** Generate ancestors (dead grandparents that
   explain names/looks) as record-only nodes? Proposal: yes — cheap rows,
   free history.
