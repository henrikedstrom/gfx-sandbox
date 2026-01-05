# gfx-sandbox

Cross-platform graphics sandbox with Vulkan and WebGPU backends (D3D12, Metal, and ray tracing backends planned).

**Note:** Early-stage project. See `docs/PLAN.md` for roadmap.

## Guidelines

Follow `docs/AI_GUIDELINES.md` for coding standards and conventions.

**Quick reference:** C++20, `PascalCase` classes/methods, `_camelCase` members, `kPascalCase` constants.

## Project Context

- `renderer/backends/vulkan/` — Vulkan implementation
- `renderer/backends/webgpu/` — WebGPU implementation
- `application/` — Base application framework
- `samples/` — Sample applications like `gltf_viewer`
- `third_party/` — External/vendored code (do not edit)
- `docs/PLAN.md` — Development roadmap

## Build

```bash
# Native
cmake -B build
cmake --build build

# Web (Emscripten)
emcmake cmake -B build-web
cmake --build build-web
```

## Areas in Transition

Avoid expanding:
- New GLSL/WGSL shaders (Slang migration planned)
- Backend-specific patterns (RHI abstraction coming)

## Agent Behavior

- Prefer patch-level edits over sweeping rewrites
- Always search before editing (avoid duplicate utilities)
- For multi-file changes, include a brief change summary
- **Don't invent APIs** — search first; if missing, propose and explain
- **Don't introduce new dependencies** without explicit approval
- **Don't edit `third_party/`** — it's external/vendored
- **When in doubt, ask** — clarifying questions beat rework
- **Plan first** for medium+ changes (multi-file, new class, refactor)
- **Explain reasoning** before large edits
- Define stop condition: "done when build passes + sample renders correctly"
