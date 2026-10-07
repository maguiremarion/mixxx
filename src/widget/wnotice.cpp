#include "widget/wnotice.h"

#include <QFontMetrics>
#include <QLabel>
#include <QPointer>
#include <QTimer>
#include <QWidget>
#include <algorithm>

namespace WNotice {

void show(QWidget* pWindow, const QString& text, const QPoint& anchor, bool centered) {
    if (!pWindow) {
        return;
    }
    static QPointer<QLabel> s_pLabel;
    static QPointer<QTimer> s_pTimer;
    if (!s_pLabel || s_pLabel->parentWidget() != pWindow) {
        delete s_pLabel;
        s_pLabel = new QLabel(pWindow);
        s_pLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        s_pLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
        s_pLabel->setWordWrap(true);
        s_pLabel->setStyleSheet(QStringLiteral(
                "background-color: #181818; color: #ffffff; border: 1px solid #3478f2;"
                "padding: 8px 14px; font-size: 19px; font-weight: 500;"));
        s_pTimer = new QTimer(s_pLabel);
        s_pTimer->setSingleShot(true);
        QObject::connect(s_pTimer, &QTimer::timeout, s_pLabel, &QWidget::hide);
    }
    s_pLabel->setText(text);
    // As wide as its longest line needs (it used to be a fixed 380 px, far too wide for a
    // word like "Ejecting..."), wrapping only past the maximum.
    s_pLabel->ensurePolished();
    const QFontMetrics fontMetrics = s_pLabel->fontMetrics();
    int textWidth = 0;
    for (const QString& line : text.split(QChar('\n'))) {
        textWidth = std::max(textWidth, fontMetrics.horizontalAdvance(line));
    }
    const int chrome = 2 * 14 + 2 * 1 + 20; // left/right padding + border + slack (the text is
                                            // a little wider than the plain metrics say)
    s_pLabel->setFixedWidth(std::min(std::min(380, pWindow->width() / 2), textWidth + chrome));
    s_pLabel->adjustSize();
    QPoint pos = anchor;
    if (centered) {
        pos.setX(pos.x() - s_pLabel->width() / 2);
    }
    pos.setX(std::clamp(pos.x(), 0, std::max(0, pWindow->width() - s_pLabel->width())));
    pos.setY(std::clamp(pos.y(), 0, std::max(0, pWindow->height() - s_pLabel->height())));
    s_pLabel->move(pos);
    s_pLabel->show();
    s_pLabel->raise();
    s_pTimer->start(4000);
}

} // namespace WNotice
