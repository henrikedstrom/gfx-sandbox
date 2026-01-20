/// @file  Log.h
/// @brief Main logging header - includes all logging functionality.
///
// Usage:
//
// Built-in categories:
//   Log::Info(Log::Application, "Application started");
//   Log::Warning(Log::Scene, "Failed to load model: {}", path);
//   Log::Error(Log::Renderer, "Backend initialization failed");
//
// Adding custom categories:
//   LOG_CATEGORY(Vulkan);  // Define at file scope in your header/implementation
//   Log::Info(Log::Vulkan, "Device: {}", deviceName);
//   Log::Debug(Log::Vulkan, "Extension count: {}", count);

#pragma once

// Standard Library Headers
#include <format>
#include <source_location>
#include <string_view>
#include <utility>

// Project Headers
#include "LogLevel.h"
#include "LogManager.h"
#include "LogMessage.h"

namespace Log {

//----------------------------------------------------------------------
// Log level structs with CTAD
//
// CTAD enables clean syntax (Log::Info(category, "msg", args)) while
// correctly capturing std::source_location. Required because default
// arguments cannot follow parameter packs.

// Trace
template <typename Category, typename... Args>
struct Trace {
    Trace(Category, std::format_string<Args...> fmt, Args&&... args,
          std::source_location loc = std::source_location::current()) {
        if constexpr (Category::kCompileLevel <= Level::Trace) {
            if (LogManager::Instance().ShouldLog(Category::kName, Level::Trace)) {
                LogManager::Instance().Log(Level::Trace, Category::kName, loc,
                                           std::format(fmt, std::forward<Args>(args)...));
            }
        }
    }
};
template <typename Category, typename... Args>
Trace(Category, std::format_string<Args...>, Args&&...) -> Trace<Category, Args...>;

// Debug
template <typename Category, typename... Args>
struct Debug {
    Debug(Category, std::format_string<Args...> fmt, Args&&... args,
          std::source_location loc = std::source_location::current()) {
        if constexpr (Category::kCompileLevel <= Level::Debug) {
            if (LogManager::Instance().ShouldLog(Category::kName, Level::Debug)) {
                LogManager::Instance().Log(Level::Debug, Category::kName, loc,
                                           std::format(fmt, std::forward<Args>(args)...));
            }
        }
    }
};
template <typename Category, typename... Args>
Debug(Category, std::format_string<Args...>, Args&&...) -> Debug<Category, Args...>;

// Info
template <typename Category, typename... Args>
struct Info {
    Info(Category, std::format_string<Args...> fmt, Args&&... args,
         std::source_location loc = std::source_location::current()) {
        if constexpr (Category::kCompileLevel <= Level::Info) {
            if (LogManager::Instance().ShouldLog(Category::kName, Level::Info)) {
                LogManager::Instance().Log(Level::Info, Category::kName, loc,
                                           std::format(fmt, std::forward<Args>(args)...));
            }
        }
    }
};
template <typename Category, typename... Args>
Info(Category, std::format_string<Args...>, Args&&...) -> Info<Category, Args...>;

// Warning
template <typename Category, typename... Args>
struct Warning {
    Warning(Category, std::format_string<Args...> fmt, Args&&... args,
            std::source_location loc = std::source_location::current()) {
        if constexpr (Category::kCompileLevel <= Level::Warning) {
            if (LogManager::Instance().ShouldLog(Category::kName, Level::Warning)) {
                LogManager::Instance().Log(Level::Warning, Category::kName, loc,
                                           std::format(fmt, std::forward<Args>(args)...));
            }
        }
    }
};
template <typename Category, typename... Args>
Warning(Category, std::format_string<Args...>, Args&&...) -> Warning<Category, Args...>;

// Error
template <typename Category, typename... Args>
struct Error {
    Error(Category, std::format_string<Args...> fmt, Args&&... args,
          std::source_location loc = std::source_location::current()) {
        if constexpr (Category::kCompileLevel <= Level::Error) {
            if (LogManager::Instance().ShouldLog(Category::kName, Level::Error)) {
                LogManager::Instance().Log(Level::Error, Category::kName, loc,
                                           std::format(fmt, std::forward<Args>(args)...));
            }
        }
    }
};
template <typename Category, typename... Args>
Error(Category, std::format_string<Args...>, Args&&...) -> Error<Category, Args...>;

// Fatal
template <typename Category, typename... Args>
struct Fatal {
    Fatal(Category, std::format_string<Args...> fmt, Args&&... args,
          std::source_location loc = std::source_location::current()) {
        if constexpr (Category::kCompileLevel <= Level::Fatal) {
            if (LogManager::Instance().ShouldLog(Category::kName, Level::Fatal)) {
                LogManager::Instance().Log(Level::Fatal, Category::kName, loc,
                                           std::format(fmt, std::forward<Args>(args)...));
            }
        }
    }
};
template <typename Category, typename... Args>
Fatal(Category, std::format_string<Args...>, Args&&...) -> Fatal<Category, Args...>;

} // namespace Log

//----------------------------------------------------------------------
// Category registration macros

// Define a log category with custom default and compile-time levels.
//
// kDefaultLevel: Runtime verbosity level used when no explicit level is set.
// kCompileLevel: Compile-time filter - log calls below this level are eliminated
//                at compile-time (zero runtime cost).
//
// Usage:
//   LOG_CATEGORY_LEVELS(Scene, ::Log::Level::Info, ::Log::Level::Debug);
#define LOG_CATEGORY_LEVELS(Name, Default, Compile)                                                \
    namespace Log {                                                                                \
    struct Log##Name {                                                                             \
        static constexpr std::string_view kName = #Name;                                           \
        static constexpr ::Log::Level kDefaultLevel = Default;                                     \
        static constexpr ::Log::Level kCompileLevel = Compile;                                     \
    };                                                                                             \
    inline constexpr Log##Name Name;                                                               \
    namespace {                                                                                     \
    struct Log##Name##Registrar {                                                                  \
        Log##Name##Registrar() {                                                                   \
            LogManager::Instance().RegisterCategory(Log##Name::kName, Log##Name::kDefaultLevel);   \
        }                                                                                          \
    };                                                                                             \
    inline Log##Name##Registrar g_##Name##Registrar;                                               \
    }                                                                                              \
    }

// Define a log category with default levels (Info, Trace).
// Creates a struct and constexpr instance in the Log:: namespace.
//
// Usage:
//   LOG_CATEGORY(Vulkan);
//   Log::Info(Log::Vulkan, "Message");
#define LOG_CATEGORY(Name) LOG_CATEGORY_LEVELS(Name, ::Log::Level::Info, ::Log::Level::Trace)

//----------------------------------------------------------------------
// Built-in categories

LOG_CATEGORY(Core);
LOG_CATEGORY(Application);
LOG_CATEGORY(Scene);
LOG_CATEGORY(Renderer);
