// Class Header
#include "BackendRegistry.h"

// Standard Library Headers
#include <exception>

// Project Headers
#include "IRenderer.h"
#include "logging/Log.h"

BackendRegistry& BackendRegistry::Instance() {
    static BackendRegistry instance;
    return instance;
}

bool BackendRegistry::Register(const std::string& name, FactoryFunc factory) {
    if (_factories.contains(name)) {
        Log::Warning(Log::Renderer, "Backend '{}' already registered", name);
        return false;
    }

    _factories[name] = std::move(factory);
    Log::Info(Log::Renderer, "Registered backend: {}", name);
    return true;
}

std::unique_ptr<IRenderer> BackendRegistry::Create(const std::string& name,
                                                   GLFWwindow* window) const {
    // Check if any backends are registered
    if (_factories.empty()) {
        Log::Error(Log::Renderer, "No backends registered");
        return nullptr;
    }

    // Use the provided name, or fall back to the default backend
    std::string backendName = name.empty() ? _defaultBackend : name;

    if (backendName.empty()) {
        Log::Error(Log::Renderer, "No backend specified and no default configured");
        return nullptr;
    }

    // Helper lambda to attempt creating a backend.
    auto tryCreate = [this, window](const std::string& backendName) -> std::unique_ptr<IRenderer> {
        auto it = _factories.find(backendName);
        if (it == _factories.end()) {
            return nullptr;
        }

        Log::Info(Log::Renderer, "Creating backend: {}", backendName);
        try {
            return it->second(window);
        } catch (const std::exception& e) {
            Log::Error(Log::Renderer, "Failed to create '{}': {}", backendName, e.what());
            return nullptr;
        }
    };

    // 1. Try the requested backend.
    if (auto renderer = tryCreate(backendName)) {
        return renderer;
    }

    // 2. Try the default backend (if different from requested).
    if (backendName != _defaultBackend && !_defaultBackend.empty()) {
        Log::Info(Log::Renderer, "Falling back to default backend: {}", _defaultBackend);
        if (auto renderer = tryCreate(_defaultBackend)) {
            return renderer;
        }
    }

    // 3. Try all remaining backends.
    for (const auto& [fallbackName, _] : _factories) {
        if (fallbackName == backendName || fallbackName == _defaultBackend) {
            continue; // Already tried these
        }

        Log::Info(Log::Renderer, "Trying fallback backend: {}", fallbackName);
        if (auto renderer = tryCreate(fallbackName)) {
            return renderer;
        }
    }

    Log::Error(Log::Renderer, "All backends failed to initialize");
    return nullptr;
}

std::vector<std::string> BackendRegistry::GetAvailableBackends() const {
    std::vector<std::string> names;
    names.reserve(_factories.size());
    for (const auto& [name, _] : _factories) {
        names.push_back(name);
    }
    return names;
}

std::string BackendRegistry::GetDefaultBackend() const {
    return _defaultBackend;
}
