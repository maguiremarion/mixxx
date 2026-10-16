#include "library/serato/seratoplaylistmodel.h"

#include <generalencapsulatedobjectframe.h>
#include <id3v2tag.h>
#include <mpegfile.h>

#include <QFile>
#include <QPainter>
#include <QPixmap>
#include <QTableView>
#include <QThread>
#include <QTimer>
#include <QtConcurrentRun>

#include "library/coverartutils.h"
#include "library/tabledelegates/tableitemdelegate.h"
#include "moc_seratoplaylistmodel.cpp"
#include "track/taglib/trackmetadata_file.h"
#include "util/fileinfo.h"

SeratoPlaylistModel::SeratoPlaylistModel(QObject* parent,
        TrackCollectionManager* trackCollectionManager,
        QSharedPointer<BaseTrackCache> trackSource)
        : BaseExternalPlaylistModel(
                  parent,
                  trackCollectionManager,
                  "mixxx.db.model.serato.playlistmodel",
                  "serato_playlists",
                  "serato_playlist_tracks",
                  trackSource) {
    m_lookupPool.setMaxThreadCount(1);
    m_lookupPool.setThreadPriority(QThread::LowPriority);
}

void SeratoPlaylistModel::initSortColumnMapping() {
    // Add a bijective mapping between the SortColumnIds and column indices
    for (int i = 0; i < static_cast<int>(TrackModel::SortColumnId::IdMax); ++i) {
        m_columnIndexBySortColumnId[i] = -1;
    }

    m_columnIndexBySortColumnId[static_cast<int>(TrackModel::SortColumnId::Artist)] =
            fieldIndex(ColumnCache::COLUMN_LIBRARYTABLE_ARTIST);
    m_columnIndexBySortColumnId[static_cast<int>(TrackModel::SortColumnId::Title)] =
            fieldIndex(ColumnCache::COLUMN_LIBRARYTABLE_TITLE);
    m_columnIndexBySortColumnId[static_cast<int>(TrackModel::SortColumnId::Album)] =
            fieldIndex(ColumnCache::COLUMN_LIBRARYTABLE_ALBUM);
    m_columnIndexBySortColumnId[static_cast<int>(TrackModel::SortColumnId::AlbumArtist)] =
            fieldIndex(ColumnCache::COLUMN_LIBRARYTABLE_ALBUMARTIST);
    m_columnIndexBySortColumnId[static_cast<int>(TrackModel::SortColumnId::Year)] =
            fieldIndex(ColumnCache::COLUMN_LIBRARYTABLE_YEAR);
    m_columnIndexBySortColumnId[static_cast<int>(TrackModel::SortColumnId::Genre)] =
            fieldIndex(ColumnCache::COLUMN_LIBRARYTABLE_GENRE);
    m_columnIndexBySortColumnId[static_cast<int>(TrackModel::SortColumnId::Composer)] =
            fieldIndex(ColumnCache::COLUMN_LIBRARYTABLE_COMPOSER);
    m_columnIndexBySortColumnId[static_cast<int>(TrackModel::SortColumnId::Grouping)] =
            fieldIndex(ColumnCache::COLUMN_LIBRARYTABLE_GROUPING);
    m_columnIndexBySortColumnId[static_cast<int>(TrackModel::SortColumnId::TrackNumber)] =
            fieldIndex(ColumnCache::COLUMN_LIBRARYTABLE_TRACKNUMBER);
    m_columnIndexBySortColumnId[static_cast<int>(TrackModel::SortColumnId::FileType)] =
            fieldIndex(ColumnCache::COLUMN_LIBRARYTABLE_FILETYPE);
    m_columnIndexBySortColumnId[static_cast<int>(
            TrackModel::SortColumnId::NativeLocation)] =
            fieldIndex(ColumnCache::COLUMN_TRACKLOCATIONSTABLE_LOCATION);
    m_columnIndexBySortColumnId[static_cast<int>(TrackModel::SortColumnId::Comment)] =
            fieldIndex(ColumnCache::COLUMN_LIBRARYTABLE_COMMENT);
    m_columnIndexBySortColumnId[static_cast<int>(TrackModel::SortColumnId::Duration)] =
            fieldIndex(ColumnCache::COLUMN_LIBRARYTABLE_DURATION);
    m_columnIndexBySortColumnId[static_cast<int>(TrackModel::SortColumnId::BitRate)] =
            fieldIndex(ColumnCache::COLUMN_LIBRARYTABLE_BITRATE);
    m_columnIndexBySortColumnId[static_cast<int>(TrackModel::SortColumnId::Bpm)] =
            fieldIndex(ColumnCache::COLUMN_LIBRARYTABLE_BPM);
    m_columnIndexBySortColumnId[static_cast<int>(TrackModel::SortColumnId::ReplayGain)] =
            fieldIndex(ColumnCache::COLUMN_LIBRARYTABLE_REPLAYGAIN);
    m_columnIndexBySortColumnId[static_cast<int>(
            TrackModel::SortColumnId::DateTimeAdded)] =
            fieldIndex(ColumnCache::COLUMN_LIBRARYTABLE_DATETIMEADDED);
    m_columnIndexBySortColumnId[static_cast<int>(TrackModel::SortColumnId::TimesPlayed)] =
            fieldIndex(ColumnCache::COLUMN_LIBRARYTABLE_TIMESPLAYED);
    m_columnIndexBySortColumnId[static_cast<int>(TrackModel::SortColumnId::LastPlayedAt)] =
            fieldIndex(ColumnCache::COLUMN_LIBRARYTABLE_LAST_PLAYED_AT);
    m_columnIndexBySortColumnId[static_cast<int>(TrackModel::SortColumnId::Rating)] =
            fieldIndex(ColumnCache::COLUMN_LIBRARYTABLE_RATING);
    m_columnIndexBySortColumnId[static_cast<int>(TrackModel::SortColumnId::Key)] =
            fieldIndex(ColumnCache::COLUMN_LIBRARYTABLE_KEY);
    m_columnIndexBySortColumnId[static_cast<int>(TrackModel::SortColumnId::Preview)] =
            fieldIndex(ColumnCache::COLUMN_LIBRARYTABLE_PREVIEW);
    m_columnIndexBySortColumnId[static_cast<int>(TrackModel::SortColumnId::CoverArt)] =
            fieldIndex(ColumnCache::COLUMN_LIBRARYTABLE_COVERART);
    m_columnIndexBySortColumnId[static_cast<int>(TrackModel::SortColumnId::Position)] =
            fieldIndex(ColumnCache::COLUMN_PLAYLISTTRACKSTABLE_POSITION);
    m_columnIndexBySortColumnId[static_cast<int>(
            TrackModel::SortColumnId::PlaylistDateTimeAdded)] =
            fieldIndex(ColumnCache::COLUMN_PLAYLISTTRACKSTABLE_DATETIMEADDED);

    m_sortColumnIdByColumnIndex.clear();
    for (int i = static_cast<int>(TrackModel::SortColumnId::IdMin);
            i < static_cast<int>(TrackModel::SortColumnId::IdMax);
            ++i) {
        TrackModel::SortColumnId sortColumn = static_cast<TrackModel::SortColumnId>(i);
        m_sortColumnIdByColumnIndex.insert(
                m_columnIndexBySortColumnId[static_cast<int>(sortColumn)],
                sortColumn);
    }
}

TrackPointer SeratoPlaylistModel::getTrack(const QModelIndex& index) const {
    qDebug() << "SeratoTrackModel::getTrack";

    TrackPointer track = BaseExternalPlaylistModel::getTrack(index);

    return track;
}

bool SeratoPlaylistModel::isColumnHiddenByDefault(int column) {
    if (
            column == fieldIndex(ColumnCache::COLUMN_LIBRARYTABLE_BITRATE) ||
            column == fieldIndex(ColumnCache::COLUMN_LIBRARYTABLE_BPM_LOCK) ||
            column == fieldIndex(ColumnCache::COLUMN_LIBRARYTABLE_ID)) {
        return true;
    }
    return BaseSqlTableModel::isColumnHiddenByDefault(column);
}

namespace {

// Reads the "Serato Overview" tag (an ID3v2 GEOB frame: 0x01 0x05, then 240 columns of 16
// bytes) out of an MP3. Other formats keep it elsewhere and just show no overview.
QByteArray readOverview(const QString& location) {
    if (!location.endsWith(QStringLiteral(".mp3"), Qt::CaseInsensitive) ||
            !QFile::exists(location)) {
        return QByteArray();
    }
    TagLib::MPEG::File file(TAGLIB_FILENAME_FROM_QSTRING(location), false);
    if (!file.isValid() || !file.ID3v2Tag()) {
        return QByteArray();
    }
    const auto& frames = file.ID3v2Tag()->frameListMap()["GEOB"];
    for (const auto* pFrame : frames) {
        const auto* pGeob =
                dynamic_cast<const TagLib::ID3v2::GeneralEncapsulatedObjectFrame*>(pFrame);
        if (pGeob && pGeob->description() == "Serato Overview") {
            const TagLib::ByteVector object = pGeob->object();
            if (object.size() > 2 + 16) {
                return QByteArray(object.data() + 2, static_cast<qsizetype>(object.size() - 2));
            }
        }
    }
    return QByteArray();
}

// Draws the Serato overview as a mirrored bar graph in the same orange as the Rekordbox one.
class SeratoOverviewDelegate : public TableItemDelegate {
  public:
    SeratoOverviewDelegate(QTableView* pTableView, const SeratoPlaylistModel* pModel)
            : TableItemDelegate(pTableView),
              m_pModel(pModel) {
    }

    void paintItem(QPainter* painter,
            const QStyleOptionViewItem& option,
            const QModelIndex& index) const override {
        paintItemBackground(painter, option, index);
        if (!m_pModel) {
            return;
        }
        const QByteArray data = m_pModel->overviewWaveform(index);
        if (data.isEmpty()) {
            return;
        }
        const QRect area = option.rect.adjusted(2, 4, -2, -4);
        if (area.width() <= 0 || area.height() <= 0) {
            return;
        }
        // Drawn once per track and cell size into a pixmap, then just blitted on every repaint.
        const qreal ratio = m_pTableView->devicePixelRatioF();
        if (area.size() != m_cachedSize || ratio != m_cachedRatio) {
            m_pixmaps.clear();
            m_cachedSize = area.size();
            m_cachedRatio = ratio;
        }
        const quint64 key = static_cast<quint64>(qHash(data, 0));
        auto it = m_pixmaps.constFind(key);
        if (it == m_pixmaps.constEnd()) {
            if (m_pixmaps.size() > 300) {
                m_pixmaps.clear();
            }
            it = m_pixmaps.insert(key, render(data, area.size(), ratio));
        }
        painter->drawPixmap(area.topLeft(), it.value());
    }

  private:
    static QPixmap render(const QByteArray& data, const QSize& size, qreal ratio) {
        QPixmap pixmap(size * ratio);
        pixmap.setDevicePixelRatio(ratio);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        const int columns = static_cast<int>(data.size() / 16);
        if (columns <= 0) {
            return pixmap;
        }
        const double halfHeight = size.height() / 2.0;
        for (int x = 0; x < size.width(); ++x) {
            const int c = std::min(columns - 1, x * columns / size.width());
            const auto* pColumn = reinterpret_cast<const quint8*>(data.constData()) + c * 16;
            // The 16 bytes are levels from the top of the column to the bottom; draw from the
            // first to the last that is lit, brighter where the level is higher.
            int first = -1;
            int last = -1;
            int peak = 0;
            for (int i = 0; i < 16; ++i) {
                if (pColumn[i] > 0) {
                    if (first < 0) {
                        first = i;
                    }
                    last = i;
                    peak = std::max<int>(peak, pColumn[i]);
                }
            }
            if (first < 0) {
                continue;
            }
            const double top = halfHeight - halfHeight * (8 - first) / 8.0;
            const double bottom = halfHeight + halfHeight * (last + 1 - 8) / 8.0;
            const double white = std::min(1.0, peak / 200.0) * 0.6;
            painter.setPen(QColor(255,
                    static_cast<int>(122 + 133 * white),
                    static_cast<int>(26 + 229 * white)));
            painter.drawLine(QLineF(x + 0.5, top, x + 0.5, bottom));
        }
        return pixmap;
    }

    QPointer<const SeratoPlaylistModel> m_pModel;
    mutable QHash<quint64, QPixmap> m_pixmaps;
    mutable QSize m_cachedSize;
    mutable qreal m_cachedRatio = 0;
};

} // namespace

QAbstractItemDelegate* SeratoPlaylistModel::delegateForColumn(
        const int index, QObject* pParent) {
    if (index == fieldIndex(ColumnCache::COLUMN_LIBRARYTABLE_WAVESUMMARYHEX)) {
        auto* pTableView = qobject_cast<QTableView*>(pParent);
        if (pTableView) {
            return new SeratoOverviewDelegate(pTableView, this);
        }
    }
    return BaseExternalPlaylistModel::delegateForColumn(index, pParent);
}

QByteArray SeratoPlaylistModel::overviewWaveform(const QModelIndex& index) const {
    const QString location = QDir::fromNativeSeparators(getTrackLocation(index));
    if (location.isEmpty()) {
        return QByteArray();
    }
    const auto cached = m_overviewByLocation.constFind(location);
    if (cached != m_overviewByLocation.constEnd()) {
        return cached.value();
    }
    if (m_overviewLookupStarted.contains(location)) {
        return QByteArray(); // being read
    }
    m_overviewLookupStarted.insert(location);

    QPointer<SeratoPlaylistModel> self(const_cast<SeratoPlaylistModel*>(this));
    (void)QtConcurrent::run(&m_lookupPool, [self, location]() {
        const QByteArray overview = readOverview(location);
        if (!self) {
            return;
        }
        QMetaObject::invokeMethod(
                self.data(),
                [self, location, overview]() {
                    if (!self) {
                        return;
                    }
                    self->m_overviewByLocation.insert(location, overview);
                    // One repaint of the column for a burst of results.
                    if (self->m_overviewRefreshQueued) {
                        return;
                    }
                    self->m_overviewRefreshQueued = true;
                    QTimer::singleShot(150, self.data(), [self]() {
                        self->m_overviewRefreshQueued = false;
                        const int column = self->fieldIndex(
                                ColumnCache::COLUMN_LIBRARYTABLE_WAVESUMMARYHEX);
                        if (column >= 0 && self->rowCount() > 0) {
                            emit self->dataChanged(self->index(0, column),
                                    self->index(self->rowCount() - 1, column));
                        }
                    });
                },
                Qt::QueuedConnection);
    });
    return QByteArray();
}

CoverInfo SeratoPlaylistModel::getCoverInfo(const QModelIndex& index) const {
    const QString location = QDir::fromNativeSeparators(getTrackLocation(index));
    if (location.isEmpty()) {
        return CoverInfo();
    }
    const auto cached = m_coverInfoByLocation.constFind(location);
    if (cached != m_coverInfoByLocation.constEnd()) {
        return cached.value();
    }
    if (m_coverLookupStarted.contains(location)) {
        return CoverInfo(); // being looked up
    }
    m_coverLookupStarted.insert(location);

    const QString album = getFieldString(index, ColumnCache::COLUMN_LIBRARYTABLE_ALBUM);
    QPointer<SeratoPlaylistModel> self(const_cast<SeratoPlaylistModel*>(this));
    (void)QtConcurrent::run(&m_lookupPool, [self, location, album]() {
        CoverInfo coverInfo;
        if (QFile::exists(location)) {
            const mixxx::FileInfo fileInfo(location);
            const QImage embeddedCover =
                    CoverArtUtils::extractEmbeddedCover(mixxx::FileAccess(fileInfo));
            coverInfo = CoverInfo(
                    CoverInfoGuesser().guessCoverInfo(fileInfo, album, embeddedCover),
                    location);
        }
        if (!self) {
            return;
        }
        QMetaObject::invokeMethod(
                self.data(),
                [self, location, coverInfo]() {
                    if (!self) {
                        return;
                    }
                    self->m_coverInfoByLocation.insert(location, coverInfo);
                    if (self->m_coverRefreshQueued) {
                        return;
                    }
                    self->m_coverRefreshQueued = true;
                    QTimer::singleShot(150, self.data(), [self]() {
                        self->m_coverRefreshQueued = false;
                        const int column = self->fieldIndex(
                                ColumnCache::COLUMN_LIBRARYTABLE_COVERART);
                        if (column >= 0 && self->rowCount() > 0) {
                            emit self->dataChanged(self->index(0, column),
                                    self->index(self->rowCount() - 1, column));
                        }
                    });
                },
                Qt::QueuedConnection);
    });
    return CoverInfo();
}

QList<QPair<int, int>> SeratoPlaylistModel::defaultColumnLayout() const {
    // Same as the Rekordbox view (see RekordboxPlaylistModel::defaultColumnLayout()): the table
    // is 766 px wide on a 1024 px screen and these add up to 764. Bump
    // defaultColumnLayoutVersion() when changing this so it replaces saved layouts.
    return {
            {fieldIndex(ColumnCache::COLUMN_PLAYLISTTRACKSTABLE_POSITION), 24},
            {fieldIndex(ColumnCache::COLUMN_LIBRARYTABLE_COVERART), 46},
            {fieldIndex(ColumnCache::COLUMN_LIBRARYTABLE_TITLE), 192},
            {fieldIndex(ColumnCache::COLUMN_LIBRARYTABLE_ARTIST), 141},
            {fieldIndex(ColumnCache::COLUMN_LIBRARYTABLE_BPM), 58},
            {fieldIndex(ColumnCache::COLUMN_LIBRARYTABLE_KEY), 59},
            {fieldIndex(ColumnCache::COLUMN_LIBRARYTABLE_DURATION), 88},
            {fieldIndex(ColumnCache::COLUMN_LIBRARYTABLE_WAVESUMMARYHEX), 156},
    };
}

int SeratoPlaylistModel::defaultFlexibleColumn() const {
    return fieldIndex(ColumnCache::COLUMN_LIBRARYTABLE_DURATION);
}

int SeratoPlaylistModel::defaultColumnLayoutVersion() const {
    return 1;
}
