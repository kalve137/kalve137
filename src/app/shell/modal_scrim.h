// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QPointer>
#include <QWidget>

#include <vector>

namespace kalve137::app {

class ModalScrim : public QWidget {
    Q_OBJECT

public:
    explicit ModalScrim(QWidget* window);

    void hold(bool held);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    void track(QWidget* dialog, bool shown);
    void refresh();

    std::vector<QPointer<QWidget>> m_dialogs;
    int m_holds = 0;
};

class ScopedDim {
public:
    explicit ScopedDim(QWidget* parent);
    ~ScopedDim();
    ScopedDim(const ScopedDim&) = delete;
    ScopedDim& operator=(const ScopedDim&) = delete;

private:
    QPointer<ModalScrim> m_scrim;
};

template <typename Show>
auto dimmed(QWidget* parent, Show&& show)
{
    const ScopedDim dim(parent);
    return show();
}

} // namespace kalve137::app
