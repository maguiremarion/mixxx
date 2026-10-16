#pragma once

#include <QHash>
#include <QPointer>
#include <QSet>
#include <QThreadPool>

#include "library/baseexternalplaylistmodel.h"
#include "library/coverart.h"

class TrackCollectionManager;
class SeratoPlaylistModel;

class SeratoPlaylistModel : public BaseExternalPlaylistModel {
    Q_OBJECT
  public:
    SeratoPlaylistModel(QObject* parent,
            TrackCollectionManager* pTrackCollectionManager,
            QSharedPointer<BaseTrackCache> trackSource);
    TrackPointer getTrack(const QModelIndex& index) const override;
    bool isColumnHiddenByDefault(int column) override;
    /// What a fresh install shows in the Serato view: the same columns as the Rekordbox one
    /// (sized for a 1024x600 screen).
    QList<QPair<int, int>> defaultColumnLayout() const override;
    int defaultColumnLayoutVersion() const override;
    int defaultFlexibleColumn() const override;
    /// Serato tracks aren't in the library, so their cover art is worked out from the file
    /// (embedded art, or an image in its folder) on a background thread the first time a row is
    /// drawn, then the Cover Art column repaints. Results are remembered.
    CoverInfo getCoverInfo(const QModelIndex& index) const override;
    /// The Overview column: the waveform Serato stored in the file's tags ("Serato Overview"),
    /// read on a background thread the first time a row is drawn. Empty for a track Serato never
    /// analysed. Each of the 240 columns is 16 bytes of vertical levels.
    QByteArray overviewWaveform(const QModelIndex& index) const;
    QAbstractItemDelegate* delegateForColumn(const int index, QObject* pParent) override;

  protected:
    void initSortColumnMapping() override;

  private:
    mutable QHash<QString, CoverInfo> m_coverInfoByLocation;
    mutable QSet<QString> m_coverLookupStarted;
    bool m_coverRefreshQueued = false;
    mutable QHash<QString, QByteArray> m_overviewByLocation;
    mutable QSet<QString> m_overviewLookupStarted;
    bool m_overviewRefreshQueued = false;
    // One low-priority worker: lookups read tags off the USB stick one at a time and leave the
    // CPU to the UI.
    mutable QThreadPool m_lookupPool;
};
