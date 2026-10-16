#include "library/autodj/dlgautodj.h"

#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>

#include "controllers/keyboard/keyboardeventfilter.h"
#include "library/library.h"
#include "library/playlisttablemodel.h"
#include "moc_dlgautodj.cpp"
#include "preferences/dialog/dlgpreferences.h"
#include "track/track.h"
#include "util/assert.h"
#include "util/duration.h"
#include "widget/wlibrary.h"
#include "widget/wtracktableview.h"
#include "widget/wtracktableviewheader.h"

namespace {
const char* kPreferenceGroupName = "[Auto DJ]";
const char* kRepeatPlaylistPreference = "Requeue";
} // anonymous namespace

DlgAutoDJ::DlgAutoDJ(WLibrary* parent,
        UserSettingsPointer pConfig,
        Library* pLibrary,
        AutoDJProcessor* pProcessor,
        KeyboardEventFilter* pKeyboard)
        : QWidget(parent),
          Ui::DlgAutoDJ(),
          m_pConfig(pConfig),
          m_pAutoDJProcessor(pProcessor),
          m_pTrackTableView(new WTrackTableView(this,
                  m_pConfig,
                  pLibrary,
                  parent->getTrackTableBackgroundColorOpacity())),
          m_bShowButtonText(parent->getShowButtonText()),
          m_pAutoDJTableModel(nullptr) {
    setupUi(this);

    m_pTrackTableView->installEventFilter(pKeyboard);

    connect(m_pTrackTableView,
            &WTrackTableView::loadTrack,
            this,
            &DlgAutoDJ::loadTrack);
    connect(m_pTrackTableView,
            &WTrackTableView::loadTrackToPlayer,
            this,
            &DlgAutoDJ::loadTrackToPlayer);
    connect(m_pTrackTableView,
            &WTrackTableView::trackSelected,
            this,
            &DlgAutoDJ::trackSelected);
    connect(m_pTrackTableView,
            &WTrackTableView::trackSelected,
            this,
            &DlgAutoDJ::updateSelectionInfo);

    connect(pLibrary,
            &Library::setTrackTableFont,
            m_pTrackTableView,
            &WTrackTableView::setTrackTableFont);
    connect(pLibrary,
            &Library::setTrackTableRowHeight,
            m_pTrackTableView,
            &WTrackTableView::setTrackTableRowHeight);
    connect(pLibrary,
            &Library::setSelectedClick,
            m_pTrackTableView,
            &WTrackTableView::setSelectedClick);

    QBoxLayout* box = qobject_cast<QBoxLayout*>(layout());
    VERIFY_OR_DEBUG_ASSERT(box) { //Assumes the form layout is a QVBox/QHBoxLayout!
    } else {
        box->removeWidget(m_pTrackTablePlaceholder);
        m_pTrackTablePlaceholder->hide();
        box->insertWidget(1, m_pTrackTableView);
    }

    // We do _NOT_ take ownership of this from AutoDJProcessor.
    m_pAutoDJTableModel = m_pAutoDJProcessor->getTableModel();
    m_pTrackTableView->loadTrackModel(m_pAutoDJTableModel);

    // Do not set this because it disables auto-scrolling
    //m_pTrackTableView->setDragDropMode(QAbstractItemView::InternalMove);

    connect(pushButtonAutoDJ,
            &QPushButton::clicked,
            this,
            &DlgAutoDJ::toggleAutoDJButton);

    setupActionButton(pushButtonFadeNow, &DlgAutoDJ::fadeNowButton, tr("Fade"));
    setupActionButton(pushButtonSkipNext, &DlgAutoDJ::skipNextButton, tr("Skip"));
    setupActionButton(pushButtonShuffle, &DlgAutoDJ::shufflePlaylistButton, tr("Shuffle"));
    setupActionButton(pushButtonAddRandomTrack, &DlgAutoDJ::addRandomTrackButton, tr("Random"));

    m_enableBtnTooltip = tr(
            "Enable Auto DJ\n"
            "\n"
            "Shortcut: Shift+F12");
    m_disableBtnTooltip = tr(
            "Disable Auto DJ\n"
            "\n"
            "Shortcut: Shift+F12");
    QString fadeBtnTooltip = tr(
            "Trigger the transition to the next track\n"
            "\n"
            "Shortcut: Shift+F11");
    QString skipBtnTooltip = tr(
            "Skip the next track in the Auto DJ queue\n"
            "\n"
            "Shortcut: Shift+F10");
    QString shuffleBtnTooltip = tr(
            "Shuffle the content of the Auto DJ queue\n"
            "\n"
            "Shortcut: Shift+F9");
    QString addRandomTrackBtnTooltip = tr(
            "Adds a random track from track sources (crates) to the Auto DJ queue.\n"
            "If no track sources are configured, the track is added from the library instead.");
    QString repeatBtnTooltip = tr(
            "Repeat the playlist");
    QString spinBoxTransitionTooltip = tr(
            "Determines the duration of the transition");
    QString labelTransitionTooltip = tr(
            // "sec" as in seconds
            "Seconds");
    QString fadeModeTooltip = tr(
            "Auto DJ Fade Modes\n"
            "\n"
            "Full Intro + Outro:\n"
            "Play the full intro and outro. Use the intro or outro length as the\n"
            "crossfade time, whichever is shorter. If no intro or outro are marked,\n"
            "use the selected crossfade time.\n"
            "\n"
            "Fade At Outro Start:\n"
            "Start crossfading at the outro start. If the outro is longer than the\n"
            "intro, cut off the end of the outro. Use the intro or outro length as\n"
            "the crossfade time, whichever is shorter. If no intro or outro are\n"
            "marked, use the selected crossfade time.\n"
            "\n"
            "Full Track:\n"
            "Play the whole track. Begin crossfading from the selected number of\n"
            "seconds before the end of the track. A negative crossfade time adds\n"
            "silence between tracks.\n"
            "\n"
            "Skip Silence:\n"
            "Play the whole track except for silence at the beginning and end.\n"
            "Begin crossfading from the selected number of seconds before the\n"
            "last sound.\n"
            "\n"
            "Skip Silence Start Full Volume:\n"
            "The same as Skip Silence, but starting transitions with a centered\n"
            "crossfader, so that the intro starts at full volume.\n");

    pushButtonFadeNow->setToolTip(fadeBtnTooltip);
    pushButtonSkipNext->setToolTip(skipBtnTooltip);
    pushButtonShuffle->setToolTip(shuffleBtnTooltip);
    pushButtonAddRandomTrack->setToolTip(addRandomTrackBtnTooltip);
    pushButtonRepeatPlaylist->setToolTip(repeatBtnTooltip);
    spinBoxTransition->setToolTip(spinBoxTransitionTooltip);
    labelTransitionAppendix->setToolTip(labelTransitionTooltip);
    fadeModeCombobox->setToolTip(fadeModeTooltip);

    // Prevent the interactive widgets from being focused with Tab or Shift+Tab
    fadeModeCombobox->setFocusPolicy(Qt::ClickFocus);
    spinBoxTransition->setFocusPolicy(Qt::ClickFocus);
    // work around QLineEdit being protected
    QLineEdit* lineEditTransition(spinBoxTransition->findChild<QLineEdit*>());
    lineEditTransition->setFocusPolicy(Qt::ClickFocus);
    // Needed to catch Enter, Return and Escape keypresses
    lineEditTransition->installEventFilter(this);

    connect(spinBoxTransition,
            QOverload<int>::of(&QSpinBox::valueChanged),
            this,
            &DlgAutoDJ::transitionSliderChanged);

    fadeModeCombobox->addItem(tr("Full Intro + Outro"),
            static_cast<int>(AutoDJProcessor::TransitionMode::FullIntroOutro));
    fadeModeCombobox->addItem(tr("Fade At Outro Start"),
            static_cast<int>(AutoDJProcessor::TransitionMode::FadeAtOutroStart));
    fadeModeCombobox->addItem(tr("Full Track"),
            static_cast<int>(AutoDJProcessor::TransitionMode::FixedFullTrack));
    fadeModeCombobox->addItem(tr("Skip Silence"),
            static_cast<int>(AutoDJProcessor::TransitionMode::FixedSkipSilence));
    fadeModeCombobox->addItem(tr("Skip Silence Start Full Volume"),
            static_cast<int>(AutoDJProcessor::TransitionMode::FixedStartCenterSkipSilence));
    fadeModeCombobox->setCurrentIndex(
            fadeModeCombobox->findData(static_cast<int>(m_pAutoDJProcessor->getTransitionMode())));
    connect(fadeModeCombobox,
            QOverload<int>::of(&QComboBox::activated),
            this,
            &DlgAutoDJ::slotTransitionModeChanged);

    connect(pushButtonRepeatPlaylist,
            &QPushButton::clicked,
            this,
            &DlgAutoDJ::slotRepeatPlaylistChanged);
    if (m_bShowButtonText) {
        pushButtonRepeatPlaylist->setText(tr("Repeat"));
    }
    bool repeatPlaylist = m_pConfig->getValue<bool>(
            ConfigKey(kPreferenceGroupName, kRepeatPlaylistPreference));
    pushButtonRepeatPlaylist->setChecked(repeatPlaylist);
    slotRepeatPlaylistChanged(repeatPlaylist);

    // Setup DlgAutoDJ UI based on the current AutoDJProcessor state. Keep in
    // mind that AutoDJ may already be active when DlgAutoDJ is created (due to
    // skin changes, etc.).
    spinBoxTransition->setValue(static_cast<int>(m_pAutoDJProcessor->getTransitionTime()));
    connect(m_pAutoDJProcessor,
            &AutoDJProcessor::transitionTimeChanged,
            this,
            &DlgAutoDJ::transitionTimeChanged);

    connect(m_pAutoDJProcessor,
            &AutoDJProcessor::autoDJError,
            this,
            &DlgAutoDJ::autoDJError);

    connect(m_pAutoDJProcessor,
            &AutoDJProcessor::autoDJStateChanged,
            this,
            &DlgAutoDJ::autoDJStateChanged);
    autoDJStateChanged(m_pAutoDJProcessor->getState());

    updateSelectionInfo();

    if (DlgPreferences::kioskMode()) {
        applyKioskLayout();
    }
}

DlgAutoDJ::~DlgAutoDJ() {
    qDebug() << "~DlgAutoDJ()";

    // Delete m_pTrackTableView before the table model. This is because the
    // table view saves the header state using the model.
    delete m_pTrackTableView;
}

void DlgAutoDJ::applyKioskLayout() {
    m_kiosk = true;
    setAttribute(Qt::WA_StyledBackground, true); // the skin gives the page its grey
    LibraryFeatureControls->hide();
    labelSelectionInfo->hide();
    m_pTrackTableView->setObjectName(QStringLiteral("AutoDjQueue"));

    const auto makeLabel = [this](const QString& text, const char* objectName) {
        auto* pLabel = new QLabel(text, this);
        pLabel->setObjectName(QLatin1String(objectName));
        pLabel->setWordWrap(true);
        return pLabel;
    };
    const auto makeButton = [this](const QString& text, const char* objectName) {
        auto* pButton = new QPushButton(text, this);
        pButton->setObjectName(QLatin1String(objectName));
        pButton->setFocusPolicy(Qt::NoFocus);
        return pButton;
    };

    // --- the introduction, shown while there is no session
    m_pIntroPage = new QWidget(this);
    auto* pIntro = new QVBoxLayout(m_pIntroPage);
    pIntro->setContentsMargins(10, 4, 12, 12);
    pIntro->setSpacing(10);
    pIntro->addWidget(makeLabel(tr("Auto DJ"), "AutoDjTitle"));
    pIntro->addWidget(makeLabel(tr("Auto DJ plays a list of tracks one after another and mixes "
                                   "each into the next for you. It matches the tempo, lines up "
                                   "the beats and blends the bass, so the music keeps flowing "
                                   "while you do something else."),
            "AutoDjText"));
    // The three steps: the number sits in a column of its own, so a step that wraps lines up under
    // its own words and not under the number.
    auto* pSteps = new QGridLayout();
    pSteps->setContentsMargins(0, 0, 0, 0);
    pSteps->setHorizontalSpacing(10);
    pSteps->setVerticalSpacing(6);
    pSteps->setColumnStretch(1, 1);
    const QStringList steps = {
            tr("Start a session."),
            tr("Browse your music, tap a track and choose ADD TO AUTO DJ."),
            tr("Sit back. The first track starts playing and the rest follow. Keep adding tracks, "
               "or tap one in the queue to take it out. End the session whenever you like."),
    };
    for (int i = 0; i < steps.size(); ++i) {
        QLabel* pNumber = makeLabel(QStringLiteral("%1.").arg(i + 1), "AutoDjSteps");
        pNumber->setWordWrap(false);
        pNumber->setAlignment(Qt::AlignTop | Qt::AlignRight);
        pNumber->setMinimumWidth(22);
        pSteps->addWidget(pNumber, i, 0, Qt::AlignTop);
        pSteps->addWidget(makeLabel(steps.at(i), "AutoDjSteps"), i, 1);
    }
    pIntro->addLayout(pSteps);
    pIntro->addSpacing(8);
    QPushButton* pStart = makeButton(tr("START AUTO DJ SESSION"), "AutoDjStartBtn");
    pStart->setFixedHeight(48);
    pStart->setMinimumWidth(280);
    connect(pStart, &QPushButton::clicked, this, [this]() {
        m_pAutoDJProcessor->startSession();
    });
    pIntro->addWidget(pStart, 0, Qt::AlignLeft);
    pIntro->addStretch(1);

    // --- a session: one bar (title, the two settings, end), a line saying what they do, the queue
    m_pSessionBar = new QWidget(this);
    auto* pSession = new QVBoxLayout(m_pSessionBar);
    pSession->setContentsMargins(12, 0, 12, 4);
    pSession->setSpacing(6);

    auto* pBarRow = new QHBoxLayout();
    pBarRow->setSpacing(8);
    pBarRow->addWidget(makeLabel(tr("AUTO DJ"), "AutoDjBarTitle"));
    auto* pLive = makeLabel(tr("ACTIVE"), "AutoDjLive");
    pLive->setWordWrap(false);
    pBarRow->addWidget(pLive);
    pBarRow->addStretch(1);
    for (int beats : {8, 16, 32}) {
        QPushButton* pLength = makeButton(tr("%1").arg(beats), "AutoDjSegBtn");
        pLength->setCheckable(true);
        pLength->setFixedSize(46, 40);
        pLength->setProperty("beats", beats);
        connect(pLength, &QPushButton::clicked, this, [this, beats]() {
            m_pAutoDJProcessor->setSmartBeats(beats);
            updateSmartButtons();
        });
        m_lengthBtns.append(pLength);
        pBarRow->addWidget(pLength);
    }
    pBarRow->addWidget(makeLabel(tr("BEATS"), "AutoDjCaption"), 0, Qt::AlignVCenter);
    pBarRow->addSpacing(36);
    m_pOrderBtn = makeButton(tr("SMART ORDER"), "AutoDjBarBtn");
    m_pOrderBtn->setFixedSize(150, 40);
    pBarRow->addWidget(m_pOrderBtn);
    pBarRow->addStretch(1);
    m_pEndBtn = makeButton(tr("END SESSION"), "AutoDjEndBtn");
    m_pEndBtn->setFixedHeight(40);
    pBarRow->addWidget(m_pEndBtn);
    pSession->addLayout(pBarRow);

    // What the two settings do, one line each
    auto* pBeatsHelp = makeLabel(
            tr("BEATS: how long each blend lasts. 32 is about 15 seconds at 128 BPM."),
            "AutoDjHelp");
    auto* pOrderHelp = makeLabel(
            tr("SMART ORDER: puts the queue in an order where tempo and key change the least."),
            "AutoDjHelp");
    pBeatsHelp->setWordWrap(false);
    pOrderHelp->setWordWrap(false);
    pSession->addWidget(pBeatsHelp);
    pSession->addWidget(pOrderHelp);

    // --- shown instead of the (empty) queue
    // Three short lines, centred, instead of one paragraph that wraps with a lone word on its
    // last line
    m_pEmptyHint = makeLabel(tr("Your queue is empty.\n\n"
                                "Browse to a track, tap it and choose ADD TO AUTO DJ.\n"
                                "The first track you add starts playing."),
            "AutoDjText");
    m_pEmptyHint->setWordWrap(false);
    m_pEmptyHint->setAlignment(Qt::AlignCenter);
    m_pEmptyHint->setContentsMargins(20, 20, 20, 20);

    QBoxLayout* pBox = qobject_cast<QBoxLayout*>(layout());
    pBox->insertWidget(0, m_pIntroPage, 1);
    pBox->insertWidget(1, m_pSessionBar);
    pBox->insertWidget(2, m_pEmptyHint, 1);

    connect(m_pOrderBtn, &QPushButton::clicked, this, [this]() {
        m_pAutoDJProcessor->sortQueueSmart();
    });
    // Ending a session takes two taps, like SHUT DOWN: a stray touch must not stop the music.
    connect(m_pEndBtn, &QPushButton::clicked, this, [this]() {
        if (!m_endArmed) {
            m_endArmed = true;
            m_pEndBtn->setText(tr("TAP AGAIN TO END"));
            m_pEndBtn->setProperty("armed", true);
            m_pEndBtn->style()->unpolish(m_pEndBtn);
            m_pEndBtn->style()->polish(m_pEndBtn);
            QPointer<QPushButton> pGuard(m_pEndBtn);
            QTimer::singleShot(4000, this, [this, pGuard]() {
                if (pGuard && m_endArmed) {
                    m_endArmed = false;
                    pGuard->setText(tr("END SESSION"));
                    pGuard->setProperty("armed", false);
                    pGuard->style()->unpolish(pGuard);
                    pGuard->style()->polish(pGuard);
                }
            });
            return;
        }
        m_endArmed = false;
        m_pAutoDJProcessor->endSession();
    });

    connect(m_pAutoDJProcessor, &AutoDJProcessor::sessionActiveChanged, this, &DlgAutoDJ::updateSessionView);
    connect(m_pAutoDJProcessor, &AutoDJProcessor::smartSettingsChanged, this, &DlgAutoDJ::updateSmartButtons);
    connect(m_pAutoDJTableModel, &QAbstractItemModel::rowsInserted, this, &DlgAutoDJ::updateSessionView);
    connect(m_pAutoDJTableModel, &QAbstractItemModel::rowsRemoved, this, &DlgAutoDJ::updateSessionView);
    connect(m_pAutoDJTableModel, &QAbstractItemModel::modelReset, this, &DlgAutoDJ::updateSessionView);
    // The session is always a smart mix
    m_pAutoDJProcessor->setSmartMixEnabled(true);
    updateSmartButtons();
    updateSessionView();
}

void DlgAutoDJ::updateSmartButtons() {
    if (!m_kiosk) {
        return;
    }
    for (const QPointer<QPushButton>& pButton : std::as_const(m_lengthBtns)) {
        pButton->setChecked(pButton->property("beats").toInt() == m_pAutoDJProcessor->smartBeats());
    }
}

void DlgAutoDJ::updateSessionView() {
    if (!m_kiosk) {
        return;
    }
    const bool active = m_pAutoDJProcessor->isSessionActive();
    const int rows = m_pAutoDJTableModel->rowCount();
    m_pIntroPage->setVisible(!active);
    m_pSessionBar->setVisible(active);
    m_pEmptyHint->setVisible(active && rows == 0);
    m_pTrackTableView->setVisible(active && rows > 0);
    m_pOrderBtn->setEnabled(rows > 2);
    if (!active) {
        m_endArmed = false;
        m_pEndBtn->setText(tr("END SESSION"));
    }
}

void DlgAutoDJ::setupActionButton(QPushButton* pButton,
        void (DlgAutoDJ::*pSlot)(bool),
        const QString& fallbackText) {
    connect(pButton, &QPushButton::clicked, this, pSlot);
    if (m_bShowButtonText) {
        pButton->setText(fallbackText);
    }
}

void DlgAutoDJ::onShow() {
    m_pAutoDJTableModel->select();
    if (m_kiosk) {
        // The header's text is drawn in the header's own font, which a table gets when its model
        // is loaded. This table loads its model before the skin has styled it, so its header kept
        // the plain application font while the other tables' headers got the styled one. Now that
        // the page is on screen, give it the table's styled font.
        if (auto* pHeader = qobject_cast<WTrackTableViewHeader*>(
                    m_pTrackTableView->horizontalHeader())) {
            qInfo() << "Auto DJ header font" << pHeader->font() << "table font"
                    << m_pTrackTableView->font();
            pHeader->setFont(m_pTrackTableView->font());
        }
    }
}

void DlgAutoDJ::onSearch(const QString& text) {
    // Do not allow filtering the Auto DJ playlist, because
    // Auto DJ will work from the filtered table
    Q_UNUSED(text);
}

void DlgAutoDJ::shufflePlaylistButton(bool) {
    QModelIndexList indexList = m_pTrackTableView->selectionModel()->selectedRows();

    // Activate regardless of button being checked
    m_pAutoDJProcessor->shufflePlaylist(indexList);
}

void DlgAutoDJ::skipNextButton(bool) {
    // Activate regardless of button being checked
    m_pAutoDJProcessor->skipNext();
}

void DlgAutoDJ::fadeNowButton(bool) {
    // Activate regardless of button being checked
    m_pAutoDJProcessor->fadeNow();
}

void DlgAutoDJ::toggleAutoDJButton(bool enable) {
    m_pAutoDJProcessor->toggleAutoDJ(enable);
}

// TODO If there's a way to migrate the translations move this
// to AutoDJProcessor in order to keep this class minimal
void DlgAutoDJ::autoDJError(AutoDJProcessor::AutoDJError error) {
    switch (error) {
    case AutoDJProcessor::ADJ_NOT_TWO_DECKS:
        QMessageBox::warning(nullptr,
                tr("Auto DJ"),
                tr("Auto DJ requires two decks assigned to opposite sides of the crossfader."),
                QMessageBox::Ok);
        break;
    case AutoDJProcessor::ADJ_BOTH_DECKS_PLAYING:
        QMessageBox::warning(nullptr,
                tr("Auto DJ"),
                m_kiosk ? tr("Both decks are playing. Pause one of them, then add another track.")
                        : tr("One deck must be stopped to enable Auto DJ mode."),
                QMessageBox::Ok);
        break;
    case AutoDJProcessor::ADJ_UNUSED_DECK_PLAYING:
        QMessageBox::warning(nullptr,
                tr("Auto DJ"),
                tr("Decks not used for Auto DJ must be stopped to enable Auto DJ mode."),
                QMessageBox::Ok);
        break;
    case AutoDJProcessor::ADJ_OK:
    default:
        break;
    }
}

void DlgAutoDJ::transitionTimeChanged(int time) {
    spinBoxTransition->setValue(time);
}

void DlgAutoDJ::transitionSliderChanged(int value) {
    m_pAutoDJProcessor->setTransitionTime(value);
}

void DlgAutoDJ::autoDJStateChanged(AutoDJProcessor::AutoDJState state) {
    if (state == AutoDJProcessor::ADJ_DISABLED) {
        pushButtonAutoDJ->setChecked(false);
        pushButtonAutoDJ->setToolTip(m_enableBtnTooltip);
        if (m_bShowButtonText) {
            pushButtonAutoDJ->setText(tr("Enable"));
        }
        pushButtonFadeNow->setEnabled(false);
        pushButtonSkipNext->setEnabled(false);
    } else {
        // No matter the mode, you can always disable once it is enabled.
        pushButtonAutoDJ->setChecked(true);
        pushButtonAutoDJ->setToolTip(m_disableBtnTooltip);
        if (m_bShowButtonText) {
            pushButtonAutoDJ->setText(tr("Disable"));
        }

        // If fading, you can't hit fade now.
        if (state == AutoDJProcessor::ADJ_LEFT_FADING ||
                state == AutoDJProcessor::ADJ_RIGHT_FADING ||
                state == AutoDJProcessor::ADJ_ENABLE_P1LOADED) {
            pushButtonFadeNow->setEnabled(false);
        } else {
            pushButtonFadeNow->setEnabled(true);
        }

        pushButtonSkipNext->setEnabled(true);
    }
}

void DlgAutoDJ::slotTransitionModeChanged(int newIndex) {
    m_pAutoDJProcessor->setTransitionMode(
            static_cast<AutoDJProcessor::TransitionMode>(
                    fadeModeCombobox->itemData(newIndex).toInt()));
    // Clicking on a transition mode item moves keyboard focus to the list widget.
    // Move focus back to the previously focused library widget.
    ControlObject::set(ConfigKey("[Library]", "refocus_prev_widget"), 1);
}

void DlgAutoDJ::slotRepeatPlaylistChanged(bool checked) {
    m_pConfig->setValue(ConfigKey(kPreferenceGroupName, kRepeatPlaylistPreference),
            checked);
}

void DlgAutoDJ::updateSelectionInfo() {
    QModelIndexList indices = m_pTrackTableView->selectionModel()->selectedRows();

    // Derive total duration from the table model. This is much faster than
    // getting the duration from individual track objects.
    mixxx::Duration duration = m_pAutoDJTableModel->getTotalDuration(indices);

    QString label;

    if (!indices.isEmpty()) {
        label.append(mixxx::DurationBase::formatTime(duration.toDoubleSeconds()));
        label.append(QString(" (%1)").arg(indices.size()));
        labelSelectionInfo->setToolTip(tr("Displays the duration and number of selected tracks."));
        labelSelectionInfo->setText(label);
        labelSelectionInfo->setEnabled(true);
    } else {
        labelSelectionInfo->setText("");
        labelSelectionInfo->setEnabled(false);
    }
}

bool DlgAutoDJ::hasFocus() const {
    return m_pTrackTableView->hasFocus();
}

void DlgAutoDJ::setFocus() {
    m_pTrackTableView->setFocus();
}

void DlgAutoDJ::pasteFromSidebar() {
    m_pTrackTableView->pasteFromSidebar();
}

void DlgAutoDJ::keyPressEvent(QKeyEvent* pEvent) {
    // If we receive key events either the mode selector or the spinbox are focused.
    // Return, Enter and Escape move focus back to the previously focused
    // library widget in order to immediately allow keyboard shortcuts again.
    if (pEvent->key() == Qt::Key_Return ||
            pEvent->key() == Qt::Key_Enter ||
            pEvent->key() == Qt::Key_Escape) {
        ControlObject::set(ConfigKey("[Library]", "refocus_prev_widget"), 1);
        return;
    }
    QWidget::keyPressEvent(pEvent);
}

void DlgAutoDJ::saveCurrentViewState() {
    m_pTrackTableView->saveCurrentViewState();
}

bool DlgAutoDJ::restoreCurrentViewState() {
    return m_pTrackTableView->restoreCurrentViewState();
}
