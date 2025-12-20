// Standard Library
#include <cstdint>
#include <cstdlib>
#include <memory>

// Third-Party
#if defined(__EMSCRIPTEN__)
#include <emscripten/emscripten.h>
#endif

// Project
#include "Application.h"

// Implemented by each sample app
std::unique_ptr<Application> CreateApplication(uint32_t width, uint32_t height);

namespace {
constexpr uint32_t kDefaultWidth = 800;
constexpr uint32_t kDefaultHeight = 600;
} // namespace

int main() {
    auto app = CreateApplication(kDefaultWidth, kDefaultHeight);
    if (!app) {
        return EXIT_FAILURE;
    }

    app->Run();

#if defined(__EMSCRIPTEN__)
    emscripten_exit_with_live_runtime();
#endif

    return EXIT_SUCCESS;
}
