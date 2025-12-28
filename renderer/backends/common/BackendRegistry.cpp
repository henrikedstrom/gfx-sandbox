// Class Header
#include "BackendRegistry.h"

// Standard Library Headers
#include <exception>
#include <iostream>

// Project Headers
#include "IRenderer.h"

BackendRegistry& BackendRegistry::Instance() {
    static BackendRegistry instance;
    return instance;
}

bool BackendRegistry::Register(const std::string& name, FactoryFunc factory) {
    if (_factories.contains(name)) {
        std::cerr << "[BackendRegistry] Backend '" << name << "' already registered." << std::endl;
        return false;
    }

    _factories[name] = std::move(factory);
    std::cout << "[BackendRegistry] Registered backend: " << name << std::endl;
    return true;
}

std::unique_ptr<IRenderer> BackendRegistry::Create(const std::string& name,
                                                    GLFWwindow* window) const {
    // Check if any backends are registered
    if (_factories.empty()) {
        std::cerr << "[BackendRegistry] No backends registered." << std::endl;
        return nullptr;
    }

    // Use the provided name, or fall back to the default backend
    std::string backendName = name.empty() ? _defaultBackend : name;

    if (backendName.empty()) {
        std::cerr << "[BackendRegistry] No backend specified and no default configured." << std::endl;
        return nullptr;
    }

    // Helper lambda to attempt creating a backend.
    auto tryCreate = [this, window](const std::string& backendName) -> std::unique_ptr<IRenderer> {
        auto it = _factories.find(backendName);
        if (it == _factories.end()) {
            return nullptr;
        }

        std::cout << "[BackendRegistry] Creating backend: " << backendName << std::endl;
        try {
            return it->second(window);
        } catch (const std::exception& e) {
            std::cerr << "[BackendRegistry] Failed to create '" << backendName << "': " << e.what()
                      << std::endl;
            return nullptr;
        }
    };

    // 1. Try the requested backend.
    if (auto renderer = tryCreate(backendName)) {
        return renderer;
    }

    // 2. Try the default backend (if different from requested).
    if (backendName != _defaultBackend && !_defaultBackend.empty()) {
        std::cout << "[BackendRegistry] Falling back to default backend: " << _defaultBackend
                  << std::endl;
        if (auto renderer = tryCreate(_defaultBackend)) {
            return renderer;
        }
    }

    // 3. Try all remaining backends.
    for (const auto& [fallbackName, _] : _factories) {
        if (fallbackName == backendName || fallbackName == _defaultBackend) {
            continue; // Already tried these
        }

        std::cout << "[BackendRegistry] Trying fallback backend: " << fallbackName << std::endl;
        if (auto renderer = tryCreate(fallbackName)) {
            return renderer;
        }
    }

    std::cerr << "[BackendRegistry] All backends failed to initialize." << std::endl;
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
