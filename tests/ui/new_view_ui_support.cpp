// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "new_view_ui_support.h"

#include "app/shell/main_window.h"
#include "app/view/camera_commands.h"
#include "camera/camera.h"
#include "render/chunk_set.h"
#include "render/cpu_backend.h"
#include "render/draw_list.h"
#include "render/rhi/rhi_view.h"
#include "viewer/occt_view_widget.h"

#include <QApplication>
#include <QColor>
#include <QImage>
#include <QMouseEvent>
#include <QTest>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>

namespace kalve137::uitest {

namespace {

constexpr int kTransitionWaitMs = 3000;
constexpr double kMatrixTolerance = 1e-9;
constexpr double kDirectionTolerance = 1e-9;

} // namespace

void waitForCamera(app::CameraCommands& commands)
{
    EXPECT_TRUE(QTest::qWaitFor([&] { return !commands.isAnimating(); }, kTransitionWaitMs));
}

void expectSameMatrix(const camera::Mat4& ours, const camera::Mat4& theirs, const std::string& what)
{
    for (int row = 0; row < 4; ++row) {
        for (int column = 0; column < 4; ++column) {
            const double expected = theirs.at(row, column);
            EXPECT_NEAR(ours.at(row, column), expected, kMatrixTolerance * std::max(1.0, std::abs(expected)))
                << what << " at " << row << "," << column;
        }
    }
}

void expectFollowed(UiDriver& ui, const std::string& step)
{
    const camera::Camera& leading = ui.window().cameraCommands()->camera();
    const camera::Camera hidden = ui.window().oldView()->currentCamera();
    EXPECT_EQ(hidden.isOrthographic(), leading.isOrthographic()) << step;
    expectSameMatrix(hidden.view(), leading.view(), step + " hidden view");
    expectSameMatrix(hidden.projection(), leading.projection(), step + " hidden projection");
    expectSameMatrix(ui.newView()->camera().view(), leading.view(), step + " new view");
    expectSameMatrix(ui.newView()->camera().projection(), leading.projection(), step + " new projection");
}

void expectDirection(const camera::Camera& camera, const camera::Vec3& direction, const std::string& step)
{
    EXPECT_LT(camera::distance(camera.direction(), camera::normalized(direction)), kDirectionTolerance) << step;
}

void sendMouse(render::RhiView& canvas, QEvent::Type type, QPointF pos, Qt::MouseButton button,
               Qt::MouseButtons buttons)
{
    QMouseEvent event(type, pos, canvas.mapToGlobal(pos), button, buttons, Qt::NoModifier);
    QApplication::sendEvent(&canvas, &event);
}

bool sameColor(const QColor& a, const QColor& b)
{
    return std::abs(a.red() - b.red()) <= kColorTolerance && std::abs(a.green() - b.green()) <= kColorTolerance &&
           std::abs(a.blue() - b.blue()) <= kColorTolerance;
}

render::Image cpuOf(const render::RhiView& canvas)
{
    render::ChunkSet chunks;
    chunks.update(canvas.scene(), canvas.viewportStyle());
    chunks.setHighlight(canvas.chunks().highlight());
    render::DrawList list = chunks.draw(canvas.frame());
    list.grid = canvas.grid().draw(canvas.camera(), canvas.viewport());
    list.datums = canvas.datumDraw();
    render::CpuBackend backend;
    render::Image cpu;
    backend.draw(list, 2, cpu);
    return cpu;
}

Comparison compare(const QImage& gpu, const render::Image& cpu)
{
    Comparison result;
    result.sameSize = gpu.width() == cpu.width && gpu.height() == cpu.height;
    if (!result.sameSize) {
        return result;
    }
    const QRgb background = cpu.at(0, 0);
    for (int y = 0; y < cpu.height; ++y) {
        const auto* line = reinterpret_cast<const QRgb*>(gpu.constScanLine(y));
        for (int x = 0; x < cpu.width; ++x) {
            const QRgb ours = cpu.at(x, y);
            const QRgb theirs = line[x];
            result.body += ours != background ? 1 : 0;
            const int gap = std::max({std::abs(qRed(ours) - qRed(theirs)), std::abs(qGreen(ours) - qGreen(theirs)),
                                      std::abs(qBlue(ours) - qBlue(theirs))});
            result.different += gap > kBackendTolerance ? 1 : 0;
        }
    }
    return result;
}

QImage grabbed(render::RhiView& canvas)
{
    return canvas.grabFramebuffer().convertToFormat(QImage::Format_ARGB32);
}

void expectShown(render::RhiView& canvas, const QImage& before, const std::string& step)
{
    const QImage gpu = grabbed(canvas);
    ASSERT_FALSE(gpu.isNull()) << step;
    const render::Image cpu = cpuOf(canvas);
    const Comparison comparison = compare(gpu, cpu);
    ASSERT_TRUE(comparison.sameSize) << step << ": " << gpu.width() << "x" << gpu.height() << " against " << cpu.width
                                     << "x" << cpu.height;
    const std::size_t pixels = static_cast<std::size_t>(cpu.width) * static_cast<std::size_t>(cpu.height);
    EXPECT_GT(comparison.body, pixels / 50) << step;
    EXPECT_LT(comparison.different, pixels / 100) << step << ": " << comparison.different << " pixels differ";
    if (!before.isNull() && before.size() == gpu.size()) {
        EXPECT_NE(before, gpu) << step << " changed nothing on screen";
    }
}

std::size_t differing(const QImage& a, const QImage& b)
{
    if (a.size() != b.size()) {
        return static_cast<std::size_t>(std::max(a.width() * a.height(), b.width() * b.height()));
    }
    std::size_t count = 0;
    for (int y = 0; y < a.height(); ++y) {
        const auto* first = reinterpret_cast<const QRgb*>(a.constScanLine(y));
        const auto* second = reinterpret_cast<const QRgb*>(b.constScanLine(y));
        for (int x = 0; x < a.width(); ++x) {
            const int gap =
                std::max({std::abs(qRed(first[x]) - qRed(second[x])), std::abs(qGreen(first[x]) - qGreen(second[x])),
                          std::abs(qBlue(first[x]) - qBlue(second[x]))});
            count += gap > kColorTolerance ? 1 : 0;
        }
    }
    return count;
}

camera::Camera obliqueAt(const snapshot::Point& center, double scale)
{
    camera::Camera camera;
    EXPECT_TRUE(camera.setEyeAndCenter({center[0] + 300.0, center[1] - 200.0, center[2] + 250.0},
                                       {center[0], center[1], center[2]}));
    EXPECT_TRUE(camera.setUp({0.0, 0.0, 1.0}));
    EXPECT_TRUE(camera.setScale(scale));
    return camera;
}

} // namespace kalve137::uitest
