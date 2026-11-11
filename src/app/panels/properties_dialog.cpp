// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/panels/properties_dialog.h"

#include "app/theme/theme.h"

#include "geometry/measure.h"

#include <QApplication>
#include <QClipboard>
#include <QDialog>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QTextBrowser>
#include <QVBoxLayout>

#include <gp_XYZ.hxx>

#include <algorithm>
#include <cstddef>

namespace kalve137::app {

namespace {

constexpr int kDialogWidth = theme::kWidthRegular;
constexpr int kDialogHeight = 420;
constexpr double kCubicMillimetersPerCubicCentimeter = 1000.0;

QString number(double value)
{
    return QString::number(value, 'f', 2);
}

QString point(const gp_Pnt& p)
{
    return QStringLiteral("%1, %2, %3").arg(number(p.X()), number(p.Y()), number(p.Z()));
}

QString row(const QString& label, const QString& value)
{
    return QStringLiteral("<tr><td style='padding-right:16px'>%1</td><td>%2</td></tr>").arg(label, value);
}

QString section(const QString& title, const geometry::BodyProperties& p)
{
    const gp_Pnt size(p.boxMax.X() - p.boxMin.X(), p.boxMax.Y() - p.boxMin.Y(), p.boxMax.Z() - p.boxMin.Z());
    return QStringLiteral("<h3>%1</h3><table>").arg(title.toHtmlEscaped()) +
           row(QDialog::tr("Volume"),
               QDialog::tr("%1 mm³ (%2 cm³)")
                   .arg(number(p.volume), number(p.volume / kCubicMillimetersPerCubicCentimeter))) +
           row(QDialog::tr("Surface area"), QDialog::tr("%1 mm²").arg(number(p.area))) +
           row(QDialog::tr("Center of mass"), point(p.centerOfMass)) + row(QDialog::tr("Size X, Y, Z"), point(size)) +
           QStringLiteral("</table>");
}

} // namespace

void showProperties(QWidget* parent, const core::Model& model)
{
    QString html;
    geometry::BodyProperties total;
    gp_XYZ weighted;
    for (std::size_t i = 0; i < model.bodies.size(); ++i) {
        const geometry::BodyProperties p = geometry::properties(model.bodies[i].shape);
        html += section(QString::fromStdString(model.bodies[i].name), p);
        total.volume += p.volume;
        total.area += p.area;
        weighted += p.centerOfMass.XYZ() * p.volume;
        if (i == 0) {
            total.boxMin = p.boxMin;
            total.boxMax = p.boxMax;
        } else {
            total.boxMin.SetCoord(std::min(total.boxMin.X(), p.boxMin.X()), std::min(total.boxMin.Y(), p.boxMin.Y()),
                                  std::min(total.boxMin.Z(), p.boxMin.Z()));
            total.boxMax.SetCoord(std::max(total.boxMax.X(), p.boxMax.X()), std::max(total.boxMax.Y(), p.boxMax.Y()),
                                  std::max(total.boxMax.Z(), p.boxMax.Z()));
        }
    }
    if (model.bodies.empty()) {
        html = QDialog::tr("There are no bodies yet.");
    } else if (model.bodies.size() > 1) {
        if (total.volume > 0.0) {
            total.centerOfMass = gp_Pnt(weighted / total.volume);
        }
        html += section(QDialog::tr("All bodies"), total);
    }

    QDialog dialog(parent);
    dialog.setWindowTitle(QDialog::tr("Properties"));
    dialog.resize(kDialogWidth, kDialogHeight);
    auto* layout = new QVBoxLayout(&dialog);
    auto* text = new QTextBrowser(&dialog);
    text->setHtml(html);
    layout->addWidget(text);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    QPushButton* copy = buttons->addButton(QDialog::tr("Copy"), QDialogButtonBox::ActionRole);
    layout->addWidget(buttons);
    QObject::connect(copy, &QPushButton::clicked, text,
                     [text] { QApplication::clipboard()->setText(text->toPlainText()); });
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    dialog.exec();
}

} // namespace kalve137::app
