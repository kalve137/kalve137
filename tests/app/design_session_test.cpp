// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/session/design_session.h"

#include "core/parameter_table.h"
#include "geometry/shapes.h"

#include <gtest/gtest.h>

#include <QString>

#include <gp_Pnt.hxx>
#include <gp_Vec.hxx>

#include <cstddef>
#include <optional>
#include <variant>

namespace kalve137::app {
namespace {

constexpr double kTolerance = 1e-9;

class DesignSessionTest : public testing::Test {
protected:
    void SetUp() override
    {
        m_session.edit(QStringLiteral("Import"), [this](core::Document& document) {
            document.add(core::ImportFeature{"box.step", {{"Box", geometry::makeBox(10.0, 10.0, 10.0)}}});
            m_pin = document.add(
                core::ImportFeature{"pin.step", {{"Pin", geometry::makeCylinder(gp_Pnt(30.0, 0.0, 0.0), 2.0, 10.0)}}});
        });
        QObject::connect(&m_session, &DesignSession::historyJumped, [this] { ++m_jumps; });
    }

    void movePin()
    {
        m_session.edit(QStringLiteral("Move"), [this](core::Document& document) {
            core::MoveFeature move;
            move.bodies = {{m_pin, 0}};
            move.translation = gp_Vec(0.0, 20.0, 0.0);
            document.add(move);
        });
    }

    snapshot::Scene snapshots() { return m_session.snapshots().scene(m_session.model()); }

    DesignSession m_session;
    core::FeatureId m_pin = 0;
    int m_jumps = 0;
};

TEST_F(DesignSessionTest, AnOrdinaryEditKeepsTheSnapshots)
{
    snapshots();
    movePin();
    EXPECT_EQ(m_jumps, 0);
    EXPECT_EQ(m_session.snapshots().size(), 2U);
}

TEST_F(DesignSessionTest, UndoGivesThePreEditSnapshot)
{
    const snapshot::Scene before = snapshots();
    movePin();
    snapshots();
    m_session.undo();
    EXPECT_EQ(m_jumps, 1);
    EXPECT_EQ(m_session.snapshots().size(), 0U);
    const snapshot::Scene undone = snapshots();
    ASSERT_EQ(undone.bodies.size(), before.bodies.size());
    for (std::size_t i = 0; i < undone.bodies.size(); ++i) {
        EXPECT_EQ(undone.bodies[i]->mesh->vertices, before.bodies[i]->mesh->vertices);
        EXPECT_EQ(undone.bodies[i]->mesh->triangles, before.bodies[i]->mesh->triangles);
    }
}

TEST_F(DesignSessionTest, RedoDropsTheSnapshots)
{
    movePin();
    m_session.undo();
    snapshots();
    m_session.redo();
    EXPECT_EQ(m_jumps, 2);
    EXPECT_EQ(m_session.snapshots().size(), 0U);
}

TEST_F(DesignSessionTest, TimelineChangesDropTheSnapshots)
{
    snapshots();
    m_session.edit(QStringLiteral("Roll Back"), [](core::Document& document) { document.setMarker(1); });
    EXPECT_EQ(m_jumps, 1);
    snapshots();
    m_session.edit(QStringLiteral("Roll Forward"),
                   [](core::Document& document) { document.setMarker(document.features().size()); });
    EXPECT_EQ(m_jumps, 2);
    m_session.edit(QStringLiteral("Suppress"),
                   [this](core::Document& document) { document.find(m_pin)->suppressed = true; });
    EXPECT_EQ(m_jumps, 3);
    EXPECT_EQ(m_session.snapshots().size(), 0U);
}

TEST(DesignSessionParameters, OneEditIsOneUndoStep)
{
    DesignSession session;
    core::FeatureId extrude = 0;
    ASSERT_FALSE(session.tryEdit(QStringLiteral("Add w"), [&extrude](core::Document& document) {
        extrude = document.add(core::ExtrudeFeature{});
        if (std::optional<core::ExpressionError> error = core::addParameter(document, "w", "40 mm")) {
            return error;
        }
        return core::setFieldExpression(document, {extrude, "distance"}, "w * 2");
    }));
    const auto distance = [&] {
        return std::get<core::ExtrudeFeature>(session.document().find(extrude)->data).distance;
    };
    EXPECT_NEAR(distance(), 80.0, kTolerance);

    ASSERT_FALSE(session.tryEdit(QStringLiteral("Edit w"), [](core::Document& document) {
        return core::setParameterExpression(document, "w", "25 mm");
    }));
    EXPECT_NEAR(distance(), 50.0, kTolerance);
    EXPECT_TRUE(session.tryEdit(QStringLiteral("Edit w"), [](core::Document& document) {
        return core::setParameterExpression(document, "w", "0 mm");
    }));
    EXPECT_EQ(session.undoLabel(), QStringLiteral("Edit w"));

    session.undo();
    EXPECT_EQ(session.document().parameters().front().expression, "40 mm");
    EXPECT_NEAR(distance(), 80.0, kTolerance);
    EXPECT_EQ(session.undoLabel(), QStringLiteral("Add w"));
    session.redo();
    EXPECT_EQ(session.document().parameters().front().expression, "25 mm");
    EXPECT_NEAR(distance(), 50.0, kTolerance);
    EXPECT_FALSE(session.canRedo());
}

} // namespace
} // namespace kalve137::app
