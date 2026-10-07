#include "widget/wonscreenkeyboard.h"

#include <QApplication>
#include <QGridLayout>
#include <QKeyEvent>
#include <QPushButton>
#include <QResizeEvent>
#include <QStyle>

#include "moc_wonscreenkeyboard.cpp"

namespace {
constexpr int kKeyHeight = 50;
constexpr int kRowCount = 5;
constexpr int kGridUnits = 22; // grid columns per row: a 1-unit key is a 2-column cell

QPointer<WOnScreenKeyboard> s_pKeyboard;
} // namespace

WOnScreenKeyboard::WOnScreenKeyboard(QWidget* pParent)
        : QFrame(pParent) {
    setObjectName(QStringLiteral("OnScreenKeyboard"));
    setAttribute(Qt::WA_StyledBackground);
    // Flat, square, no gradients (like the rest of the skin).
    setStyleSheet(QStringLiteral(
            "#OnScreenKeyboard { background-color: #0e0e0e; border-top: 2px solid #3478f2; }"
            "#OnScreenKeyboard QPushButton { background-color: #262626; color: #e5e6ea;"
            "  border: 1px solid #3a3a3a; border-radius: 0; font-size: 22px; padding: 0; }"
            "#OnScreenKeyboard QPushButton:pressed { background-color: #3478f2; color: #fff; }"
            "#OnScreenKeyboard QPushButton[special=\"true\"] { background-color: #1c1c1c;"
            "  font-size: 16px; font-weight: 600; }"
            "#OnScreenKeyboard QPushButton[on=\"true\"] { background-color: #3478f2; color: #fff; }"));
    buildKeys();
    parent()->installEventFilter(this);
    hide();
}

void WOnScreenKeyboard::showIn(QWidget* pWindow) {
    if (!pWindow) {
        return;
    }
    if (!s_pKeyboard || s_pKeyboard->parentWidget() != pWindow) {
        delete s_pKeyboard;
        s_pKeyboard = new WOnScreenKeyboard(pWindow);
    }
    s_pKeyboard->setShifted(false);
    s_pKeyboard->reposition();
    s_pKeyboard->show();
    s_pKeyboard->raise();
}

void WOnScreenKeyboard::hideKeyboard() {
    if (s_pKeyboard) {
        s_pKeyboard->hide();
    }
}

bool WOnScreenKeyboard::eventFilter(QObject* pObj, QEvent* pEvent) {
    if (pObj == parent() && pEvent->type() == QEvent::Resize && isVisible()) {
        reposition();
    }
    return QFrame::eventFilter(pObj, pEvent);
}

void WOnScreenKeyboard::reposition() {
    QWidget* pWindow = parentWidget();
    const int height = kRowCount * kKeyHeight + 2 * (kRowCount + 1) + 2;
    setGeometry(0, pWindow->height() - height, pWindow->width(), height);
}

void WOnScreenKeyboard::sendKey(int key, const QString& text) {
    QWidget* pTarget = QApplication::focusWidget();
    if (!pTarget) {
        return;
    }
    QKeyEvent press(QEvent::KeyPress, key, Qt::NoModifier, text);
    QApplication::sendEvent(pTarget, &press);
    QKeyEvent release(QEvent::KeyRelease, key, Qt::NoModifier, text);
    QApplication::sendEvent(pTarget, &release);
}

void WOnScreenKeyboard::setShifted(bool shifted) {
    m_shifted = shifted;
    for (QPushButton* pKey : std::as_const(m_letterKeys)) {
        const QString letter = pKey->property("letter").toString();
        pKey->setText(shifted ? letter.toUpper() : letter);
    }
    if (m_pShiftKey) {
        m_pShiftKey->setProperty("on", shifted);
        m_pShiftKey->style()->unpolish(m_pShiftKey);
        m_pShiftKey->style()->polish(m_pShiftKey);
    }
}

void WOnScreenKeyboard::buildKeys() {
    auto* pGrid = new QGridLayout(this);
    pGrid->setContentsMargins(2, 4, 2, 2);
    pGrid->setSpacing(2);

    // One key: `span` grid columns wide, starting at `col` in row `row`.
    auto addKey = [this, pGrid](int row, int col, int span, const QString& text, bool special) {
        auto* pKey = new QPushButton(text, this);
        pKey->setFocusPolicy(Qt::NoFocus); // never steal the focus from the search box
        pKey->setMinimumHeight(kKeyHeight);
        pKey->setProperty("special", special);
        pKey->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        pGrid->addWidget(pKey, row, col, 1, span);
        return pKey;
    };
    auto addChar = [this, addKey](int row, int col, int span, const QString& text, bool letter) {
        QPushButton* pKey = addKey(row, col, span, text, false);
        if (letter) {
            pKey->setProperty("letter", text);
            m_letterKeys.append(pKey);
        }
        connect(pKey, &QPushButton::clicked, this, [this, pKey, letter]() {
            const QString typed = pKey->text();
            sendKey(0, typed);
            if (letter && m_shifted) {
                setShifted(false); // shift applies to one letter
            }
        });
    };

    // Row 0: 1 2 3 4 5 6 7 8 9 0 + backspace
    const QString digits = QStringLiteral("1234567890");
    for (int i = 0; i < digits.size(); ++i) {
        addChar(0, i * 2, 2, digits.mid(i, 1), false);
    }
    QPushButton* pBack = addKey(0, 20, 2, QStringLiteral("⌫"), true);
    pBack->setAutoRepeat(true);
    connect(pBack, &QPushButton::clicked, this, [this]() { sendKey(Qt::Key_Backspace, QString()); });

    // Row 1: q-p, indented by half a key
    const QString row1 = QStringLiteral("qwertyuiop");
    for (int i = 0; i < row1.size(); ++i) {
        addChar(1, i * 2, 2, row1.mid(i, 1), true);
    }
    // Row 2: a-l and ":" (search filters like bpm:120 or key:Am)
    const QString row2 = QStringLiteral("asdfghjkl");
    for (int i = 0; i < row2.size(); ++i) {
        addChar(2, 1 + i * 2, 2, row2.mid(i, 1), true);
    }
    addChar(2, 19, 2, QStringLiteral(":"), false);
    // Row 3: shift, z-m, - and .
    m_pShiftKey = addKey(3, 0, 3, QStringLiteral("SHIFT"), true);
    connect(m_pShiftKey, &QPushButton::clicked, this, [this]() { setShifted(!m_shifted); });
    const QString row3 = QStringLiteral("zxcvbnm");
    for (int i = 0; i < row3.size(); ++i) {
        addChar(3, 3 + i * 2, 2, row3.mid(i, 1), true);
    }
    addChar(3, 17, 2, QStringLiteral("-"), false);
    addChar(3, 19, 2, QStringLiteral("."), false);
    // Row 4: hide, quote, space, clear, search
    QPushButton* pHide = addKey(4, 0, 3, QStringLiteral("HIDE"), true);
    connect(pHide, &QPushButton::clicked, this, [this]() {
        hide();
        if (QWidget* pFocus = QApplication::focusWidget()) {
            pFocus->clearFocus(); // also lets the box know it is done
        }
    });
    addChar(4, 3, 2, QStringLiteral("\""), false);
    QPushButton* pSpace = addKey(4, 5, 10, QStringLiteral("SPACE"), true);
    connect(pSpace, &QPushButton::clicked, this, [this]() { sendKey(Qt::Key_Space, QStringLiteral(" ")); });
    QPushButton* pClear = addKey(4, 15, 3, QStringLiteral("CLEAR"), true);
    connect(pClear, &QPushButton::clicked, this, []() {
        if (QWidget* pFocus = QApplication::focusWidget()) {
            QKeyEvent selectAll(QEvent::KeyPress, Qt::Key_A, Qt::ControlModifier);
            QApplication::sendEvent(pFocus, &selectAll);
            QKeyEvent del(QEvent::KeyPress, Qt::Key_Delete, Qt::NoModifier);
            QApplication::sendEvent(pFocus, &del);
        }
    });
    QPushButton* pGo = addKey(4, 18, 4, QStringLiteral("SEARCH"), true);
    connect(pGo, &QPushButton::clicked, this, [this]() {
        sendKey(Qt::Key_Return, QStringLiteral("\r"));
        hide();
    });

    for (int c = 0; c < kGridUnits; ++c) {
        pGrid->setColumnStretch(c, 1);
    }
}
