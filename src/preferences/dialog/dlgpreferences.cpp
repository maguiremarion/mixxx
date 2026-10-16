#include "preferences/dialog/dlgpreferences.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QComboBox>
#include <QElapsedTimer>
#include <QMouseEvent>
#include <QPointer>
#include <QScrollBar>
#include <QVariantAnimation>
#include <QDialog>
#include <QEvent>
#include <QMoveEvent>
#include <QPalette>
#include <QResizeEvent>
#include <QHBoxLayout>
#include <QPainter>
#include <QLabel>
#include <QRegularExpression>
#include <QScreen>
#include <QTimer>
#include <QScrollArea>
#include <QStyledItemDelegate>
#include <QSlider>
#include <QStyle>
#include <QtGlobal>

#include "control/controlobject.h"
#include "controllers/dlgprefcontrollers.h"
#include "library/library.h"
#include "library/trackcollectionmanager.h"
#include "moc_dlgpreferences.cpp"
#include "controllers/dlgprefcontroller.h"
#include "preferences/dialog/dlgpreflibrary.h"
#include "preferences/dialog/dlgprefsound.h"
#include "util/color/color.h"
#include "util/desktophelper.h"
#include "util/widgethelper.h"

#ifdef __VINYLCONTROL__
#include "preferences/dialog/dlgprefvinyl.h"
#endif // __VINYLCONTROL__

#include "preferences/dialog/dlgprefautodj.h"
#include "preferences/dialog/dlgprefcolors.h"
#include "preferences/dialog/dlgprefdeck.h"
#include "preferences/dialog/dlgprefeffects.h"
#include "preferences/dialog/dlgprefinterface.h"
#include "preferences/dialog/dlgprefmixer.h"
#include "preferences/dialog/dlgprefwaveform.h"
#include "util/cmdlineargs.h"
#include "waveform/waveformwidgetfactory.h"

#ifdef __BROADCAST__
#include "preferences/dialog/dlgprefbroadcast.h"
#endif // __BROADCAST__

#include "preferences/dialog/dlgprefbeats.h"
#include "preferences/dialog/dlgprefkey.h"
#include "preferences/dialog/dlgprefrecord.h"
#include "preferences/dialog/dlgprefreplaygain.h"

#ifdef __MODPLUG__
#include "preferences/dialog/dlgprefmodplug.h"
#endif // __MODPLUG__

#ifdef Q_OS_MACOS
#include "util/darkappearance.h"
#endif

namespace {
// Finger scrolling for the kiosk: follows the finger with a short glide, keeps gliding a little
// after the finger is lifted while moving. Used by the drop-down lists and by the Preferences pages.
class DragScroller : public QObject {
  public:
    using QObject::QObject;

    bool down() const {
        return m_down;
    }
    bool dragging() const {
        return m_dragging;
    }
    void press(int y, QScrollBar* pBar) {
        if (m_pAnimation) {
            m_pAnimation->stop();
        }
        m_down = true;
        m_dragging = false;
        m_pressY = y;
        m_lastY = y;
        m_velocity = 0.0;
        m_clock.start();
        m_lastMs = 0;
        m_startValue = pBar->value();
    }
    // Returns true while the finger is dragging (the event is then used up).
    bool move(int y, QScrollBar* pBar) {
        if (!m_down) {
            return false;
        }
        if (!m_dragging && qAbs(y - m_pressY) > 12) {
            m_dragging = true;
            m_startValue = pBar->value();
            m_pressY = y; // scrolling starts from here, so the list does not jump by the 12 px
            m_lastY = y;
        }
        if (!m_dragging) {
            return false;
        }
        const qint64 now = m_clock.elapsed();
        if (now > m_lastMs) {
            const double v = static_cast<double>(y - m_lastY) / static_cast<double>(now - m_lastMs);
            m_velocity = 0.5 * m_velocity + 0.5 * v;
            m_lastMs = now;
            m_lastY = y;
        }
        animateTo(pBar,
                qBound(pBar->minimum(), m_startValue - (y - m_pressY), pBar->maximum()),
                90,
                QEasingCurve::OutQuad);
        return true;
    }
    // Returns true if this was the end of a drag (then it is not a tap).
    bool release(QScrollBar* pBar) {
        const bool wasDragging = m_dragging;
        m_down = false;
        m_dragging = false;
        if (wasDragging) {
            const double velocity = -m_velocity; // scroll bar units per ms
            if (qAbs(velocity) > 0.25) {
                const int end = qBound(pBar->minimum(),
                        pBar->value() + static_cast<int>(velocity * 260),
                        pBar->maximum());
                animateTo(pBar, end, 420, QEasingCurve::OutCubic);
            }
        }
        return wasDragging;
    }

  private:
    void animateTo(QScrollBar* pBar, int end, int durationMs, QEasingCurve::Type curve) {
        if (!m_pAnimation) {
            m_pAnimation = new QVariantAnimation(this);
            connect(m_pAnimation, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
                if (m_pBar) {
                    m_pBar->setValue(value.toInt());
                }
            });
        }
        m_pAnimation->stop();
        m_pBar = pBar;
        m_pAnimation->setDuration(durationMs);
        m_pAnimation->setEasingCurve(curve);
        m_pAnimation->setStartValue(pBar->value());
        m_pAnimation->setEndValue(end);
        m_pAnimation->start();
    }

    QVariantAnimation* m_pAnimation = nullptr;
    QPointer<QScrollBar> m_pBar;
    QElapsedTimer m_clock;
    qint64 m_lastMs = 0;
    bool m_down = false;
    bool m_dragging = false;
    int m_pressY = 0;
    int m_lastY = 0;
    int m_startValue = 0;
    double m_velocity = 0.0; // pixels per ms, finger
};

// Rows of a drop-down list drawn by hand: flat, the text always at the same place. (The style
// sheet's hover / selected states moved the text of the row under the finger.)
class FlatListDelegate : public QStyledItemDelegate {
  public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter* pPainter,
            const QStyleOptionViewItem& option,
            const QModelIndex& index) const override {
        const bool selected = option.state.testFlag(QStyle::State_Selected);
        // Only the selected row is marked. There is no hover colour: a finger leaves no "mouse
        // left" event when a drag takes over, which kept the first-touched row grey.
        pPainter->save();
        pPainter->fillRect(option.rect,
                selected ? QColor(0x34, 0x78, 0xf2) : QColor(0x18, 0x18, 0x18));
        pPainter->setFont(option.font);
        pPainter->setPen(selected ? QColor(Qt::white) : QColor(0xe5, 0xe6, 0xea));
        const QRect textRect = option.rect.adjusted(12, 0, -12, 0);
        pPainter->drawText(textRect,
                Qt::AlignVCenter | Qt::AlignLeft,
                QFontMetrics(option.font)
                        .elidedText(index.data(Qt::DisplayRole).toString(),
                                Qt::ElideMiddle,
                                textRect.width()));
        pPainter->restore();
    }
    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex&) const override {
        return QSize(option.rect.width(), 44);
    }
};

// Drop-down lists in the kiosk: at most five rows (they scroll with the wide scroll bar from
// dialogs.qss when there are more), text cut with "..." in the middle instead of being clipped,
// and a finger dragged over the list scrolls it (the highlight used to follow the finger). A tap,
// with no drag, still picks the row under the finger. The drag is handled here, before any
// widget's own handling, because the list's popup also gets moves of a press that began on the
// combo box itself.
class KioskDropDownFilter : public QObject {
  public:
    explicit KioskDropDownFilter(QObject* pParent)
            : QObject(pParent),
              m_listScroller(this),
              m_pageScroller(this) {
    }
    bool eventFilter(QObject* pObj, QEvent* pEvent) override {
        switch (pEvent->type()) {
        case QEvent::Polish:
            // Pop-ups and dialogs have no title bar and no close button: whoever is asked
            // something answers with the dialog's own buttons (Escape still closes it). Set before
            // the window is first shown.
            if (auto* pDialog = qobject_cast<QDialog*>(pObj)) {
                pDialog->setWindowFlag(Qt::FramelessWindowHint, true);
            }
            if (auto* pCombo = qobject_cast<QComboBox*>(pObj)) {
                pCombo->setMaxVisibleItems(5);
                pCombo->setItemDelegate(new FlatListDelegate(pCombo));
                if (auto* pView = pCombo->view()) {
                    pView->setTextElideMode(Qt::ElideMiddle);
                    pView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
                    pView->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
                }
            }
            return false;
        case QEvent::Show:
            if (pObj->inherits("QComboBoxPrivateContainer")) {
                // The popup list scrolls by whole rows (the scroll bar counts rows) unless told
                // otherwise: that made a drag jump from row to row. By the pixel, with the scroll
                // bar always showing so one can see where in the list it is.
                const auto prepare = [pPopup = QPointer<QObject>(pObj)]() {
                    if (!pPopup) {
                        return;
                    }
                    if (auto* pList = pPopup->findChild<QAbstractItemView*>()) {
                        pList->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
                        pList->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
                        pList->verticalScrollBar()->setSingleStep(44);
                    }
                };
                prepare();
                QTimer::singleShot(0, pObj, prepare);
            }
            return false;
        case QEvent::MouseButtonPress:
        case QEvent::MouseMove:
        case QEvent::MouseButtonRelease:
            break;
        default:
            return false;
        }
        if (m_sendingCancel) {
            return false;
        }
        auto* pWidget = qobject_cast<QWidget*>(pObj);
        if (!pWidget) {
            return false;
        }
        auto* pMouse = static_cast<QMouseEvent*>(pEvent);
        const QEvent::Type type = pEvent->type();
        if (auto* pView = qobject_cast<QAbstractItemView*>(pWidget->parent());
                pView && pView->viewport() == pWidget && pView->parentWidget() &&
                pView->parentWidget()->inherits("QComboBoxPrivateContainer")) {
            return filterList(pView, pMouse);
        }
        // A press anywhere on a Preferences page (not on a list or slider, which scroll or slide
        // themselves) can start scrolling the page.
        if (type == QEvent::MouseButtonPress) {
            m_area = nullptr;
            m_pendingCombo = nullptr;
            m_target = nullptr;
            if (pMouse->button() != Qt::LeftButton ||
                    !pWidget->window()->inherits("DlgPreferences")) {
                return false;
            }
            for (QWidget* pAncestor = pWidget; pAncestor; pAncestor = pAncestor->parentWidget()) {
                if (auto* pArea = qobject_cast<QScrollArea*>(pAncestor)) {
                    m_area = pArea;
                    break;
                }
                if (pAncestor->inherits("QAbstractItemView") ||
                        pAncestor->inherits("QAbstractSlider") ||
                        pAncestor->inherits("QAbstractScrollArea") ||
                        pAncestor->inherits("QAbstractSpinBox") ||
                        pAncestor->inherits("QLineEdit")) {
                    return false;
                }
                if (pAncestor->window() == pAncestor) {
                    return false;
                }
            }
            if (!m_area || m_area->verticalScrollBar()->maximum() <= 0) {
                m_area = nullptr;
                return false;
            }
            m_pageScroller.press(pMouse->globalPosition().toPoint().y(), m_area->verticalScrollBar());
            m_target = pWidget;
            // A drop-down opens on the press: wait for the lift instead, so a drag can scroll
            if (auto* pCombo = qobject_cast<QComboBox*>(pWidget)) {
                m_pendingCombo = pCombo;
                return true;
            }
            // A label, a frame or the page itself ignores the press, and then no moves follow
            // it: take the press so they arrive. (Buttons take it themselves.)
            return !pWidget->inherits("QAbstractButton");
        }
        if (!m_area || !m_pageScroller.down()) {
            return false;
        }
        QScrollBar* pBar = m_area->verticalScrollBar();
        if (type == QEvent::MouseMove) {
            const bool wasDragging = m_pageScroller.dragging();
            if (!m_pageScroller.move(pMouse->globalPosition().toPoint().y(), pBar)) {
                return m_pendingCombo != nullptr;
            }
            if (!wasDragging && m_target && !m_pendingCombo) {
                // cancel the press the widget under the finger got: release outside of it
                m_sendingCancel = true;
                QMouseEvent cancel(QEvent::MouseButtonRelease,
                        QPointF(-1000, -1000),
                        pMouse->globalPosition(),
                        Qt::LeftButton,
                        Qt::NoButton,
                        Qt::NoModifier);
                QCoreApplication::sendEvent(m_target, &cancel);
                m_sendingCancel = false;
            }
            return true;
        }
        // release
        const bool wasDragging = m_pageScroller.release(pBar);
        QPointer<QComboBox> pCombo = m_pendingCombo;
        m_pendingCombo = nullptr;
        m_area = nullptr;
        if (pCombo && !wasDragging) {
            QTimer::singleShot(0, pCombo, [pCombo]() {
                if (pCombo) {
                    pCombo->showPopup();
                }
            });
        }
        return wasDragging || pCombo;
    }

  private:
    bool filterList(QAbstractItemView* pView, QMouseEvent* pMouse) {
        auto* pBar = pView->verticalScrollBar();
        const int y = pMouse->position().toPoint().y();
        if (pMouse->type() == QEvent::MouseButtonRelease) {
            return m_listScroller.release(pBar); // the end of a drag is not a pick
        }
        const bool left = pMouse->type() == QEvent::MouseButtonPress
                ? pMouse->button() == Qt::LeftButton
                : pMouse->buttons().testFlag(Qt::LeftButton);
        if (!left) {
            return false;
        }
        if (pMouse->type() == QEvent::MouseButtonPress || !m_listScroller.down()) {
            m_listScroller.press(y, pBar);
            return false;
        }
        const bool wasDragging = m_listScroller.dragging();
        const bool dragging = m_listScroller.move(y, pBar);
        if (dragging && !wasDragging) {
            // The list took the press (the row stays "pressed" and drawn as such): end it
            // outside every row, which picks nothing.
            m_sendingCancel = true;
            QMouseEvent cancel(QEvent::MouseButtonRelease,
                    QPointF(-1000, -1000),
                    pMouse->globalPosition(),
                    Qt::LeftButton,
                    Qt::NoButton,
                    Qt::NoModifier);
            QCoreApplication::sendEvent(pView->viewport(), &cancel);
            QEvent leave(QEvent::Leave);
            QCoreApplication::sendEvent(pView->viewport(), &leave);
            pView->viewport()->update();
            m_sendingCancel = false;
        }
        return dragging;
    }

    DragScroller m_listScroller;
    DragScroller m_pageScroller;
    QPointer<QScrollArea> m_area;
    QPointer<QWidget> m_target;
    QPointer<QComboBox> m_pendingCombo;
    bool m_sendingCancel = false;
};
} // namespace

DlgPreferences::DlgPreferences(
        std::shared_ptr<mixxx::ScreensaverManager> pScreensaverManager,
        std::shared_ptr<mixxx::skin::SkinLoader> pSkinLoader,
        std::shared_ptr<SoundManager> pSoundManager,
        std::shared_ptr<ControllerManager> pControllerManager,
        std::shared_ptr<VinylControlManager> pVCManager,
        std::shared_ptr<EffectsManager> pEffectsManager,
        std::shared_ptr<SettingsManager> pSettingsManager,
        std::shared_ptr<Library> pLibrary,
        bool includeWaveformPreferences)
        : m_allPages(),
          m_pConfig(pSettingsManager->settings()),
          m_pageSizeHint(QSize(0, 0)) {
    setupUi(this);
    fixSliderStyle();
    contentsTreeWidget->setHeaderHidden(true);

    // Add '&' to default button labels to always have Alt shortcuts, indpependent
    // of operating system.
    //: Preferences standard buttons: consider the other buttons to choose a unique Alt hotkey (&)
    buttonBox->button(QDialogButtonBox::Help)->setText(tr("&Help"));
    //: Preferences standard buttons: consider the other buttons to choose a unique Alt hotkey (&)
    buttonBox->button(QDialogButtonBox::RestoreDefaults)->setText(tr("&Restore Defaults"));
    //: Preferences standard buttons: consider the other buttons to choose a unique Alt hotkey (&)
    buttonBox->button(QDialogButtonBox::Apply)->setText(tr("&Apply"));
    //: Preferences standard buttons: consider the other buttons to choose a unique Alt hotkey (&)
    buttonBox->button(QDialogButtonBox::Cancel)->setText(tr("&Cancel"));
    //: Preferences standard buttons: consider the other buttons to choose a unique Alt hotkey (&)
    buttonBox->button(QDialogButtonBox::Ok)->setText(tr("&Ok"));

    // No icons on OK / Apply / Cancel: text only (flat look).
    for (QAbstractButton* pButton : buttonBox->buttons()) {
        pButton->setIcon(QIcon());
    }

    connect(buttonBox,
            QOverload<QAbstractButton*>::of(&QDialogButtonBox::clicked),
            this,
            &DlgPreferences::slotButtonPressed);

    connect(contentsTreeWidget,
            &QTreeWidget::currentItemChanged,
            this,
            &DlgPreferences::changePage);

    while (pagesWidget->count() > 0) {
        pagesWidget->removeWidget(pagesWidget->currentWidget());
    }

    // Check the text color of the palette for whether to use dark or light icons
    // (the kiosk's pages are dark whatever the system palette is: always the light icons)
    if (kioskMode() || !Color::isDimColor(palette().text().color())) {
        m_iconsPath.setPath(":/images/preferences/light/");
    } else {
        m_iconsPath.setPath(":/images/preferences/dark/");
    }

    // Construct page widgets and associated sidebar items
    m_pSoundDlg = std::make_unique<DlgPrefSound>(this, pSoundManager, m_pConfig);
    m_soundPage = PreferencesPage(
            m_pSoundDlg.get(),
            new QTreeWidgetItem(contentsTreeWidget, QTreeWidgetItem::Type));
    addPageWidget(m_soundPage,
            kioskMode() ? tr("Sound") : tr("Sound Hardware"),
            "ic_preferences_soundhardware.svg");
    if (kioskMode()) {
        // Output, Input and Settings are entries of their own under "Sound", like a controller's
        // page is under "Controllers".
        auto makeSoundItem = [this](const QString& title) {
            auto* pItem = new QTreeWidgetItem(m_soundPage.pTreeItem, QTreeWidgetItem::Type);
            pItem->setText(0, title);
            pItem->setTextAlignment(0, Qt::AlignLeft | Qt::AlignVCenter);
            pItem->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
            return pItem;
        };
        m_pSoundOutputItem = makeSoundItem(tr("Output"));
        m_pSoundInputItem = makeSoundItem(tr("Input"));
        m_pSoundSettingsItem = makeSoundItem(tr("Settings"));
        contentsTreeWidget->expandItem(m_soundPage.pTreeItem);
    }

    DlgPrefLibrary* plibraryPage = new DlgPrefLibrary(this, m_pConfig, pLibrary);
    connect(plibraryPage,
            &DlgPrefLibrary::scanLibrary,
            pLibrary->trackCollectionManager(),
            &TrackCollectionManager::startLibraryScan);
    addPageWidget(PreferencesPage(plibraryPage,
                          new QTreeWidgetItem(contentsTreeWidget, QTreeWidgetItem::Type)),
            tr("Library"),
            "ic_preferences_library.svg");

    QTreeWidgetItem* pControllerRootItem =
            new QTreeWidgetItem(contentsTreeWidget, QTreeWidgetItem::Type);
    m_pControllersDlg = new DlgPrefControllers(
            this, m_pConfig, pControllerManager, pControllerRootItem);
    addPageWidget(PreferencesPage(m_pControllersDlg,
                          pControllerRootItem),
            tr("Controllers"),
            "ic_preferences_controllers.svg");

#ifdef __VINYLCONTROL__
    // It's important for this to be before the connect for wsound.
    // TODO(rryan) determine why/if this is still true
    addPageWidget(PreferencesPage(
                          new DlgPrefVinyl(this, pVCManager, m_pConfig),
                          new QTreeWidgetItem(contentsTreeWidget, QTreeWidgetItem::Type)),
            tr("Vinyl Control"),
            "ic_preferences_vinyl.svg");
#endif // __VINYLCONTROL__

#ifdef MIXXX_USE_QML
    if (!CmdlineArgs::Instance().isQml())
#endif
    {
        DlgPrefInterface* pInterfacePage = new DlgPrefInterface(this,
                pScreensaverManager,
                pSkinLoader,
                m_pConfig);
        connect(pInterfacePage,
                &DlgPrefInterface::tooltipModeChanged,
                this,
                &DlgPreferences::tooltipModeChanged);
        connect(pInterfacePage,
                &DlgPrefInterface::reloadUserInterface,
                this,
                &DlgPreferences::reloadUserInterface,
                Qt::DirectConnection);
        connect(pInterfacePage,
                &DlgPrefInterface::menuBarAutoHideChanged,
                this,
                &DlgPreferences::menuBarAutoHideChanged,
                Qt::DirectConnection);
        addPageWidget(PreferencesPage(pInterfacePage,
                              new QTreeWidgetItem(
                                      contentsTreeWidget, QTreeWidgetItem::Type)),
                tr("Interface"),
                "ic_preferences_interface.svg");
    }

    // The shared native page is used by both legacy and LateNightQML. In QML
    // mode QmlApplication initializes the factory as the preferences backend
    // before constructing this dialog.
    if (includeWaveformPreferences && WaveformWidgetFactory::isCreated()) {
        addPageWidget(PreferencesPage(
                              new DlgPrefWaveform(this, m_pConfig, pLibrary),
                              new QTreeWidgetItem(contentsTreeWidget, QTreeWidgetItem::Type)),
                tr("Waveforms"),
                "ic_preferences_waveforms.svg");
    }

    addPageWidget(PreferencesPage(
                          new DlgPrefColors(this, m_pConfig, pLibrary),
                          new QTreeWidgetItem(contentsTreeWidget, QTreeWidgetItem::Type)),
            tr("Colors"),
            "ic_preferences_colors.svg");

    addPageWidget(PreferencesPage(
                          new DlgPrefDeck(this, m_pConfig),
                          new QTreeWidgetItem(contentsTreeWidget, QTreeWidgetItem::Type)),
            tr("Decks"),
            "ic_preferences_decks.svg");

    addPageWidget(PreferencesPage(
                          new DlgPrefMixer(this, pEffectsManager, m_pConfig),
                          new QTreeWidgetItem(contentsTreeWidget, QTreeWidgetItem::Type)),
            tr("Mixer"),
            "ic_preferences_crossfader.svg");

    addPageWidget(PreferencesPage(
                          new DlgPrefEffects(this, m_pConfig, pEffectsManager),
                          new QTreeWidgetItem(contentsTreeWidget, QTreeWidgetItem::Type)),
            tr("Effects"),
            "ic_preferences_effects.svg");

    addPageWidget(PreferencesPage(
                          new DlgPrefAutoDJ(this, m_pConfig),
                          new QTreeWidgetItem(contentsTreeWidget, QTreeWidgetItem::Type)),
            tr("Auto DJ"),
            "ic_preferences_autodj.svg");

#ifdef __BROADCAST__
    addPageWidget(PreferencesPage(
                          new DlgPrefBroadcast(this, pSettingsManager->broadcastSettings()),
                          new QTreeWidgetItem(contentsTreeWidget, QTreeWidgetItem::Type)),
            tr("Live Broadcasting"),
            "ic_preferences_broadcast.svg");
#endif // __BROADCAST__

    addPageWidget(PreferencesPage(
                          new DlgPrefRecord(this, m_pConfig),
                          new QTreeWidgetItem(contentsTreeWidget, QTreeWidgetItem::Type)),
            tr("Recording"),
            "ic_preferences_recording.svg");

    addPageWidget(PreferencesPage(
                          new DlgPrefBeats(this, m_pConfig),
                          new QTreeWidgetItem(contentsTreeWidget, QTreeWidgetItem::Type)),
            tr("Beat Detection"),
            "ic_preferences_bpmdetect.svg");

    addPageWidget(PreferencesPage(
                          new DlgPrefKey(this, m_pConfig),
                          new QTreeWidgetItem(contentsTreeWidget, QTreeWidgetItem::Type)),
            tr("Key Detection"),
            "ic_preferences_keydetect.svg");
    addPageWidget(PreferencesPage(
                          new DlgPrefReplayGain(this, m_pConfig),
                          new QTreeWidgetItem(contentsTreeWidget, QTreeWidgetItem::Type)),
            tr("Normalization"),
            "ic_preferences_replaygain.svg");

#ifdef __MODPLUG__
    addPageWidget(PreferencesPage(
                          new DlgPrefModplug(this, m_pConfig),
                          new QTreeWidgetItem(contentsTreeWidget, QTreeWidgetItem::Type)),
            tr("Modplug Decoder"),
            "ic_preferences_modplug.svg");
#endif // __MODPLUG__

    // Find accept and apply buttons
    const auto buttons = buttonBox->buttons();
    for (QAbstractButton* button : buttons) {
        QDialogButtonBox::ButtonRole role = buttonBox->buttonRole(button);
        if (role == QDialogButtonBox::ButtonRole::ApplyRole) {
            m_pApplyButton = button;
        } else if (role == QDialogButtonBox::ButtonRole::AcceptRole) {
            m_pAcceptButton = button;
        }
    }

    if (kioskMode()) {
        useKioskButtons();
        qApp->installEventFilter(new KioskDropDownFilter(this));
    }

    labelWarning->hide();
    labelWarningIcon->hide();
    labelWarning->setText(tr(
            "<font color='#BB0000'><b>Some preferences pages have errors. "
            "To apply the changes please first fix the issues.</b></font>"));
    QIcon icon = style()->standardIcon(QStyle::SP_MessageBoxWarning);
    labelWarningIcon->setPixmap(icon.pixmap(16));

    // Install event handler to generate closeDlg signal
    installEventFilter(this);

    // If we don't call this explicitly, then we default to showing the sound
    // hardware page but the tree item is not selected.
    showSoundHardwarePage();
}

DlgPreferences::~DlgPreferences() {
    // store last geometry in mixxx.cfg
    if (m_geometry.size() == 4) {
        m_pConfig->set(ConfigKey("[Preferences]", "geometry"),
                m_geometry.join(","));
    }

    // When DlgPrefControllers is deleted it manually deletes the controller tree items,
    // which makes QTreeWidgetItem trigger this signal. Currently PreferencesPage
    // instances in m_allPages are destroyed along with pDlg objects. This disconnect
    // is kept as a safety measure.
    disconnect(contentsTreeWidget, &QTreeWidget::currentItemChanged, this, &DlgPreferences::changePage);
    // Need to explicitly delete rather than relying on child auto-deletion
    // because otherwise the QStackedWidget will delete the controller
    // preference pages (and DlgPrefControllers dynamically generates and
    // deletes them).
    delete m_pControllersDlg;
}

void DlgPreferences::changePage(QTreeWidgetItem* pCurrent, QTreeWidgetItem* pPrevious) {
    if (!pCurrent) {
        pCurrent = pPrevious;
    }

    if (handleSoundTreeItem(pCurrent)) {
        return;
    }

    if (m_pControllersDlg->handleTreeItemClick(pCurrent)) {
        // Do nothing. m_controllersPage handled this click.
        return;
    }

    for (const PreferencesPage& page : std::as_const(m_allPages)) {
        if (pCurrent == page.pTreeItem) {
            switchToPage(pCurrent->text(0), page.pDlg);
            break;
        }
    }
}

/// The kiosk's Sound entries: the heading opens (and shows its first entry), each entry shows one part
/// of the Sound page.
bool DlgPreferences::handleSoundTreeItem(QTreeWidgetItem* pItem) {
    if (!m_pSoundOutputItem || !pItem) {
        return false;
    }
    if (pItem == m_soundPage.pTreeItem) {
        contentsTreeWidget->expandItem(pItem);
        contentsTreeWidget->setCurrentItem(m_pSoundOutputItem);
        return true;
    }
    DlgPrefSound::Section section;
    if (pItem == m_pSoundOutputItem) {
        section = DlgPrefSound::Section::Output;
    } else if (pItem == m_pSoundInputItem) {
        section = DlgPrefSound::Section::Input;
    } else if (pItem == m_pSoundSettingsItem) {
        section = DlgPrefSound::Section::Settings;
    } else {
        return false;
    }
    m_pSoundDlg->showSection(section);
    switchToPage(pItem->text(0), m_soundPage.pDlg);
    return true;
}

void DlgPreferences::showSoundHardwarePage(
        std::optional<mixxx::preferences::SoundHardwareTab> tab) {
    if (m_pSoundOutputItem) {
        contentsTreeWidget->expandItem(m_soundPage.pTreeItem);
        const bool input = tab.has_value() && *tab == mixxx::preferences::SoundHardwareTab::Input;
        contentsTreeWidget->setCurrentItem(input ? m_pSoundInputItem : m_pSoundOutputItem);
        // (changePage() shows the section)
        handleSoundTreeItem(input ? m_pSoundInputItem : m_pSoundOutputItem);
        return;
    }
    switchToPage(m_soundPage.pTreeItem->text(0), m_soundPage.pDlg);
    contentsTreeWidget->setCurrentItem(m_soundPage.pTreeItem);
    if (tab.has_value()) {
        m_pSoundDlg->selectIOTab(*tab);
    }
}

void DlgPreferences::showSoundHardwareInputPage() {
    showSoundHardwarePage(mixxx::preferences::SoundHardwareTab::Input);
}

bool DlgPreferences::eventFilter(QObject* o, QEvent* e) {
    // Send a close signal if dialog is closing
    if (e->type() == QEvent::Hide) {
        onHide();
    }

    if (e->type() == QEvent::Show) {
        onShow();
    }

    // Standard event processing
    return QWidget::eventFilter(o, e);
}

void DlgPreferences::changeEvent(QEvent* pEvent) {
    static bool s_inPaletteUpdate = false;
    if (s_inPaletteUpdate) {
        QDialog::changeEvent(pEvent);
        return;
    }

    if (pEvent->type() == QEvent::PaletteChange ||
            pEvent->type() == QEvent::ApplicationPaletteChange ||
            pEvent->type() == QEvent::ThemeChange) {
        struct ResetFlag {
            bool& flag;
            explicit ResetFlag(bool& f)
                    : flag(f) {
                flag = true;
            }
            ~ResetFlag() {
                flag = false;
            }
        } resetFlag(s_inPaletteUpdate);

        // Re-apply macOS system slider styles based on the current theme mode
        fixSliderStyle();

        const QPalette appPalette = QApplication::palette();
        if (palette() != appPalette) {
            setPalette(appPalette);
        }
        // Update m_iconsPath based on the new palette's text color
        if (kioskMode() || !Color::isDimColor(appPalette.text().color())) {
            m_iconsPath.setPath(":/images/preferences/light/");
        } else {
            m_iconsPath.setPath(":/images/preferences/dark/");
        }

        // Reload tree item icons for all pages
        for (const PreferencesPage& page : std::as_const(m_allPages)) {
            if (page.pTreeItem && !page.iconFile.isEmpty()) {
                page.pTreeItem->setIcon(0, kioskMode() ? whiteIcon(m_iconsPath.filePath(page.iconFile)) : QIcon(m_iconsPath.filePath(page.iconFile)));
            }
        }

        const QList<QWidget*> children = findChildren<QWidget*>();
        for (QWidget* pChild : children) {
            pChild->setPalette(appPalette);
            if (pChild->style()) {
                pChild->style()->unpolish(pChild);
                pChild->style()->polish(pChild);
            }
            pChild->update();
        }
    }
    QDialog::changeEvent(pEvent);
}

void DlgPreferences::onHide() {
    // Notify children that we are about to hide.
    emit closeDlg();
}

void DlgPreferences::onShow() {
    if (isFullScreen()) {
        // fullscreen kiosk: the window manager sizes it, nothing to restore
        emit showDlg();
        return;
    }
    // init m_geometry
    if (m_geometry.length() < 4) {
        // load default values (optimum size)
        m_geometry = m_pConfig->getValue(
                                      ConfigKey("[Preferences]", "geometry"))
                             .split(",");
        if (m_geometry.length() < 4) {
            // Warning! geometry does NOT include the frame/title.
            QRect defaultGeometry = getDefaultGeometry();
            m_geometry.clear();
            m_geometry.append(QString::number(defaultGeometry.left()));
            m_geometry.append(QString::number(defaultGeometry.top()));
            m_geometry.append(QString::number(defaultGeometry.width()));
            m_geometry.append(QString::number(defaultGeometry.height()));
        }
    }
    int newX = m_geometry[0].toInt();
    int newY = m_geometry[1].toInt();
    int newWidth = m_geometry[2].toInt();
    int newHeight = m_geometry[3].toInt();

    const QScreen* const pScreen =
            mixxx::widgethelper::getScreenForWidgetOrApplication(*this);
    QRect screenAvailableGeometry;
    VERIFY_OR_DEBUG_ASSERT(pScreen) {
        qWarning() << "Assuming screen size of 800x600px.";
        screenAvailableGeometry = QRect(0, 0, 800, 600);
    }
    else {
        screenAvailableGeometry = pScreen->availableGeometry();
    }

    // Make sure the entire window is visible on screen and is not occluded by taskbar
    // Note: Window geometry excludes window decoration
    int windowDecorationWidth = frameGeometry().width() - geometry().width();
    int windowDecorationHeight = frameGeometry().height() - geometry().height();
    if (windowDecorationWidth <= 0) {
        windowDecorationWidth = 2;
    }
    if (windowDecorationHeight <= 0) {
        windowDecorationHeight = 30;
    }
    int availableWidth = screenAvailableGeometry.width() - windowDecorationWidth;
    int availableHeight = screenAvailableGeometry.height() - windowDecorationHeight;
    newWidth = std::min(newWidth, availableWidth);
    newHeight = std::min(newHeight, availableHeight);
    int minX = screenAvailableGeometry.x();
    int minY = screenAvailableGeometry.y();
    int maxX = screenAvailableGeometry.x() + availableWidth - newWidth;
    int maxY = screenAvailableGeometry.y() + availableHeight - newHeight;
    newX = std::clamp(newX, minX, maxX);
    newY = std::clamp(newY, minY, maxY);
    m_geometry[0] = QString::number(newX);
    m_geometry[1] = QString::number(newY);
    m_geometry[2] = QString::number(newWidth);
    m_geometry[3] = QString::number(newHeight);

    // Update geometry with last values
#ifdef __WINDOWS__
    resize(m_geometry[2].toInt(), m_geometry[3].toInt());
#else  // __WINDOWS__
    // On linux, when the window is opened for the first time by the window manager,
    // QT does not have information about the frame size so the offset is zero.
    // As such, the first time it opens the window does not include the offset,
    // so it is moved from the last position it had.
    // Excluding the offset from the saved value tries to fix that.
    int offsetX = geometry().left() - frameGeometry().left();
    int offsetY = geometry().top() - frameGeometry().top();
    newX += offsetX;
    newY += offsetY;
    setGeometry(newX,   // x position
            newY,       // y position
            newWidth,   // width
            newHeight); // height
#endif // __LINUX__ / __MACOS__
    // Move is also needed on linux.
    move(newX, newY);

    // Notify children that we are about to show.
    emit showDlg();
}

void DlgPreferences::slotButtonPressed(QAbstractButton* pButton) {
    handleButtonRole(buttonBox->buttonRole(pButton));
}

void DlgPreferences::handleButtonRole(QDialogButtonBox::ButtonRole role) {
    DlgPreferencePage* pCurrentPage = currentPage();
    switch (role) {
    case QDialogButtonBox::ResetRole:
        // Only reset to defaults on the current page.
        if (pCurrentPage) {
            pCurrentPage->slotResetToDefaults();
        }
        break;
    case QDialogButtonBox::ApplyRole:
        emit applyPreferences();
        if (!pendingConfigValidOnAllPages()) {
            return;
        }
        break;
    case QDialogButtonBox::AcceptRole:
        emit applyPreferences();
        if (!pendingConfigValidOnAllPages()) {
            return;
        }
        accept();
        // Same as Apply but close the dialog
        if (kioskMode()) {
            // The settings page (the gear) was only the way here: back to the decks.
            ControlObject::set(ConfigKey(QStringLiteral("[Tab]"), QStringLiteral("shortcut")), 0.0);
        }
        break;
    case QDialogButtonBox::RejectRole:
        emit cancelPreferences();
        reject();
        break;
    case QDialogButtonBox::HelpRole:
        if (pCurrentPage) {
            QUrl helpUrl = pCurrentPage->helpUrl();
            DEBUG_ASSERT(helpUrl.isValid());
            mixxx::DesktopHelper::openUrl(helpUrl);
        }
        break;
    default:
        break;
    }
}

bool DlgPreferences::pendingConfigValidOnAllPages() {
    for (const PreferencesPage& page : std::as_const(m_allPages)) {
        if (page.pDlg && !page.pDlg->okayToClose()) {
            // If any page is not okay to close, eg. with an invalid sound config,
            // switch to it and don't accept.
            // Fixes https://github.com/mixxxdj/mixxx/issues/6077
            // and may help with other pages in the future.
            contentsTreeWidget->setCurrentItem(page.pTreeItem);
            return false;
        }
    }
    return true;
}

namespace {

/// Kiosk preferences: on Linux (the embedded device) most pages are hidden so nobody can walk
/// into settings that break it. MIXXX_ALL_PREFS=1 shows everything again; MIXXX_KIOSK_PREFS=1
/// turns the reduced list on elsewhere (to try it on the Mac).
bool kioskPreferencesEnabled() {
    if (qEnvironmentVariableIsSet("MIXXX_ALL_PREFS")) {
        return false;
    }
    if (qEnvironmentVariableIsSet("MIXXX_KIOSK_PREFS")) {
        return true;
    }
#if defined(Q_OS_LINUX)
    return true;
#else
    return false;
#endif
}

} // namespace

/// The kiosk's own button row: Restore Defaults on the left; Cancel, Apply and "Apply and Close"
/// (the old OK, which always applied first, so the name says what it does) on the right; no Help button
/// (the manual is not part of this build). The stock QDialogButtonBox orders its buttons by the
/// platform's rules, so the row is built by hand; the buttons keep doing what their role says.
void DlgPreferences::useKioskButtons() {
    QPushButton* pOk = buttonBox->button(QDialogButtonBox::Ok);
    QPushButton* pApply = buttonBox->button(QDialogButtonBox::Apply);
    QPushButton* pCancel = buttonBox->button(QDialogButtonBox::Cancel);
    QPushButton* pReset = buttonBox->button(QDialogButtonBox::RestoreDefaults);
    if (!pOk || !pApply || !pCancel || !pReset || !buttonBox->parentWidget() ||
            !buttonBox->parentWidget()->layout()) {
        return;
    }
    pOk->setText(tr("Apply and Close"));
    pApply->setText(tr("Apply"));
    pCancel->setText(tr("Cancel"));
    pReset->setText(tr("Restore Defaults"));

    auto* pRow = new QWidget(buttonBox->parentWidget());
    auto* pLayout = new QHBoxLayout(pRow);
    pLayout->setContentsMargins(0, 0, 0, 0);
    pLayout->setSpacing(8);
    const struct {
        QPushButton* pButton;
        QDialogButtonBox::ButtonRole role;
    } buttons[] = {
            {pReset, QDialogButtonBox::ResetRole},
            {pCancel, QDialogButtonBox::RejectRole},
            {pApply, QDialogButtonBox::ApplyRole},
            {pOk, QDialogButtonBox::AcceptRole},
    };
    for (const auto& entry : buttons) {
        buttonBox->removeButton(entry.pButton);
        entry.pButton->setParent(pRow);
        pLayout->addWidget(entry.pButton);
        if (entry.pButton == pReset) {
            pLayout->addStretch(1);
        }
        const QDialogButtonBox::ButtonRole role = entry.role;
        connect(entry.pButton, &QPushButton::clicked, this, [this, role]() {
            handleButtonRole(role);
        });
    }
    buttonBox->hide(); // keeps the Help button, which is never shown
    buttonBox->parentWidget()->layout()->replaceWidget(buttonBox, pRow);
}

QIcon DlgPreferences::whiteIcon(const QString& path, int size) {
    // Rendered at twice the size so it stays sharp on a high-DPI screen.
    QPixmap pixmap = QIcon(path).pixmap(QSize(size * 2, size * 2));
    if (pixmap.isNull()) {
        return QIcon(path);
    }
    QPainter painter(&pixmap);
    painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
    painter.fillRect(pixmap.rect(), Qt::white);
    painter.end();
    return QIcon(pixmap);
}

bool DlgPreferences::kioskMode() {
    return kioskPreferencesEnabled();
}

/// No links in the kiosk's Preferences: a tap on one would open a web page. Their text stays, as plain text.
void DlgPreferences::stripLinks() {
    static const QRegularExpression anchor(
            QStringLiteral("<a\\b[^>]*>(.*?)</a>"), QRegularExpression::DotMatchesEverythingOption);
    for (QLabel* pLabel : findChildren<QLabel*>()) {
        pLabel->setOpenExternalLinks(false);
        const QString text = pLabel->text();
        if (text.contains(QStringLiteral("<a "))) {
            pLabel->setText(QString(text).replace(anchor, QStringLiteral("\\1")));
        }
        if (pLabel->textInteractionFlags().testFlag(Qt::LinksAccessibleByMouse)) {
            pLabel->setTextInteractionFlags(Qt::NoTextInteraction);
        }
    }
}

void DlgPreferences::setVisible(bool visible) {
    const bool kiosk = visible && kioskPreferencesEnabled();
    if (kiosk && !isFullScreen()) {
        setWindowState(windowState() | Qt::WindowFullScreen);
    }
    if (kiosk) {
        stripLinks();
        // A wider page list: "Controllers > DDJ-FLX2 ..." and "Sound Hardware" were cut off.
        constexpr int kKioskPageListWidth = 260;
        contentsTreeWidget->setMinimumWidth(kKioskPageListWidth);
        splitter->setSizes({kKioskPageListWidth, std::max(400, width() - kKioskPageListWidth)});
    }
    QDialog::setVisible(visible);
    if (kiosk) {
        // A window manager may ignore the fullscreen request for a dialog that has a parent
        // (labwc did: the window came up small, high on the screen). Then ask for the screen's size.
        QTimer::singleShot(0, this, [this]() {
            const QScreen* pScreen = mixxx::widgethelper::getScreenForWidgetOrApplication(*this);
            if (pScreen && isVisible() && size() != pScreen->size()) {
                setGeometry(pScreen->geometry());
            }
            splitter->setSizes({contentsTreeWidget->minimumWidth(),
                    std::max(400, width() - contentsTreeWidget->minimumWidth())});
        });
    }
}

namespace {

/// The pages kept in the kiosk list: Sound Hardware, Controllers (and each controller's page)
/// and Interface.
bool isKioskPreferencesPage(const QWidget* pPage) {
    // (Interface, with the skin choice, language and scaling, is left out: nobody changes the skin
    // of an embedded machine. MIXXX_ALL_PREFS=1 brings every page back.)
    return qobject_cast<const DlgPrefSound*>(pPage) ||
            qobject_cast<const DlgPrefControllers*>(pPage) ||
            qobject_cast<const DlgPrefController*>(pPage);
}

} // namespace

void DlgPreferences::addPageWidget(const PreferencesPage& page,
        const QString& pageTitle,
        const QString& iconFile) {
    PreferencesPage pageCopy = page;
    pageCopy.iconFile = iconFile;
    // Configure the tree button linked to the page
    pageCopy.pTreeItem->setIcon(0, kioskMode() ? whiteIcon(m_iconsPath.filePath(iconFile)) : QIcon(m_iconsPath.filePath(iconFile)));
    pageCopy.pTreeItem->setText(0, pageTitle);
    pageCopy.pTreeItem->setTextAlignment(0, Qt::AlignLeft | Qt::AlignVCenter);
    pageCopy.pTreeItem->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);

    // Embedded build: only the pages that are safe to touch show in the list. The hidden pages
    // are still created and wired up, so their saved settings keep being applied.
    if (kioskPreferencesEnabled() && !isKioskPreferencesPage(pageCopy.pDlg)) {
        pageCopy.pTreeItem->setHidden(true);
    }

    connect(this, &DlgPreferences::showDlg, pageCopy.pDlg, &DlgPreferencePage::slotShow);
    connect(this, &DlgPreferences::closeDlg, pageCopy.pDlg, &DlgPreferencePage::slotHide);
    connect(this, &DlgPreferences::showDlg, pageCopy.pDlg, &DlgPreferencePage::slotUpdate);

    connect(this, &DlgPreferences::applyPreferences, pageCopy.pDlg, &DlgPreferencePage::slotApply);
    connect(this,
            &DlgPreferences::cancelPreferences,
            pageCopy.pDlg,
            &DlgPreferencePage::slotCancel);
    connect(this,
            &DlgPreferences::resetToDefaults,
            pageCopy.pDlg,
            &DlgPreferencePage::slotResetToDefaults);

    // Add a new scroll area to the stacked pages widget containing the page
    QScrollArea* sa = new QScrollArea(pagesWidget);
    sa->setWidgetResizable(true);
    if (kioskMode()) {
        // a page is never wider than the screen: it scrolls up and down only
        sa->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    }

    sa->setWidget(pageCopy.pDlg);
    pagesWidget->addWidget(sa);
    m_allPages.append(pageCopy);

    int iframe = 2 * sa->frameWidth();
    m_pageSizeHint = m_pageSizeHint.expandedTo(
            pageCopy.pDlg->sizeHint() + QSize(iframe, iframe));
    fixSliderStyle();
}

DlgPreferencePage* DlgPreferences::currentPage() {
    QObject* pObject = pagesWidget->currentWidget();
    for (int i = 0; i < 2; ++i) {
        if (pObject == nullptr) {
            return nullptr;
        }
        QObjectList children = pObject->children();
        if (children.isEmpty()) {
            return nullptr;
        }
        pObject = children[0];
    }
    return qobject_cast<DlgPreferencePage*>(pObject);
}

void DlgPreferences::removePageWidget(DlgPreferencePage* pWidget) {
    QWidget* pParent = pWidget->parentWidget();
    VERIFY_OR_DEBUG_ASSERT(pParent) {
        return;
    }

    QWidget* pScrollArea = pParent->parentWidget();
    VERIFY_OR_DEBUG_ASSERT(pScrollArea) {
        return;
    }

    const int index = pagesWidget->indexOf(pScrollArea);
    VERIFY_OR_DEBUG_ASSERT(index >= 0 && index < m_allPages.size()) {
        return;
    }

    m_allPages.removeAt(index);
    pagesWidget->removeWidget(pScrollArea);
    delete pScrollArea;
}

void DlgPreferences::expandTreeItem(QTreeWidgetItem* pItem) {
    contentsTreeWidget->expandItem(pItem);
}

void DlgPreferences::switchToPage(const QString& pageTitle, DlgPreferencePage* pWidget) {
#ifdef __APPLE__
    // According to Apple's Human Interface Guidelines, settings dialogs have to
    // "Update the window’s title to reflect the currently visible pane."
    // This also solves the problem of the changed in terminology, Settings instead
    // of Preferences, since macOS Ventura.
    setWindowTitle(pageTitle);
#else
    Q_UNUSED(pageTitle);
#endif
    pagesWidget->setCurrentWidget(pWidget->parentWidget()->parentWidget());

    QPushButton* pButton = buttonBox->button(QDialogButtonBox::Help);
    VERIFY_OR_DEBUG_ASSERT(pButton) {
        return;
    }

    if (pWidget->helpUrl().isValid()) {
        pButton->show();
    } else {
        pButton->hide();
    }
}

void DlgPreferences::moveEvent(QMoveEvent* e) {
    if (m_geometry.length() == 4) {
#ifdef __WINDOWS__
        Q_UNUSED(e);
        m_geometry[0] = QString::number(frameGeometry().left());
        m_geometry[1] = QString::number(frameGeometry().top());
#else
        // Warning! geometry does NOT include the frame/title.
        int offsetX = geometry().left() - frameGeometry().left();
        int offsetY = geometry().top() - frameGeometry().top();
        m_geometry[0] = QString::number(e->pos().x() - offsetX);
        m_geometry[1] = QString::number(e->pos().y() - offsetY);
#endif
    }
}

void DlgPreferences::resizeEvent(QResizeEvent* e) {
    if (m_geometry.length() == 4) {
        m_geometry[2] = QString::number(e->size().width());
        m_geometry[3] = QString::number(e->size().height());
    }
}

QRect DlgPreferences::getDefaultGeometry() {
    adjustSize();
    const auto* const pScreen =
            mixxx::widgethelper::getScreenForWidgetOrApplication(*this);
    VERIFY_OR_DEBUG_ASSERT(pScreen) {
        return QRect();
    }
    QSize optimumSize = pScreen->size();

    if (frameSize() == size()) {
        // This code is reached in Gnome 2.3
        qDebug() << "guess the size of the window decoration";
        optimumSize -= QSize(2, 30);
    } else {
        optimumSize -= (frameSize() - size());
    }

    QSize staticSize = size() - pagesWidget->size();
    optimumSize = optimumSize.boundedTo(staticSize + m_pageSizeHint);

    QRect optimumRect = geometry();
    optimumRect.setSize(optimumSize);

    return optimumRect;
}

void DlgPreferences::fixSliderStyle() {
#ifdef Q_OS_MACOS
    // Only used on macOS where the default slider style has several issues:
    // - the handle is semi-transparent
    // - the slider is higher than the space we give it, which causes that:
    //   - the groove is not correctly centered vertically
    //   - the handle is cut off at the top
    // The style below is based on sliders in the macOS system settings dialogs.
    const QString styleSheetStr = darkAppearance() ? R"--(
QSlider::handle:horizontal {
    background-color: #8f8c8b; 
    border-radius: 4px;
    width: 8px;
    margin: -8px;
} 
QSlider::handle:horizontal::pressed {
    background-color: #a9a7a7;
}
QSlider::groove:horizontal {
    background: #1e1e1e; 
    height: 4px;
    border-radius: 2px;
    margin-left: 8px; 
    margin-right: 8px;
}
)--"
                                                   : R"--(
QSlider::handle:horizontal {
    background-color: #ffffff;
    border-radius: 4px;
    border: 1px solid #d4d3d3;
    width: 7px;
    margin: -8px;
}
QSlider::handle:horizontal::pressed {
    background-color: #ececec;
}
QSlider::groove:horizontal {
    background: #c6c5c5;
    height: 4px;
    border-radius: 2px;
    margin-left: 8px;
    margin-right: 8px;
}
)--";

    const QList<QSlider*> sliders = findChildren<QSlider*>();
    for (QSlider* pSlider : sliders) {
        pSlider->setStyleSheet(styleSheetStr);
    }
#endif // Q_OS_MACOS
}
