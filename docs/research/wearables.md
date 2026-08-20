# Wearables research — how the industry fits clothes to bodies

A study of how wearable/clothing systems are built in shipped games, open-source
engines, and tooling — commissioned as the groundwork for this engine's wearable
mechanism (CHARACTERS.md §5–§6). The goal it serves: **once the base models are
prepared, any number of wearables can be authored natively and simply work** —
on every body variant, in every pose, through every animation, hair included.

Status of this document: **research + recommendation**. It changes no dictated
behaviour. The dictated contract (authored once against the template, fits every
variant, masks what it covers, animates through the same skinning path, layered
slots, hair is a wearable) is taken as fixed; this document is about *which
mechanisms* the industry uses to honor such a contract, and which of them fit
this engine's principles — P1 (memory) first.

It matters now because the owner has ruled that the template body will be
remade by a dedicated modeling session. The current garments are *generated
from the same code profiles as the body*, so "fits by construction" is an
artifact of generation. An artist-made body ends that trick: the fitting
guarantee must survive on **imported meshes**, which is exactly the problem the
industry techniques below solve.

---

## 1. The problem, stated precisely

A wearable must:

1. **Fit any body variant** — bone-proportion scaling (height, shoulders,
   bulk…) *and* morph deltas (chest, face…) — without per-variant authoring.
2. **Inherit every animation** of the body, present and future, with no
   per-wearable animation work.
3. **Never clip** through the body or through the layers beneath it.
4. **Layer** (base/mid/outer per slot), each layer enclosing the one beneath.
5. **Include hair**, with headwear interaction (helmet over hair).
6. **Cost almost nothing at steady state** (P1): no per-frame fitting work, no
   per-character mesh copies, one skinning path for body + worn meshes.

## 2. The four architectures in use

Every system studied falls into four families, in ascending runtime cost:

| # | Architecture | Used by | Fit quality | Runtime cost |
|---|---|---|---|---|
| A | **Texture compositing** — clothing is texels composited onto the body texture, in body UV space | WoW (thin clothing), Second Life system layers | perfect (it *is* the body) | ~zero |
| B | **Part replacement / geoset selection** — equipment replaces or toggles pre-cut body submeshes | Morrowind/OpenMW (27 part slots, priority per slot), WoW geosets, Veloren (voxel segment merge) | exact by construction | ~zero (visibility bits) |
| C | **Shared-skeleton layered skinning** — garments are separate meshes skinned to the same rig, drawn over the body | The industry default: Unreal modular characters, Unity, Godot practice, SL rigged mesh, this engine today | good; clipping and morphs are *your* problem | one skin pass over extra vertices |
| D | **Surface binding / cages** — garment vertices bound to the body surface (barycentric + offset) or wrapped via cage pairs | MakeHuman `.mhclo`, Roblox layered clothing (inner/outer cages + RBF), Daz Auto-Fit | follows arbitrary shapes automatically | fit-time solve; steady state same as C |

Key sources: [OpenMW `npcanimation.cpp`](https://github.com/OpenMW/openmw/blob/master/apps/openmw/mwrender/npcanimation.cpp)
(slot priorities: robe 11 &gt; skirt 3 &gt; plain 0, armor wins ties),
[wowdev: Character Customization](https://wowdev.wiki/Character_Customization) /
[ItemDisplayInfo](https://wowdev.wiki/DB/ItemDisplayInfo) (geoset groups:
gloves 4xx, boots 5xx, robe 13xx — an item *selects* built-in variants),
[MakeClothes format](https://static.makehumancommunity.org/assets/creatingassets/makeclothes/clothes.html)
(per-vertex body-triangle + barycentric + scaled offset, vertex-group
constrained), [Roblox layered clothing](https://create.roblox.com/docs/resources/beyond-the-dark/layered-clothing)
(shared-topology cage pairs, RBF wrap at equip time, hidden-surface removal,
still skinned to the shared rig afterwards),
[UE modular characters](https://dev.epicgames.com/documentation/en-us/unreal-engine/working-with-modular-characters-in-unreal-engine)
(Leader Pose vs Copy Pose vs `FSkeletalMeshMerge` — merge wins steady-state,
loses morph targets).

Two instructive failures:

- **Second Life** shipped rigged mesh clothing *without* a fitting mechanism —
  clothes ignored the avatar's 218 shape sliders. The promised Mesh Deformer
  ([STORM-1716](https://jira.secondlife.com/browse/STORM-1716)) never shipped;
  the ecosystem fractured into per-brand body standards and alpha-mask kits.
  Lesson: **the fitting guarantee is the product**. Ship it with the first
  wearable, or content fragments forever.
- **WoW helmets vs hair**: hair is one geoset per style, so helmets can only
  hide *all* of it ([HelmetGeosetVisData](https://wowdev.wiki/DB/HelmetGeosetVisData));
  showing hair under helms was quoted as reclassifying ~4,900 items × races ×
  genders. Lesson: **segment hair into regions on day one** — retrofitting
  granularity into shipped content is the most expensive change in this domain.

## 3. The mechanisms, piece by piece

### 3.1 Animation inheritance: one skeleton, one palette

Universal across B/C/D: garments bind to the **same skeleton**, animation is
evaluated **once**, and every worn mesh consumes the same matrix palette. The
per-frame marginal cost of a garment is its vertices in the skinning pass plus
(if unmerged) a draw call. Unreal's tiers make the trade explicit: Leader Pose
(shared pose, separate draws) vs runtime mesh merge (one draw, one skin pass,
but morph targets are lost in the merge). GPU matrix-palette skinning is cheap
even on weak hardware (order 0.03–0.1 ms per character;
[measurements](https://gamedev.net/forums/topic/618810-gpu-skinning-and-frame-interpolation/)).

*This engine already does the strongest form of this*: body and garments ride
one palette, one dynamic-offset UBO slot, one pipeline (task 8.10/8.16). No
change needed — the research confirms the architecture.

### 3.2 Skin weights for imported garments: transfer, don't hand-author

The industry does not hand-weight clothes; weights are **transferred from the
body at import**:

- Naive nearest-surface-point copy (Maya Copy Skin Weights, Blender Data
  Transfer) works for tight clothing, fails on loose garments (a skirt's
  nearest body point is the wrong thigh).
- The state of the art is **Robust Skin Weights Transfer via Weight Inpainting**
  (Abdrashitov et al., SIGGRAPH Asia 2023 — now Unreal's built-in transfer):
  copy only where the closest point is near *and* normals agree (confidence
  gate), then diffuse/inpaint weights across the rejected vertices
  ([paper](https://www.dgp.toronto.edu/~rinat/projects/RobustSkinWeightsTransfer/index.html)).
- **Delta Mush / Direct Delta Mush** (EA SEED) is the offline cleanup that
  turns transferred weights into production deformation
  ([DDM deck](https://media.contentapi.ea.com/content/dam/ea/seed/presentations/le2019-siggraph2019-direct-delta-mush-skinning-and-variants.pdf)).

All of it is **import-tool work** — nothing of this touches the frame path.

### 3.3 Masking: the engine's current design is the industry pattern

"Don't render the body under the clothes" appears everywhere, at three
granularities:

- **Geosets / partitions** (WoW; Skyrim's numbered body-slot partitions,
  [UESP](https://ck.uesp.net/wiki/Skyrim_bodyparts_number)): pre-cut submeshes
  toggled by visibility bits — cheapest, deterministic, needs the cut decided
  up front.
- **Zap sets** (BodySlide): per-outfit authored vertex deletion masks for
  geometry that would clip a specific garment.
- **Texture alpha masks** in body UV space: finest granularity, but the body
  stays rendered (pays vertex cost) and alpha-test costs on tile-based mobile
  GPUs. Roblox instead does automated geometric hidden-surface removal.

*This engine's closed-shell region masking (index-range skip, ADR 0007 §3) is
the geoset/partition mechanism* — the right one for P1 (a dressed body costs
*fewer* triangles than a bare one). The research adds one requirement: the
imported template body must keep **region = closed shell = index range**, and
regions should be *finer* where wearables need it (hair, face; §5 below).

### 3.4 Following body morphs: bake or bind, never per-frame

Bone-proportion variants are free everywhere (the palette scales the garment
exactly as it scales the body — this engine's existing mechanism). The open
question is **morph deltas** (chest, face, future sliders). Two proven answers:

- **Bake per-garment morphs at import** (BodySlide "Conform": project the
  body's per-vertex slider deltas onto the outfit, store sparse per-garment
  deltas; [guide](https://wiki.nexusmods.com/index.php/Bodyslide:_Guide_and_Tutorial)).
  Memory scales with garments × morphs × affected vertices.
- **Surface binding** (MakeHuman `.mhclo`): store, per garment vertex, a body
  triangle + barycentric coords + offset, constrained by matching vertex
  groups; when morphs change the body, re-evaluate garment vertices from the
  deformed surface. One binding (~16–32 B/vertex) serves *any number of
  morphs*; evaluation is O(garment verts), only when sliders change.

Roblox's cage pairs are surface binding generalized to arbitrary bodies and
third-party content — bought with a heavyweight ecosystem-wide cage-topology
contract and an RBF solve. Its layering insight transfers, though: **each
layer's outer surface is the reference the next layer fits against**.

### 3.5 Hair: an item like any other, segmented from day one

- Representation on mobile budgets: **sculpted opaque hair meshes** (≤ ~2k
  tris), alpha-test only for wisps; hair cards are a fill-rate/overdraw risk on
  tile-based GPUs; shells are for fur accents, not hair. Matches this engine's
  ≤600-tri wearable budget and flat-shaded present.
- Headwear interaction, in ascending sophistication: hide-all + always-present
  **scalp cap** (WoW bald geoset); **partial hiding** via hair region masks
  (scalp/fringe/sides/length as separate index ranges); **"pressed" morph** —
  FFXIV bakes a hat-compatibility shape key into each hairstyle
  ([mechanism](https://www.xivmods.guide/how-to-add-hat-compatibility/));
  fully separate slots with tolerated clipping (Roblox/Fortnite).
- Secondary motion (ponytails, hair falls): **spring bones** — a short chain
  simulated after pose evaluation, before skinning. The open
  [VRM `VRMC_springBone` spec](https://github.com/vrm-c/vrm-specification/blob/master/specification/VRMC_springBone-1.0/README.md)
  is the reference: per joint `stiffness`, `dragForce`, `gravityPower/Dir`,
  `hitRadius`; sphere/capsule colliders; Verlet tail integration root-to-leaf,
  bone length re-constrained each step. Allocation-free, fixed-step friendly,
  proven on mobile-class hardware (VRChat Quest caps ~8 chains/avatar).

### 3.6 Held items: same-named frames

Confirmed practice (Unreal sockets, Godot `BoneAttachment3D`, Roblox
attachments): the **skeleton carries a named socket** (bone + local offset),
the **item carries its grip transform**, attach = align the two frames — one
matrix multiply per item per frame. Sheathing is a socket switch on an
animation event. This is exactly CHARACTERS.md §6.1; the Roblox detail worth
adopting is *same-named attachment on both sides* as the data contract, and
re-evaluating attachments after any spring-bone pass.

### 3.7 Where the work lives (the P1 answer)

The pattern every mobile-viable system converges on:

| Phase | Work |
|---|---|
| **Import (offline tool)** | weight transfer + inpainting, surface-binding capture, morph baking, masking/partition assignment, enclosure validation, LOD build |
| **Spawn/equip (rare, job lanes)** | palette build, binding re-evaluation for the character's morphs (cached), mask bit resolution, socket wiring |
| **Every frame (steady state)** | one pose evaluation, one palette upload, one skin pass over body + visible garments, spring-bone step for budgeted chains. **Nothing else.** |

Cloth *simulation* is the expensive outlier the entire pattern exists to avoid;
ordinary clothing costs no per-frame work beyond skinning.

## 4. Recommendation for this engine *(proposal — awaiting verdict)*

The current runtime architecture is validated by the research and **stays**:
one rig, one palette, single skinning path, region-shell masking, per-layer
thickness, layered slots. What must be *added* for the artist-made-body era is
the **import-time fitting pipeline** — the piece that turns "fits by
construction" (generator) into "fits by baked data" (imported meshes):

1. **The template body is the authoring contract.** The engine publishes the
   template body (glTF) — mesh, rig, regions, UV chart. Wearables are modeled
   against it in any DCC and imported like any asset (P5).
2. **Weights at import**: accept authored weights when present; otherwise
   transfer from the body — confidence-gated nearest-surface copy with
   inpainting fallback (§3.2). Clamp to the engine's ≤4 influences.
3. **Surface binding at import** (MakeHuman-style, vertex-group constrained,
   §3.4): per garment vertex, body triangle + barycentric + offset. Bone-scale
   variants stay free via the palette; **morph deltas** re-fit garments by
   re-evaluating the binding at spawn/equip on job lanes, cached per
   (garment, variant-morph-set) — never per frame. This one mechanism covers
   every present and future morph without per-garment morph authoring, at
   fixed memory per garment (P1 over the baked-morph alternative).
4. **Layering**: layer *k* binds against layer *k−1*'s outer surface offset
   by its thickness profile (the Roblox chaining insight, evaluated offline /
   at equip — no runtime cages, no RBF at runtime). Import validates
   enclosure across the variant extremes (the MODELING.md fit gate).
5. **Masking**: covered regions resolve to index-range skips exactly as today;
   wearable `covers` declarations validated against the template's region set
   at import. Optional per-garment zap sets if a garment needs finer removal
   than a region.
6. **Hair**: a wearable in `head_hair` with **mandatory sub-regions**
   (scalp-cap / fringe / sides / length) so headwear can hide subsets; an
   optional `pressed` morph for the hat-compressed look; always a scalp cap so
   hide-all degrades gracefully. Spring-bone chains (VRM semantics, budgeted
   ~8–12 joints/character, fixed-step, frozen by LOD) as the later
   secondary-motion extension.
7. **Held items**: keep §6.1; formalize skeleton-side named sockets + item-side
   grip frames, attach after pose (and after springs).

Explicitly **rejected** (with reasons): runtime cloth simulation and runtime
cage/RBF solves (P1 — frame path must stay fitting-free); Leader-Pose-style
per-part pose copies (we already share one palette); texture-composited
clothing as the primary mechanism (no silhouette change; revisit as an *option*
when the texture pipeline lands); Second Life's ship-now-fit-never (§2).

## 5. What the new template body must provide (handoff to the modeling session)

The wearable mechanism is only as good as the contract the base model honors.
For wearables to "just work", the remodeled body must ship with:

1. **Region segmentation as closed shells** mapping to index ranges
   (BodyRegion set, MODELING.md §3.5) — including **separate scalp cap and
   face regions**, and hair-relevant granularity from day one (§2, WoW
   lesson).
2. **The canonical rig binding** (17 joints today; ≤4 influences, ≤2 in
   shipped content) with three loops across every bending joint — transferred
   weights are only as good as the body weights they come from.
3. **A frozen topology + vertex-order contract**: surface bindings and morph
   deltas reference the template's triangles by index. Re-importing a body
   with different topology invalidates every binding — topology changes are
   format-version events, not silent edits (MODELING.md determinism rule).
4. **Morph deltas expressed on the template mesh** (face/chest sub-schemas)
   so bindings can consume them; proportions stay in the palette
   (ADR 0007: palette scale, never new geometry).
5. **The published authoring reference** (glTF export of the template, with
   regions/vertex groups named) — the file a wearable artist models against.
6. **Vertex groups per region** on the body surface, constraining what a
   garment vertex may bind to (MakeHuman's guard against a sleeve binding to
   the torso).

## 6. Validation gates for wearables (extends MODELING.md's definition of done)

From the case studies, the checks that catch real failures:

- **Enclosure**: every layer encloses the body/layer beneath on the template
  *and at the variant extremes* (already a MODELING.md gate — keep it).
- **Pose sweep**: fit holds with every joint at its limit (transferred weights
  fail at joints first).
- **Coverage consistency**: `covers` ∩ rendered-regions = ∅; no bare gap
  between adjacent garments (hem/boot rule).
- **Binding validity**: every garment vertex bound within its permitted vertex
  group, offset within the layer's thickness envelope.
- **Budget**: wearable ≤ 600/350/150 tris by LOD; ≤4 influences; hair ≤ head
  budget; spring chains within the per-character joint budget.
- **Looked at**: render the dressed body — front/three-quarter/side/back —
  on the template and the two most extreme variants, posed.

## 7. Sources

Fitting &amp; skinning: [Robust Skin Weights Transfer (SIGGRAPH Asia 2023)](https://www.dgp.toronto.edu/~rinat/projects/RobustSkinWeightsTransfer/index.html) ·
[UE Modular Characters](https://dev.epicgames.com/documentation/en-us/unreal-engine/working-with-modular-characters-in-unreal-engine) ·
[Direct Delta Mush (EA SEED)](https://media.contentapi.ea.com/content/dam/ea/seed/presentations/le2019-siggraph2019-direct-delta-mush-skinning-and-variants.pdf) ·
[Deformation Transfer (Sumner &amp; Popović 2004)](https://dl.acm.org/doi/10.1145/1015706.1015736) ·
[Bounded Biharmonic Weights](https://dl.acm.org/doi/10.1145/2578850) ·
[TailorNet (CVPR 2020)](https://openaccess.thecvf.com/content_CVPR_2020/papers/Patel_TailorNet_Predicting_Clothing_in_3D_as_a_Function_of_Human_CVPR_2020_paper.pdf)

Case studies: [OpenMW npcanimation.cpp](https://github.com/OpenMW/openmw/blob/master/apps/openmw/mwrender/npcanimation.cpp) ·
[UESP Morrowind file format](https://en.uesp.net/wiki/Morrowind_Mod:Mod_File_Format) ·
[MakeHuman clothes format](https://static.makehumancommunity.org/assets/creatingassets/makeclothes/clothes.html) ·
[wowdev Character Customization](https://wowdev.wiki/Character_Customization) ·
[wowdev ItemDisplayInfo](https://wowdev.wiki/DB/ItemDisplayInfo) ·
[wowdev HelmetGeosetVisData](https://wowdev.wiki/DB/HelmetGeosetVisData) ·
[Skyrim body-part slots](https://ck.uesp.net/wiki/Skyrim_bodyparts_number) ·
[BodySlide &amp; Outfit Studio](https://www.nexusmods.com/skyrimspecialedition/mods/201) ·
[BodySlide zap sets](https://github.com/ousnius/BodySlide-and-Outfit-Studio/wiki/Adding-zaps-to-projects) ·
[Roblox layered clothing](https://create.roblox.com/docs/resources/beyond-the-dark/layered-clothing) ·
[Roblox HSR patent US12190427](https://image-ppubs.uspto.gov/dirsearch-public/print/downloadPdf/12190427) ·
[Second Life Mesh Deformer (STORM-1716)](https://jira.secondlife.com/browse/STORM-1716) ·
[SL Fitted Mesh](https://wiki.secondlife.com/wiki/Mesh/Rigging_Fitted_Mesh) ·
[Veloren armor guide](https://book.veloren.net/contributors/guides/adding-armor/guide.html) ·
[Daz Transfer Utility](http://docs.daz3d.com/doku.php/public/software/dazstudio/4/referenceguide/interface/action/index/dztransferutilityaction/start)

Hair &amp; attachments: [VRM springBone 1.0 spec](https://github.com/vrm-c/vrm-specification/blob/master/specification/VRMC_springBone-1.0/README.md) ·
[FFXIV hat-compatibility morphs](https://www.xivmods.guide/how-to-add-hat-compatibility/) ·
[VRChat Android limits](https://creators.vrchat.com/platforms/android/quest-content-limitations/) ·
[UE Skeletal Mesh Sockets](https://dev.epicgames.com/documentation/en-us/unreal-engine/skeletal-mesh-sockets-in-unreal-engine) ·
[Godot BoneAttachment3D](https://docs.godotengine.org/en/stable/classes/class_boneattachment3d.html) ·
[Roblox attachments](https://create.roblox.com/docs/art/characters/creating/verify-attachments) ·
[Blender Data Transfer](https://docs.blender.org/manual/en/latest/modeling/modifiers/modify/data_transfer.html) ·
[GPU skinning measurements](https://gamedev.net/forums/topic/618810-gpu-skinning-and-frame-interpolation/)
