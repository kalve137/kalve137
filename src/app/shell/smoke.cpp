// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/shell/smoke.h"

#include "app/session/design_session.h"
#include "app/shell/main_window.h"
#include "app/theme/fonts.h"

#include "core/recompute.h"
#include "core/serialization.h"
#include "core/shape_store.h"
#include "geometry/exchange.h"
#include "log/log.h"
#include "viewer/occt_view_widget.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QImageReader>
#include <QStandardPaths>

#include <exception>
#include <format>
#include <fstream>
#include <string>
#include <vector>

namespace kalve137::app {

namespace {

constexpr int kRenderTimeoutMs = 20000;
constexpr int kEventSliceMs = 20;
constexpr int kWindowWidth = 1280;
constexpr int kWindowHeight = 800;

class Report {
public:
    void check(bool passed, const std::string& what)
    {
        m_lines.push_back(std::format("{} {}", passed ? "ok  " : "FAIL", what));
        if (passed) {
            K137_INFO(App, "Smoke check passed: {}", what);
        } else {
            K137_ERROR(App, "Smoke check failed: {}", what);
            m_passed = false;
        }
    }

    bool passed() const { return m_passed; }

    void write(const std::filesystem::path& path) const
    {
        std::ofstream out(path);
        for (const std::string& line : m_lines) {
            out << line << '\n';
        }
        out << (m_passed ? "PASSED" : "FAILED") << '\n';
    }

private:
    std::vector<std::string> m_lines;
    bool m_passed = true;
};

bool waitForFrame(viewer::OcctViewWidget* view)
{
    bool rendered = false;
    const QMetaObject::Connection connection =
        QObject::connect(view, &viewer::OcctViewWidget::rendered, [&rendered] { rendered = true; });
    view->update();
    QElapsedTimer timer;
    timer.start();
    while (!rendered && timer.elapsed() < kRenderTimeoutMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, kEventSliceMs);
    }
    QObject::disconnect(connection);
    return rendered;
}

std::string utf8(const std::filesystem::path& path)
{
    return QDir::toNativeSeparators(QString::fromStdU16String(path.u16string())).toStdString();
}

} // namespace

void prepareSmoke()
{
    QStandardPaths::setTestModeEnabled(true);
    for (const QStandardPaths::StandardLocation location :
         {QStandardPaths::AppLocalDataLocation, QStandardPaths::AppDataLocation}) {
        if (const QString folder = QStandardPaths::writableLocation(location); !folder.isEmpty()) {
            QDir(folder).removeRecursively();
        }
    }
}

int runSmoke(const std::filesystem::path& design, const std::filesystem::path& out)
{
    Report report;
    std::error_code error;
    std::filesystem::create_directories(out, error);

    report.check(!QGuiApplication::platformName().isEmpty(),
                 std::format("Platform plugin {}", QGuiApplication::platformName().toStdString()));
    report.check(QImageReader::supportedImageFormats().contains("svg"), "SVG image plugin");
    report.check(fonts::families().qt == QStringLiteral("Roboto"),
                 std::format("UI font {}", fonts::families().qt.toStdString()));

    MainWindow window;
    window.setAttribute(Qt::WA_DontShowOnScreen);
    window.resize(kWindowWidth, kWindowHeight);
    window.show();
    auto* view = window.findChild<viewer::OcctViewWidget*>();
    report.check(view != nullptr && waitForFrame(view), "OpenGL view rendered");

    bool loaded = false;
    try {
        core::ShapeStore shapes(core::shapeFolder(design));
        const core::Document document = core::loadDesign(design, shapes);
        loaded = !document.features().empty();
    } catch (const std::exception& failure) {
        report.check(false, std::format("Open {}: {}", utf8(design), failure.what()));
    }
    if (loaded) {
        window.openDesignAt(design);
        QCoreApplication::processEvents();
        const core::Model& model = window.findChild<DesignSession*>()->model();
        int failed = 0;
        for (const auto& [id, status] : model.status) {
            if (status.health == core::FeatureHealth::Failed) {
                ++failed;
            }
        }
        report.check(failed == 0 && !model.bodies.empty(),
                     std::format("Recomputed {} with {} bodies and {} failed features", utf8(design.filename()),
                                 model.bodies.size(), failed));
        report.check(view != nullptr && waitForFrame(view), "OpenGL view rendered the design");

        const std::filesystem::path step = out / "smoke.step";
        const std::filesystem::path stl = out / "smoke.stl";
        try {
            geometry::writeStep(model.stepParts(), step, std::string("Kalve 137 ") + KALVE137_VERSION);
            geometry::writeStl(model.shapes(), stl);
            const std::size_t readBack = geometry::readStepParts(step).size();
            report.check(readBack == model.bodies.size(), std::format("STEP export read back {} bodies", readBack));
            report.check(std::filesystem::file_size(stl, error) > 0, "STL export");
        } catch (const std::exception& failure) {
            report.check(false, std::format("Export: {}", failure.what()));
        }
    }

    report.write(out / "smoke.txt");
    return report.passed() ? 0 : 1;
}

} // namespace kalve137::app
