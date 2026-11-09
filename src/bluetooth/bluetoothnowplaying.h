#pragma once

#include <QString>
#include <QWidget>

class QLabel;
class QProgressBar;
class QPushButton;
class QTimer;
class BtIconButton;

// The Decks page while a phone plays over Bluetooth ("the DJ has left, put something chill on"): the picture
// the phone sent (or a flat tile drawn from the album / artist name: nothing is looked up online), the title,
// artist and album, and previous / play-pause / next. MixxxMainWindow puts it under the top bar, which stays
// usable (BROWSE, the gear ...), and shows it only while the Decks tab is open.
class BluetoothNowPlaying : public QWidget {
    Q_OBJECT
  public:
    explicit BluetoothNowPlaying(QWidget* pParent);

  signals:
    // "BACK TO DECKS": the phone is disconnected and the player takes the sound card back
    void backRequested();

  protected:
    void showEvent(QShowEvent* pEvent) override;
    void hideEvent(QHideEvent* pEvent) override;

  private slots:
    void refresh();
    void updateProgress();

  private:
    QLabel* m_pArt;
    QLabel* m_pFrom;
    QLabel* m_pTitle;
    QLabel* m_pArtist;
    QLabel* m_pAlbum;
    BtIconButton* m_pPrevious;
    BtIconButton* m_pPlayPause;
    BtIconButton* m_pNext;
    QProgressBar* m_pProgress;
    QLabel* m_pElapsed;
    QLabel* m_pTotal;
    QTimer* m_pTick;
    QString m_shownArtKey;
};
