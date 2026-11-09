#pragma once

#include <QEvent>
#include <QMargins>
#include <QRect>
#include <QWidget>
#include <QtGlobal>

// Virtual bezel for the Pi's touch screen: the screen's frame covers the right-hand edge of the picture,
// so the right-hand N pixels are "dead". The windows keep their full-screen size (the compositor and Qt
// know nothing about the frame), but their contents are laid out without that strip and a black, input-
// swallowing strip is drawn over it. Nothing is squashed: the layout simply has N pixels less to share.
//
// N comes from the environment (MIXXX_SCREEN_INSET_RIGHT, set by the Pi's supervisor script; 0 / unset
// everywhere else, where none of this does anything).
namespace mixxx {
namespace kiosk {

inline int screenInsetRight() {
    static const int inset = []() {
        bool ok = false;
        const int value = qEnvironmentVariableIntValue("MIXXX_SCREEN_INSET_RIGHT", &ok);
        return (ok && value > 0) ? value : 0;
    }();
    return inset;
}

// What is left of a screen's available area once the dead strip is taken off (for placing pop-ups).
inline QRect screenMinusInset(QRect available) {
    available.setRight(available.right() - screenInsetRight());
    return available;
}

class ScreenBezel : public QWidget {
  public:
    ScreenBezel(QWidget* pWindow, int width)
            : QWidget(pWindow),
              m_width(width) {
        setObjectName(QStringLiteral("ScreenBezel"));
        setAttribute(Qt::WA_StyledBackground, true);
        setStyleSheet(QStringLiteral("#ScreenBezel { background-color: #000000; border: none; }"));
        pWindow->installEventFilter(this);
        reposition();
        show();
    }

  protected:
    bool eventFilter(QObject* pObj, QEvent* pEvent) override {
        if (pObj == parent()) {
            switch (pEvent->type()) {
            case QEvent::Resize:
            case QEvent::Show:
            case QEvent::LayoutRequest:
            case QEvent::ChildAdded:
            case QEvent::ChildPolished:
                reposition();
                break;
            default:
                break;
            }
        }
        return QWidget::eventFilter(pObj, pEvent);
    }

  private:
    void reposition() {
        QWidget* pWindow = parentWidget();
        if (!pWindow) {
            return;
        }
        setGeometry(pWindow->width() - m_width, 0, m_width, pWindow->height());
        raise();
    }

    int m_width;
};

// Takes the dead strip off a top-level window (main window, full-screen Preferences): its contents are
// laid out without it and the black strip is drawn there. Safe to call more than once.
inline void installScreenBezel(QWidget* pWindow) {
    const int inset = screenInsetRight();
    if (inset <= 0 || !pWindow || pWindow->findChild<QWidget*>(QStringLiteral("ScreenBezel"), Qt::FindDirectChildrenOnly)) {
        return;
    }
    QMargins margins = pWindow->contentsMargins();
    margins.setRight(margins.right() + inset);
    pWindow->setContentsMargins(margins);
    new ScreenBezel(pWindow, inset);
}

} // namespace kiosk
} // namespace mixxx
