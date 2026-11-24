// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "new_view_ui_support.h"

#include "app/shell/main_window.h"
#include "app/theme/theme.h"
#include "render/graphics_api.h"
#include "render/rhi/rhi_view.h"

#include <QLabel>
#include <QMenuBar>
#include <QTest>

namespace kalve137::uitest {
namespace {

constexpr QSize kWindowSize{800, 600};

#if defined(_WIN32)
constexpr render::GraphicsApi kUnavailableApi = render::GraphicsApi::Metal;
#else
constexpr render::GraphicsApi kUnavailableApi = render::GraphicsApi::Direct3D11;
#endif

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

TEST_F(NewViewTest, AnOpenGlBelowThreePointTwoShowsTheRequirementOnce)
{
    const render::DeviceProbe old{
        .created = true, .texelFetch = true, .openGl = true, .openGlEs = false, .major = 3, .minor = 1};
    const render::StartProblem problem = render::startProblem(old);
    ASSERT_EQ(problem, render::StartProblem::TooOld);
    QWidget host;
    host.setAttribute(Qt::WA_DontShowOnScreen);
    host.resize(kWindowSize);
    QLabel* notice = render::makeFailureNotice(render::GraphicsApi::OpenGL, problem, &host);
    host.show();
    QTest::qWait(kSettleMs);
    const QList<QLabel*> messages = host.findChildren<QLabel*>(QStringLiteral("viewFailure"));
    ASSERT_EQ(messages.size(), 1);
    EXPECT_EQ(messages.front(), notice);
    EXPECT_TRUE(notice->text().contains(QStringLiteral("OpenGL 3.2 or OpenGL ES 3.0"))) << notice->text().toStdString();
    EXPECT_TRUE(notice->text().contains(QStringLiteral("--view new")));
}

} // namespace
} // namespace kalve137::uitest
