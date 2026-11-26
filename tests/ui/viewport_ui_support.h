// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ui_test.h"

#include "app/view/camera_commands.h"
#include "camera/camera.h"
#include "render/cpu_backend.h"
#include "render/graphics_api.h"
#include "render/rhi/viewport.h"
#include "snapshot/mesh.h"

#include <QColor>
#include <QEvent>
#include <QImage>
#include <QPointF>

#include <array>
#include <cstddef>
#include <string>

namespace kalve137::uitest {

inline constexpr int kColorTolerance = 2;
inline constexpr int kBackendTolerance = 24;
inline constexpr int kSettleMs = 200;

#if defined(_WIN32)
inline constexpr std::array kOtherApis{render::GraphicsApi::Direct3D12, render::GraphicsApi::OpenGL};
#else
inline constexpr std::array<render::GraphicsApi, 0> kOtherApis{};
#endif

struct Comparison {
    bool sameSize = false;
    std::size_t body = 0;
    std::size_t different = 0;
};

class GridHidden {
public:
    explicit GridHidden(render::Viewport& canvas) : m_canvas(canvas), m_original(canvas.grid().settings())
    {
        render::GridSettings hidden = m_original;
        hidden.visible = false;
        m_canvas.setGrid(hidden);
    }
    GridHidden(const GridHidden&) = delete;
    GridHidden& operator=(const GridHidden&) = delete;
    ~GridHidden() { m_canvas.setGrid(m_original); }

private:
    render::Viewport& m_canvas;
    render::GridSettings m_original;
};

void waitForCamera(app::CameraCommands& commands);
void expectSameMatrix(const camera::Mat4& ours, const camera::Mat4& theirs, const std::string& what);
void expectFollowed(UiDriver& ui, const std::string& step);
void expectDirection(const camera::Camera& camera, const camera::Vec3& direction, const std::string& step);
void sendMouse(render::Viewport& canvas, QEvent::Type type, QPointF pos, Qt::MouseButton button,
               Qt::MouseButtons buttons);
bool sameColor(const QColor& a, const QColor& b);
render::Image cpuOf(const render::Viewport& canvas);
Comparison compare(const QImage& gpu, const render::Image& cpu);
QImage grabbed(render::Viewport& canvas);
void expectShown(render::Viewport& canvas, const QImage& before, const std::string& step);
std::size_t differing(const QImage& a, const QImage& b);
camera::Camera obliqueAt(const snapshot::Point& center, double scale);

using ViewportTest = UiTest;

} // namespace kalve137::uitest
