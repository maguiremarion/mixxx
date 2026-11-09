#include "bluetooth/bluetoothnowplaying.h"

#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPixmap>
#include <QProgressBar>
#include <QPushButton>
#include <QStyleOptionButton>
#include <QTimer>
#include <QVBoxLayout>

#include "bluetooth/bluetoothsession.h"
#include "moc_bluetoothnowplaying.cpp"

namespace {

// The phone sends 280 x 280 (a 200 x 200 native picture, or a 280 x 280 variant): shown at that size, 1:1, so it is sharp
constexpr int kArtSize = 282; // + 1 px border each side
// The width of the text column: 993 lit pixels, 40 on each side, the picture and the gap to it
constexpr int kTextWidth = 993 - 80 - kArtSize - 40;

// A flat tile for a track with no picture: one dark colour per album / artist (the same song always gets the
// same one) with the first letter big on it. No gradient, nothing from the internet.
QPixmap makeTile(const QString& album, const QString& artist, const QString& title, int size) {
    static const QColor kColors[] = {
            QColor(0x1f, 0x3b, 0x5c),
            QColor(0x3b, 0x2f, 0x5c),
            QColor(0x5c, 0x2f, 0x3b),
            QColor(0x2f, 0x5c, 0x4a),
            QColor(0x5c, 0x4a, 0x2f),
            QColor(0x2f, 0x4a, 0x5c),
            QColor(0x4a, 0x2f, 0x5c),
            QColor(0x5c, 0x3f, 0x2f),
    };
    const QString key = (album.isEmpty() ? title : album) + QLatin1Char('|') + artist;
    const uint hash = qHash(key);
    const QColor color = kColors[hash % (sizeof(kColors) / sizeof(kColors[0]))];
    QString initial;
    for (const QChar c : key) {
        if (c.isLetterOrNumber()) {
            initial = QString(c.toUpper());
            break;
        }
    }
    QPixmap pixmap(size, size);
    pixmap.fill(color);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    QFont font = painter.font();
    font.setPixelSize(size * 55 / 100);
    font.setWeight(QFont::Bold);
    painter.setFont(font);
    painter.setPen(QColor(0xe5, 0xe6, 0xea));
    painter.drawText(QRect(0, 0, size, size), Qt::AlignCenter, initial);
    return pixmap;
}

QString formatTime(qint64 ms) {
    const qint64 seconds = qMax<qint64>(0, ms / 1000);
    return QStringLiteral("%1:%2").arg(seconds / 60).arg(seconds % 60, 2, 10, QLatin1Char('0'));
}

} // namespace

// A flat, square transport button with its symbol drawn by hand (the fonts have no reliable media glyphs)
class BtIconButton : public QPushButton {
  public:
    enum class Kind { Previous, PlayPause, Next };

    explicit BtIconButton(Kind kind, QWidget* pParent)
            : QPushButton(pParent),
              m_kind(kind) {
        setObjectName(QStringLiteral("BtTransport"));
        setFocusPolicy(Qt::NoFocus);
        setFixedSize(96, 56);
    }

    void setPlaying(bool playing) {
        if (m_playing != playing) {
            m_playing = playing;
            update();
        }
    }

  protected:
    void paintEvent(QPaintEvent* pEvent) override {
        QPushButton::paintEvent(pEvent); // the flat background from the style sheet
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(0xe5, 0xe6, 0xea));
        const QPointF c(width() / 2.0, height() / 2.0);
        const qreal h = 12; // half the symbol's height
        auto triangle = [&](qreal cx, bool pointsRight) {
            const qreal w = 13;
            QPolygonF p;
            if (pointsRight) {
                p << QPointF(cx - w / 2, c.y() - h) << QPointF(cx + w / 2, c.y()) << QPointF(cx - w / 2, c.y() + h);
            } else {
                p << QPointF(cx + w / 2, c.y() - h) << QPointF(cx - w / 2, c.y()) << QPointF(cx + w / 2, c.y() + h);
            }
            painter.drawPolygon(p);
        };
        switch (m_kind) {
        case Kind::Previous:
            painter.drawRect(QRectF(c.x() - 15, c.y() - h, 4, 2 * h));
            triangle(c.x() - 2, false);
            triangle(c.x() + 11, false);
            break;
        case Kind::Next:
            painter.drawRect(QRectF(c.x() + 11, c.y() - h, 4, 2 * h));
            triangle(c.x() - 11, true);
            triangle(c.x() + 2, true);
            break;
        case Kind::PlayPause:
            if (m_playing) { // pause bars: tapping pauses
                painter.drawRect(QRectF(c.x() - 11, c.y() - h, 7, 2 * h));
                painter.drawRect(QRectF(c.x() + 4, c.y() - h, 7, 2 * h));
            } else {
                QPolygonF p;
                p << QPointF(c.x() - 8, c.y() - h - 2) << QPointF(c.x() + 12, c.y()) << QPointF(c.x() - 8, c.y() + h + 2);
                painter.drawPolygon(p);
            }
            break;
        }
    }

  private:
    Kind m_kind;
    bool m_playing = false;
};

BluetoothNowPlaying::BluetoothNowPlaying(QWidget* pParent)
        : QWidget(pParent) {
    setObjectName(QStringLiteral("BtNowPlaying"));
    setAttribute(Qt::WA_StyledBackground, true);
    setStyleSheet(QStringLiteral(
            "#BtNowPlaying { background-color: #0e0e0e; }"
            "#BtFrom { color: #3478f2; font-size: 14px; font-weight: 700; background: transparent; }"
            "#BtTitle { color: #ffffff; font-size: 44px; font-weight: 700; background: transparent; }"
            "#BtArtist { color: #e5e6ea; font-size: 28px; font-weight: 400; background: transparent; }"
            "#BtAlbum { color: #8c8c94; font-size: 20px; font-weight: 400; background: transparent; }"
            "#BtTime { color: #8c8c94; font-size: 14px; font-weight: 600; background: transparent; }"
            "#BtArt { background-color: #181818; border: 1px solid #2e2e34; }"
            "QProgressBar#BtProgress { background-color: #262626; border: none; border-radius: 0; min-height: 6px; max-height: 6px; }"
            "QProgressBar#BtProgress::chunk { background-color: #3478f2; }"
            "QPushButton#BtTransport { background-color: #262626; border: 1px solid #3a3a3a; border-radius: 0; }"
            "QPushButton#BtTransport:pressed { background-color: #3478f2; border-color: #3478f2; }"
            "QPushButton#BtBack { background-color: #1c1c1c; color: #e5e6ea; border: 1px solid #333333;"
            " border-radius: 0; font-size: 13px; font-weight: 600; padding: 0 18px; }"
            "QPushButton#BtBack:pressed { background-color: #2e2e34; }"));

    // The picture and the text start at the same height (the block is centred on the page as a whole), with BACK
    // TO DECKS in the bottom right corner
    auto* pRoot = new QVBoxLayout(this);
    pRoot->setContentsMargins(40, 14, 40, 14);
    pRoot->setSpacing(0);
    pRoot->addStretch(1);
    auto* pBody = new QHBoxLayout();
    pBody->setSpacing(40);
    pRoot->addLayout(pBody);
    pRoot->addStretch(1);

    m_pArt = new QLabel(this);
    m_pArt->setObjectName(QStringLiteral("BtArt"));
    m_pArt->setFixedSize(kArtSize, kArtSize);
    m_pArt->setAlignment(Qt::AlignCenter);
    pBody->addWidget(m_pArt, 0, Qt::AlignTop);

    // The text column is exactly as tall as the picture, so that the title starts level with the picture's top and the
    // transport buttons end level with its bottom
    auto* pColumnBox = new QWidget(this);
    pColumnBox->setFixedHeight(kArtSize);
    auto* pColumn = new QVBoxLayout(pColumnBox);
    pColumn->setContentsMargins(0, 0, 0, 0);
    pColumn->setSpacing(4);
    m_pFrom = new QLabel(this);
    m_pFrom->setObjectName(QStringLiteral("BtFrom"));
    pColumn->addWidget(m_pFrom);
    m_pTitle = new QLabel(this);
    m_pTitle->setObjectName(QStringLiteral("BtTitle"));
    m_pTitle->setWordWrap(true);
    m_pTitle->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    pColumn->addWidget(m_pTitle);
    m_pArtist = new QLabel(this);
    m_pArtist->setObjectName(QStringLiteral("BtArtist"));
    pColumn->addWidget(m_pArtist);
    m_pAlbum = new QLabel(this);
    m_pAlbum->setObjectName(QStringLiteral("BtAlbum"));
    pColumn->addWidget(m_pAlbum);
    pColumn->addStretch(1);

    // The playhead: the time so far, the bar, the length
    auto* pTimes = new QHBoxLayout();
    pTimes->setSpacing(12);
    m_pProgress = new QProgressBar(this);
    m_pProgress->setObjectName(QStringLiteral("BtProgress"));
    m_pProgress->setRange(0, 1000);
    m_pProgress->setTextVisible(false);
    m_pElapsed = new QLabel(QStringLiteral("0:00"), this);
    m_pElapsed->setObjectName(QStringLiteral("BtTime"));
    m_pElapsed->setMinimumWidth(40);
    m_pTotal = new QLabel(QStringLiteral("0:00"), this);
    m_pTotal->setObjectName(QStringLiteral("BtTime"));
    m_pTotal->setMinimumWidth(40);
    m_pTotal->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    pTimes->addWidget(m_pElapsed);
    pTimes->addWidget(m_pProgress, 1, Qt::AlignVCenter);
    pTimes->addWidget(m_pTotal);
    pColumn->addLayout(pTimes);
    pColumn->addSpacing(10);

    auto* pTransport = new QHBoxLayout();
    pTransport->setSpacing(10);
    m_pPrevious = new BtIconButton(BtIconButton::Kind::Previous, this);
    m_pPlayPause = new BtIconButton(BtIconButton::Kind::PlayPause, this);
    m_pNext = new BtIconButton(BtIconButton::Kind::Next, this);
    pTransport->addWidget(m_pPrevious);
    pTransport->addWidget(m_pPlayPause);
    pTransport->addWidget(m_pNext);
    pTransport->addStretch(1);
    pColumn->addLayout(pTransport);
    pBody->addWidget(pColumnBox, 1, Qt::AlignTop);

    auto* pBack = new QPushButton(tr("BACK TO DECKS"), this);
    pBack->setObjectName(QStringLiteral("BtBack"));
    pBack->setFocusPolicy(Qt::NoFocus);
    pBack->setFixedHeight(40);
    pRoot->addWidget(pBack, 0, Qt::AlignRight);

    m_pTick = new QTimer(this);
    m_pTick->setInterval(250);
    connect(m_pTick, &QTimer::timeout, this, &BluetoothNowPlaying::updateProgress);

    connect(pBack, &QPushButton::clicked, this, &BluetoothNowPlaying::backRequested);
    if (BluetoothSession* pSession = BluetoothSession::instance()) {
        connect(m_pPrevious, &QPushButton::clicked, pSession, &BluetoothSession::previous);
        connect(m_pNext, &QPushButton::clicked, pSession, &BluetoothSession::next);
        connect(m_pPlayPause, &QPushButton::clicked, pSession, [pSession]() {
            if (pSession->playing()) {
                pSession->pause();
            } else {
                pSession->play();
            }
        });
        connect(pSession, &BluetoothSession::changed, this, &BluetoothNowPlaying::refresh);
    }
    refresh();
}

void BluetoothNowPlaying::showEvent(QShowEvent* pEvent) {
    QWidget::showEvent(pEvent);
    m_pTick->start();
    updateProgress();
}

void BluetoothNowPlaying::hideEvent(QHideEvent* pEvent) {
    QWidget::hideEvent(pEvent);
    m_pTick->stop();
}

void BluetoothNowPlaying::updateProgress() {
    BluetoothSession* pSession = BluetoothSession::instance();
    if (!pSession) {
        return;
    }
    const qint64 duration = pSession->durationMs();
    const qint64 position = pSession->positionMs();
    m_pProgress->setValue(duration > 0 ? static_cast<int>(position * 1000 / duration) : 0);
    m_pElapsed->setText(formatTime(position));
    m_pTotal->setText(duration > 0 ? formatTime(duration) : QStringLiteral("-:--"));
}

void BluetoothNowPlaying::refresh() {
    BluetoothSession* pSession = BluetoothSession::instance();
    if (!pSession) {
        return;
    }
    m_pFrom->setText(pSession->deviceName().isEmpty()
                    ? tr("BLUETOOTH")
                    : tr("PLAYING FROM %1").arg(pSession->deviceName().toUpper()));
    const QString title = pSession->title().isEmpty() ? tr("Bluetooth audio") : pSession->title();
    {
        // 38 px in at most two lines; a longer title gets smaller type and three lines
        int pixels = 38;
        int lines = 2;
        for (const int size : {38, 32, 27}) {
            QFont font = m_pTitle->font();
            font.setPixelSize(size);
            font.setWeight(QFont::Bold);
            const QFontMetrics metrics(font);
            const int needed = metrics.boundingRect(QRect(0, 0, kTextWidth, 10000), Qt::TextWordWrap, title).height() /
                    metrics.lineSpacing();
            pixels = size;
            lines = size == 38 ? 2 : 3;
            if (needed <= lines) {
                break;
            }
        }
        QFont font = m_pTitle->font();
        font.setPixelSize(pixels);
        font.setWeight(QFont::Bold);
        m_pTitle->setStyleSheet(QStringLiteral("font-size: %1px;").arg(pixels));
        m_pTitle->setFixedHeight(QFontMetrics(font).lineSpacing() * lines + 2);
        m_pTitle->setText(title);
    }
    m_pArtist->setText(m_pArtist->fontMetrics().elidedText(pSession->artist(), Qt::ElideRight, kTextWidth));
    m_pArtist->setVisible(!pSession->artist().isEmpty());
    m_pAlbum->setText(m_pAlbum->fontMetrics().elidedText(pSession->album(), Qt::ElideRight, kTextWidth));
    m_pAlbum->setVisible(!pSession->album().isEmpty());
    m_pPlayPause->setPlaying(pSession->playing());
    updateProgress();

    // The phone's picture when it has sent one for this track, else the tile. Reloaded only when it changes.
    const QString artPath = pSession->artPath();
    const QFileInfo artInfo(artPath);
    const bool haveArt = !artPath.isEmpty() && artInfo.exists();
    QString artKey;
    if (haveArt) {
        artKey = artPath + QLatin1Char('@') + QString::number(artInfo.lastModified().toMSecsSinceEpoch());
    } else {
        artKey = QStringLiteral("tile|") + pSession->album() + QLatin1Char('|') + pSession->artist() +
                QLatin1Char('|') + pSession->title();
    }
    if (artKey == m_shownArtKey) {
        return;
    }
    m_shownArtKey = artKey;
    QPixmap pixmap;
    if (haveArt && pixmap.load(artPath)) {
        if (pixmap.width() > kArtSize - 2 || pixmap.height() > kArtSize - 2) {
            pixmap = pixmap.scaled(kArtSize - 2, kArtSize - 2, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        }
    } else {
        pixmap = makeTile(pSession->album(), pSession->artist(), pSession->title(), kArtSize - 2);
    }
    m_pArt->setPixmap(pixmap);
}
