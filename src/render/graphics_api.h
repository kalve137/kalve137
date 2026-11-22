// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <optional>
#include <string_view>
#include <utility>

namespace kalve137::render {

enum class GraphicsApi { Direct3D11, Direct3D12, Metal, OpenGL, Vulkan };

inline constexpr std::array kGraphicsApiKeys{
    std::pair{std::string_view("d3d11"), GraphicsApi::Direct3D11},
    std::pair{std::string_view("d3d12"), GraphicsApi::Direct3D12},
    std::pair{std::string_view("metal"), GraphicsApi::Metal},
    std::pair{std::string_view("opengl"), GraphicsApi::OpenGL},
    std::pair{std::string_view("vulkan"), GraphicsApi::Vulkan},
};

constexpr GraphicsApi platformGraphicsApi()
{
#if defined(_WIN32)
    return GraphicsApi::Direct3D11;
#elif defined(__APPLE__)
    return GraphicsApi::Metal;
#else
    return GraphicsApi::OpenGL;
#endif
}

constexpr std::optional<GraphicsApi> graphicsApiNamed(std::string_view key)
{
    for (const auto& [name, api] : kGraphicsApiKeys) {
        if (name == key) {
            return api;
        }
    }
    return std::nullopt;
}

constexpr std::string_view graphicsApiName(GraphicsApi api)
{
    switch (api) {
    case GraphicsApi::Direct3D11:
        return "Direct3D 11";
    case GraphicsApi::Direct3D12:
        return "Direct3D 12";
    case GraphicsApi::Metal:
        return "Metal";
    case GraphicsApi::OpenGL:
        return "OpenGL";
    case GraphicsApi::Vulkan:
        return "Vulkan";
    }
    return "an unknown graphics API";
}

} // namespace kalve137::render
