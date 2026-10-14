#include "widget/weffectselector.h"

#include <QAbstractItemView>
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
    // The list scrolls with a finger drag (the skin's "combobox-popup: 0" makes it a plain list with
    // a scroll bar instead of a menu with up / down arrows).
    view()->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    QScroller::grabGesture(view()->viewport(), QScroller::LeftMouseButtonGesture);
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

/// This opens the popup. Overrides showPopup() so we can set the visibility control,
/// both when clicking the down arrow and when triggering the control.
void WEffectSelector::showPopup() {
    if (count() > 0) {
        // The list is as wide as the box by default, which cut long effect names off ("Key C...rison").
        // Measured with the font the skin gives the list, so make that apply first.
        view()->ensurePolished();
        const QFontMetrics metrics(view()->font());
        int widest = width();
        for (int i = 0; i < count(); ++i) {
            widest = std::max(widest, metrics.horizontalAdvance(itemText(i)) + 72);
        }
        view()->setMinimumWidth(widest);
        QComboBox::showPopup();
        // Qt sizes the popup window to the box; make it as wide as the names and keep it on the
        // screen (this selector sits at the right edge, so the list grows leftwards).
        if (QWidget* pPopup = view()->window(); pPopup && pPopup != window()) {
            const QScreen* pScreen = screen();
            const QRect avail = pScreen ? pScreen->availableGeometry() : QRect(0, 0, 1024, 600);
            QRect rect = pPopup->geometry();
            rect.setWidth(std::min(std::max(rect.width(), widest + view()->verticalScrollBar()->sizeHint().width()),
                    avail.width() - 16));
            rect.moveLeft(std::clamp(rect.left(), avail.left() + 8, avail.right() - rect.width() - 8));
            pPopup->setGeometry(rect);
        }
        emit presetListVisibleChanged(true);
    }
}

/// Same as showPopup(), override to set the visibility control for both GUI and
/// control changes
void WEffectSelector::hidePopup() {
    QComboBox::hidePopup();
    emit presetListVisibleChanged(false);
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
    }

    return QComboBox::event(pEvent);
}

