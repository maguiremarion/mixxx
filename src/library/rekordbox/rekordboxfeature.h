// This feature reads tracks, playlists and folders from removable Recordbox
// prepared devices (USB drives, etc), by parsing the binary *.PDB files
// stored on each removable device. It does not read the locally stored
// Rekordbox database (Collection).

// It draws heavily from the hard work completed here:

//      https://github.com/Deep-Symmetry/crate-digger

// And uses the C++ Kaitai Struct binary parsing libraries:

//      http://kaitai.io
//      https://github.com/kaitai-io/kaitai_struct
//      https://github.com/kaitai-io/kaitai_struct_cpp_stl_runtime

// The *.PDB C++ files:

//      rekordbox_pdb.h
//      rekordbox_pdb.cpp

// Were generated from the following structure definition file:

//      https://github.com/Deep-Symmetry/crate-digger/blob/master/src/main/kaitai/rekordbox_pdb.ksy

#pragma once

#include <QFuture>
#include <QHash>
#include <QSet>
#include <QThreadPool>
#include <QFutureWatcher>
#include <QStringListModel>
#include <QtConcurrentRun>
#include <fstream>

#include "library/baseexternallibraryfeature.h"
#include "library/baseexternalplaylistmodel.h"
#include "library/baseexternaltrackmodel.h"
#include "library/coverart.h"
#include "library/treeitemmodel.h"
#include "util/parented_ptr.h"

class TrackCollectionManager;
class BaseExternalPlaylistModel;

class RekordboxPlaylistModel : public BaseExternalPlaylistModel {
    Q_OBJECT
  public:
    RekordboxPlaylistModel(QObject* parent,
            TrackCollectionManager* pTrackCollectionManager,
            QSharedPointer<BaseTrackCache> trackSource);
    TrackPointer getTrack(const QModelIndex& index) const override;
    bool isColumnHiddenByDefault(int column) override;
    bool isColumnInternal(int column) override;
    /// What a fresh install shows in the Rekordbox view (sized for a 1024x600 screen).
    QList<QPair<int, int>> defaultColumnLayout() const override;
    /// Rekordbox tracks aren't in Mixxx's library, so there is no stored cover art for
    /// them. It is worked out from the file (embedded art, or an image in its folder) on a
    /// background thread the first time a row is drawn: until it is ready this returns no
    /// cover, then the Cover Art column repaints. Results are remembered.
    CoverInfo getCoverInfo(const QModelIndex& index) const override;
    /// The Overview column: Rekordbox's own 400 point preview waveform (from the track's ANLZ
    /// analysis file), read on a background thread the first time a row is drawn. Empty until
    /// it is ready (or if the track has none); the column repaints when it arrives.
    QByteArray previewWaveform(const QModelIndex& index) const;
    QAbstractItemDelegate* delegateForColumn(const int index, QObject* pParent) override;

  protected:
    void initSortColumnMapping() override;

  private:
    mutable QHash<QString, CoverInfo> m_coverInfoByLocation;
    // Locations whose cover is being looked up (or already was).
    mutable QSet<QString> m_coverLookupStarted;
    // One low-priority worker: lookups read tags off the USB stick one at a time and
    // leave the CPU to the UI.
    mutable QThreadPool m_coverLookupPool;
    bool m_coverRefreshQueued = false;
    mutable QHash<QString, QByteArray> m_previewByAnlzPath;
    mutable QSet<QString> m_previewLookupStarted;
    bool m_previewRefreshQueued = false;};

class RekordboxFeature : public BaseExternalLibraryFeature {
    Q_OBJECT
  public:
    RekordboxFeature(Library* pLibrary, UserSettingsPointer pConfig);
    ~RekordboxFeature() override;

    QVariant title() override;
    static bool isSupported();
    void bindLibraryWidget(WLibrary* libraryWidget,
            KeyboardEventFilter* keyboard) override;

    TreeItemModel* sidebarModel() const override;

  public slots:
    void activate() override;
    void activateChild(const QModelIndex& index) override;
    void refreshLibraryModels();
    void onRekordboxDevicesFound();
    void onTracksFound();

  private slots:
    void htmlLinkClicked(const QUrl& link);

  private:
    QString formatRootViewHtml() const;
    std::unique_ptr<BaseSqlTableModel> createPlaylistModelForPlaylist(
            const QVariant& data) override;

    parented_ptr<TreeItemModel> m_pSidebarModel;
    parented_ptr<RekordboxPlaylistModel> m_pRekordboxPlaylistModel;

    QFutureWatcher<QList<TreeItem*>> m_devicesFutureWatcher;
    QFuture<QList<TreeItem*>> m_devicesFuture;
    QFutureWatcher<QString> m_tracksFutureWatcher;
    QFuture<QString> m_tracksFuture;
    QString m_title;

    QSharedPointer<BaseTrackCache> m_trackSource;
};
