# Audited executor overlay

`executor.patch` contains generic public-runtime adaptations, not generated
game code or original executable data. Apply it only through
`cmake/scene_backend.cmake`, which verifies its input and keeps all output
inside the selected build directory. See
[`docs/GPU_SCENE_BACKEND.md`](../../docs/GPU_SCENE_BACKEND.md) for provenance,
completion/pacing rules, test evidence and remaining limitations.
