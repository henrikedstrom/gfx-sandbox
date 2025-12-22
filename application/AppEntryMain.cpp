// Standard Library Headers
#include <cstdlib>
#include <memory>

// Third-Party Library Headers
#if defined(__EMSCRIPTEN__)
#include <emscripten/emscripten.h>
#endif

// Project Headers
#include "Application.h"

// Implemented by each sample app
std::unique_ptr<Application> CreateApplication(int argc, char** argv);

int main(int argc, char** argv) {
    auto app = CreateApplication(argc, argv);
    if (!app) {
        return EXIT_FAILURE;
    }

    app->Run();

#if defined(__EMSCRIPTEN__)
    emscripten_exit_with_live_runtime();
#endif

    return EXIT_SUCCESS;
}
