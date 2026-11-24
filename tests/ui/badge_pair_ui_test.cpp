// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/session/design_session.h"
#include "app/shell/main_window.h"
#include "app/shell/startup_options.h"
#include "app/sketch/constraint_badge.h"
#include "app/sketch/dimension_label.h"
#include "app/sketch/sketch_editor_detail.h"
#include "app/theme/theme.h"
#include "camera/camera.h"
#include "core/document.h"
#include "pair_shots.h"
#include "render/annotations.h"
#include "render/rhi/rhi_view.h"
#include "ui_driver.h"
#include "viewer/occt_view_widget.h"

#include <AIS_InteractiveContext.hxx>
#include <AIS_InteractiveObject.hxx>
#include <Graphic3d_AspectMarker3d.hxx>
#include <Graphic3d_MarkerImage.hxx>
#include <Graphic3d_ZLayerId.hxx>
#include <Image_PixMap.hxx>
#include <Prs3d_Drawer.hxx>
#include <Prs3d_PointAspect.hxx>
#include <gp_Pnt.hxx>

#include <QAction>
#include <QColor>
#include <QImage>
#include <QPainter>
#include <QPointF>
#include <QRect>
#include <QString>
#include <QTest>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace kalve137::uitest {
namespace {

constexpr int kSettleMs = 300;
constexpr double kReach = 400.0;
constexpr double kScale = 100.0;
constexpr int kChange = 6;
constexpr int kInk = 40;
constexpr double kStrongShare = 0.75;
constexpr double kSizeTolerance = 1.0;
constexpr int kColorTolerance = 24;
constexpr int kBadgeColumns = 6;
constexpr double kBadgeStepX = 60.0;
constexpr double kBadgeStepY = 50.0;
constexpr int kLabelColumns = 3;
constexpr double kLabelStepX = 120.0;
constexpr double kLabelStepY = 50.0;
constexpr int kCropWidth = 90;
constexpr int kCropHeight = 40;
constexpr int kSheetSide = 720;
constexpr int kSheetColumns = 2;
constexpr int kSheetGap = 8;
constexpr int kCameraMoves = 5;

enum class Look { Plain, Selected, Conflict };

struct BadgeSample {
    render::BadgeIcon icon = render::BadgeIcon::Coincident;
    Look look = Look::Plain;
};

struct LabelSample {
    QString text;
    Look look = Look::Plain;
};

struct Shape {
    QRect box;
    int corner = 0;
    QRect ink;
    QRgb fill = 0;
    QRgb mark = 0;
    QRgb border = 0;
};

QString iconName(render::BadgeIcon icon)
{
    switch (icon) {
    case render::BadgeIcon::Coincident:
        return QStringLiteral("coincident");
    case render::BadgeIcon::Equal:
        return QStringLiteral("equal");
    case render::BadgeIcon::Horizontal:
        return QStringLiteral("horizontal");
    case render::BadgeIcon::Lock:
        return QStringLiteral("lock");
    case render::BadgeIcon::Midpoint:
        return QStringLiteral("midpoint");
    case render::BadgeIcon::Parallel:
        return QStringLiteral("parallel");
    case render::BadgeIcon::Perpendicular:
        return QStringLiteral("perpendicular");
    case render::BadgeIcon::Symmetric:
        return QStringLiteral("symmetric");
    case render::BadgeIcon::Tangent:
        return QStringLiteral("tangent");
    case render::BadgeIcon::Vertical:
        return QStringLiteral("vertical");
    }
    return {};
}

std::string lookName(Look look)
{
    switch (look) {
    case Look::Plain:
        return "plain";
    case Look::Selected:
        return "selected";
    case Look::Conflict:
        return "conflict";
    }
    return {};
}

std::vector<BadgeSample> everyBadge()
{
    std::vector<BadgeSample> samples;
    samples.reserve(render::kBadgeIcons.size() + 2);
    for (const render::BadgeIcon icon : render::kBadgeIcons) {
        samples.push_back({icon, Look::Plain});
    }
    samples.push_back({render::BadgeIcon::Coincident, Look::Selected});
    samples.push_back({render::BadgeIcon::Equal, Look::Conflict});
    return samples;
}

std::vector<LabelSample> everyLabel()
{
    return {{QStringLiteral("R12.5"), Look::Plain},
            {QString::fromUtf8("\xC3\x98"
                               "8"),
             Look::Plain},
            {QString::fromUtf8("45\xC2\xB0"), Look::Selected},
            {QStringLiteral("-3.25"), Look::Conflict},
            {QStringLiteral("30"), Look::Plain}};
}

snapshot::Color colorOf(const QColor& color)
{
    return {static_cast<float>(color.redF()), static_cast<float>(color.greenF()), static_cast<float>(color.blueF()),
            static_cast<float>(color.alphaF())};
}

QColor badgeIconColor(Look look)
{
    return look == Look::Conflict ? app::theme::palette().chrome.conflict : app::theme::palette().viewport.annotation;
}

QColor badgeFill()
{
    QColor fill = app::theme::palette().chrome.surface;
    fill.setAlpha(235);
    return fill;
}

QColor borderOf(Look look)
{
    return look == Look::Selected ? app::theme::palette().chrome.accent : app::theme::palette().chrome.border;
}

QColor labelColor(Look look)
{
    switch (look) {
    case Look::Selected:
        return app::theme::palette().chrome.accent;
    case Look::Conflict:
        return app::theme::palette().chrome.conflictText;
    case Look::Plain:
        break;
    }
    return app::theme::palette().viewport.annotation;
}

void lookDown(app::MainWindow& window)
{
    render::RhiView& canvas = *window.newView();
    camera::Camera camera = canvas.camera();
    camera.setProjectionType(camera::Projection::Orthographic);
    EXPECT_TRUE(camera.setEyeAndCenter({0.0, 0.0, kReach}, {0.0, 0.0, 0.0}));
    EXPECT_TRUE(camera.setUp({0.0, 1.0, 0.0}));
    EXPECT_TRUE(camera.setScale(kScale));
    canvas.setCamera(camera);
}

QRect cropAround(const QPointF& at, double ratio)
{
    const int width = static_cast<int>(std::lround(kCropWidth * ratio));
    const int height = static_cast<int>(std::lround(kCropHeight * ratio));
    const int x = static_cast<int>(std::floor(at.x() * ratio));
    const int y = static_cast<int>(std::floor(at.y() * ratio));
    return {x - (width / 2), y - (height / 2), width, height};
}

QRect drawnIn(const QImage& image, const QRect& crop, QRgb background)
{
    QRect box;
    const QRect inside = crop.intersected(image.rect());
    for (int y = inside.top(); y <= inside.bottom(); ++y) {
        for (int x = inside.left(); x <= inside.right(); ++x) {
            if (gapOf(image.pixel(x, y), background) > kChange) {
                box = box.united(QRect(x, y, 1, 1));
            }
        }
    }
    return box;
}

int cornerOf(const QImage& image, const QRect& box, QRgb background)
{
    for (int x = box.left(); x <= box.right(); ++x) {
        if (gapOf(image.pixel(x, box.top()), background) > kChange) {
            return x - box.left();
        }
    }
    return box.width();
}

QRgb mostCommon(const std::vector<QRgb>& pixels)
{
    std::map<QRgb, int> counts;
    for (const QRgb pixel : pixels) {
        ++counts[pixel];
    }
    return std::ranges::max_element(counts, {}, &std::pair<const QRgb, int>::second)->first;
}

QImage imageOf(const Handle(AIS_InteractiveObject)& object)
{
    const Handle(Image_PixMap) pixels = object->Attributes()->PointAspect()->Aspect()->GetMarkerImage()->GetImage();
    QImage image(static_cast<int>(pixels->SizeX()), static_cast<int>(pixels->SizeY()), QImage::Format_RGBA8888);
    for (int row = 0; row < image.height(); ++row) {
        std::memcpy(image.scanLine(row), pixels->Row(static_cast<std::size_t>(row)),
                    static_cast<std::size_t>(image.width()) * 4);
    }
    return image;
}

QImage over(const QImage& image, QRgb background)
{
    QImage flat(image.size(), QImage::Format_RGB32);
    flat.fill(background);
    QPainter painter(&flat);
    painter.drawImage(0, 0, image);
    return flat;
}

Shape shapeOf(const QImage& image, const QRect& crop, QRgb background, double ratio)
{
    Shape shape;
    shape.box = drawnIn(image, crop, background);
    if (shape.box.isNull()) {
        return shape;
    }
    shape.corner = cornerOf(image, shape.box, background);
    const int inset = static_cast<int>(std::ceil(2.0 * ratio));
    const QRect inner = shape.box.adjusted(inset, inset, -inset, -inset);
    std::vector<QRgb> pixels;
    for (int y = inner.top(); y <= inner.bottom(); ++y) {
        for (int x = inner.left(); x <= inner.right(); ++x) {
            pixels.push_back(image.pixel(x, y));
        }
    }
    shape.fill = pixels.empty() ? background : mostCommon(pixels);
    int strongest = 0;
    for (const QRgb pixel : pixels) {
        strongest = std::max(strongest, gapOf(pixel, shape.fill));
    }
    std::vector<QRgb> strong;
    for (int y = inner.top(); y <= inner.bottom(); ++y) {
        for (int x = inner.left(); x <= inner.right(); ++x) {
            const int gap = gapOf(image.pixel(x, y), shape.fill);
            if (gap > kInk) {
                shape.ink = shape.ink.united(QRect(x, y, 1, 1));
            }
            if (gap >= strongest * kStrongShare && strongest > kInk) {
                strong.push_back(image.pixel(x, y));
            }
        }
    }
    shape.mark = strong.empty() ? shape.fill : median(strong);
    shape.border = image.pixel(shape.box.left(), shape.box.center().y());
    return shape;
}

QImage zoomSheet(const QImage& image, const std::vector<QRect>& crops)
{
    const int tileWidth = kSheetSide / kSheetColumns;
    const int tileHeight = tileWidth * kCropHeight / kCropWidth;
    const int rows = static_cast<int>((crops.size() + kSheetColumns - 1) / kSheetColumns);
    QImage sheet(kSheetSide, tileHeight * rows, QImage::Format_RGB32);
    sheet.fill(Qt::black);
    QPainter painter(&sheet);
    for (std::size_t i = 0; i < crops.size(); ++i) {
        const int column = static_cast<int>(i % kSheetColumns);
        const int row = static_cast<int>(i / kSheetColumns);
        painter.drawImage(
            QPoint(column * tileWidth, row * tileHeight),
            image.copy(crops[i]).scaled(QSize(tileWidth, tileHeight), Qt::IgnoreAspectRatio, Qt::FastTransformation));
    }
    return sheet;
}

QImage sideBySide(const QImage& old, const QImage& fresh)
{
    QImage sheet(old.width() + kSheetGap + fresh.width(), std::max(old.height(), fresh.height()), QImage::Format_RGB32);
    sheet.fill(Qt::white);
    QPainter painter(&sheet);
    painter.drawImage(0, 0, old);
    painter.drawImage(old.width() + kSheetGap, 0, fresh);
    return sheet;
}

class BadgePairTest : public testing::Test {
protected:
    void SetUp() override
    {
        resetSettings();
        takeProblems();
    }

    void TearDown() override
    {
        for (const std::string& problem : takeProblems()) {
            ADD_FAILURE() << problem;
        }
        resetSettings();
    }

    bool start()
    {
        if (app::startupFromEnvironment().view != app::ViewKind::New) {
            return false;
        }
        m_new = showPairWindow(app::ViewKind::New);
        if (m_new->newView() == nullptr || !m_new->newView()->isReady()) {
            return false;
        }
        m_old = showPairWindow(app::ViewKind::Old);
        EXPECT_TRUE(m_old->oldView()->isReady());
        for (app::MainWindow* window : {m_old.get(), m_new.get()}) {
            if (QAction* grid = actionNamed(*window, QStringLiteral("Show Grid"));
                grid != nullptr && grid->isChecked()) {
                grid->trigger();
            }
            auto* session = window->findChild<app::DesignSession*>();
            session->reset(core::Document{}, {});
            session->setOriginVisible(false);
        }
        QTest::qWait(kSettleMs);
        m_folder = pairFolder("BadgePairTest");
        std::filesystem::create_directories(m_folder);
        return true;
    }

    camera::Vec3 worldAt(double x, double y) const
    {
        const render::RhiView& canvas = *m_new->newView();
        const QPointF zero = screenOf(canvas.camera(), canvas.viewport(), {0.0, 0.0, 0.0});
        const QPointF unit = screenOf(canvas.camera(), canvas.viewport(), {1.0, 0.0, 0.0});
        const double perUnit = unit.x() - zero.x();
        const double ratio = canvas.viewport().pixelRatio;
        const QPointF target(std::round((zero.x() + x) * ratio) / ratio, std::round((zero.y() + y) * ratio) / ratio);
        return {(target.x() - zero.x()) / perUnit, -(target.y() - zero.y()) / perUnit, 0.0};
    }

    void place()
    {
        lookDown(*m_new);
        QTest::qWait(kSettleMs);
        const double ratio = m_old->oldView()->devicePixelRatioF();
        const Handle(AIS_InteractiveContext)& context = m_old->oldView()->aisContext();
        const std::vector<BadgeSample> badges = everyBadge();
        for (std::size_t i = 0; i < badges.size(); ++i) {
            const double column = static_cast<double>(i % kBadgeColumns) - ((kBadgeColumns - 1) / 2.0);
            const std::size_t line = i / kBadgeColumns;
            const auto row = static_cast<double>(line);
            const camera::Vec3 at = worldAt(column * kBadgeStepX, (row - 2.0) * kBadgeStepY);
            m_anchors.push_back(at);
            m_names.push_back(std::format("{} {}", iconName(badges[i].icon).toStdString(), lookName(badges[i].look)));
            Handle(AIS_InteractiveObject) badge =
                app::makeConstraintBadge(gp_Pnt(at.x, at.y, at.z), iconName(badges[i].icon),
                                         badgeIconColor(badges[i].look), badges[i].look == Look::Selected, ratio);
            show(context, badge);
            m_annotations.badges.push_back({.anchor = at,
                                            .icon = badges[i].icon,
                                            .iconColor = colorOf(badgeIconColor(badges[i].look)),
                                            .fill = colorOf(badgeFill()),
                                            .border = colorOf(borderOf(badges[i].look))});
        }
        const std::vector<LabelSample> labels = everyLabel();
        for (std::size_t i = 0; i < labels.size(); ++i) {
            const double column = static_cast<double>(i % kLabelColumns) - ((kLabelColumns - 1) / 2.0);
            const std::size_t line = i / kLabelColumns;
            const auto row = static_cast<double>(line);
            const camera::Vec3 at = worldAt(column * kLabelStepX, (row + 0.5) * kLabelStepY);
            m_anchors.push_back(at);
            m_names.push_back(std::format("label {} {}", labels[i].text.toStdString(), lookName(labels[i].look)));
            const app::DimensionLabel label = app::makeDimensionLabel(
                gp_Pnt(at.x, at.y, at.z), labels[i].text, labelColor(labels[i].look), labels[i].look == Look::Selected,
                app::sketch_editor_detail::kDimensionTextPixels, ratio);
            show(context, label.object);
            m_labelImages.push_back(imageOf(label.object));
            m_annotations.labels.push_back({.anchor = at,
                                            .text = labels[i].text.toStdU32String(),
                                            .color = colorOf(labelColor(labels[i].look)),
                                            .fill = colorOf(app::theme::palette().viewport.canvas),
                                            .border = colorOf(borderOf(labels[i].look))});
        }
        context->UpdateCurrentViewer();
        m_new->newView()->setAnnotations(m_annotations);
    }

    void show(const Handle(AIS_InteractiveContext)& context, const Handle(AIS_InteractiveObject)& object)
    {
        object->SetZLayer(Graphic3d_ZLayerId_Topmost);
        context->Display(object, AIS_WireFrame, -1, false);
        m_shownOld.push_back(object);
    }

    void clearShown()
    {
        for (const Handle(AIS_InteractiveObject)& object : m_shownOld) {
            m_old->oldView()->aisContext()->Remove(object, false);
        }
        m_shownOld.clear();
        m_old->oldView()->aisContext()->UpdateCurrentViewer();
        m_new->newView()->setAnnotations({});
    }

    std::unique_ptr<app::MainWindow> m_old;
    std::unique_ptr<app::MainWindow> m_new;
    std::filesystem::path m_folder;
    std::vector<Handle(AIS_InteractiveObject)> m_shownOld;
    std::vector<camera::Vec3> m_anchors;
    std::vector<std::string> m_names;
    std::vector<QImage> m_labelImages;
    render::Annotations m_annotations;
};

TEST_F(BadgePairTest, EveryBadgeAndLabelKeepsTheOldViewsSizeCornerTextAndColor)
{
    if (!start()) {
        GTEST_SKIP() << "Runs with KALVE137_VIEW=new and a started new view";
    }
    place();
    const Shots shots = grabPair(*m_old, *m_new, [](app::MainWindow& window) { lookDown(window); });
    savePair(shots, m_folder, "badges");
    const double ratio = shots.viewport.pixelRatio;
    const QRgb background = shots.background.rgb();
    const bool colors = drawsReferenceColors(*m_old->oldView());
    const std::size_t badgeCount = everyBadge().size();
    std::ofstream table(m_folder / "badge-sizes.tsv");
    table << "item\told shown\told size\tnew size\told corner\tnew corner\told ink\tnew ink\told fill\tnew fill\told "
             "mark\tnew mark\told border\tnew border\n";
    std::vector<QRect> crops;
    for (std::size_t i = 0; i < m_anchors.size(); ++i) {
        const std::string& name = m_names[i];
        const QRect crop = cropAround(screenOf(shots.camera, shots.viewport, m_anchors[i]), ratio);
        crops.push_back(crop);
        const Shape shown = shapeOf(shots.old, crop, background, ratio);
        const Shape fresh = shapeOf(shots.fresh, crop, background, ratio);
        const QImage label = i >= badgeCount ? over(m_labelImages[i - badgeCount], background) : QImage();
        const Shape old = label.isNull() ? shown : shapeOf(label, label.rect(), background, ratio);
        table << name << '\t' << shown.box.width() << 'x' << shown.box.height() << '\t' << old.box.width() << 'x'
              << old.box.height() << '\t' << fresh.box.width() << 'x' << fresh.box.height() << '\t' << old.corner
              << '\t' << fresh.corner << '\t' << old.ink.width() << 'x' << old.ink.height() << '\t' << fresh.ink.width()
              << 'x' << fresh.ink.height() << '\t' << hex(old.fill) << '\t' << hex(fresh.fill) << '\t' << hex(old.mark)
              << '\t' << hex(fresh.mark) << '\t' << hex(old.border) << '\t' << hex(fresh.border) << '\n';
        ASSERT_FALSE(shown.box.isNull()) << name << " shows in the old view";
        ASSERT_FALSE(fresh.box.isNull()) << name << " shows in the new view";
        EXPECT_NEAR(fresh.box.width(), old.box.width(), kSizeTolerance * ratio) << name << " width";
        EXPECT_NEAR(fresh.box.height(), old.box.height(), kSizeTolerance * ratio) << name << " height";
        EXPECT_NEAR(QRectF(fresh.box).center().x(), QRectF(shown.box).center().x(), kSizeTolerance * ratio)
            << name << " sits where it did";
        EXPECT_NEAR(QRectF(fresh.box).center().y(), QRectF(shown.box).center().y(), kSizeTolerance * ratio)
            << name << " sits where it did";
        EXPECT_NEAR(fresh.corner, old.corner, kSizeTolerance * ratio) << name << " corner";
        if (i >= badgeCount) {
            EXPECT_NEAR(fresh.ink.height(), old.ink.height(), kSizeTolerance * ratio) << name << " text height";
        }
        if (colors) {
            EXPECT_LE(gapOf(fresh.fill, old.fill), kColorTolerance)
                << name << " fill " << hex(fresh.fill) << " against " << hex(old.fill);
            EXPECT_LE(gapOf(fresh.mark, old.mark), kColorTolerance)
                << name << " icon or text " << hex(fresh.mark) << " against " << hex(old.mark);
            EXPECT_LE(gapOf(fresh.border, old.border), kColorTolerance)
                << name << " border " << hex(fresh.border) << " against " << hex(old.border);
        }
    }
    const QImage sheet = sideBySide(zoomSheet(shots.old, crops), zoomSheet(shots.fresh, crops));
    sheet.save(QString::fromStdU16String((m_folder / "badges zoomed.png").u16string()));
    clearShown();
}

TEST_F(BadgePairTest, BadgesAndLabelsFollowTheCameraHereAndOnOpenGl)
{
    if (!start()) {
        GTEST_SKIP() << "Runs with KALVE137_VIEW=new and a started new view";
    }
    place();
    render::RhiView& canvas = *m_new->newView();
    const camera::Camera start = canvas.camera();
    const auto check = [&](render::RhiView& view, const std::string& name) {
        for (int move = 0; move < kCameraMoves; ++move) {
            camera::Camera camera = start;
            const double angle = 0.5 * move;
            EXPECT_TRUE(camera.setEyeAndCenter(
                {kReach * 0.3 * std::sin(angle), -kReach * 0.3 * std::cos(angle), kReach}, {0.0, 0.0, 0.0}));
            EXPECT_TRUE(camera.setUp({0.0, 1.0, 0.0}));
            EXPECT_TRUE(camera.setScale(kScale * (1.0 - (0.08 * move))));
            view.setCamera(camera);
            const QImage drawn = view.grabFramebuffer().convertToFormat(QImage::Format_RGB32);
            const camera::Viewport viewport = view.viewport();
            const snapshot::Color& paper = view.viewportStyle().background;
            const QRgb background = QColor::fromRgbF(paper.red, paper.green, paper.blue).rgb();
            for (std::size_t i = 0; i < m_anchors.size(); ++i) {
                const QPointF at = screenOf(view.camera(), viewport, m_anchors[i]);
                const QRect box = drawnIn(drawn, cropAround(at, viewport.pixelRatio), background);
                const std::string where = std::format("{} move {} {}", name, move, m_names[i]);
                ASSERT_FALSE(box.isNull()) << where << " shows";
                const QPointF middle = QRectF(box).center();
                EXPECT_NEAR(middle.x(), at.x() * viewport.pixelRatio, 1.0 + viewport.pixelRatio) << where;
                EXPECT_NEAR(middle.y(), at.y() * viewport.pixelRatio, 1.0 + viewport.pixelRatio) << where;
            }
        }
    };
    check(canvas, "main");
    if (!render::canStart(render::GraphicsApi::OpenGL)) {
        clearShown();
        return;
    }
    {
        render::RhiView view(render::GraphicsApi::OpenGL);
        view.setAttribute(Qt::WA_DontShowOnScreen);
        view.resize(canvas.size());
        view.show();
        view.setViewportStyle(canvas.viewportStyle());
        view.setGrid(canvas.grid().settings());
        view.setScene(canvas.scene());
        view.setAnnotations(m_annotations);
        check(view, "opengl");
    }
    clearShown();
}

} // namespace
} // namespace kalve137::uitest
