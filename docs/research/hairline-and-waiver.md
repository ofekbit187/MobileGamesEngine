# Tasks 13.7a and 13.7b — the loop is cut, and the gate is green for the first time

**From:** the character asset pipeline session · **To:** the architect, cc the owner, the textures and wearables sessions
**Implements:** ADR 0012 — Ruling 1 (waiver), Ruling 2 (LOD0 cap), Ruling 3 (proportion gate)
**Evidence:** `mge_uv_report`, `ctest`, `mge_body_preview`

---

## 1. What shipped

**New body content hash: `1067c74324b6e091`** (2 097 vertices, 2 388 triangles, 12 parts).
Trio + re-cut garments + re-baked bindings, atomic, as ADR 0008 requires.

```
mge_uv_report --gate  ->  chart status: CONFORMS (with waivers)   exit 0
```

**That is the first time this gate has been green.** It was `REFUSED` on four rules before
the repack, two after it, one after 13.7, and none now.

| | LOD0 | LOD1 | LOD2 |
|---|---|---|---|
| B-5 cap (after ADR 0012) | 2 400 | 1 300 | 650 |
| shipped | **2 388** | **1 200** | **560** |

LOD1 and LOD2 are exactly where they were. The 200 triangles ADR 0012 added went to the
boundary loop and nowhere else: LOD0 decimates to 2 185 and the bisect brings it to 2 388.

## 2. The loop, proved by topology rather than by eye

The architect asked whether a render would show the hairline as a real loop rather than a
staircase. **It would not, and that is worth saying plainly:** the body is smooth-shaded and
untextured, so the Face/Scalp seam is invisible in any render the engine can currently
produce. A render here would be evidence of nothing.

What settles it is topology, and `the_hairline_is_one_closed_loop` measures it:

```
hairline: 87 boundary edges in 2 closed loop(s):
    77 vertices, centred (-0.001, 1.616, -0.061)
    10 vertices, centred (-0.003, 1.558, -0.080)
```

Every boundary vertex has **exactly two** boundary edges — that is what makes a loop rather
than a branching seam — and the edges close into rings.

Two details worth recording:

- **Matched by position, not by index.** Since 13.7 the regions are separate glTF primitives,
  so Face and Scalp own distinct vertex copies along the seam and *no edge is literally
  shared between them*. An index-based test would have found zero shared edges and concluded
  something false.
- **The second ring is real, and it is not the ears.** I assumed ears when I first saw two
  rings and the measurement said otherwise: it sits on the **mid-line** (x ≈ 0) at the
  measured mouth height (1.553 m), not out at the ears (|x| = 0.093 m). It is the rim of the
  source mesh's own mouth opening, which falls inside the Face region and therefore bounds
  it. The comment in the test says what was measured, not what I guessed.

## 3. The proportion gate now tracks the body

ADR 0012 Ruling 3. The bands come from the **torso's own extent** instead of fixed world
heights. Measured across decimation targets, holding everything else constant:

| target | hip width, old gate | hip width, fixed gate |
|---|---|---|
| 2 400 | — | 0.347 m |
| 2 185 | — | 0.347 m |
| 2 100 | **0.146 m** | 0.372 m |
| 1 990 | **0.146 m** | 0.372 m |
| 2 200 | 0.347 m | 0.347 m |

The old gate's number moved by 2.4× for a reason that had nothing to do with proportions.
The fixed one varies by 7 % across a 20 % swing in triangle budget, which is the decimator
moving vertices — a real effect, at a sane magnitude. Both this gate and the loop test now
**print their numbers**, so the next person to break one can see what it read.

```
proportions: shoulders 0.428 m, waist 0.343 m, hips 0.347 m (torso 0.777..1.525 m)
```

**One thing for the architect, since the sampling defect is no longer hiding it:**
`hips > waist` now holds by **4 mm**. That margin is this body's anatomy — a realistic male
waist and hip are close in width — not a measurement artifact, so I have not touched the
assertion. But it is thin, and a body variant or a re-decimation could cross it. It is a
different kind of fragile from the one just fixed, and worth knowing about rather than
discovering.

## 4. The waiver mechanism, and the guard that fired for real

Built to ADR 0011's design and ADR 0012's ruling, under the recorded seam exception.

```
waive <asset> <rule> <region> <measured> | <reason> | <retires when>
```

It surfaces as a third verdict, `WAIV`, never `pass`, and every waiver prints under the
summary with the condition that retires it. `stretch_max` itself stays 1.50 — the standard
does not move to fit the content pointed at it.

Two safety properties, both deliberate:

1. **A waiver names one REGION.** Waiving the arms says nothing about the face. Without this
   the Face's provisional waiver and the arms' outright ones could not coexist in one rule.
2. **A waiver stops applying if the measured value drifts past what was recorded.** A waiver
   is granted for a known state, not as a blanket over a rule.

**The second one is not theoretical — it fired on this very change.** 13.7a's bisect moved
`HandL` from 1.93× to 1.96×, and the gate refused to cover it:

```
FAIL  stretch_above_max  8 of 12 regions stretch more than 1.50x; not waived: HandL 1.96x
```

That is why the recorded values in the standard are the post-13.7a measurements rather than
the 13.7 ones carried forward. A waiver mechanism that had silently absorbed the drift would
have been worse than no waiver, and the first thing it did was prove it does not.

Final state, all eight over the rule and all covered:

```
Face 2.08x   ArmL 1.57x   ArmR 1.74x   HandL 1.96x
HandR 1.61x  LegL 1.63x   LegR 1.69x   FootR 1.53x
```

The Face's line is the provisional one, and its retirement condition is a person rather than
a threshold, exactly as ADR 0012 ruled:

> *retires when: the owner judges the first authored face texture; if it reads, a face-only
> re-unwrap (301 triangles, sourced not originated) rides the next contract-version event*

## 5. Evidence

```
mge_uv_report --gate   CONFORMS (with waivers), exit 0 — 6 pass, 1 WAIV, 0 FAIL
                       12 of 12 regions own geometry, every region at 472 px/m, 0% spread
ctest                  12/12, including the_hairline_is_one_closed_loop
host runner            steady-state heap allocations: 0
mge_garment_fit        all six garments re-baked against 1067c74324b6e091
determinism            two independent pipeline runs, byte-identical .mgeskin (B-14)
```

## 6. One note on ADR 0012's own text

Its Consequences section says the hairline loop "becomes the pipeline session's work after
13.8/13.9", while the covering instruction ordered 13.7a and 13.7b first. I followed the
instruction. Flagging it only so the ADR and the record agree when someone reads them cold.
