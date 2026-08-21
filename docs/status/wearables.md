# Wearables & equipment

**Session:** session_01TV2bC4KKZ42EJLh6UdtAun
**Branch:** `claude/wearables-system-research-1h16tq`
**State:** ready
**Updated:** 2026-08-21 — 13.10 merged to integration by the architect

## Now
Awaiting dispatch. Open in this area: **13.11** the first authored garment catalogue (tunic,
trousers, boots, short hair — the D-4 set chosen to stress the mechanism), **13.12** the
zero-code-per-garment proof, **13.5** equip-time outfit chaining (deferred by ADR 0008 addendum
until the body deliverables land), and **8.21** virtual-model wearables. 13.11 waits on the
pipeline session's 13.8/13.9.

## Needs from the architect
Nothing outstanding. **ADR 0013 ratifies both contract corrections** (`B-24` pit clearance,
§9.1 covered rims) and records why implementing before the ruling was accepted this once and is
not a precedent — the ledger you are reading now is the channel that was missing.

## Last landed
**13.10** (`28f0322`) — the seven §9 acceptance gates, six passing and one honestly BLOCKED on
task 13.8. They run from one place and are read from two: `tools/wearable_gates` for whoever
changed the body, `tests/test_wearable_gates.cpp` in CI. Established by measurement that a
+35 mm normal offset self-intersects on any body with a nose, and that a one-shell body cannot
leave "no hole" — three wrong ways to measure pit clearance are documented in
`docs/research/pit-measurement.md` so nobody repeats them.

**Watch item:** the trouser hem at the ankle with no boots leaves 42 mm of rim against a 45 mm
allowance — 3 mm of headroom. The first authored trousers that sit higher will fail that gate.
