// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "render/box_select.h"

#include "test_meshes.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <vector>

namespace kalve137::render {
namespace {

constexpr double kHalf = 10.0;
constexpr double kMargin = 2.0;
const camera::Viewport kViewport{200, 200, 1.0};

class SnapshotBox : public testing::Test {
protected:
    void SetUp() override
    {
        ASSERT_TRUE(m_camera.setEyeAndCenter({0.0, 0.0, 100.0}, {0.0, 0.0, 0.0}));
        ASSERT_TRUE(m_camera.setUp({0.0, 1.0, 0.0}));
        m_camera.setScale(4.0 * kHalf);
        m_low = m_camera.project({-kHalf, kHalf, 0.0}, kViewport);
        m_high = m_camera.project({kHalf, -kHalf, 0.0}, kViewport);
    }

    std::vector<PickedItem> select(ScreenRect rect, bool crossing, PickKinds kinds = {.points = true}) const
    {
        return boxCandidates(m_bodies, m_camera, kViewport, rect, crossing, kinds);
    }

    static std::vector<std::uint32_t> indices(const std::vector<PickedItem>& items, PickKind kind)
    {
        std::vector<std::uint32_t> found;
        for (const PickedItem& item : items) {
            if (item.kind == kind) {
                found.push_back(item.index);
            }
        }
        std::ranges::sort(found);
        return found;
    }

    camera::Camera m_camera;
    camera::ScreenPoint m_low;
    camera::ScreenPoint m_high;
    std::vector<std::shared_ptr<const snapshot::Body>> m_bodies = {
        test::shared(test::bodyOf(test::edgedBox({-kHalf, -kHalf, -kHalf}, {kHalf, kHalf, kHalf})))};
};

TEST_F(SnapshotBox, AWindowAroundTheBoxTakesEveryItemSeenOrNot)
{
    const std::vector<PickedItem> items =
        select({m_low.x - kMargin, m_low.y - kMargin, m_high.x + kMargin, m_high.y + kMargin}, false);
    EXPECT_EQ(indices(items, PickKind::Face).size(), 6U);
    EXPECT_EQ(indices(items, PickKind::Edge).size(), 12U);
    EXPECT_EQ(indices(items, PickKind::Point).size(), 8U);
}

TEST_F(SnapshotBox, AWindowTakesOnlyWhatLiesWhollyInside)
{
    const double middle = (m_low.x + m_high.x) / 2.0;
    const std::vector<PickedItem> items =
        select({m_low.x - kMargin, m_low.y - kMargin, middle, m_high.y + kMargin}, false);
    EXPECT_EQ(indices(items, PickKind::Face), std::vector<std::uint32_t>({0}));
    EXPECT_EQ(indices(items, PickKind::Edge).size(), 4U);
    EXPECT_EQ(indices(items, PickKind::Point).size(), 4U);
}

TEST_F(SnapshotBox, ACrossingTakesWhatItTouches)
{
    const double middle = (m_low.x + m_high.x) / 2.0;
    const std::vector<PickedItem> items =
        select({middle - 1.0, m_low.y - kMargin, middle + 1.0, m_high.y + kMargin}, true);
    EXPECT_EQ(indices(items, PickKind::Face), std::vector<std::uint32_t>({2, 3, 4, 5}));
    EXPECT_EQ(indices(items, PickKind::Edge).size(), 4U);
    EXPECT_TRUE(indices(items, PickKind::Point).empty());
}

TEST_F(SnapshotBox, ACrossingInsideOneFaceTakesTheFacesAroundIt)
{
    const double x = (m_low.x + m_high.x) / 2.0;
    const double y = (m_low.y + m_high.y) / 2.0;
    const std::vector<PickedItem> items = select({x - 1.0, y - 1.0, x + 1.0, y + 1.0}, true);
    EXPECT_EQ(indices(items, PickKind::Face), std::vector<std::uint32_t>({4, 5}));
    EXPECT_TRUE(indices(items, PickKind::Edge).empty());
}

TEST_F(SnapshotBox, OnlyTheAskedKindsCount)
{
    const std::vector<PickedItem> items =
        select({m_low.x - kMargin, m_low.y - kMargin, m_high.x + kMargin, m_high.y + kMargin}, false,
               {.faces = false, .edges = true, .points = false});
    EXPECT_TRUE(indices(items, PickKind::Face).empty());
    EXPECT_EQ(indices(items, PickKind::Edge).size(), 12U);
    EXPECT_TRUE(indices(items, PickKind::Point).empty());
}

TEST_F(SnapshotBox, ARightToLeftDragCrosses)
{
    EXPECT_TRUE(isCrossingBox({10.0, 0.0}, {5.0, 20.0}));
    EXPECT_FALSE(isCrossingBox({5.0, 0.0}, {10.0, 20.0}));
    const ScreenRect rect = ScreenRect::spanning({10.0, 20.0}, {5.0, 0.0});
    EXPECT_EQ(rect.left, 5.0);
    EXPECT_EQ(rect.top, 0.0);
    EXPECT_EQ(rect.right, 10.0);
    EXPECT_EQ(rect.bottom, 20.0);
}

} // namespace
} // namespace kalve137::render
