# gfx-sandbox

A cross-platform sandbox for exploring graphics APIs, libraries, and rendering techniques.

## Features

- glTF 2.0 model loading with PBR material support
- Image-Based Lighting (IBL) with compute shader-generated irradiance and specular maps
- Basic camera controls
- WebAssembly support via Emscripten

## Requirements

### Native Builds

- **CMake 3.22+**
- **Python 3** — required by Dawn for code generation
- **Vulkan SDK** — only if building with `-DENABLE_VULKAN=ON`

CMake will automatically fetch Dawn, GLFW, and GLM.

### Web Builds

- **Emscripten** — tested with version in [`.emscripten-version`](.emscripten-version)

## Native Build

```sh
# Configure and build
cmake -B build && cmake --build build -j8

# Run one of the sample apps
./build/samples/gltf_viewer/gltf_viewer
```

## Web Build

Ensure your Emscripten environment is activated (`source emsdk_env.sh` or `emsdk_env.bat`).

```sh
# Configure and build with Emscripten
emcmake cmake -B build-web && cmake --build build-web -j8

# Run one of the sample apps (opens browser automatically)
emrun build-web/samples/gltf_viewer/gltf_viewer.html
```

## Sample Apps

### gltf_viewer

A glTF 2.0 model viewer with PBR rendering and image-based lighting.

#### Controls

| Input | Action |
|-------|--------|
| Left Mouse | Orbit camera |
| Shift + Left Mouse | Pan camera |
| Middle Mouse | Pan camera |
| Scroll Wheel | Zoom |
| `A` | Toggle model animation |
| `Shift+A` | Reset model orientation |
| `R` | Reload shaders |
| `Home` | Reset camera to model |
| `Esc` | Quit |

#### Drag & Drop

- `.glb` / `.gltf` — Load a new model
- `.hdr` — Load a new environment map

