// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_test.h"

#include "app/shell/main_window.h"
#include "app/theme/theme.h"
#include "render/graphics_api.h"
#include "render/rhi/rhi_view.h"

#include <QColor>
#include <QImage>
#include <QLabel>
#include <QMenuBar>
#include <QTest>

#include <cstdlib>

namespace kalve137::uitest {
namespace {

using NewViewTest = UiTest;

constexpr int kColorTolerance = 2;
constexpr int kSettleMs = 200;
constexpr QSize kWindowSize{800, 600};

#if defined(_WIN32)
constexpr render::GraphicsApi kUnavailableApi = render::GraphicsApi::Metal;
#else
constexpr render::GraphicsApi kUnavailableApi = render::GraphicsApi::Direct3D11;
#endif

bool near(const QColor& a, const QColor& b)
{
    return std::abs(a.red() - b.red()) <= kColorTolerance && std::abs(a.green() - b.green()) <= kColorTolerance &&
           std::abs(a.blue() - b.blue()) <= kColorTolerance;
}

TEST_F(NewViewTest, ClearsToTheViewportColor)
{
    render::RhiView* canvas = ui->newView();
    if (canvas == nullptr) {
        GTEST_SKIP() << "Runs with KALVE137_VIEW=new";
    }
    const QImage image = canvas->grabFramebuffer();
    ASSERT_FALSE(image.isNull());
    const QColor expected = app::theme::palette().viewport.canvas;
    for (const QPoint at : {QPoint(1, 1), image.rect().center(), QPoint(image.width() - 2, image.height() - 2)}) {
        const QColor seen = image.pixelColor(at);
        EXPECT_TRUE(near(seen, expected)) << "Pixel " << at.x() << "," << at.y() << " is " << seen.name().toStdString()
                                          << " instead of " << expected.name().toStdString();
    }
    ui->save("canvas");
}

TEST_F(NewViewTest, AnUnavailableApiShowsOneMessageInAPaintedWindow)
{
    allowProblem("graphics device");
    EXPECT_FALSE(render::canStart(kUnavailableApi));
    app::MainWindow window({.view = app::ViewKind::New, .graphicsApi = kUnavailableApi});
    window.setAttribute(Qt::WA_DontShowOnScreen);
    window.resize(kWindowSize);
    window.show();
    QTest::qWait(kSettleMs);
    EXPECT_EQ(window.newView(), nullptr);
    const QList<QLabel*> messages = window.findChildren<QLabel*>(QStringLiteral("viewFailure"));
    ASSERT_EQ(messages.size(), 1);
    EXPECT_TRUE(messages.front()->isVisible());
    EXPECT_EQ(window.centralWidget(), messages.front());
    const QString api = QString::fromUtf8(render::graphicsApiName(kUnavailableApi).data());
    EXPECT_TRUE(messages.front()->text().contains(api)) << messages.front()->text().toStdString();
    EXPECT_FALSE(window.menuBar()->grab().isNull());
}

} // namespace
} // namespace kalve137::uitest
