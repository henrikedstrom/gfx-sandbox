// Class Header
#include "Application.h"

// Standard Library Headers
#include <algorithm>
#include <cassert>
#include <cctype>

// Third-Party Library Headers
#include <GLFW/glfw3.h>
#if defined(__EMSCRIPTEN__)
#include <emscripten/emscripten.h>
#endif

Application* Application::s_instance = nullptr;

// ----------------------------------------------------------------------
// Emscripten drop-file plumbing

#if defined(__EMSCRIPTEN__)
extern "C" void wasm_OnDropFile(const char* filename, uint8_t* data, int length) {
    std::string filenameStr(filename ? filename : "");
    Application* app = Application::GetInstance();
    if (!app) {
        return;
    }
    app->DispatchFileDropped(filenameStr, data, length);
}

static void EmscriptenSetDropCallback() {
    // clang-format off
    EM_ASM(
        const showErrorPopup = (message) => {
            const popup = document.createElement('div');
            popup.innerText = message;
            popup.style.position = 'fixed';
            popup.style.top = '50%';
            popup.style.left = '50%';
            popup.style.transform = 'translate(-50%, -50%)';
            popup.style.backgroundColor = 'rgba(255, 0, 0, 0.8)';
            popup.style.color = 'white';
            popup.style.padding = '15px 25px';
            popup.style.borderRadius = '8px';
            popup.style.fontSize = '18px';
            popup.style.fontWeight = 'bold';
            popup.style.zIndex = '1000';
            document.body.appendChild(popup);
            setTimeout(() => { popup.remove(); }, 3000);
            console.error('ERROR: ' + message);
        };

        const canvas = document.getElementById('canvas');
        if (!canvas) {
            console.error('ERROR: Canvas element with id=\"canvas\" not found.');
            return;
        }

        canvas.ondragover = (event) => { event.preventDefault(); };

        canvas.ondrop = async (event) => {
            event.preventDefault();
            const file = event.dataTransfer.files[0];
            if (!file) { return; }

            const reader = new FileReader();
            reader.onload = (e) => {
                const data = new Uint8Array(e.target.result);
                const dataPtr = Module._malloc(data.length);
                if (!dataPtr) {
                    showErrorPopup('Memory allocation failed for file data!');
                    return;
                }
                Module.HEAPU8.set(data, dataPtr);

                const nameLength = Module.lengthBytesUTF8(file.name) + 1;
                const filenamePtr = Module._malloc(nameLength);
                if (!filenamePtr) {
                    showErrorPopup('Memory allocation failed for filename!');
                    Module._free(dataPtr);
                    return;
                }
                Module.stringToUTF8(file.name, filenamePtr, nameLength);

                Module.ccall(
                    'wasm_OnDropFile',
                    'void',
                    ['number', 'number', 'number'],
                    [filenamePtr, dataPtr, data.length]
                );

                Module._free(dataPtr);
                Module._free(filenamePtr);
            };
            reader.readAsArrayBuffer(file);
        };
    );
    // clang-format on
}
#endif

// ----------------------------------------------------------------------
// GLFW callbacks

// ----------------------------------------------------------------------
// Application

Application* Application::GetInstance() {
    return s_instance;
}

void Application::DispatchFileDropped(const std::string& filename, uint8_t* data, int length) {
    OnFileDropped(filename, data, length);
}

Application::Application(uint32_t windowWidth, uint32_t windowHeight, const char* title) :
    _initialWindowWidth(windowWidth), _initialWindowHeight(windowHeight), _title(title) {
    assert(!s_instance);
    s_instance = this;
}

Application::~Application() {
    if (_window) {
        glfwDestroyWindow(_window);
    }
    glfwTerminate();
    s_instance = nullptr;
}

void Application::RequestQuit() noexcept {
    _quitApp = true;
}

void Application::Run() {
    if (!glfwInit()) {
        return;
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    _window = glfwCreateWindow(static_cast<int>(_initialWindowWidth),
                               static_cast<int>(_initialWindowHeight), _title, nullptr, nullptr);
    if (!_window) {
        glfwTerminate();
        return;
    }

    // Query the actual framebuffer size (handles HiDPI/Retina displays)
    int fbWidth = 0;
    int fbHeight = 0;
    glfwGetFramebufferSize(_window, &fbWidth, &fbHeight);
    _framebufferWidth = static_cast<uint32_t>(fbWidth);
    _framebufferHeight = static_cast<uint32_t>(fbHeight);

    glfwSetKeyCallback(_window, []([[maybe_unused]] GLFWwindow* window, int key,
                                   [[maybe_unused]] int scancode, int action, int mods) {
        static bool keyState[GLFW_KEY_LAST] = {false};

        if (key >= 0 && key < GLFW_KEY_LAST) {
            bool keyPressed = action == GLFW_PRESS && !keyState[key];

            if (action == GLFW_PRESS) {
                keyState[key] = true;
            } else if (action == GLFW_RELEASE) {
                keyState[key] = false;
            }

            if (keyPressed) {
                Application* app = Application::GetInstance();
                if (app) {
                    app->OnKeyPressed(key, mods);
                }
            }
        }
    });

    glfwSetFramebufferSizeCallback(_window,
                                   []([[maybe_unused]] GLFWwindow* window, int width, int height) {
                                       Application* app = Application::GetInstance();
                                       if (!app) {
                                           return;
                                       }
                                       // Clamp non-positive sizes before casting to unsigned.
                                       const int clampedWidth = (width > 0) ? width : 0;
                                       const int clampedHeight = (height > 0) ? height : 0;

                                       app->_framebufferWidth = static_cast<uint32_t>(clampedWidth);
                                       app->_framebufferHeight = static_cast<uint32_t>(clampedHeight);
                                       app->OnResize(clampedWidth, clampedHeight);
                                   });

#if defined(__EMSCRIPTEN__)
    EmscriptenSetDropCallback();
#else
    glfwSetDropCallback(_window,
                        []([[maybe_unused]] GLFWwindow* window, int count, const char** paths) {
                            if (count <= 0 || !paths || !paths[0]) {
                                return;
                            }
                            Application* app = Application::GetInstance();
                            if (!app) {
                                return;
                            }
                            app->DispatchFileDropped(paths[0]);
                        });
#endif

    OnInit();
    MainLoop();
}

void Application::MainLoop() {
#if defined(__EMSCRIPTEN__)
    emscripten_set_main_loop_arg([](void* arg) { static_cast<Application*>(arg)->ProcessFrame(); },
                                 this, 0, false);
#else
    while (!glfwWindowShouldClose(_window) && !_quitApp) {
        glfwPollEvents();
        ProcessFrame();
    }
#endif
}

void Application::ProcessFrame() {
    auto currentTime = std::chrono::high_resolution_clock::now();
    float deltaTimeMs = 16.67f;

    if (_hasLastTime) {
        auto delta = std::chrono::duration_cast<std::chrono::microseconds>(currentTime - _lastTime);
        deltaTimeMs = static_cast<float>(delta.count()) / 1000.0f;
        if (deltaTimeMs <= 0.0f || deltaTimeMs > 100.0f) {
            deltaTimeMs = 16.67f;
        }
    }
    _lastTime = currentTime;
    _hasLastTime = true;

    OnFrame(deltaTimeMs * 0.001f);
}
