// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/session/design_session.h"
#include "app/shell/main_window.h"
#include "app/shell/startup_options.h"
#include "app/theme/theme.h"
#include "camera/camera.h"
#include "core/document.h"
#include "core/feature.h"
#include "geometry/shapes.h"
#include "pair_shots.h"
#include "render/rhi/rhi_view.h"
#include "scenario.h"
#include "snapshot/body.h"
#include "ui_driver.h"
#include "viewer/occt_view_widget.h"

#include <QColor>
#include <QImage>
#include <QString>

#include <gp_Pnt.hxx>
#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace kalve137::uitest {
namespace {

constexpr int kFaceTolerance = 15;
constexpr int kBackgroundTolerance = 2;
constexpr int kInset = 2;
constexpr double kMinFacing = 0.15;
constexpr double kMinFacePixels = 400.0;
constexpr std::array kSampleSteps{0.2, 0.4, 0.6, 0.8};

struct Case {
    std::string name;
    std::function<void(app::MainWindow&)> load;
    bool measureFaces = true;
};

struct FaceSamples {
    std::vector<QPoint> pixels;
    double area = 0.0;
};

std::optional<FaceSamples> visibleFace(const snapshot::Mesh& mesh, const std::vector<std::size_t>& triangles,
                                       const Shots& shots)
{
    const camera::Vec3 toEye = -shots.camera.direction();
    camera::Vec3 normal{};
    camera::Vec3 center{};
    double area = 0.0;
    for (const std::size_t i : triangles) {
        const snapshot::Triangle& t = mesh.triangles[i];
        const camera::Vec3 a = toVec(mesh.vertices[t[0]]);
        const camera::Vec3 b = toVec(mesh.vertices[t[1]]);
        const camera::Vec3 c = toVec(mesh.vertices[t[2]]);
        const camera::Vec3 cross = camera::cross(b - a, c - a);
        const double size = camera::length(cross) / 2.0;
        area += size;
        center = center + ((a + b + c) / 3.0) * size;
        if (mesh.normals.size() == mesh.vertices.size()) {
            for (const std::uint32_t index : t) {
                normal = normal + toVec(mesh.normals[index]);
            }
        } else {
            normal = normal + cross;
        }
    }
    if (area <= 0.0 || camera::dot(camera::normalized(normal), toEye) < kMinFacing) {
        return std::nullopt;
    }
    center = center / area;
    FaceSamples samples;
    for (const std::size_t i : triangles) {
        const snapshot::Triangle& t = mesh.triangles[i];
        std::array<camera::ScreenPoint, 3> corners{};
        for (std::size_t k = 0; k < 3; ++k) {
            corners[k] = shots.camera.project(toVec(mesh.vertices[t[k]]), shots.viewport);
        }
        samples.area += std::abs(((corners[1].x - corners[0].x) * (corners[2].y - corners[0].y)) -
                                 ((corners[2].x - corners[0].x) * (corners[1].y - corners[0].y))) /
                        2.0;
        for (const std::uint32_t index : t) {
            for (const double step : kSampleSteps) {
                const camera::Vec3 at = center + (toVec(mesh.vertices[index]) - center) * step;
                const camera::ScreenPoint pixel = shots.camera.project(at, shots.viewport);
                const QPoint point(static_cast<int>(std::floor(pixel.x)), static_cast<int>(std::floor(pixel.y)));
                if (QRect(kInset, kInset, shots.fresh.width() - (2 * kInset), shots.fresh.height() - (2 * kInset))
                        .contains(point)) {
                    samples.pixels.push_back(point);
                }
            }
        }
    }
    if (samples.area < kMinFacePixels || samples.pixels.empty()) {
        return std::nullopt;
    }
    return samples;
}

struct Share {
    std::size_t body = 0;
    std::size_t close = 0;
};

Share bodyShare(const Shots& shots)
{
    const QRgb background = shots.background.rgb();
    const auto isBody = [&](int x, int y) { return gapOf(shots.fresh.pixel(x, y), background) > kBackgroundTolerance; };
    Share share;
    for (int y = kInset; y < shots.fresh.height() - kInset; ++y) {
        for (int x = kInset; x < shots.fresh.width() - kInset; ++x) {
            bool inside = true;
            for (int dy = -kInset; dy <= kInset && inside; ++dy) {
                for (int dx = -kInset; dx <= kInset && inside; ++dx) {
                    inside = isBody(x + dx, y + dy);
                }
            }
            if (!inside) {
                continue;
            }
            ++share.body;
            share.close += gapOf(shots.fresh.pixel(x, y), shots.old.pixel(x, y)) <= kFaceTolerance ? 1 : 0;
        }
    }
    return share;
}

class ShadingPairTest : public testing::Test {
protected:
    void SetUp() override
    {
        resetSettings();
        takeProblems();
    }

    void TearDown() override
    {
        app::theme::setScheme(app::theme::Scheme::Dark);
        for (const std::string& problem : takeProblems()) {
            ADD_FAILURE() << problem;
        }
        resetSettings();
    }
};

TEST_F(ShadingPairTest, EveryVisibleFaceMatchesTheOldView)
{
    if (app::startupFromEnvironment().view != app::ViewKind::New) {
        GTEST_SKIP() << "Runs with KALVE137_VIEW=new";
    }
    const std::filesystem::path folder = pairFolder("ShadingPairTest");
    std::filesystem::create_directories(folder);
    std::ofstream faces(folder / "shading-pairs.tsv");
    faces << "case\ttheme\tbody\tface\told\tnew\tgap\n";
    std::ofstream shares(folder / "shading-share.tsv");
    shares << "case\ttheme\tbody pixels\twithin 15\tshare\n";

    const std::vector<Case> cases{
        {.name = "box",
         .load = [](app::MainWindow& window) { window.findChild<app::DesignSession*>()->reset(blockDocument(), {}); }},
        {.name = "sample",
         .load =
             [](app::MainWindow& window) {
                 window.openDesignAt(std::filesystem::path(KALVE137_SOURCE_DIR) / "assets" / "samples" /
                                     "dodecahedron.k137");
             }},
        {.name = "cylinder",
         .load =
             [](app::MainWindow& window) {
                 core::Document document;
                 geometry::StepPart pin;
                 pin.name = "Pin";
                 pin.shape = geometry::makeCylinder(gp_Pnt(0.0, 0.0, 0.0), 10.0, 20.0);
                 document.add(core::ImportFeature{"pin.step", {std::move(pin)}});
                 window.findChild<app::DesignSession*>()->reset(std::move(document), {});
             },
         .measureFaces = false},
    };
    for (const Case& sample : cases) {
        const std::unique_ptr<app::MainWindow> newWindow = showPairWindow(app::ViewKind::New);
        if (newWindow->newView() == nullptr || !newWindow->newView()->isReady()) {
            GTEST_SKIP() << "The new view did not start";
        }
        const std::unique_ptr<app::MainWindow> oldWindow = showPairWindow(app::ViewKind::Old);
        ASSERT_TRUE(oldWindow->oldView()->isReady());
        for (app::MainWindow* window : {oldWindow.get(), newWindow.get()}) {
            showOnlyBodies(*window, QStringLiteral("Shaded"));
            sample.load(*window);
        }
        for (const auto& [scheme, theme] :
             {std::pair{app::theme::Scheme::Dark, "dark"}, std::pair{app::theme::Scheme::Light, "light"}}) {
            app::theme::setScheme(scheme);
            const Shots shots = grabPair(*oldWindow, *newWindow, homeCamera);
            ASSERT_EQ(shots.old.size(), shots.fresh.size()) << sample.name;
            ASSERT_EQ(shots.fresh.width(), shots.viewport.width) << sample.name;
            const std::string stem = std::format("{}-{}", sample.name, theme);
            savePair(shots, folder, stem);

            const Share share = bodyShare(shots);
            EXPECT_GT(share.body, 1000U) << stem << " shows the body";
            shares << sample.name << '\t' << theme << '\t' << share.body << '\t' << share.close << '\t'
                   << std::format("{:.3f}", share.body == 0
                                                ? 0.0
                                                : static_cast<double>(share.close) / static_cast<double>(share.body))
                   << '\n';
            if (!sample.measureFaces) {
                continue;
            }
            int measured = 0;
            for (std::size_t b = 0; b < shots.scene.bodies.size(); ++b) {
                const snapshot::Mesh& mesh = *shots.scene.bodies[b]->mesh;
                std::map<std::uint32_t, std::vector<std::size_t>> trianglesOf;
                for (std::size_t i = 0; i < mesh.triangles.size(); ++i) {
                    trianglesOf[mesh.faces[i]].push_back(i);
                }
                for (const auto& [face, triangles] : trianglesOf) {
                    const std::optional<FaceSamples> samples = visibleFace(mesh, triangles, shots);
                    if (!samples) {
                        continue;
                    }
                    std::vector<QRgb> olds;
                    std::vector<QRgb> news;
                    for (const QPoint pixel : samples->pixels) {
                        olds.push_back(shots.old.pixel(pixel));
                        news.push_back(shots.fresh.pixel(pixel));
                    }
                    const QRgb old = median(olds);
                    const QRgb fresh = median(news);
                    const int gap = gapOf(old, fresh);
                    faces << sample.name << '\t' << theme << '\t' << b << '\t' << face << '\t' << hex(old) << '\t'
                          << hex(fresh) << '\t' << gap << '\n';
                    EXPECT_LE(gap, kFaceTolerance)
                        << stem << " body " << b << " face " << face << ": old " << hex(old) << ", new " << hex(fresh);
                    ++measured;
                }
            }
            EXPECT_GE(measured, 3) << stem << " shows at least three faces";
        }
        app::theme::setScheme(app::theme::Scheme::Dark);
    }
}

} // namespace
} // namespace kalve137::uitest
