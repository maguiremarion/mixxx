#include "widget/weffectselector.h"
#include "util/screeninset.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QListWidget>
#include <QScreen>
#include <QScrollBar>
#include <QScroller>
#include <QStyleOptionComboBox>
#include <QStylePainter>
#include <QtDebug>
#include <algorithm>

#include "effects/effectsmanager.h"
#include "effects/visibleeffectslist.h"
#include "moc_weffectselector.cpp"
#include "widget/effectwidgetutils.h"

namespace {
// How long the list stays after the last FX SELECT press before that effect is loaded.
constexpr int kPickCommitMs = 2000;

/// The font the skin gives the effect list rows (style.qss: font-size 18px, font-weight 600), for measuring names.
/// Measured with an explicit font: the list's own font is only set once it has been styled, and names measured with
/// the plain font came out too narrow (the longest ones were cut off). Bold is the widest the rows can be.
QFont effectListFont(const QFont& base) {
    QFont font = base;
    font.setPixelSize(18);
    font.setWeight(QFont::Bold);
    return font;
}
} // namespace

WEffectSelector::WEffectSelector(QWidget* pParent, EffectsManager* pEffectsManager)
        : QComboBox(pParent),
          WBaseWidget(this),
          m_pEffectsManager(pEffectsManager),
          m_pVisibleEffectsList(pEffectsManager->getVisibleEffectsList()) {
    // Prevent this widget from getting focused by Tab/Shift+Tab
    // to avoid interfering with using the library via keyboard.
    // Allow click focus though so the list can always be opened by mouse,
    // see https://github.com/mixxxdj/mixxx/issues/10184
    setFocusPolicy(Qt::ClickFocus);
    m_pickTimer.setSingleShot(true);
    m_pickTimer.setInterval(kPickCommitMs);
    connect(&m_pickTimer, &QTimer::timeout, this, &WEffectSelector::commitPick);
}

void WEffectSelector::setup(const QDomNode& node, const SkinContext& context) {
    // EffectWidgetUtils propagates NULLs so this is all safe.
    EffectChainPointer pChainSlot = EffectWidgetUtils::getEffectChainFromNode(
            node, context, m_pEffectsManager);
    m_pEffectSlot = EffectWidgetUtils::getEffectSlotFromNode(
            node, context, pChainSlot);

    if (m_pEffectSlot != nullptr) {
        connect(m_pVisibleEffectsList.data(),
                &VisibleEffectsList::visibleEffectsListChanged,
                this,
                &WEffectSelector::populate);
        connect(m_pEffectSlot.data(),
                &EffectSlot::effectChanged,
                this,
                &WEffectSelector::slotEffectUpdated);
        connect(this,
                QOverload<int>::of(&QComboBox::activated),
                this,
                &WEffectSelector::slotEffectSelected);
        // Show/hide the effects list
        connect(m_pEffectSlot.data(),
                &EffectSlot::presetListShowRequest,
                this,
                &WEffectSelector::slotPresetListShowRequest);
        // Step through the list (controller FX SELECT)
        connect(m_pEffectSlot.data(),
                &EffectSlot::presetPickRequest,
                this,
                &WEffectSelector::slotPresetPickRequest);
        // Callback when list is shown/hidden
        connect(this,
                &WEffectSelector::presetListVisibleChanged,
                m_pEffectSlot.data(),
                &EffectSlot::slotPresetListVisibleChanged);
    } else {
        SKIN_WARNING(node,
                context,
                QStringLiteral("EffectSelector node could not attach to effect "
                               "slot."));
    }

    populate();
}

void WEffectSelector::populate() {
    blockSignals(true);
    clear(); // Should hide popup

    // A to Z by the name that is shown (the list arrives in the order the effects were registered)
    QList<EffectManifestPointer> visibleEffectManifests = m_pVisibleEffectsList->getList();
    std::sort(visibleEffectManifests.begin(),
            visibleEffectManifests.end(),
            [](const EffectManifestPointer& a, const EffectManifestPointer& b) {
                return QString::localeAwareCompare(a->displayName(), b->displayName()) < 0;
            });
    // Add empty item: no effect
    addItem(kNoEffectString);
    setItemData(0, QVariant(tr("No effect loaded.")), Qt::ToolTipRole);

    for (int i = 0; i < visibleEffectManifests.size(); ++i) {
        const EffectManifestPointer pManifest = visibleEffectManifests.at(i);
        // Full names in the list (the popup widens to fit them, see showPopup()); the closed box
        // shrinks the text to fit instead (see paintEvent()).
        addItem(pManifest->displayName(), QVariant(pManifest->uniqueId()));

        QString name = pManifest->name();
        QString description = pManifest->description();
        // <b> makes the effect name bold. Also, like <span> it serves as hack
        // to get Qt to treat the string as rich text so it automatically wraps long lines.
        setItemData(i + 1,
                QVariant(QStringLiteral("<b>") + name +
                        QStringLiteral("</b><br/>") + description),
                Qt::ToolTipRole);
    }

    slotEffectUpdated();
    blockSignals(false);
}

void WEffectSelector::slotEffectSelected(int newIndex) {
    const EffectManifestPointer pManifest =
            m_pEffectsManager->getBackendManager()->getManifestFromUniqueId(
                    itemData(newIndex).toString());

    m_pEffectSlot->loadEffectWithDefaults(pManifest);

    setBaseTooltip(itemData(newIndex, Qt::ToolTipRole).toString());
    // Clicking an effect item moves keyboard focus to the list view.
    // Move focus back to the previously focused library widget.
    ControlObject::set(ConfigKey("[Library]", "refocus_prev_widget"), 1);
}

void WEffectSelector::slotEffectUpdated() {
    int newIndex;

    if (m_pEffectSlot != nullptr) {
        if (m_pEffectSlot->getManifest() != nullptr) {
            EffectManifestPointer pManifest = m_pEffectSlot->getManifest();
            newIndex = findData(QVariant(pManifest->uniqueId()));
        } else {
            newIndex = findData(QVariant());
        }
    } else {
        newIndex = findData(QVariant());
    }

    if (kEffectDebugOutput) {
        qDebug() << "WEffectSelector::slotEffectUpdated"
                 << "old" << itemData(currentIndex())
                 << "new" << itemData(newIndex);
    }

    if (newIndex != -1 && newIndex != currentIndex()) {
        setCurrentIndex(newIndex);
        setBaseTooltip(itemData(newIndex, Qt::ToolTipRole).toString());
    }
}

void WEffectSelector::slotPresetListShowRequest(bool show) {
    if (!isVisible()) {
        return;
    }
    if (show) {
        showPopup();
    } else {
        hidePopup();
    }
}

// ONE list serves a tap on the effect name and the controller's FX SELECT button. It is not the combo box's own popup:
// on Wayland a popup that was not opened by a tap is not placed by its box but floats in the middle of the screen, and
// the two kinds of list could be open at once, leaving one stuck. This is a tooltip-type window, placed relative to the
// main window, which sits right under the name, above the waveforms, and takes taps and finger drags.

void WEffectSelector::slotPresetPickRequest(int direction) {
    if (!isVisible() || count() < 1) {
        return;
    }
    if (!isListOpen()) {
        // The first press only opens the list, on the effect that is loaded now; the next presses step
        m_indexBefore = currentIndex();
        m_controllerDriven = true;
        openList(currentIndex());
        m_pickTimer.start();
        return;
    }
    if (!m_controllerDriven) {
        // The list was opened by a tap: the controller takes over from where the list is
        m_controllerDriven = true;
        m_indexBefore = currentIndex();
    }
    // Wrap around the whole list (the first row is "no effect")
    const int next = (m_pList->currentRow() + direction + count()) % count();
    // The box shows the name being looked at, the list highlights it; nothing is loaded yet
    setCurrentIndex(next);
    m_pList->setCurrentRow(next);
    m_pList->scrollToItem(m_pList->item(next), QAbstractItemView::PositionAtCenter);
    m_pickTimer.start();
}

bool WEffectSelector::isListOpen() const {
    return m_pList && m_pList->isVisible();
}

void WEffectSelector::openList(int highlightRow) {
    constexpr int kRowHeight = 40;
    constexpr int kMaxVisibleRows = 7;
    constexpr int kScrollBarWidth = 22;
    if (!m_pList) {
        m_pList = new QListWidget(window());
        m_pList->setWindowFlags(Qt::ToolTip | Qt::FramelessWindowHint);
        m_pList->setAttribute(Qt::WA_ShowWithoutActivating);
        m_pList->setFocusPolicy(Qt::NoFocus);
        m_pList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        m_pList->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        // Scrolls with a finger drag
        m_pList->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
        QScroller::grabGesture(m_pList->viewport(), QScroller::LeftMouseButtonGesture);
        // The look of the skin's list (style.qss, "#EffectSelector QAbstractItemView"): grey, light text, finger-sized rows
        m_pList->setStyleSheet(QStringLiteral(
                "QListWidget { background-color: #262626; color: #e5e6ea; font-size: 18px; font-weight: 600;"
                "  border: 1px solid #3a3a3a; outline: 0;"
                "  selection-background-color: #3478f2; selection-color: #ffffff; }"
                // (no ::item:selected rule: a styled selected item is laid out with different padding, which moved
                // the highlighted name to the right)
                "QListWidget::item { min-height: 40px; padding: 0 12px; }"
                "QScrollBar:vertical { background: #1c1c1c; width: 22px; border: none; }"
                "QScrollBar::handle:vertical { background: #5a5a60; min-height: 40px; border-radius: 0; }"
                "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0px; }"));
        connect(m_pList, &QListWidget::itemClicked, this, [this](QListWidgetItem* pItem) {
            // A tap on a name loads that effect now
            const int row = m_pList->row(pItem);
            closeList(false);
            if (row >= 0 && row < count()) {
                setCurrentIndex(row);
                slotEffectSelected(row);
            }
        });
    }
    // The names (they can change when effects are hidden or added)
    if (m_pList->count() != count()) {
        m_pList->clear();
        for (int i = 0; i < count(); ++i) {
            m_pList->addItem(itemText(i));
        }
    }

    // As wide as the longest name needs (the row padding and the border are the style above), no wider; plus the scroll bar
    // when the list scrolls
    const QFontMetrics metrics(effectListFont(m_pList->font()));
    int widest = 0;
    for (int i = 0; i < count(); ++i) {
        widest = std::max(widest, metrics.horizontalAdvance(itemText(i)));
    }
    const int visibleRows = std::min(count(), kMaxVisibleRows);
    const int listWidth = widest + 24 + 2 + (count() > kMaxVisibleRows ? kScrollBarWidth : 0);
    const int listHeight = visibleRows * kRowHeight + 2;
    m_pList->resize(listWidth, listHeight);

    // Under the box, its right edges lined up (the box is at the right edge of the screen, so the list grows
    // leftwards); above it when there is no room below
    const QScreen* pScreen = screen();
    const QRect avail = mixxx::kiosk::screenMinusInset(
            pScreen ? pScreen->availableGeometry() : QRect(0, 0, 1024, 600));
    const QPoint boxBottomRight = mapToGlobal(QPoint(width(), height()));
    const int x = std::clamp(boxBottomRight.x() - listWidth, avail.left() + 4, avail.right() - listWidth - 4);
    int y = boxBottomRight.y() + 2;
    if (y + listHeight > avail.bottom()) {
        y = mapToGlobal(QPoint(0, 0)).y() - listHeight - 2;
    }
    m_pList->move(x, y);
    m_pList->setCurrentRow(highlightRow);
    m_pList->show();
    m_pList->raise();
    m_pList->scrollToItem(m_pList->item(highlightRow), QAbstractItemView::PositionAtCenter);

    // A tap anywhere else closes the list (a tooltip-type window has no grab that would do it)
    qApp->installEventFilter(this);
    emit presetListVisibleChanged(true);
}

void WEffectSelector::closeList(bool cancel) {
    m_pickTimer.stop();
    qApp->removeEventFilter(this);
    const bool wasOpen = isListOpen();
    if (m_pList) {
        m_pList->hide();
    }
    if (cancel && m_controllerDriven && m_indexBefore >= 0 && m_indexBefore < count()) {
        // Stepping was given up: show the loaded effect again
        setCurrentIndex(m_indexBefore);
    }
    m_controllerDriven = false;
    if (wasOpen) {
        emit presetListVisibleChanged(false);
    }
}

bool WEffectSelector::eventFilter(QObject* pObj, QEvent* pEvent) {
    if (isListOpen() &&
            (pEvent->type() == QEvent::MouseButtonPress || pEvent->type() == QEvent::TouchBegin)) {
        const QWidget* pWidget = qobject_cast<QWidget*>(pObj);
        // A press on the list itself is a tap on a name or the start of a scroll; a press on the box toggles the list
        // (showPopup() below). Anywhere else: close.
        if (pWidget && pWidget != this && pWidget != m_pList && !m_pList->isAncestorOf(pWidget)) {
            closeList(true);
        }
    }
    return QComboBox::eventFilter(pObj, pEvent);
}

void WEffectSelector::commitPick() {
    if (!isListOpen()) {
        return;
    }
    const int row = m_pList->currentRow();
    const int before = m_indexBefore;
    closeList(false);
    if (row >= 0 && row != before) {
        slotEffectSelected(row);
    }
}

/// Opens (or closes, when it is already open) the list. Overrides showPopup() so a tap on the box, the show_preset_list
/// control and the controller's FX SELECT all use the same list.
void WEffectSelector::showPopup() {
    if (isListOpen()) {
        closeList(true);
        return;
    }
    if (count() > 0) {
        m_controllerDriven = false;
        openList(currentIndex());
    }
}

void WEffectSelector::hidePopup() {
    closeList(true);
}

void WEffectSelector::paintEvent(QPaintEvent* pEvent) {
    Q_UNUSED(pEvent);
    // The frame and arrow as the skin styles them, then the effect name drawn here: shrunk until
    // it fits the box (down to a minimum), only shortened if even that is too long. Cutting
    // names off with "..." looked bad, and the list below shows them in full anyway.
    QStyleOptionComboBox option;
    initStyleOption(&option);
    const QString text = option.currentText;
    option.currentText.clear();
    option.currentIcon = QIcon();

    QStylePainter painter(this);
    painter.drawComplexControl(QStyle::CC_ComboBox, option);

    // The whole box, a few pixels in from each edge, with the text centred in it: the style's
    // "edit field" leaves room on the right for a drop-down arrow, which is hidden in this skin.
    const QRect textRect = rect().adjusted(6, 0, -6, 0);
    QFont textFont = font();
    int pixelSize = textFont.pixelSize() > 0 ? textFont.pixelSize() : QFontInfo(textFont).pixelSize();
    constexpr int kMinPixelSize = 10;
    while (pixelSize > kMinPixelSize &&
            QFontMetrics(textFont).horizontalAdvance(text) > textRect.width()) {
        textFont.setPixelSize(--pixelSize);
    }
    painter.setFont(textFont);
    painter.setPen(m_textColor.isValid() ? m_textColor : palette().color(QPalette::ButtonText));
    painter.drawText(textRect,
            Qt::AlignVCenter | Qt::AlignHCenter,
            QFontMetrics(textFont).elidedText(text, Qt::ElideRight, textRect.width()));
}

bool WEffectSelector::event(QEvent* pEvent) {
    if (pEvent->type() == QEvent::ToolTip) {
        updateTooltip();
    } else if (pEvent->type() == QEvent::Wheel && !hasFocus()) {
        // don't change effect by scrolling hovered effect selector
        return true;
    } else if (pEvent->type() == QEvent::Hide) {
        // The page with the selector was left while its list was open
        closeList(true);
    }

    return QComboBox::event(pEvent);
}

