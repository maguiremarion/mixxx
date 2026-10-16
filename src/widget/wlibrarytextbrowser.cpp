#include "widget/wlibrarytextbrowser.h"

#include <QAbstractTextDocumentLayout>
#include <QKeyEvent>
#include <QPushButton>
#include <QScrollBar>
#include <QTextBlock>
#include <QTextDocument>

#include "control/controlobject.h"
#include "moc_wlibrarytextbrowser.cpp"

namespace {
const QString kDriveAnchor = QStringLiteral("refresh-drives");
constexpr int kDriveButtonHeight = 40;
} // namespace

WLibraryTextBrowser::WLibraryTextBrowser(QWidget* parent)
        : QTextBrowser(parent),
          m_pDriveButton(new QPushButton(QStringLiteral("  ") + tr("Refresh Drives"), viewport())) {
    m_pDriveButton->setObjectName(QStringLiteral("DriveRefreshBtn"));
    m_pDriveButton->setFocusPolicy(Qt::NoFocus);
    m_pDriveButton->hide();
    connect(m_pDriveButton, &QPushButton::clicked, this, []() {
        // [App],refresh_drives is a push button: 1 then 0
        const ConfigKey key(QStringLiteral("[App]"), QStringLiteral("refresh_drives"));
        ControlObject::set(key, 1.0);
        ControlObject::set(key, 0.0);
    });
    connect(document(), &QTextDocument::contentsChanged, this, &WLibraryTextBrowser::updateDriveButton);
    connect(document()->documentLayout(),
            &QAbstractTextDocumentLayout::documentSizeChanged,
            this,
            &WLibraryTextBrowser::updateDriveButton);
    connect(verticalScrollBar(), &QScrollBar::valueChanged, this, &WLibraryTextBrowser::updateDriveButton);
}

QString WLibraryTextBrowser::refreshDrivesSectionHtml() {
    // The second paragraph is only room (a tall empty line holding the anchor): the button goes there.
    return QStringLiteral(
                   "<p style=\"font-size:17px; color:#8c8c94; margin-top:26px;\">%1</p>"
                   "<p style=\"font-size:34px; margin-top:2px;\"><a name=\"%2\">&nbsp;</a></p>")
            .arg(tr("Not seeing your drive?"), kDriveAnchor);
}

void WLibraryTextBrowser::updateDriveButton() {
    if (!m_pDriveButton) {
        return;
    }
    QTextBlock found;
    for (QTextBlock block = document()->begin(); block.isValid() && !found.isValid();
            block = block.next()) {
        for (QTextBlock::iterator it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment fragment = it.fragment();
            if (fragment.isValid() && fragment.charFormat().anchorNames().contains(kDriveAnchor)) {
                found = block;
                break;
            }
        }
    }
    if (!found.isValid()) {
        m_pDriveButton->hide();
        return;
    }
    const QRectF rect = document()->documentLayout()->blockBoundingRect(found);
    const int x = static_cast<int>(rect.left() + document()->documentMargin()) -
            horizontalScrollBar()->value();
    const int y = static_cast<int>(rect.top()) - verticalScrollBar()->value() + 4;
    // As wide as the line above it ("Not seeing your drive?" at 17 px): it is for the few times a
    // drive is missing, not something to draw the eye
    QFont headingFont = font();
    headingFont.setPixelSize(17);
    const int width = QFontMetrics(headingFont).horizontalAdvance(tr("Not seeing your drive?")) + 4;
    m_pDriveButton->setGeometry(x, y, width, kDriveButtonHeight);
    m_pDriveButton->show();
    m_pDriveButton->raise();
}

void WLibraryTextBrowser::resizeEvent(QResizeEvent* pEvent) {
    QTextBrowser::resizeEvent(pEvent);
    updateDriveButton();
}

void WLibraryTextBrowser::showEvent(QShowEvent* pEvent) {
    QTextBrowser::showEvent(pEvent);
    updateDriveButton();
}

bool WLibraryTextBrowser::hasFocus() const {
    return QWidget::hasFocus();
}

void WLibraryTextBrowser::setFocus() {
    QWidget::setFocus();
}

void WLibraryTextBrowser::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Left && event->modifiers() & Qt::ControlModifier) {
        event->ignore();
        return;
    }
    QTextBrowser::keyPressEvent(event);
}
