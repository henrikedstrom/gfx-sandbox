# Copilot Instructions

C++ graphics rendering project with Vulkan and WebGPU backends (D3D12, Metal, ray tracing planned).

## Guidelines

Follow the coding standards in `docs/AI_GUIDELINES.md`.

## Key Conventions

- **Naming:** `PascalCase` for classes/methods, `_camelCase` for members, `kPascalCase` for constants
- **C++ Style:** C++20, RAII, `nullptr`, explicit `override`/`final`
- **Architecture:** Keep Vulkan and WebGPU backends structurally symmetric
- **Commits:** Conventional Commits format — `type(scope): description`

## Project Context

- `renderer/backends/vulkan/` — Vulkan implementation
- `renderer/backends/webgpu/` — WebGPU implementation
- `application/` — Base application framework
- `samples/` — Sample applications like `gltf_viewer`
- `third_party/` — External/vendored code (do not edit)
- `docs/PLAN.md` — Development roadmap

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
