# AI Coding Guidelines

## AI Collaboration Principles

- Prefer minimal, targeted edits over broad refactors
- Preserve existing architecture and naming unless explicitly asked to change them
- Do not introduce new third-party dependencies or subsystems without explicit approval
- Do not edit or format code in `third_party/` — it's external/vendored
- Avoid speculative abstractions — solve the problem in front of you
- Match the existing code style and patterns — do not impose your own
- Explain your reasoning briefly before large edits

## Project Overview

Cross-platform graphics sandbox with Vulkan and WebGPU backends (D3D12, Metal, and Ray Tracing / Path Tracing backends will be added later). Targets native (Windows/macOS/Linux) and web (Emscripten/WASM).

**Key directories:**
- `renderer/` — Core renderer interfaces and backend implementations
- `renderer/backends/vulkan/` — Vulkan backend
- `renderer/backends/webgpu/` — WebGPU backend  
- `application/` — Base application framework (windowing, input, camera, etc)
- `samples/` — Sample applications (e.g., `gltf_viewer`, future OpenUSD viewer)

## Project Status

This project is in early development. See `docs/PLAN.md` for the roadmap.

**Areas in transition:**
- **Shaders:** Will migrate to Slang — avoid adding new GLSL/WGSL files
- **Backend code:** RHI abstraction planned (Vulkan, WebGPU, D3D12, Metal) — keep backend-specific code isolated
- **Ray tracing:** Multiple backends planned (hardware RT, compute shader, CPU/Embree) — design for abstraction

When making changes, prefer patterns that will survive the planned refactors.

## Code Style

### Naming
- **Classes/Structs:** `PascalCase` — `VulkanRenderer`, `SubMesh`
- **Methods/Functions:** `PascalCase` — `CreateRenderPass()`, `UpdateUniforms()`
- **Member variables:** `_camelCase` prefix — `_window`, `_vsyncEnabled`
- **Static members:** `s_camelCase` prefix — `s_instance`
- **Constants:** `kPascalCase` — `kMaxSetsPerPool`
- **Parameters/Locals:** `camelCase` — `modelMatrix`, `srcBuffer`

### File Structure

**Header files (.h):**
```cpp
/// @file  FileName.h
/// @brief One-line description.

#pragma once

// Standard Library Headers
#include <vector>
#include <string>

// Third-Party Library Headers
#include <glm/glm.hpp>

// Project Headers
#include "IRenderer.h"

// Forward Declarations
class SomeClass;
```

**Implementation files (.cpp):**
```cpp
// Class Header
#include "FileName.h"

// Standard Library Headers
#include <algorithm>
#include <format>

// Third-Party Library Headers
#include <GLFW/glfw3.h>

// Project Headers
#include "OtherClass.h"
```

**Include order rationale:**
- **Headers:** Standard Library → Third-Party → Project → Forward Declarations
  - Ensures headers are self-contained (standard library dependencies are included first)
  - Most stable to least stable dependencies
- **Implementation files:** Class Header first, then same order as headers
  - Including your own header first catches missing includes in the header
  - Same ordering for consistency

**Angle brackets vs quotes:**
- `<header>` — External dependencies: standard library, system headers, third-party libraries (GLFW, glm, Vulkan, etc.)
- `"header"` — Project headers: files that are part of this codebase (`"logging/Log.h"`, `"IRenderer.h"`, etc.)

**Include paths:**
- Use include path-relative paths, not relative directory paths (`"../"`)
- Since `core/` is in the include path, use `"logging/LogMessage.h"` not `"../LogMessage.h"`
- This makes includes explicit, portable, and consistent across the codebase
- Example: From `core/logging/sinks/ISink.h`, use `"logging/LogMessage.h"` not `"../LogMessage.h"`

### Class Structure
1. Public interface first, then protected, then private
2. Mark non-copyable/non-movable explicitly when appropriate
3. Use `final` for classes not intended as base classes
4. Use `override` for virtual method implementations
5. Use `[[maybe_unused]]` for intentionally unused parameters
6. Use `noexcept` where appropriate

### Modern C++
- Use C++20 features
- Use RAII for resource management
- Prefer `nullptr` over `NULL`
- Use `auto` for complex iterator/template types
- Initialize members with `{}` in declarations
- Use `const` liberally (methods, references, locals)
- Use `explicit` on single-argument constructors
- Use `std::unique_ptr` for owned resources
- Use `alignas` for GPU uniform struct members
- Only use [[nodiscard]] on functions where ignoring the return value is almost certainly a bug (e.g., failure-return APIs).
- Use `std::format` for string formatting (C++20)

### Comments
- **Keep comments brief** — explain "why", not "what" the code does
- **Don't comment self-explanatory code** — if the code is clear, no comment needed
- **Use section dividers** for logical grouping in implementation files:
  ```cpp
  //----------------------------------------------------------------------
  // Section Name
  ```
- **File headers:** Use `/// @file` + `/// @brief` at the top of header files
- **Class/type documentation:** Use `///` for file-level and top-level class, struct, and enum documentation (single or multi-line). Do not use `///` for types, members, or methods that are inside another class or struct
- **Inside classes/structs:** Use `//` for nested types, members, and methods. Regular `//` comments are acceptable when the name and signature are not self-documenting enough; do not comment more than necessary
- **Enum values:** Use regular `//` comments for enum value documentation, not `///<`
- **Section comments:** Use `//` for section headers like `// Public Interface`, `// Accessors`, `// Private Member Functions`
- **Comment workarounds and non-obvious decisions** — future readers need context

## Error Handling

- **Assertions:** Use for programmer errors and invariant violations (debug builds)
- **Exceptions:** Acceptable for unrecoverable initialization errors; avoid in hot paths
- **Return early:** For recoverable runtime failures (e.g., failed to acquire swapchain image), log and return
- **Logging:** Use `VK_LOG_*`/`WGPU_LOG_*` macros; avoid logging in tight loops or per-frame hot paths

## Architecture Patterns

### Backend Symmetry
Keep Vulkan and WebGPU backends structurally similar where possible:
- Similar method names and organization
- Matching uniform struct layouts (with `alignas`)
- Consistent submesh/material organization

### Resource Patterns
- Create/destroy resources in matched pairs
- Use `std::unique_ptr` for owned subsystems
- Initialize RAII handles to null state: `vk::raii::Buffer _buffer{nullptr}`

## Problem Solving

- Read and understand existing code before proposing changes
- **Don't invent APIs:** If a function or pattern isn't found in-repo, search first; if truly missing, propose a minimal addition and explain why
- **Clarify before changing behavior:** If a change affects rendering output or performance, ask for confirmation unless fixing an obvious bug
- **When in doubt, ask:** Clarifying questions before implementing beats rework after
- **Plan before implementing:** For medium+ changes, propose a plan and get agreement before starting
- **Stop and ask on design conflicts:** When encountering issues that require changing agreed-upon designs (namespace names, API signatures, class hierarchies), present options rather than picking one. Examples:
  - Name conflicts (e.g., namespace collides with standard library)
  - Interface changes that affect usage patterns
  - Architectural pivots due to technical constraints

**Change scale guide:**
| Scale | Examples | Approach |
|-------|----------|----------|
| Small | Bug fix, tweak, single-file edit | Just do it |
| Medium | New feature, multi-file refactor, new class | Outline plan first |
| Large | New system, architectural change, cross-cutting refactor | Detailed plan required |

## Definition of Done

Before considering a change complete:

1. **Build passes:** Native build compiles; linters pass (see Tooling section)
2. **Runs correctly:** Relevant sample app launches and behaves as expected (when feasible to test)
3. **No drive-by changes:** Touch only what's necessary — avoid formatting-only diffs or unrelated cleanups
4. **Backend symmetry:** If changing Vulkan public behavior, either mirror in WebGPU or add a `// TODO:` explaining why not (and vice versa)

## Code Review Checklist

Graphics-specific concerns that compile but can still be wrong:

- **Lifetime & ownership:** All GPU/OS resources must be RAII-owned; destruction order matters (e.g., views → images → device); no raw `new`/`delete` outside RAII wrappers
- **Threading:** Don't introduce cross-thread device usage unless already present in the codebase
- **Alignment & ABI:** Uniform structs use `alignas`, padding matches between C++ and shaders, layouts match across backends
- **Error handling:** Follow the Error Handling section above
- **Allocations:** Avoid hidden allocations in hot paths (per-frame code)

## Tooling

- **`.clang-format`:** Defines formatting — do not change format rules unless requested
  - **Formatting scope:** Only format files in `application/`, `core/`, `renderer/`, and `samples/` directories
  - **Do NOT format:** `third_party/`, `build/`, `build-*/`, or any `_deps/` directories
  - **Commands:**
    - **PowerShell (Windows):** `Get-ChildItem -Path application,core,renderer,samples -Include *.cpp,*.h,*.hpp -Recurse | ForEach-Object { clang-format -i $_.FullName }`
    - **Bash/sh (Linux/macOS):** `find application core renderer samples -type f \( -name "*.cpp" -o -name "*.h" -o -name "*.hpp" \) -exec clang-format -i {} +`
- **`.clang-tidy`:** Follow its rules; treat warnings in touched code as errors
- **Compiler warnings:** No new warnings in touched modules

## Build Commands

```sh
# Native
cmake -B build
cmake --build build

# Web (requires Emscripten)
emcmake cmake -B build-web
cmake --build build-web
```

**Note:** On Windows/PowerShell, use `;` to chain commands, not `&&`.

## Git Commits

Use [Conventional Commits](https://www.conventionalcommits.org/) format:

```
type(scope): description
```

**Types:** `feat`, `fix`, `refactor`, `docs`, `ci`, `build`

**Scopes:** `vulkan`, `webgpu`, or omit for cross-cutting changes

**Style:**
- Imperative mood: "add feature" not "added feature"
- Lowercase description
- Keep subject line concise (~50 chars)
- Add brief bullet points in body when needed, but don't over-explain — the diff speaks for itself

**Examples:**
```
feat(vulkan): implement shadow mapping
fix(webgpu): align uniform buffer to 256 bytes
refactor: extract common shader utilities
docs: update build instructions
```

