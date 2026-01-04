# Rough Plan


---

## Phase 1: Infrastructure

| Feature | Notes |
|---------|-------|
| FPS Counter | Display in title bar and/or through ImGui overlay. |
| Logging System | Unify existing `VK_LOG_*`/`WGPU_LOG_*` macros into a shared system with levels, categories, and sinks. |
| Dear ImGui | Integrate with WebGPU/Vulkan. |
| Console Variables | Edit via ImGui panel initially. Support simple command parsing/execution. |
| GPU Profiling | Per-pass timings, pipeline stats. ImGui visualization. |

---

## Phase 2: Slang Migration + RHI Abstraction

### 2a. Slang Shading Language Migration

Migrate all shaders to [Slang](https://shader-slang.com/).

**Steps:**
1. Add Slang to build system (CMake integration available)
2. Validate WGSL output path early (direct emit or Slang → SPIRV → Tint → WGSL)
3. Set up reflection-based binding layout generation
4. Convert `gltf_pbr` shaders
5. Convert environment/IBL compute shaders
6. Update shader loading in backends to use Slang-compiled outputs
7. Remove legacy GLSL/WGSL files
8. Validate hot reload still works

### 2b. RHI Design & Implementation

**Minimal inital API (extend later):**

```cpp
namespace rhi {
    class Device;
    class Swapchain;     // Surface + backbuffers
    class Buffer;
    class Texture;
    class TextureView;   // VkImageView / wgpu::TextureView
    class Sampler;
    class ShaderModule;  // Slang-compiled shader
    class Pipeline;
    class BindGroup;     // Descriptor set / bind group
    class RenderPass;
    class CommandBuffer;
}
```

**Considerations**:

1. **Shader compilation:** Slang handles cross-compilation; RHI loads the appropriate format per backend. Use Slang reflection for binding layouts.
2. **Resource binding model:** Find common ground between Vulkan descriptor sets, WebGPU bind groups, D3D12 root signatures, Metal argument buffers
3. **Synchronization:** Implicit initially (RHI manages fences internally); expose explicit API later if needed
4. **Resource transitions:** Implicit tracking initially; add explicit barrier API for Vulkan/D3D12 later if needed

**Steps:**
1. Define RHI interfaces
2. Implement Vulkan backend behind RHI
3. Implement WebGPU backend behind RHI
4. Verify feature parity (same visual output from both)
5. Remove backend-specific code from renderer

---

## Phase 3: Shadows + Post-Processing

Both exercise the RHI in new ways (depth textures, render-to-texture, multi-pass) and will likely surface the need to extend or refactor certain APIs.

### 3a. Lighting + Shadows

| Feature | Notes |
|---------|-------|
| Punctual lights | Point, spot, area lights |
| Basic shadow mapping | Single directional light, PCF filtering |
| Cascaded Shadow Maps | Multiple frustum splits |
| Virtual Shadow Maps | Possibly postpone until later |

Add ImGui debug views for shadow maps, cascade splits, and depth visualization.

### 3b. Post-Processing

Move tonemapping out of fragment shader into a proper post-process pass. Enables HDR pipeline and additional effects.

| Effect | Notes |
|--------|-------|
| Tonemapping | Already implemented in shader; move to post-process |
| TAA | Temporal anti-aliasing |
| SSAO | Screen-space ambient occlusion |
| Bloom | Downsample + blur + composite |
| Additional | DOF, motion blur, color grading, etc. (as needed) |

---

## Phase 4: Backend Expansion

1. D3D12 (Windows)
2. Metal (macOS)

---

## Phase 5: GPU-Driven Rendering

- Indirect draws (MDI)
- GPU frustum culling
- Hi-Z occlusion culling
- Meshlets (+ mesh shaders where available)

Note: WebGPU currently lacks MDI (use multiple single indirect draws) and mesh shaders (use compute fallback).

---

## Phase 6: Content Pipeline

| Feature | Notes |
|---------|-------|
| Extended glTF | Add support for more PBR extensions. Possibly animations, punctual lights, and other features. |
| MaterialX | Integrate MaterialX library, Slang shadergen, drag-n-drop of materials. |


---

## Phase 7: Ray Tracing

### 7a. Embree Integration

- CPU reference implementation.

### 7b. Compute Shader Ray Tracing

- Works across all HW backends, including WebGPU.

### 7c. Hardware RT APIs

- Extend RHI to ray tracing.
- Implement in Vulkan first, then the other APIs.

### 7d. OptiX Integration
- Explore OptiX and CUDA and how they compare to VulkanRT/DXR.

### 7e. Denoising
- Explore denoising solutions for all RT backends.

---

## Phase 8: OpenUSD & Hydra

| Feature | Notes |
|---------|-------|
| Hydra Render Delegate | Add HdRenderDelegate for the renderer. |
| USD Viewer Sample | New sample integrating OpenUSD, Hydra, and the new render delegate. |
| Third-Party Renderers | Integrate HdStorm, HdEmbree, and possibly other delegates in the sample app. |


