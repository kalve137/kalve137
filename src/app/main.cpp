// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/diagnostics/diagnostics.h"
#include "app/shell/batch_import.h"
#include "app/shell/main_window.h"
#include "app/shell/smoke.h"
#include "app/theme/theme.h"

#include "log/log.h"
#include <OcctQtTools.h>

#include <QApplication>
#include <QIcon>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QSet>
#include <QStandardPaths>
#include <QStringList>
#include <QSvgRenderer>
#include <QTimer>

#include <array>
#include <exception>
#include <filesystem>

namespace {

constexpr std::array kIconSizes{16, 20, 24, 32, 40, 48, 64, 128, 256};
constexpr int kTinyIconMaxPixels = 20;
constexpr int kSmallIconMaxPixels = 32;

QIcon appIcon()
{
    QIcon icon;
    for (const int size : kIconSizes) {
        QSvgRenderer renderer(size <= kTinyIconMaxPixels    ? QStringLiteral(":/kalve137/kalve137-tiny.svg")
                              : size <= kSmallIconMaxPixels ? QStringLiteral(":/kalve137/kalve137-small.svg")
                                                            : QStringLiteral(":/kalve137/kalve137.svg"));
        QPixmap pixmap(size, size);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        renderer.render(&painter);
        painter.end();
        icon.addPixmap(pixmap);
    }
    return icon;
}

class Application : public QApplication {
public:
    using QApplication::QApplication;

    bool notify(QObject* receiver, QEvent* event) override
    {
        try {
            return QApplication::notify(receiver, event);
        } catch (const std::exception& error) {
            report(QString::fromUtf8(error.what()), receiver, event);
        } catch (...) {
            report(QStringLiteral("Unknown error"), receiver, event);
        }
        return false;
    }

private:
    void report(const QString& message, QObject* receiver, QEvent* event)
    {
        K137_ERROR(App, "Uncaught exception in {} handling event {}: {}",
                   receiver != nullptr ? receiver->metaObject()->className() : "nothing",
                   event != nullptr ? static_cast<int>(event->type()) : -1, message.toStdString());
        if (m_reporting || m_reported.contains(message)) {
            return;
        }
        m_reported.insert(message);
        m_reporting = true;
        QTimer::singleShot(0, this, [this, message] {
            QMessageBox::warning(activeWindow(), QStringLiteral("Kalve 137"),
                                 tr("Something went wrong and the last action was stopped:\n%1").arg(message));
            m_reporting = false;
        });
    }

    bool m_reporting = false;
    QSet<QString> m_reported;
};

} // namespace

int main(int argc, char** argv)
{
    OcctQtTools::qtGlPlatformSetup();

    Application app(argc, argv);
    const QStringList arguments = QApplication::arguments();
    const QString mode = arguments.size() > 1 ? arguments.at(1) : QString();
    const bool smoke = mode == QLatin1String(kalve137::app::kSmokeFlag);
    const bool importBatch = mode == QLatin1String(kalve137::app::kImportBatchFlag);
    const bool importOne = mode == QLatin1String(kalve137::app::kImportOneFlag);
    const bool headless = smoke || importBatch || importOne;
    QApplication::setApplicationName("Kalve 137");
    QApplication::setOrganizationName(headless ? kalve137::app::kSmokeOrganization : "Kalve 137");
    QApplication::setApplicationVersion(KALVE137_VERSION);
    if (smoke) {
        kalve137::app::prepareSmoke();
    } else if (headless) {
        QStandardPaths::setTestModeEnabled(true);
    }
    kalve137::app::diagnostics::startLogging();
    kalve137::app::diagnostics::installCrashHandler();
    QApplication::setWindowIcon(appIcon());
    kalve137::app::theme::apply(app);

    int result = 0;
    if (smoke) {
        constexpr qsizetype kSmokeArguments = 4;
        result = arguments.size() == kSmokeArguments
                     ? kalve137::app::runSmoke(std::filesystem::path(arguments.at(2).toStdU16String()),
                                               std::filesystem::path(arguments.at(3).toStdU16String()))
                     : 2;
    } else if (importBatch) {
        constexpr qsizetype kBatchArguments = 4;
        constexpr qsizetype kBatchTimeoutArguments = 5;
        const int timeout =
            arguments.size() == kBatchTimeoutArguments ? arguments.at(4).toInt() : kalve137::app::kImportTimeoutSeconds;
        result = (arguments.size() == kBatchArguments || arguments.size() == kBatchTimeoutArguments) && timeout > 0
                     ? kalve137::app::runImportBatch(std::filesystem::path(arguments.at(2).toStdU16String()),
                                                     std::filesystem::path(arguments.at(3).toStdU16String()), timeout)
                     : 2;
    } else if (importOne) {
        constexpr qsizetype kOneArguments = 4;
        result = arguments.size() == kOneArguments
                     ? kalve137::app::runImportOne(std::filesystem::path(arguments.at(2).toStdU16String()),
                                                   std::filesystem::path(arguments.at(3).toStdU16String()))
                     : 2;
    } else {
        kalve137::app::MainWindow window;
        window.show();
        if (arguments.size() > 1) {
            window.openDesignAt(std::filesystem::path(arguments.at(1).toStdU16String()));
        }
        result = app.exec();
    }
    kalve137::app::diagnostics::stopLogging();
    return result;
}
