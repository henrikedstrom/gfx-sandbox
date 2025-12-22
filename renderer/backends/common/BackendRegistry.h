#pragma once

// Standard Library Headers
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

// Forward Declarations
class IRenderer;

// BackendRegistry Class
class BackendRegistry {
  public:
    using FactoryFunc = std::function<std::unique_ptr<IRenderer>()>;

    static BackendRegistry& Instance();

    bool Register(const std::string& name, FactoryFunc factory);
    std::unique_ptr<IRenderer> Create(const std::string& name = "") const;
    std::vector<std::string> GetAvailableBackends() const;
    std::string GetDefaultBackend() const;

    // Non-copyable
    BackendRegistry(const BackendRegistry&) = delete;
    BackendRegistry& operator=(const BackendRegistry&) = delete;

  private:
    BackendRegistry() = default;

    std::map<std::string, FactoryFunc> _factories;
    std::string _defaultBackend{"webgpu"}; // Make webgpu the default backend for now
};
