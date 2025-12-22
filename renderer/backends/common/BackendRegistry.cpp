// Class Header
#include "BackendRegistry.h"

// Standard Library Headers
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

std::unique_ptr<IRenderer> BackendRegistry::Create(const std::string& name) const {
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

    // Look up the requested backend
    auto it = _factories.find(backendName);
    if (it == _factories.end()) {
        std::cerr << "[BackendRegistry] Backend '" << backendName << "' not found. Available: ";
        for (const auto& [n, _] : _factories) {
            std::cerr << n << " ";
        }
        std::cerr << std::endl;
        return nullptr;
    }

    std::cout << "[BackendRegistry] Creating backend: " << backendName << std::endl;
    return it->second();
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
