#pragma once

#include <QFrame>
#include <QList>
#include <QPointer>

class QPushButton;

/// A small on-screen keyboard that lives INSIDE the main window (a child widget along its bottom
/// edge). The Pi kiosk runs Mixxx fullscreen, and its compositor draws a fullscreen window above
/// every system keyboard, so the system one (Squeekboard) ended up hidden behind it.
/// Keys go to whatever widget has the keyboard focus (the search box), as ordinary key presses;
/// its buttons never take focus, so tapping them doesn't pull focus away from the box.
class WOnScreenKeyboard : public QFrame {
    Q_OBJECT
  public:
    /// Shows the keyboard over the bottom of `pWindow` (created on first use).
    static void showIn(QWidget* pWindow);
    static void hideKeyboard();

  protected:
    bool eventFilter(QObject* pObj, QEvent* pEvent) override;

  private:
    explicit WOnScreenKeyboard(QWidget* pParent);

    void buildKeys();
    void reposition();
    void sendKey(int key, const QString& text);
    void setShifted(bool shifted);

    bool m_shifted = false;
    QList<QPushButton*> m_letterKeys;
    QPushButton* m_pShiftKey = nullptr;
};
