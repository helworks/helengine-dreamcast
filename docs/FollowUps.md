# Dreamcast follow-ups

Deferred items with a decision owner. Each names why it was not done at the time.

## Two component support rules match nothing

`builder/DreamcastPlatformDefinitionFactory.cs` declares
`helengine.meshcomponent` and `helengine.cameracomponent` in lowercase. The build
graph derives component type ids from the type's full name, producing
`helengine.MeshComponent` and `helengine.CameraComponent`, and the lookup is
ordinal. So those two rules match nothing and enforce nothing.

Confirmed twice independently during the 2026-09-18 sprite milestone: once by the
implementer that added the sprite rules, and again by that task's reviewer, both
by applying the engine's own derivation by hand.

**Why it was not fixed there.** Correcting the casing would make two dormant rules
start enforcing "one static triangle-list mesh" and "one perspective camera" for
the first time. That can newly reject scenes which build today, so it is a
behaviour change needing its own build and verification rather than a two-line
edit inside an unrelated milestone. The real enforcement already lives in
`DreamcastSceneCompatibilityValidator`, which demonstrably works, so the exposure
is a dead second layer rather than absent validation.

**Why this file exists.** The same factory now also declares three correctly-cased
rules for sprites, text and rounded rectangles, sitting directly beside the two
broken ones. The inconsistency is exactly what a later reader will "tidy", in
either direction, without knowing that one direction changes behaviour.

**To close it:** correct the two ids, then run a full graph build and the scene
verifier against a project that builds today, and confirm nothing newly fails.
