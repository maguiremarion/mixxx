#include "library/rekordbox/rekordboxfeature.h"

#include <mp3guessenc.h>
#include <rekordbox_anlz.h>
#include <rekordbox_pdb.h>

#include <QDir>
#include <QMutexLocker>
#include <QMutex>
#include <QSvgRenderer>
#include <QIcon>
#include <QTableView>
#include <QPainter>
#include <typeinfo>
#include <QThread>
#include <QTimer>
#include <QPointer>
#include <QMap>
#include <QMessageBox>
#include <QSettings>
#include <QString>
#include <QStringList>
#include <QTextCodec>
#include <QtDebug>

#include "engine/engine.h"
#include "library/coverartutils.h"
#include "library/dao/trackschema.h"
#include "library/tabledelegates/tableitemdelegate.h"
#include "library/library.h"
#include "library/queryutil.h"
#include "library/rekordbox/rekordboxconstants.h"
#include "library/trackcollection.h"
#include "library/trackcollectionmanager.h"
#include "library/treeitem.h"
#include "moc_rekordboxfeature.cpp"
#include "track/beats.h"
#include "track/cue.h"
#include "track/keyfactory.h"
#include "track/keyutils.h"
#include "track/track.h"
#include "util/color/color.h"
#include "util/db/dbconnectionpooled.h"
#include "util/db/dbconnectionpooler.h"
#include "util/sandbox.h"
#include "waveform/waveform.h"
#include "widget/wtracktableview.h"
#include "widget/wlibrary.h"
#include "widget/wlibrarytextbrowser.h"

#define IS_RECORDBOX_DEVICE "::isRecordboxDevice::"
#define IS_NOT_RECORDBOX_DEVICE "::isNotRecordboxDevice::"
// Third element of a drive row's data: lets the sidebar draw an eject button on it.
#define IS_DRIVE_ROW "::driveRow::"

namespace {

const QString kRekordboxLibraryTable = QStringLiteral("rekordbox_library");
const QString kRekordboxPlaylistsTable = QStringLiteral("rekordbox_playlists");
const QString kRekordboxPlaylistTracksTable = QStringLiteral("rekordbox_playlist_tracks");

// depending on the filesystem of the external media, rekordbox seems
// to store its metadata in different paths:
const QStringList kPdbPaths = {
        QStringLiteral("PIONEER/rekordbox/export.pdb"),  // FAT32/exFat
        QStringLiteral(".PIONEER/rekordbox/export.pdb"), // HFS+ media
};
const QString kPLaylistPathDelimiter = QStringLiteral("-->");

QString findRekordboxPdbPath(const QString& devicePath) {
    const QDir deviceDir(devicePath);
    for (const auto& pdbPath : kPdbPaths) {
        const QFileInfo pdbFileInfo(deviceDir.filePath(pdbPath));
        if (pdbFileInfo.exists() && pdbFileInfo.isFile()) {
            return pdbFileInfo.filePath();
        }
    }
    return {};
}

enum class IDForColor : uint8_t {
    Pink = 1,
    Red,
    Orange,
    Yellow,
    Green,
    Aqua,
    Blue,
    Purple
};

constexpr mixxx::RgbColor kColorForIDPink(0xF870F8);
constexpr mixxx::RgbColor kColorForIDRed(0xF870900);
constexpr mixxx::RgbColor kColorForIDOrange(0xF8A030);
constexpr mixxx::RgbColor kColorForIDYellow(0xF8E331);
constexpr mixxx::RgbColor kColorForIDGreen(0x1EE000);
constexpr mixxx::RgbColor kColorForIDAqua(0x16C0F8);
constexpr mixxx::RgbColor kColorForIDBlue(0x0150F8);
constexpr mixxx::RgbColor kColorForIDPurple(0x9808F8);
constexpr mixxx::RgbColor kColorForIDNoColor(0x0);

struct memory_cue_loop_t {
    mixxx::audio::FramePos startPosition;
    mixxx::audio::FramePos endPosition;
    QString comment;
    mixxx::RgbColor::optional_t color;
};

bool createLibraryTable(QSqlDatabase& database, const QString& tableName) {
    qDebug() << "Creating Rekordbox library table: " << tableName;

    QSqlQuery query(database);
    query.prepare(
            "CREATE TABLE IF NOT EXISTS " + tableName +
            " ("
            "    id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "    rb_id INTEGER,"
            "    artist TEXT,"
            "    title TEXT,"
            "    album TEXT,"
            "    year INTEGER,"
            "    genre TEXT,"
            "    tracknumber TEXT,"
            "    location TEXT UNIQUE,"
            "    comment TEXT,"
            "    duration INTEGER,"
            "    bitrate TEXT,"
            "    bpm FLOAT,"
            "    key TEXT,"
            // The numeric key (Mixxx's ChromaticKey) that sorting by the Key column orders by.
            // Without it that sort made the whole query fail and every list came up empty.
            "    key_id INTEGER DEFAULT 0,"
            "    rating INTEGER,"
            "    analyze_path TEXT UNIQUE,"
            "    device TEXT,"
            "    color INTEGER,"
            // Always NULL: only here so the model has a Cover Art column (the images
            // come from RekordboxPlaylistModel::getCoverInfo()).
            "    coverart BLOB,"
            // Also always NULL: gives the model an Overview column (see previewWaveform()).
            "    wavesummaryhex BLOB"
            ");");

    if (!query.exec()) {
        LOG_FAILED_QUERY(query);
        return false;
    }

    return true;
}

bool createPlaylistsTable(QSqlDatabase& database, const QString& tableName) {
    qDebug() << "Creating Rekordbox playlists table: " << tableName;

    QSqlQuery query(database);
    query.prepare(
            "CREATE TABLE IF NOT EXISTS " + tableName +
            " ("
            "    id INTEGER PRIMARY KEY,"
            "    name TEXT UNIQUE"
            ");");

    if (!query.exec()) {
        LOG_FAILED_QUERY(query);
        return false;
    }

    return true;
}

bool createPlaylistTracksTable(QSqlDatabase& database, const QString& tableName) {
    qDebug() << "Creating Rekordbox playlist tracks table: " << tableName;

    QSqlQuery query(database);
    query.prepare(
            "CREATE TABLE IF NOT EXISTS " + tableName +
            " ("
            "    id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "    playlist_id INTEGER REFERENCES rekordbox_playlists(id),"
            "    track_id INTEGER REFERENCES rekordbox_library(id),"
            "    position INTEGER"
            ");");

    if (!query.exec()) {
        LOG_FAILED_QUERY(query);
        return false;
    }

    return true;
}

bool dropTable(QSqlDatabase& database, const QString& tableName) {
    qDebug() << "Dropping Rekordbox table: " << tableName;

    QSqlQuery query(database);
    query.prepare("DROP TABLE IF EXISTS " + tableName);

    if (!query.exec()) {
        LOG_FAILED_QUERY(query);
        return false;
    }

    return true;
}

// This function is executed in a separate thread other than the main thread
// The returned list owns the pointers, but we can't use a unique_ptr because
// the result is passed by a const reference inside QFuture and than copied
// to the main thread requiring a copy-able object.
QList<TreeItem*> findRekordboxDevices() {
    QThread* thisThread = QThread::currentThread();
    thisThread->setPriority(QThread::LowPriority);

    QList<TreeItem*> foundDevices;

#if defined(__WINDOWS__)
    // Repopulate drive list
    QFileInfoList drives = QDir::drives();
    // show drive letters
    foreach (QFileInfo drive, drives) {
        // Using drive.filePath() instead of drive.canonicalPath() as it
        // freezes interface too much if there is a network share mounted
        // (drive letter assigned) but unavailable
        //
        // drive.canonicalPath() make a system call to the underlying filesystem
        // introducing delay if it is unreadable.
        // drive.filePath() doesn't make any access to the filesystem and consequently
        // shorten the delay

        if (!findRekordboxPdbPath(drive.filePath()).isEmpty()) {
            QString displayPath = drive.filePath();
            if (displayPath.endsWith("/")) {
                displayPath.chop(1);
            }
            QList<QString> data;
            data << drive.filePath();
            data << IS_RECORDBOX_DEVICE;
            data << IS_DRIVE_ROW;
            data << IS_DRIVE_ROW;
            auto* pFoundDevice = new TreeItem(
                    std::move(displayPath),
                    QVariant(data));
            foundDevices << pFoundDevice;
        }
    }
#elif defined(__LINUX__)
    // To get devices on Linux, we look for directories under /media and
    // /run/media/$USER.
    QFileInfoList devices;

    // Add folders under /media to devices.
    devices += QDir(QStringLiteral("/media")).entryInfoList(QDir::AllDirs | QDir::NoDotAndDotDot);

    // Add folders under /media/$USER to devices.
    QDir mediaUserDir(QStringLiteral("/media/") + QString::fromLocal8Bit(qgetenv("USER")));
    devices += mediaUserDir.entryInfoList(
            QDir::AllDirs | QDir::NoDotAndDotDot);

    // Add folders under /run/media/$USER to devices.
    QDir runMediaUserDir(QStringLiteral("/run/media/") + QString::fromLocal8Bit(qgetenv("USER")));
    devices += runMediaUserDir.entryInfoList(
            QDir::AllDirs | QDir::NoDotAndDotDot);

    foreach (QFileInfo device, devices) {
        if (!findRekordboxPdbPath(device.filePath()).isEmpty()) {
            auto* pFoundDevice = new TreeItem(
                    device.fileName(),
                    QVariant(QList<QString>{device.filePath(), IS_RECORDBOX_DEVICE, IS_DRIVE_ROW}));
            foundDevices << pFoundDevice;
        }
    }
#else // __APPLE__
    QFileInfoList devices = QDir(QStringLiteral("/Volumes")).entryInfoList(QDir::AllDirs | QDir::NoDotAndDotDot);

    foreach (QFileInfo device, devices) {
        if (!findRekordboxPdbPath(device.filePath()).isEmpty()) {
            QList<QString> data;
            data << device.filePath();
            data << IS_RECORDBOX_DEVICE;
            data << IS_DRIVE_ROW;
            auto* pFoundDevice = new TreeItem(
                    device.fileName(),
                    QVariant(data));
            foundDevices << pFoundDevice;
        }
    }
#endif

    return foundDevices;
}

template<typename Base, typename T>
inline bool instanceof (const T* ptr) {
    return dynamic_cast<const Base*>(ptr) != nullptr;
}

QString fromUtf16LeString(const std::string& toConvert) {
    // Kaitai uses std::string as single container for all string encodings.
    return QTextCodec::codecForName("UTF-16LE")
            ->toUnicode(toConvert.data(), static_cast<int>(toConvert.length()));
}

QString fromUtf16BeString(const std::string& toConvert) {
    // Kaitai uses std::string as single container for all string encodings.
    int length = static_cast<int>(toConvert.length()) - 2; // strip off trailing nullbyte
    return QTextCodec::codecForName("UTF-16BE")->toUnicode(toConvert.data(), length);
}

// Functions getText and parseDeviceDB are roughly based on the following Java file:
// https://github.com/Deep-Symmetry/crate-digger/commit/f09fa9fc097a2a428c43245ddd542ac1370c1adc
// getText is needed because the strings in the PDB file "have a variety of obscure representations".

QString getText(rekordbox_pdb_t::device_sql_string_t* deviceString) {
    QString text;

    if (instanceof <rekordbox_pdb_t::device_sql_short_ascii_t>(deviceString->body())) {
        rekordbox_pdb_t::device_sql_short_ascii_t* shortAsciiString =
                static_cast<rekordbox_pdb_t::device_sql_short_ascii_t*>(deviceString->body());
        text = QString::fromStdString(shortAsciiString->text());
    } else if (instanceof <rekordbox_pdb_t::device_sql_long_ascii_t>(deviceString->body())) {
        rekordbox_pdb_t::device_sql_long_ascii_t* longAsciiString =
                static_cast<rekordbox_pdb_t::device_sql_long_ascii_t*>(deviceString->body());
        text = QString::fromStdString(longAsciiString->text());
    } else if (instanceof <rekordbox_pdb_t::device_sql_long_utf16le_t>(deviceString->body())) {
        rekordbox_pdb_t::device_sql_long_utf16le_t* longUtf16leString =
                static_cast<rekordbox_pdb_t::device_sql_long_utf16le_t*>(deviceString->body());
        text = fromUtf16LeString(longUtf16leString->text());
    }

    // Some strings read from Rekordbox *.PDB files contain random null characters
    // which if not removed cause Mixxx to crash when attempting to read file paths
    return text.remove(QChar('\x0'));
}

int createDevicePlaylist(QSqlDatabase& database, const QString& devicePath) {
    int playlistID = kInvalidPlaylistId;

    QSqlQuery queryInsertIntoDevicePlaylist(database);
    queryInsertIntoDevicePlaylist.prepare(
            "INSERT INTO " + kRekordboxPlaylistsTable +
            " (name) "
            "VALUES (:name)");

    queryInsertIntoDevicePlaylist.bindValue(":name", devicePath);

    if (!queryInsertIntoDevicePlaylist.exec()) {
        LOG_FAILED_QUERY(queryInsertIntoDevicePlaylist)
                << "devicePath: " << devicePath;
        return playlistID;
    }

    QSqlQuery idQuery(database);
    idQuery.prepare("select id from " + kRekordboxPlaylistsTable + " where name=:path");
    idQuery.bindValue(":path", devicePath);

    if (!idQuery.exec()) {
        LOG_FAILED_QUERY(idQuery)
                << "devicePath: " << devicePath;
        return playlistID;
    }

    while (idQuery.next()) {
        playlistID = idQuery.value(idQuery.record().indexOf("id")).toInt();
    }

    return playlistID;
}

mixxx::RgbColor colorFromID(int colorID) {
    switch (static_cast<IDForColor>(colorID)) {
    case IDForColor::Pink:
        return kColorForIDPink;
    case IDForColor::Red:
        return kColorForIDRed;
    case IDForColor::Orange:
        return kColorForIDOrange;
    case IDForColor::Yellow:
        return kColorForIDYellow;
    case IDForColor::Green:
        return kColorForIDGreen;
    case IDForColor::Aqua:
        return kColorForIDAqua;
    case IDForColor::Blue:
        return kColorForIDBlue;
    case IDForColor::Purple:
        return kColorForIDPurple;
    }
    return kColorForIDNoColor;
}

void insertTrack(
        QSqlDatabase& database,
        rekordbox_pdb_t::track_row_t* track,
        QSqlQuery& query,
        QSqlQuery& queryInsertIntoDevicePlaylistTracks,
        QMap<uint32_t, QString>& artistsMap,
        QMap<uint32_t, QString>& albumsMap,
        QMap<uint32_t, QString>& genresMap,
        QMap<uint32_t, QString>& keysMap,
        const QString& devicePath,
        const QString& device,
        int audioFilesCount) {
    int rbID = static_cast<int>(track->id());
    QString title = getText(track->title());
    QString artist = artistsMap[track->artist_id()];
    QString album = albumsMap[track->album_id()];
    QString year = QString::number(track->year());
    QString genre = genresMap[track->genre_id()];
    QString location = devicePath + getText(track->file_path());
    float bpm = static_cast<float>(track->tempo() / 100.0);
    int bitrate = static_cast<int>(track->bitrate());
    QString key = keysMap[track->key_id()];
    int playtime = static_cast<int>(track->duration());
    int rating = static_cast<int>(track->rating());
    QString comment = getText(track->comment());
    QString tracknumber = QString::number(track->track_number());
    QString anlzPath = devicePath + getText(track->analyze_path());

    query.bindValue(":rb_id", rbID);
    query.bindValue(":artist", artist);
    query.bindValue(":title", title);
    query.bindValue(":album", album);
    query.bindValue(":genre", genre);
    query.bindValue(":year", year);
    query.bindValue(":duration", playtime);
    query.bindValue(":location", location);
    query.bindValue(":rating", rating);
    query.bindValue(":comment", comment);
    query.bindValue(":tracknumber", tracknumber);
    query.bindValue(":key", key);
    query.bindValue(":key_id", static_cast<int>(KeyUtils::guessKeyFromText(key)));
    query.bindValue(":bpm", bpm);
    query.bindValue(":bitrate", bitrate);
    query.bindValue(":analyze_path", anlzPath);
    query.bindValue(":device", device);
    query.bindValue(":color",
            mixxx::RgbColor::toQVariant(
                    colorFromID(static_cast<int>(track->color_id()))));

    if (!query.exec()) {
        LOG_FAILED_QUERY(query);
    }

    int trackID = -1;
    QSqlQuery finderQuery(database);
    finderQuery.prepare("select id from " + kRekordboxLibraryTable +
            " where rb_id=:rb_id and device=:device");
    finderQuery.bindValue(":rb_id", rbID);
    finderQuery.bindValue(":device", device);

    if (!finderQuery.exec()) {
        LOG_FAILED_QUERY(finderQuery)
                << "rbID:" << rbID;
    }

    if (finderQuery.next()) {
        trackID = finderQuery.value(finderQuery.record().indexOf("id")).toInt();
    }

    // Insert into device all tracks playlist
    queryInsertIntoDevicePlaylistTracks.bindValue(":track_id", trackID);
    queryInsertIntoDevicePlaylistTracks.bindValue(":position", audioFilesCount);

    if (!queryInsertIntoDevicePlaylistTracks.exec()) {
        LOG_FAILED_QUERY(queryInsertIntoDevicePlaylistTracks)
                << "trackID:" << trackID
                << "position:" << audioFilesCount;
    }
}

void buildPlaylistTree(
        QSqlDatabase& database,
        TreeItem* parent,
        uint32_t parentID,
        QMap<uint32_t, QString>& playlistNameMap,
        QMap<uint32_t, bool>& playlistIsFolderMap,
        QMap<uint32_t, QMap<uint32_t, uint32_t>>& playlistTreeMap,
        QMap<uint32_t, QMap<uint32_t, uint32_t>>& playlistTrackMap,
        const QString& playlistPath,
        const QString& device);

// `device` and `devicePath` are copies taken on the main thread before this runs: reading them
// from deviceItem here raced with activateChild() rewriting the item's data right after it
// started this thread, and could see an empty path (so nothing was imported and the drive
// stayed blank until Mixxx was restarted).
QString parseDeviceDBImpl(mixxx::DbConnectionPoolPtr dbConnectionPool,
        TreeItem* deviceItem,
        QString device,
        QString devicePath) {

    qDebug() << "parseDeviceDB device: " << device << " devicePath: " << devicePath;

    const QString dbPath = findRekordboxPdbPath(devicePath);

    if (dbPath.isEmpty()) {
        return devicePath;
    }

    // The pooler limits the lifetime all thread-local connections,
    // that should be closed immediately before exiting this function.
    const mixxx::DbConnectionPooler dbConnectionPooler(dbConnectionPool);
    QSqlDatabase database = mixxx::DbConnectionPooled(dbConnectionPool);

    //Open the database connection in this thread.
    VERIFY_OR_DEBUG_ASSERT(database.isOpen()) {
        qDebug() << "Failed to open database for Rekordbox parser."
                 << database.lastError();
        return QString();
    }

    //Give thread a low priority
    QThread* thisThread = QThread::currentThread();
    thisThread->setPriority(QThread::LowPriority);

    // A big page cache (64 MB, per connection) keeps SQLite from spilling a large import to
    // disk mid-way, which would lock the GUI thread's own queries out until the commit.
    QSqlQuery(database).exec("PRAGMA cache_size = -65536");

    ScopedTransaction transaction(database);

    QSqlQuery query(database);
    query.prepare("INSERT INTO " + kRekordboxLibraryTable +
            " (rb_id, artist, title, album, year,"
            "genre,comment,tracknumber,bpm, bitrate,duration, location,"
            "rating,key,key_id,analyze_path,device,color) VALUES (:rb_id, :artist, "
            ":title, :album, :year,:genre,"
            ":comment, :tracknumber,:bpm, :bitrate,:duration, :location,"
            ":rating,:key,:key_id,:analyze_path,:device,:color)");

    int audioFilesCount = 0;

    // Create a playlist for all the tracks on a device
    int playlistID = createDevicePlaylist(database, devicePath);

    QSqlQuery queryInsertIntoDevicePlaylistTracks(database);
    queryInsertIntoDevicePlaylistTracks.prepare(
            "INSERT INTO " + kRekordboxPlaylistTracksTable +
            " (playlist_id, track_id, position) "
            "VALUES (:playlist_id, :track_id, :position)");

    queryInsertIntoDevicePlaylistTracks.bindValue(":playlist_id", playlistID);

    mixxx::FileInfo fileInfo(dbPath);
    if (!Sandbox::askForAccess(&fileInfo)) {
        return QString();
    }
    std::ifstream ifs(dbPath.toStdString(), std::ifstream::binary);
    kaitai::kstream ks(&ifs);

    rekordbox_pdb_t rekordboxDB = rekordbox_pdb_t(&ks);

    // There are other types of tables (eg. COLOR), these are the only ones we are
    // interested at the moment. Perhaps when/if
    // https://github.com/mixxxdj/mixxx/issues/6852
    // is completed, this can be revisited.
    // Attempt was made to also recover HISTORY
    // playlists (which are found on removable Rekordbox devices), however
    // they didn't appear to contain valid row_ref_t structures.
    constexpr int totalTables = 8;

    rekordbox_pdb_t::page_type_t tableOrder[totalTables] = {
            rekordbox_pdb_t::PAGE_TYPE_KEYS,
            rekordbox_pdb_t::PAGE_TYPE_GENRES,
            rekordbox_pdb_t::PAGE_TYPE_ARTISTS,
            rekordbox_pdb_t::PAGE_TYPE_ALBUMS,
            rekordbox_pdb_t::PAGE_TYPE_PLAYLIST_ENTRIES,
            rekordbox_pdb_t::PAGE_TYPE_TRACKS,
            rekordbox_pdb_t::PAGE_TYPE_PLAYLIST_TREE,
            rekordbox_pdb_t::PAGE_TYPE_HISTORY};

    QMap<uint32_t, QString> keysMap;
    QMap<uint32_t, QString> genresMap;
    QMap<uint32_t, QString> artistsMap;
    QMap<uint32_t, QString> albumsMap;
    QMap<uint32_t, QString> playlistNameMap;
    QMap<uint32_t, bool> playlistIsFolderMap;
    QMap<uint32_t, QMap<uint32_t, uint32_t>> playlistTreeMap;
    QMap<uint32_t, QMap<uint32_t, uint32_t>> playlistTrackMap;

    bool folderOrPlaylistFound = false;

    for (int tableOrderIndex = 0; tableOrderIndex < totalTables; tableOrderIndex++) {
        for (const auto& table : *rekordboxDB.tables()) {
            if (table->type() == tableOrder[tableOrderIndex]) {
                uint16_t lastIndex = table->last_page()->index();
                rekordbox_pdb_t::page_ref_t* currentRef = table->first_page();

                while (true) {
                    rekordbox_pdb_t::page_t* page = currentRef->body();

                    if (page->is_data_page()) {
                        for (const auto& rowgroup : *page->row_groups()) {
                            for (const auto& rowRef : *rowgroup->rows()) {
                                if (rowRef->present()) {
                                    switch (tableOrder[tableOrderIndex]) {
                                    case rekordbox_pdb_t::PAGE_TYPE_KEYS: {
                                        auto* key =
                                                static_cast<rekordbox_pdb_t::key_row_t*>(
                                                        rowRef->body());
                                        keysMap[key->id()] = getText(key->name());
                                    } break;
                                    case rekordbox_pdb_t::PAGE_TYPE_GENRES: {
                                        auto* genre =
                                                static_cast<rekordbox_pdb_t::genre_row_t*>(
                                                        rowRef->body());
                                        genresMap[genre->id()] = getText(genre->name());
                                    } break;
                                    case rekordbox_pdb_t::PAGE_TYPE_ARTISTS: {
                                        auto* artist =
                                                static_cast<rekordbox_pdb_t::artist_row_t*>(
                                                        rowRef->body());
                                        artistsMap[artist->id()] = getText(artist->name());
                                    } break;
                                    case rekordbox_pdb_t::PAGE_TYPE_ALBUMS: {
                                        auto* album =
                                                static_cast<rekordbox_pdb_t::album_row_t*>(
                                                        rowRef->body());
                                        albumsMap[album->id()] = getText(album->name());
                                    } break;
                                    case rekordbox_pdb_t::PAGE_TYPE_PLAYLIST_ENTRIES: {
                                        auto* playlistEntry =
                                                static_cast<rekordbox_pdb_t::playlist_entry_row_t*>(
                                                        rowRef->body());
                                        playlistTrackMap
                                                [playlistEntry->playlist_id()]
                                                [playlistEntry->entry_index()] =
                                                        playlistEntry
                                                                ->track_id();
                                    } break;
                                    case rekordbox_pdb_t::PAGE_TYPE_TRACKS: {
                                        insertTrack(database,
                                                static_cast<rekordbox_pdb_t::track_row_t*>(
                                                        rowRef->body()),
                                                query,
                                                queryInsertIntoDevicePlaylistTracks,
                                                artistsMap,
                                                albumsMap,
                                                genresMap,
                                                keysMap,
                                                devicePath,
                                                device,
                                                audioFilesCount);

                                        audioFilesCount++;
                                    } break;
                                    case rekordbox_pdb_t::PAGE_TYPE_PLAYLIST_TREE: {
                                        auto* playlistTree =
                                                static_cast<rekordbox_pdb_t::playlist_tree_row_t*>(
                                                        rowRef->body());

                                        playlistNameMap[playlistTree->id()] =
                                                getText(playlistTree->name());
                                        playlistIsFolderMap[playlistTree
                                                                    ->id()] =
                                                playlistTree->is_folder();
                                        playlistTreeMap
                                                [playlistTree->parent_id()]
                                                [playlistTree->sort_order()] =
                                                        playlistTree->id();

                                        folderOrPlaylistFound = true;
                                    } break;
                                    default:
                                        // we currently don't handle any other
                                        // data, even though there is more.
                                        break;
                                    }
                                }
                            }
                        }
                    }

                    if (currentRef->index() == lastIndex) {
                        break;
                    } else {
                        currentRef = page->next_page();
                    }
                }
            }
        }
    }

    if (audioFilesCount > 0 || folderOrPlaylistFound) {
        // Under the drive: "All Tracks" (the playlist the parser makes for the whole drive,
        // named after the device path) and a "Playlists" folder holding Rekordbox's own
        // playlist/folder tree. The folder has no tracks of its own: its empty path makes
        // activateChild() just let it open and shut.
        if (audioFilesCount > 0) {
            deviceItem->appendChild(QObject::tr("All Tracks"),
                    QVariant(QList<QString>{devicePath, IS_NOT_RECORDBOX_DEVICE}));
        }
        TreeItem* pPlaylistsParent = deviceItem;
        if (folderOrPlaylistFound) {
            pPlaylistsParent = deviceItem->appendChild(QObject::tr("Playlists"),
                    QVariant(QList<QString>{QString(), IS_NOT_RECORDBOX_DEVICE}));
        }
        // Recursively build playlist/folder TreeItem children
        buildPlaylistTree(database,
                pPlaylistsParent,
                0,
                playlistNameMap,
                playlistIsFolderMap,
                playlistTreeMap,
                playlistTrackMap,
                devicePath,
                device);
    }

    qDebug() << "Found: " << audioFilesCount << " audio files in Rekordbox device " << device;

    transaction.commit();

    return devicePath;
}

// Same as parseDeviceDBImpl, but says WHAT failed: QtConcurrent turns any exception thrown here
// into a bare "std::exception" by the time onTracksFound() sees it.
QString parseDeviceDB(mixxx::DbConnectionPoolPtr dbConnectionPool,
        TreeItem* deviceItem,
        QString device,
        QString devicePath) {
    try {
        return parseDeviceDBImpl(std::move(dbConnectionPool), deviceItem, device, devicePath);
    } catch (const std::exception& e) {
        qWarning() << "Rekordbox: reading the database of" << device << "failed:"
                   << typeid(e).name() << e.what();
        throw;
    } catch (...) {
        qWarning() << "Rekordbox: reading the database of" << device
                   << "failed with an unknown exception";
        throw;
    }
}

void buildPlaylistTree(
        QSqlDatabase& database,
        TreeItem* parent,
        uint32_t parentID,
        QMap<uint32_t, QString>& playlistNameMap,
        QMap<uint32_t, bool>& playlistIsFolderMap,
        QMap<uint32_t, QMap<uint32_t, uint32_t>>& playlistTreeMap,
        QMap<uint32_t, QMap<uint32_t, uint32_t>>& playlistTrackMap,
        const QString& playlistPath,
        const QString& device) {
    for (uint32_t childIndex = 0;
            childIndex < (uint32_t)playlistTreeMap[parentID].size();
            childIndex++) {
        uint32_t childID = playlistTreeMap[parentID][childIndex];
        if (childID == 0) {
            continue;
        }
        QString playlistItemName = playlistNameMap[childID];

        QString currentPath = playlistPath + kPLaylistPathDelimiter + playlistItemName;

        TreeItem* child = parent->appendChild(playlistItemName,
                QVariant(QList<QString>{currentPath, IS_NOT_RECORDBOX_DEVICE}));

        // Create a playlist for this child
        QSqlQuery queryInsertIntoPlaylist(database);
        queryInsertIntoPlaylist.prepare(
                "INSERT INTO " + kRekordboxPlaylistsTable +
                " (name) "
                "VALUES (:name)");

        queryInsertIntoPlaylist.bindValue(":name", currentPath);

        if (!queryInsertIntoPlaylist.exec()) {
            LOG_FAILED_QUERY(queryInsertIntoPlaylist)
                    << "currentPath" << currentPath;
            return;
        }

        QSqlQuery idQuery(database);
        idQuery.prepare("select id from " + kRekordboxPlaylistsTable + " where name=:path");
        idQuery.bindValue(":path", currentPath);

        if (!idQuery.exec()) {
            LOG_FAILED_QUERY(idQuery)
                    << "currentPath" << currentPath;
            return;
        }

        int playlistID = kInvalidPlaylistId;
        while (idQuery.next()) {
            playlistID = idQuery.value(idQuery.record().indexOf("id")).toInt();
        }

        QSqlQuery queryInsertIntoPlaylistTracks(database);
        queryInsertIntoPlaylistTracks.prepare(
                "INSERT INTO " + kRekordboxPlaylistTracksTable +
                " (playlist_id, track_id, position) "
                "VALUES (:playlist_id, :track_id, :position)");

        if (playlistTrackMap.contains(childID)) {
            // Add playlist tracks for children
            for (uint32_t trackIndex = 1; trackIndex <=
                    static_cast<uint32_t>(playlistTrackMap[childID].size());
                    trackIndex++) {
                uint32_t rbTrackID = playlistTrackMap[childID][trackIndex];

                int trackID = -1;
                QSqlQuery finderQuery(database);
                finderQuery.prepare("select id from " + kRekordboxLibraryTable +
                        " where rb_id=:rb_id and device=:device");
                finderQuery.bindValue(":rb_id", rbTrackID);
                finderQuery.bindValue(":device", device);

                if (!finderQuery.exec()) {
                    LOG_FAILED_QUERY(finderQuery)
                            << "rbTrackID:" << rbTrackID
                            << "device:" << device;
                    return;
                }

                if (finderQuery.next()) {
                    trackID = finderQuery.value(finderQuery.record().indexOf("id")).toInt();
                }

                queryInsertIntoPlaylistTracks.bindValue(":playlist_id", playlistID);
                queryInsertIntoPlaylistTracks.bindValue(":track_id", trackID);
                queryInsertIntoPlaylistTracks.bindValue(":position", static_cast<int>(trackIndex));

                if (!queryInsertIntoPlaylistTracks.exec()) {
                    LOG_FAILED_QUERY(queryInsertIntoPlaylistTracks)
                            << "playlistID:" << playlistID
                            << "trackID:" << trackID
                            << "trackIndex:" << trackIndex;

                    return;
                }
            }
        }

        if (playlistIsFolderMap[childID]) {
            // If this child is a folder (playlists are only leaf nodes), build playlist tree for it
            buildPlaylistTree(database,
                    child,
                    childID,
                    playlistNameMap,
                    playlistIsFolderMap,
                    playlistTreeMap,
                    playlistTrackMap,
                    currentPath,
                    device);
        }
    }
}

void clearDeviceTables(QSqlDatabase& database, TreeItem* child) {
    ScopedTransaction transaction(database);

    int trackID = -1;
    int playlistID = kInvalidPlaylistId;
    QSqlQuery tracksQuery(database);
    tracksQuery.prepare("select id from " + kRekordboxLibraryTable + " where device=:device");
    tracksQuery.bindValue(":device", child->getLabel());

    QSqlQuery deletePlaylistsQuery(database);
    deletePlaylistsQuery.prepare("delete from " + kRekordboxPlaylistsTable + " where id=:id");

    QSqlQuery deletePlaylistTracksQuery(database);
    deletePlaylistTracksQuery.prepare("delete from " +
            kRekordboxPlaylistTracksTable + " where playlist_id=:playlist_id");

    if (!tracksQuery.exec()) {
        LOG_FAILED_QUERY(tracksQuery)
                << "device:" << child->getLabel();
    }

    while (tracksQuery.next()) {
        trackID = tracksQuery.value(tracksQuery.record().indexOf("id")).toInt();

        QSqlQuery playlistTracksQuery(database);
        playlistTracksQuery.prepare("select playlist_id from " +
                kRekordboxPlaylistTracksTable + " where track_id=:track_id");
        playlistTracksQuery.bindValue(":track_id", trackID);

        if (!playlistTracksQuery.exec()) {
            LOG_FAILED_QUERY(playlistTracksQuery)
                    << "trackID:" << trackID;
        }

        while (playlistTracksQuery.next()) {
            playlistID = playlistTracksQuery
                                 .value(playlistTracksQuery.record().indexOf(
                                         "playlist_id"))
                                 .toInt();

            deletePlaylistsQuery.bindValue(":id", playlistID);

            if (!deletePlaylistsQuery.exec()) {
                LOG_FAILED_QUERY(deletePlaylistsQuery)
                        << "playlistID:" << playlistID;
            }

            deletePlaylistTracksQuery.bindValue(":playlist_id", playlistID);

            if (!deletePlaylistTracksQuery.exec()) {
                LOG_FAILED_QUERY(deletePlaylistTracksQuery)
                        << "playlistID:" << playlistID;
            }
        }
    }

    QSqlQuery deleteTracksQuery(database);
    deleteTracksQuery.prepare("delete from " + kRekordboxLibraryTable + " where device=:device");
    deleteTracksQuery.bindValue(":device", child->getLabel());

    if (!deleteTracksQuery.exec()) {
        LOG_FAILED_QUERY(deleteTracksQuery)
                << "device:" << child->getLabel();
    }

    transaction.commit();
}

void setHotCue(TrackPointer track,
        mixxx::audio::FramePos startPosition,
        mixxx::audio::FramePos endPosition,
        int id,
        const QString& label,
        mixxx::RgbColor::optional_t color) {
    CuePointer pCue;
    const QList<CuePointer> cuePoints = track->getCuePoints();
    for (const CuePointer& trackCue : cuePoints) {
        if (trackCue->getHotCue() == id) {
            pCue = trackCue;
            break;
        }
    }

    mixxx::CueType type = mixxx::CueType::HotCue;
    if (endPosition.isValid()) {
        type = mixxx::CueType::Loop;
    }

    if (pCue) {
        // The cue may have been imported before as a plain hot cue (or the other
        // way round, if the loop was removed in Rekordbox), so update its type too.
        pCue->setType(type);
        pCue->setStartAndEndPosition(startPosition, endPosition);
    } else {
        pCue = track->createAndAddCue(
                type,
                id,
                startPosition,
                endPosition);
    }
    pCue->setLabel(label);
    if (color) {
        pCue->setColor(*color);
    }
}

void readAnalyze(TrackPointer track,
        mixxx::audio::SampleRate sampleRate,
        int timingOffset,
        bool ignoreCues,
        const QString& anlzPath) {
    if (!QFile(anlzPath).exists()) {
        return;
    }

    qDebug() << "Rekordbox ANLZ path:" << anlzPath << " for: " << track->getTitle();

    std::ifstream ifs(anlzPath.toStdString(), std::ifstream::binary);
    kaitai::kstream ks(&ifs);

    rekordbox_anlz_t anlz = rekordbox_anlz_t(&ks);

    const double sampleRateKhz = sampleRate / 1000.0;

    QList<memory_cue_loop_t> memoryCuesAndLoops;
    int lastHotCueIndex = 0;

    for (const auto& section : *anlz.sections()) {
        switch (section->fourcc()) {
        case rekordbox_anlz_t::SECTION_TAGS_BEAT_GRID: {
            if (!ignoreCues) {
                break;
            }

            auto* beatGridTag =
                    static_cast<rekordbox_anlz_t::beat_grid_tag_t*>(
                            section->body());

            QVector<mixxx::audio::FramePos> beats;

            for (const auto& beat : *beatGridTag->beats()) {
                int time = static_cast<int>(beat->time()) - timingOffset;
                // Ensure no offset times are less than 1
                if (time < 1) {
                    time = 1;
                }
                beats << mixxx::audio::FramePos(sampleRateKhz * static_cast<double>(time));
            }

            const auto pBeats = mixxx::Beats::fromBeatPositions(
                    sampleRate,
                    beats,
                    mixxx::rekordboxconstants::beatsSubversion);
            track->trySetBeats(pBeats);

            // Rekordbox numbers every beat 1-4 within its bar. If the grid starts on beat n, the
            // first "1" is the (5 - n) % 4'th beat (counting from 0): used to mark bar starts
            // in red on the waveform.
            if (!beatGridTag->beats()->empty()) {
                const int firstBeatNumber =
                        static_cast<int>(beatGridTag->beats()->front()->beat_number());
                if (firstBeatNumber >= 1 && firstBeatNumber <= 4) {
                    track->setDownbeatPhase((5 - firstBeatNumber) % 4);
                }
            }
        } break;
        case rekordbox_anlz_t::SECTION_TAGS_CUES: {
            if (ignoreCues) {
                break;
            }

            auto* cuesTag =
                    static_cast<rekordbox_anlz_t::cue_tag_t*>(
                            section->body());

            for (const auto& cueEntry : *cuesTag->cues()) {
                int time = static_cast<int>(cueEntry->time()) - timingOffset;
                // Ensure no offset times are less than 1
                if (time < 1) {
                    time = 1;
                }
                const auto position = mixxx::audio::FramePos(
                        sampleRateKhz * static_cast<double>(time));

                switch (cuesTag->type()) {
                case rekordbox_anlz_t::CUE_LIST_TYPE_MEMORY_CUES: {
                    switch (cueEntry->type()) {
                    case rekordbox_anlz_t::CUE_ENTRY_TYPE_MEMORY_CUE: {
                        memory_cue_loop_t memoryCue;
                        memoryCue.startPosition = position;
                        memoryCue.endPosition = mixxx::audio::kInvalidFramePos;
                        memoryCue.color = mixxx::RgbColor::nullopt();
                        memoryCuesAndLoops << memoryCue;
                    } break;
                    case rekordbox_anlz_t::CUE_ENTRY_TYPE_LOOP: {
                        int endTime = static_cast<int>(cueEntry->loop_time()) - timingOffset;
                        // Ensure no offset times are less than 1
                        if (endTime < 1) {
                            endTime = 1;
                        }

                        memory_cue_loop_t loop;
                        loop.startPosition = position;
                        loop.endPosition = mixxx::audio::FramePos(
                                sampleRateKhz * static_cast<double>(endTime));
                        loop.color = mixxx::RgbColor::nullopt();
                        memoryCuesAndLoops << loop;
                    } break;
                    }
                } break;
                case rekordbox_anlz_t::CUE_LIST_TYPE_HOT_CUES: {
                    int hotCueIndex = static_cast<int>(cueEntry->hot_cue() - 1);
                    if (hotCueIndex > lastHotCueIndex) {
                        lastHotCueIndex = hotCueIndex;
                    }
                    // A Rekordbox hot cue can carry a loop ("cue with loop"). Import it
                    // as a Mixxx saved loop so triggering it jumps there AND loops.
                    mixxx::audio::FramePos hotCueEnd = mixxx::audio::kInvalidFramePos;
                    if (cueEntry->type() == rekordbox_anlz_t::CUE_ENTRY_TYPE_LOOP) {
                        int endTime = static_cast<int>(cueEntry->loop_time()) - timingOffset;
                        if (endTime < 1) {
                            endTime = 1;
                        }
                        hotCueEnd = mixxx::audio::FramePos(
                                sampleRateKhz * static_cast<double>(endTime));
                    }
                    setHotCue(
                            track,
                            position,
                            hotCueEnd,
                            hotCueIndex,
                            QString(),
                            mixxx::RgbColor::nullopt());
                } break;
                }
            }
        } break;
        case rekordbox_anlz_t::SECTION_TAGS_CUES_2: {
            if (ignoreCues) {
                break;
            }

            auto* cuesExtendedTag =
                    static_cast<rekordbox_anlz_t::cue_extended_tag_t*>(
                            section->body());

            for (const auto& cueExtendedEntry : *cuesExtendedTag->cues()) {
                int time = static_cast<int>(cueExtendedEntry->time()) - timingOffset;
                // Ensure no offset times are less than 1
                if (time < 1) {
                    time = 1;
                }
                const auto position = mixxx::audio::FramePos(
                        sampleRateKhz * static_cast<double>(time));

                switch (cuesExtendedTag->type()) {
                case rekordbox_anlz_t::CUE_LIST_TYPE_MEMORY_CUES: {
                    switch (cueExtendedEntry->type()) {
                    case rekordbox_anlz_t::CUE_ENTRY_TYPE_MEMORY_CUE: {
                        memory_cue_loop_t memoryCue;
                        memoryCue.startPosition = position;
                        memoryCue.endPosition = mixxx::audio::kInvalidFramePos;
                        memoryCue.comment = fromUtf16BeString(cueExtendedEntry->comment());
                        memoryCue.color = colorFromID(static_cast<int>(
                                cueExtendedEntry->color_id()));
                        memoryCuesAndLoops << memoryCue;
                    } break;
                    case rekordbox_anlz_t::CUE_ENTRY_TYPE_LOOP: {
                        int endTime =
                                static_cast<int>(
                                        cueExtendedEntry->loop_time()) -
                                timingOffset;
                        // Ensure no offset times are less than 1
                        if (endTime < 1) {
                            endTime = 1;
                        }

                        memory_cue_loop_t loop;
                        loop.startPosition = position;
                        loop.endPosition = mixxx::audio::FramePos(
                                sampleRateKhz * static_cast<double>(endTime));
                        loop.comment = fromUtf16BeString(cueExtendedEntry->comment());
                        loop.color = colorFromID(static_cast<int>(cueExtendedEntry->color_id()));
                        memoryCuesAndLoops << loop;
                    } break;
                    }
                } break;
                case rekordbox_anlz_t::CUE_LIST_TYPE_HOT_CUES: {
                    int hotCueIndex = static_cast<int>(cueExtendedEntry->hot_cue() - 1);
                    if (hotCueIndex > lastHotCueIndex) {
                        lastHotCueIndex = hotCueIndex;
                    }
                    // Hot cue with a loop: see the note in the basic cue tag above.
                    mixxx::audio::FramePos hotCueEnd = mixxx::audio::kInvalidFramePos;
                    if (cueExtendedEntry->type() == rekordbox_anlz_t::CUE_ENTRY_TYPE_LOOP) {
                        int endTime = static_cast<int>(cueExtendedEntry->loop_time()) -
                                timingOffset;
                        if (endTime < 1) {
                            endTime = 1;
                        }
                        hotCueEnd = mixxx::audio::FramePos(
                                sampleRateKhz * static_cast<double>(endTime));
                    }
                    setHotCue(track,
                            position,
                            hotCueEnd,
                            hotCueIndex,
                            fromUtf16BeString(cueExtendedEntry->comment()),
                            mixxx::RgbColor(qRgb(
                                    static_cast<int>(
                                            cueExtendedEntry->color_red()),
                                    static_cast<int>(
                                            cueExtendedEntry->color_green()),
                                    static_cast<int>(cueExtendedEntry
                                                    ->color_blue()))));
                } break;
                }
            }
        } break;
        default:
            break;
        }
    }

    if (memoryCuesAndLoops.size() > 0) {
        std::sort(memoryCuesAndLoops.begin(),
                memoryCuesAndLoops.end(),
                [](const memory_cue_loop_t& a, const memory_cue_loop_t& b)
                        -> bool { return a.startPosition < b.startPosition; });

        bool mainCueFound = false;

        // Add memory cues and loops
        for (int memoryCueOrLoopIndex = 0;
                memoryCueOrLoopIndex < memoryCuesAndLoops.size();
                memoryCueOrLoopIndex++) {
            memory_cue_loop_t memoryCueOrLoop = memoryCuesAndLoops[memoryCueOrLoopIndex];

            if (!mainCueFound && !memoryCueOrLoop.endPosition.isValid()) {
                // Set first chronological memory cue as Mixxx MainCue
                track->setMainCuePosition(memoryCueOrLoop.startPosition);
                CuePointer pMainCue = track->findCueByType(mixxx::CueType::MainCue);
                pMainCue->setLabel(memoryCueOrLoop.comment);
                pMainCue->setColor(*memoryCueOrLoop.color);
                mainCueFound = true;
            } else {
                // Mixxx v2.4 will feature multiple loops, so these saved here will be usable
                // For 2.3, Mixxx treats them as hotcues and the first one will be loaded as the single loop Mixxx supports
                lastHotCueIndex++;
                setHotCue(
                        track,
                        memoryCueOrLoop.startPosition,
                        memoryCueOrLoop.endPosition,
                        lastHotCueIndex,
                        memoryCueOrLoop.comment,
                        memoryCueOrLoop.color);
            }
        }
    }
}

} // anonymous namespace

RekordboxPlaylistModel::RekordboxPlaylistModel(QObject* parent,
        TrackCollectionManager* trackCollectionManager,
        QSharedPointer<BaseTrackCache> trackSource)
        : BaseExternalPlaylistModel(parent,
                  trackCollectionManager,
                  "mixxx.db.model.rekordbox.playlistmodel",
                  kRekordboxPlaylistsTable,
                  kRekordboxPlaylistTracksTable,
                  trackSource) {
    m_coverLookupPool.setMaxThreadCount(1);
    m_coverLookupPool.setThreadPriority(QThread::LowPriority);
    m_prepareTrackPool.setMaxThreadCount(1);
    m_prepareTrackPool.setThreadPriority(QThread::LowPriority);
}

void RekordboxPlaylistModel::initSortColumnMapping() {
    // Add a bijective mapping between the SortColumnIds and column indices
    for (int i = 0; i < static_cast<int>(TrackModel::SortColumnId::IdMax); ++i) {
        m_columnIndexBySortColumnId[i] = -1;
    }

    m_columnIndexBySortColumnId[static_cast<int>(
            TrackModel::SortColumnId::Artist)] =
            fieldIndex(ColumnCache::COLUMN_LIBRARYTABLE_ARTIST);
    m_columnIndexBySortColumnId[static_cast<int>(
            TrackModel::SortColumnId::Title)] =
            fieldIndex(ColumnCache::COLUMN_LIBRARYTABLE_TITLE);
    m_columnIndexBySortColumnId[static_cast<int>(
            TrackModel::SortColumnId::Album)] =
            fieldIndex(ColumnCache::COLUMN_LIBRARYTABLE_ALBUM);
    m_columnIndexBySortColumnId[static_cast<int>(
            TrackModel::SortColumnId::AlbumArtist)] =
            fieldIndex(ColumnCache::COLUMN_LIBRARYTABLE_ALBUMARTIST);
    m_columnIndexBySortColumnId[static_cast<int>(
            TrackModel::SortColumnId::Year)] =
            fieldIndex(ColumnCache::COLUMN_LIBRARYTABLE_YEAR);
    m_columnIndexBySortColumnId[static_cast<int>(
            TrackModel::SortColumnId::Genre)] =
            fieldIndex(ColumnCache::COLUMN_LIBRARYTABLE_GENRE);
    m_columnIndexBySortColumnId[static_cast<int>(
            TrackModel::SortColumnId::Composer)] =
            fieldIndex(ColumnCache::COLUMN_LIBRARYTABLE_COMPOSER);
    m_columnIndexBySortColumnId[static_cast<int>(
            TrackModel::SortColumnId::Grouping)] =
            fieldIndex(ColumnCache::COLUMN_LIBRARYTABLE_GROUPING);
    m_columnIndexBySortColumnId[static_cast<int>(
            TrackModel::SortColumnId::TrackNumber)] =
            fieldIndex(ColumnCache::COLUMN_LIBRARYTABLE_TRACKNUMBER);
    m_columnIndexBySortColumnId[static_cast<int>(
            TrackModel::SortColumnId::FileType)] =
            fieldIndex(ColumnCache::COLUMN_LIBRARYTABLE_FILETYPE);
    m_columnIndexBySortColumnId[static_cast<int>(
            TrackModel::SortColumnId::NativeLocation)] =
            fieldIndex(ColumnCache::COLUMN_TRACKLOCATIONSTABLE_LOCATION);
    m_columnIndexBySortColumnId[static_cast<int>(
            TrackModel::SortColumnId::Comment)] =
            fieldIndex(ColumnCache::COLUMN_LIBRARYTABLE_COMMENT);
    m_columnIndexBySortColumnId[static_cast<int>(
            TrackModel::SortColumnId::Duration)] =
            fieldIndex(ColumnCache::COLUMN_LIBRARYTABLE_DURATION);
    m_columnIndexBySortColumnId[static_cast<int>(
            TrackModel::SortColumnId::BitRate)] =
            fieldIndex(ColumnCache::COLUMN_LIBRARYTABLE_BITRATE);
    m_columnIndexBySortColumnId[static_cast<int>(
            TrackModel::SortColumnId::Bpm)] =
            fieldIndex(ColumnCache::COLUMN_LIBRARYTABLE_BPM);
    m_columnIndexBySortColumnId[static_cast<int>(
            TrackModel::SortColumnId::ReplayGain)] =
            fieldIndex(ColumnCache::COLUMN_LIBRARYTABLE_REPLAYGAIN);
    m_columnIndexBySortColumnId[static_cast<int>(
            TrackModel::SortColumnId::DateTimeAdded)] =
            fieldIndex(ColumnCache::COLUMN_LIBRARYTABLE_DATETIMEADDED);
    m_columnIndexBySortColumnId[static_cast<int>(
            TrackModel::SortColumnId::TimesPlayed)] =
            fieldIndex(ColumnCache::COLUMN_LIBRARYTABLE_TIMESPLAYED);
    m_columnIndexBySortColumnId[static_cast<int>(
            TrackModel::SortColumnId::LastPlayedAt)] =
            fieldIndex(ColumnCache::COLUMN_LIBRARYTABLE_LAST_PLAYED_AT);
    m_columnIndexBySortColumnId[static_cast<int>(
            TrackModel::SortColumnId::Rating)] =
            fieldIndex(ColumnCache::COLUMN_LIBRARYTABLE_RATING);
    m_columnIndexBySortColumnId[static_cast<int>(
            TrackModel::SortColumnId::Key)] =
            fieldIndex(ColumnCache::COLUMN_LIBRARYTABLE_KEY);
    m_columnIndexBySortColumnId[static_cast<int>(
            TrackModel::SortColumnId::Preview)] =
            fieldIndex(ColumnCache::COLUMN_LIBRARYTABLE_PREVIEW);
    m_columnIndexBySortColumnId[static_cast<int>(
            TrackModel::SortColumnId::Color)] =
            fieldIndex(ColumnCache::COLUMN_LIBRARYTABLE_COLOR);
    m_columnIndexBySortColumnId[static_cast<int>(
            TrackModel::SortColumnId::CoverArt)] =
            fieldIndex(ColumnCache::COLUMN_LIBRARYTABLE_COVERART);
    m_columnIndexBySortColumnId[static_cast<int>(
            TrackModel::SortColumnId::Position)] =
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

namespace {

// mp3guessenc scans the whole MP3 to work out which decoder offset Rekordbox's timing needs. That is
// slow from a USB stick, so the result is remembered, and prepareTrack() computes it in the
// background as soon as a row is selected. The tool keeps state, so one scan at a time.
QMutex s_timingShiftMutex;
QHash<QString, int> s_timingShiftByLocation;

int cachedTimingShiftCase(const QString& location) {
    QMutexLocker locker(&s_timingShiftMutex);
    const auto cached = s_timingShiftByLocation.constFind(location);
    if (cached != s_timingShiftByLocation.constEnd()) {
        return cached.value();
    }
    const int result = mp3guessenc_timing_shift_case(location.toStdString().c_str());
    s_timingShiftByLocation.insert(location, result);
    return result;
}

} // namespace

void RekordboxPlaylistModel::prepareTrack(const QModelIndex& index) const {
    const QString location =
            getFieldVariant(index, ColumnCache::COLUMN_TRACKLOCATIONSTABLE_LOCATION).toString();
    const int generation = ++(*m_prepareGeneration);
    if (!location.endsWith(".mp3", Qt::CaseInsensitive)) {
        return;
    }
    const auto pGeneration = m_prepareGeneration;
    (void)QtConcurrent::run(&m_prepareTrackPool, [pGeneration, generation, location]() {
        if (pGeneration->load() != generation || !QFile::exists(location)) {
            return; // the user moved on to another row, or the file is gone
        }
        cachedTimingShiftCase(location);
    });
}

TrackPointer RekordboxPlaylistModel::getTrack(const QModelIndex& index) const {
    qDebug() << "RekordboxTrackModel::getTrack";

    TrackPointer track = BaseExternalPlaylistModel::getTrack(index);
    QString location = getFieldVariant(
            index, ColumnCache::COLUMN_TRACKLOCATIONSTABLE_LOCATION)
                               .toString();

    if (!QFile(location).exists()) {
        return track;
    }

    // The following code accounts for timing offsets required to
    // correctly align timing information (cue points, loops, beatgrids)
    // exported from Rekordbox. This is caused by different MP3
    // decoders treating MP3s encoded in a variety of different cases
    // differently. The mp3guessenc library is used to determine which
    // case the MP3 is classified in. See the following PR for more
    // detailed information:
    // https://github.com/mixxxdj/mixxx/pull/2119

    int timingOffset = 0;

    if (location.endsWith(".mp3", Qt::CaseInsensitive)) {
        int timingShiftCase = cachedTimingShiftCase(location);

        qDebug() << "Timing shift case:" << timingShiftCase << "for MP3 file:" << location;

        switch (timingShiftCase) {
#ifdef __COREAUDIO__
        case EXIT_CODE_CASE_A:
            timingOffset = 12;
            break;
        case EXIT_CODE_CASE_B:
            timingOffset = 13;
            break;
        case EXIT_CODE_CASE_C:
            timingOffset = 26;
            break;
        case EXIT_CODE_CASE_D:
            timingOffset = 50;
            break;
#elif defined(__MAD__)
        case EXIT_CODE_CASE_A:
        case EXIT_CODE_CASE_D:
            timingOffset = 26;
            break;
#elif defined(__FFMPEG__)
        case EXIT_CODE_CASE_D:
            timingOffset = 26;
            break;
#endif
        }
    }

#ifdef __COREAUDIO__
    if (location.toLower().endsWith(".m4a")) {
        timingOffset = 48;
    }
#endif

    mixxx::audio::SampleRate sampleRate = track->getSampleRate();

    QString anlzPath =
            getFieldVariant(index, ColumnCache::COLUMN_REKORDBOX_ANALYZE_PATH)
                    .toString();
    QString anlzPathExt = anlzPath.left(anlzPath.length() - 3) + "EXT";

    if (QFile(anlzPathExt).exists()) {
        // Beatgrids appear to be only correct in legacy ANLZ file
        readAnalyze(track, sampleRate, timingOffset, true, anlzPath);
        readAnalyze(track, sampleRate, timingOffset, false, anlzPathExt);
    } else {
        readAnalyze(track, sampleRate, timingOffset, false, anlzPath);
    }

    // Assume that the key of the file the has been analyzed in Recordbox is correct
    // and prevent the AnalyzerKey from re-analyzing.
    // Form 5.4.3 Key format depends on the preferences option:
    // Classic: Abm,B,Ebm,F#,Bbm,Db,Fm,Ab,…,F#m,A,Dbm,E
    // Alphanumeric (Camelot): 1A,1B,2A,2B,3A,3B,4A,4B,…,11A,11B,12A,12B
    // Not reckognized: 1m, 01A
    // Earlier versions allow any format
    // Decision: We normalize the KeyText here to not write garbage to the
    // file metadata and it is unlikely to loose extra info.
    track->setKeys(KeyFactory::makeBasicKeysNormalized(
            getFieldVariant(index, ColumnCache::COLUMN_LIBRARYTABLE_KEY).toString(),
            mixxx::track::io::key::USER));

    track->setColor(mixxx::RgbColor::fromQVariant(
            getFieldVariant(index, ColumnCache::COLUMN_LIBRARYTABLE_COLOR)));

    return track;
}

namespace {

// Reads the preview waveform ("PWAV": 400 bytes, per byte the low 5 bits are the height and
// the high 3 bits how white that column is) out of a Rekordbox ANLZ .DAT file.
QByteArray readPreviewWaveform(const QString& anlzPath) {
    if (anlzPath.isEmpty() || !QFile::exists(anlzPath)) {
        return QByteArray();
    }
    try {
        std::ifstream ifs(anlzPath.toStdString(), std::ifstream::binary);
        kaitai::kstream ks(&ifs);
        rekordbox_anlz_t anlz = rekordbox_anlz_t(&ks);
        for (const auto& section : *anlz.sections()) {
            if (section->fourcc() == rekordbox_anlz_t::SECTION_TAGS_WAVE_PREVIEW) {
                auto* pTag = static_cast<rekordbox_anlz_t::wave_preview_tag_t*>(section->body());
                const std::string data = pTag->data();
                return QByteArray(data.data(), static_cast<qsizetype>(data.size()));
            }
        }
    } catch (...) {
        // unreadable analysis file: the track just shows no overview
    }
    return QByteArray();
}

// Draws the Rekordbox preview waveform as a mirrored bar graph, orange fading to white.
class RekordboxOverviewDelegate : public TableItemDelegate {
  public:
    RekordboxOverviewDelegate(QTableView* pTableView, const RekordboxPlaylistModel* pModel)
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
        const QByteArray data = m_pModel->previewWaveform(index);
        if (data.isEmpty()) {
            return;
        }
        const QRect area = option.rect.adjusted(2, 4, -2, -4);
        if (area.width() <= 0 || area.height() <= 0) {
            return;
        }
        // The ~170 coloured lines of a waveform are drawn once per track and cell size into a
        // pixmap, which is then just blitted on every repaint (scrolling repaints a lot).
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
                m_pixmaps.clear(); // far more than a screenful: start over
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
        const double halfHeight = size.height() / 2.0;
        const int count = static_cast<int>(data.size());
        for (int x = 0; x < size.width(); ++x) {
            const int i = std::min(count - 1, x * count / size.width());
            const auto value = static_cast<quint8>(data.at(i));
            const double amplitude = (value & 0x1F) / 31.0 * halfHeight;
            const double white = (value >> 5) / 7.0;
            // orange, fading to white where Rekordbox marks the column "whiter" (it is blue in
            // Rekordbox itself; orange here so it doesn't clash with the blue row highlight)
            painter.setPen(QColor(255,
                    static_cast<int>(122 + 133 * white),
                    static_cast<int>(26 + 229 * white)));
            painter.drawLine(QLineF(x + 0.5, halfHeight - amplitude, x + 0.5, halfHeight + amplitude));
        }
        return pixmap;
    }

    QPointer<const RekordboxPlaylistModel> m_pModel;
    mutable QHash<quint64, QPixmap> m_pixmaps;
    mutable QSize m_cachedSize;
    mutable qreal m_cachedRatio = 0;
};

} // namespace

QAbstractItemDelegate* RekordboxPlaylistModel::delegateForColumn(
        const int index, QObject* pParent) {
    if (index == fieldIndex(ColumnCache::COLUMN_LIBRARYTABLE_WAVESUMMARYHEX)) {
        auto* pTableView = qobject_cast<QTableView*>(pParent);
        if (pTableView) {
            return new RekordboxOverviewDelegate(pTableView, this);
        }
    }
    return BaseExternalPlaylistModel::delegateForColumn(index, pParent);
}

QByteArray RekordboxPlaylistModel::previewWaveform(const QModelIndex& index) const {
    const QString anlzPath =
            getFieldVariant(index, ColumnCache::COLUMN_REKORDBOX_ANALYZE_PATH).toString();
    if (anlzPath.isEmpty()) {
        return QByteArray();
    }
    const auto cached = m_previewByAnlzPath.constFind(anlzPath);
    if (cached != m_previewByAnlzPath.constEnd()) {
        return cached.value();
    }
    if (m_previewLookupStarted.contains(anlzPath)) {
        return QByteArray(); // being read
    }
    m_previewLookupStarted.insert(anlzPath);

    QPointer<RekordboxPlaylistModel> self(const_cast<RekordboxPlaylistModel*>(this));
    (void)QtConcurrent::run(&m_coverLookupPool, [self, anlzPath]() {
        const QByteArray preview = readPreviewWaveform(anlzPath);
        if (!self) {
            return;
        }
        QMetaObject::invokeMethod(
                self.data(),
                [self, anlzPath, preview]() {
                    if (!self) {
                        return;
                    }
                    self->m_previewByAnlzPath.insert(anlzPath, preview);
                    // One repaint of the column for a burst of results.
                    if (self->m_previewRefreshQueued) {
                        return;
                    }
                    self->m_previewRefreshQueued = true;
                    QTimer::singleShot(150, self.data(), [self]() {
                        self->m_previewRefreshQueued = false;
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

CoverInfo RekordboxPlaylistModel::getCoverInfo(const QModelIndex& index) const {
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

    // Guess from the file itself on the worker thread, without adding it to the Mixxx
    // library or creating Track objects (only plain file/tag/image functions are used).
    // Missing files and files without art give an empty CoverInfo, which is remembered
    // too so the file isn't read again.
    const QString album = getFieldString(index, ColumnCache::COLUMN_LIBRARYTABLE_ALBUM);
    QPointer<RekordboxPlaylistModel> self(const_cast<RekordboxPlaylistModel*>(this));
    (void)QtConcurrent::run(&m_coverLookupPool, [self, location, album]() {
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
                    // Repaint the Cover Art column once for a burst of results.
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

QList<QPair<int, int>> RekordboxPlaylistModel::defaultColumnLayout() const {
    // The playlist position (#, sorted ascending by default for a playlist), cover art, then the
    // things you pick a track by, then the overview waveform. The table is 766 px wide on a
    // 1024 px screen (1024 - sidebar 230 - scrollbar 28); these add up to 764, so the columns fill
    // it with nothing left over on the right and it never scrolls sideways. The header widens a
    // column whose title doesn't fit and takes the pixels from the widest one (the title).
    // Everything else is hidden (turn it back on from the header's right-click menu). Bump
    // defaultColumnLayoutVersion() when changing this so it replaces saved layouts.
    return {
            {fieldIndex(ColumnCache::COLUMN_PLAYLISTTRACKSTABLE_POSITION), 24},
            {fieldIndex(ColumnCache::COLUMN_LIBRARYTABLE_COVERART), 46},
            {fieldIndex(ColumnCache::COLUMN_LIBRARYTABLE_TITLE), 200},
            {fieldIndex(ColumnCache::COLUMN_LIBRARYTABLE_ARTIST), 141},
            {fieldIndex(ColumnCache::COLUMN_LIBRARYTABLE_BPM), 58},
            {fieldIndex(ColumnCache::COLUMN_LIBRARYTABLE_KEY), 51},
            {fieldIndex(ColumnCache::COLUMN_LIBRARYTABLE_DURATION), 88},
            {fieldIndex(ColumnCache::COLUMN_LIBRARYTABLE_WAVESUMMARYHEX), 156},
    };
}

int RekordboxPlaylistModel::defaultFlexibleColumn() const {
    // Duration: its title is longer than its values, so it is the one that needs room for text.
    return fieldIndex(ColumnCache::COLUMN_LIBRARYTABLE_DURATION);
}

int RekordboxPlaylistModel::defaultColumnLayoutVersion() const {
    // 2: overview narrowed so the columns fit without sideways scrolling
    // 3: a few px moved from the title to Duration, whose title was cut off
    // 4: Duration trimmed back a little (86 -> 80)
    // 5: widths fill the table (764 of 766 px), Duration sized to its title
    // 6: only Duration is measured against its title; all other widths are fixed
    return 6;
}

bool RekordboxPlaylistModel::isColumnHiddenByDefault(int column) {
    if (column == fieldIndex(ColumnCache::COLUMN_LIBRARYTABLE_BITRATE)) {
        return true;
    }
    return BaseSqlTableModel::isColumnHiddenByDefault(column);
}

bool RekordboxPlaylistModel::isColumnInternal(int column) {
    return column == fieldIndex(ColumnCache::COLUMN_REKORDBOX_ANALYZE_PATH) ||
            BaseExternalPlaylistModel::isColumnInternal(column);
}

RekordboxFeature::RekordboxFeature(
        Library* pLibrary,
        UserSettingsPointer pConfig)
        : BaseExternalLibraryFeature(pLibrary, pConfig, QStringLiteral("rekordbox")),
          m_pSidebarModel(make_parented<TreeItemModel>(this)) {
    QString tableName = kRekordboxLibraryTable;
    QString idColumn = LIBRARYTABLE_ID;
    QStringList columns = {
            LIBRARYTABLE_ID,
            LIBRARYTABLE_ARTIST,
            LIBRARYTABLE_TITLE,
            LIBRARYTABLE_ALBUM,
            LIBRARYTABLE_YEAR,
            LIBRARYTABLE_GENRE,
            LIBRARYTABLE_TRACKNUMBER,
            TRACKLOCATIONSTABLE_LOCATION,
            LIBRARYTABLE_COMMENT,
            LIBRARYTABLE_RATING,
            LIBRARYTABLE_DURATION,
            LIBRARYTABLE_BITRATE,
            LIBRARYTABLE_BPM,
            LIBRARYTABLE_KEY,
            LIBRARYTABLE_COLOR,
            LIBRARYTABLE_COVERART,
            LIBRARYTABLE_WAVESUMMARYHEX,
            REKORDBOX_ANALYZE_PATH};

    const QStringList searchColumns = {
            LIBRARYTABLE_ARTIST,
            LIBRARYTABLE_TITLE,
            LIBRARYTABLE_ALBUM,
            LIBRARYTABLE_GENRE,
            LIBRARYTABLE_TRACKNUMBER,
            TRACKLOCATIONSTABLE_LOCATION,
            LIBRARYTABLE_COMMENT};

    m_trackSource = QSharedPointer<BaseTrackCache>::create(
            m_pTrackCollection,
            tableName,
            std::move(idColumn),
            std::move(columns),
            std::move(searchColumns),
            false);
    m_pRekordboxPlaylistModel = make_parented<RekordboxPlaylistModel>(
            this, pLibrary->trackCollectionManager(), m_trackSource);

    m_title = tr("Rekordbox");

    QSqlDatabase database = m_pTrackCollection->database();
    ScopedTransaction transaction(database);
    // Drop any leftover temporary Rekordbox database tables if they exist
    dropTable(database, kRekordboxPlaylistTracksTable);
    dropTable(database, kRekordboxPlaylistsTable);
    dropTable(database, kRekordboxLibraryTable);

    // Create new temporary Rekordbox database tables
    createLibraryTable(database, kRekordboxLibraryTable);
    createPlaylistsTable(database, kRekordboxPlaylistsTable);
    createPlaylistTracksTable(database, kRekordboxPlaylistTracksTable);
    transaction.commit();

    connect(&m_devicesFutureWatcher,
            &QFutureWatcher<QList<TreeItem*>>::finished,
            this,
            &RekordboxFeature::onRekordboxDevicesFound);
    connect(&m_tracksFutureWatcher,
            &QFutureWatcher<QString>::finished,
            this,
            &RekordboxFeature::onTracksFound);
    // initialize the model
    m_pSidebarModel->setRootItem(TreeItem::newRoot(this));

    connect(&m_loadingTimer, &QTimer::timeout, this, [this]() {
        m_loadingDots = (m_loadingDots + 1) % 4;
        updateLoadingPage();
    });
}

RekordboxFeature::~RekordboxFeature() {
    m_devicesFuture.waitForFinished();
    m_tracksFuture.waitForFinished();

    // Drop temporary Rekordbox database tables on shutdown
    QSqlDatabase database = m_pTrackCollection->database();
    ScopedTransaction transaction(database);
    dropTable(database, kRekordboxPlaylistTracksTable);
    dropTable(database, kRekordboxPlaylistsTable);
    dropTable(database, kRekordboxLibraryTable);
    transaction.commit();
}

void RekordboxFeature::bindLibraryWidget(WLibrary* pLibraryWidget,
        KeyboardEventFilter* keyboard) {
    Q_UNUSED(keyboard);
    m_pLibraryWidget = pLibraryWidget;
    parented_ptr<WLibraryTextBrowser> pEdit = make_parented<WLibraryTextBrowser>(pLibraryWidget);
    pEdit->setHtml(formatRootViewHtml());
    pLibraryWidget->registerView("REKORDBOXHOME", pEdit);

    // Shown while a drive is being read (and if reading it failed).
    parented_ptr<WLibraryTextBrowser> pLoading = make_parented<WLibraryTextBrowser>(pLibraryWidget);
    m_pLoadingView = pLoading.get();
    pLibraryWidget->registerView("REKORDBOXLOADING", pLoading);
}

void RekordboxFeature::showLoadingPage(
        const QString& heading, const QString& message, bool animate) {
    m_loadingHeading = heading;
    m_loadingMessage = message;
    m_loadingAnimate = animate;
    m_loadingDots = 1; // never start on a bare line: show dots from the first frame
    updateLoadingPage();
    if (animate) {
        m_loadingTimer.start(150); // brisk, so it's clear something is happening
    } else {
        m_loadingTimer.stop();
    }
    emit switchToView("REKORDBOXLOADING");
}

void RekordboxFeature::updateLoadingPage() {
    if (!m_pLoadingView) {
        return;
    }
    QString message = m_loadingMessage;
    if (m_loadingAnimate) {
        // Fixed width, so the text doesn't jitter as the dots come and go.
        message += QString(m_loadingDots, QChar('.')) +
                QString(3 - m_loadingDots, QChar(0x2007)); // figure space
    }
    m_pLoadingView->setHtml(QString("<h2>%1</h2><p style=\"font-size:20px;\">%2</p>")
                                    .arg(m_loadingHeading.toHtmlEscaped(),
                                            message.toHtmlEscaped()));
}

void RekordboxFeature::preloadDevices() {
    if (m_devicesFutureWatcher.isRunning()) {
        return;
    }
    m_devicesFuture = QtConcurrent::run(findRekordboxDevices);
    m_devicesFutureWatcher.setFuture(m_devicesFuture);
}

TreeItem* RekordboxFeature::findDeviceItem(const QString& devicePath) const {
    TreeItem* root = m_pSidebarModel->getRootItem();
    for (int i = 0; i < root->childRows(); ++i) {
        TreeItem* child = root->child(i);
        const QList<QVariant> data = child->getData().toList();
        if (!data.isEmpty() && data[0].toString() == devicePath) {
            return child;
        }
    }
    return nullptr;
}

void RekordboxFeature::startDeviceParse(TreeItem* pDeviceItem, bool showWhenDone) {
    QList<QVariant> data = pDeviceItem->getData().toList();
    VERIFY_OR_DEBUG_ASSERT(data.size() >= 2) {
        return;
    }
    const QString devicePath = data[0].toString();
    if (showWhenDone) {
        m_showWhenParsed = devicePath;
        showLoadingPage(pDeviceItem->getLabel(), tr("Reading drive"), true);
    }
    if (!m_parsingDevicePath.isEmpty()) {
        // One drive at a time. A tapped drive goes to the front of the line.
        m_parseQueue.removeAll(devicePath);
        if (showWhenDone) {
            m_parseQueue.prepend(devicePath);
        } else {
            m_parseQueue.append(devicePath);
        }
        return;
    }

    qDebug() << "Parse Rekordbox Device DB: " << devicePath;
    m_parsingDevicePath = devicePath;
    // Let a worker thread do the XML parsing. It gets copies of the device's name and path: the
    // item's data is changed right below, and reading it from the thread raced with that.
    m_tracksFuture = QtConcurrent::run(parseDeviceDB,
            static_cast<Library*>(parent())->dbConnectionPool(),
            pDeviceItem,
            pDeviceItem->getLabel(),
            devicePath);
    m_tracksFutureWatcher.setFuture(m_tracksFuture);

    // This device is now a playlist element, future activations should treat is as such
    data[1] = QVariant(IS_NOT_RECORDBOX_DEVICE);
    pDeviceItem->setData(QVariant(data));
}

void RekordboxFeature::startNextQueuedParse() {
    while (m_parsingDevicePath.isEmpty() && !m_parseQueue.isEmpty()) {
        TreeItem* pItem = findDeviceItem(m_parseQueue.takeFirst());
        if (!pItem) {
            continue; // unplugged while it waited
        }
        const QList<QVariant> data = pItem->getData().toList();
        if (data.size() >= 2 && data[1].toString() == IS_RECORDBOX_DEVICE) {
            startDeviceParse(pItem, m_showWhenParsed == data[0].toString());
        }
    }
}

std::unique_ptr<BaseSqlTableModel>
RekordboxFeature::createPlaylistModelForPlaylist(const QVariant& data) {
    VERIFY_OR_DEBUG_ASSERT(data.canConvert<QVariantList>()) {
        return {};
    }
    QVariantList playlists = data.toList();
    VERIFY_OR_DEBUG_ASSERT(playlists.size() > 0) {
        return {};
    }
    auto pModel = std::make_unique<RekordboxPlaylistModel>(
            this, m_pLibrary->trackCollectionManager(), m_trackSource);
    pModel->setPlaylist(playlists.at(0).toString());
    return pModel;
}

QVariant RekordboxFeature::title() {
    return m_title;
}

bool RekordboxFeature::isSupported() {
    return true;
}

TreeItemModel* RekordboxFeature::sidebarModel() const {
    return m_pSidebarModel;
}

QString RekordboxFeature::formatRootViewHtml() const {
    // Short on purpose (small touchscreen). No "rescan" link: a drive that is plugged in or
    // removed is picked up on its own within a few seconds, and tapping Rekordbox in the sidebar
    // looks again as well.
    const QString title = tr("Rekordbox Libraries");
    const QString summary = tr(
            "Drives exported from Rekordbox will show up in the 'Rekordbox' dropdown on the left.");
    const QString detail = tr(
            "This will load all tracks, playlists, beatgrids, and cues from the drive. Applicable drives are picked up automatically when plugged in and read in the background.");

    QString html;
    html.append(QString("<h1>%1</h1>").arg(title));
    html.append(QString("<p style=\"font-size:20px;\">%1</p>").arg(summary));
    html.append(QString("<p style=\"font-size:17px; color:#8c8c94;\">%1</p>").arg(detail));
    return html;
}

void RekordboxFeature::refreshLibraryModels() {
}

void RekordboxFeature::activate() {
    qDebug() << "RekordboxFeature::activate()";
    ++m_openToken; // a playlist that was about to open is no longer wanted

    // Let a worker thread do the XML parsing
    m_devicesFuture = QtConcurrent::run(findRekordboxDevices);
    m_devicesFutureWatcher.setFuture(m_devicesFuture);
    m_title = tr("(loading) Rekordbox");
    //calls a slot in the sidebar model such that 'Rekordbox (isLoading)' is displayed.
    emit featureIsLoading(this, true);

    emit enableCoverArtDisplay(true);
    emit switchToView("REKORDBOXHOME");
    emit disableSearch();
}

void RekordboxFeature::activateChild(const QModelIndex& index) {
    if (!index.isValid()) {
        return;
    }

    //access underlying TreeItem object
    TreeItem* item = static_cast<TreeItem*>(index.internalPointer());
    if (!(item && item->getData().isValid())) {
        return;
    }

    // TreeItem list data holds 2 values in a QList and have different meanings.
    // If the 2nd QList element IS_RECORDBOX_DEVICE, the 1st element is the
    // filesystem device path, and the parseDeviceDB concurrent thread to parse
    // the Rekcordbox database is initiated. If the 2nd element is
    // IS_NOT_RECORDBOX_DEVICE, the 1st element is the playlist path and it is
    // activated.
    QList<QVariant> data = item->getData().toList();
    QString playlist = data[0].toString();
    bool doParseDeviceDB = data[1].toString() == IS_RECORDBOX_DEVICE;
    if (playlist.isEmpty() && !doParseDeviceDB) {
        return; // the "Playlists" folder: nothing to show, the sidebar opens/closes it
    }

    qDebug() << "RekordboxFeature::activateChild " << item->getLabel()
             << " playlist: " << playlist << " doParseDeviceDB: " << doParseDeviceDB;

    const bool isDriveRow = data.size() >= 3 && data[2].toString() == IS_DRIVE_ROW;
    if (doParseDeviceDB) {
        startDeviceParse(item, true);
    } else if (!m_parsingDevicePath.isEmpty() && playlist == m_parsingDevicePath) {
        // Tapped while it is still being read (it was started in the background).
        m_showWhenParsed = playlist;
        showLoadingPage(item->getLabel(), tr("Reading drive"), true);
    } else if (isDriveRow) {
        // A drive that is already read: a tap only opens or closes it (the sidebar does that).
        // Its tracks are under "All Tracks"; loading them here made every tap on the drive slow.
        return;
    } else {
        openPlaylistDeferred(item->getLabel(), playlist);
    }
}

void RekordboxFeature::openPlaylistDeferred(const QString& label, const QString& playlist) {
    // Show the "Loading" page now and do the heavy part (building the track list: an SQL query,
    // the column setup, the delegates) a moment later, once that page has been painted. The tap
    // answers at once; only the list itself takes its time. A newer tap supersedes this one.
    // The dots are static: this all runs on the UI thread, where an animation can't move
    // anyway, and trying to made the stall worse.
    const quint64 token = ++m_openToken;
    showLoadingPage(label, tr("Loading tracks..."), false);
    QTimer::singleShot(60, this, [this, token, playlist]() {
        if (token != m_openToken) {
            return;
        }
        qDebug() << "Activate Rekordbox Playlist: " << playlist;
        m_pRekordboxPlaylistModel->setPlaylist(playlist);
        emit showTrackModel(m_pRekordboxPlaylistModel);
    });
}

namespace {

// A small USB stick, drawn from inline SVG so it needs no resource file. Must be called on the
// GUI thread (it makes a QPixmap).
QIcon usbDriveIcon() {
    static const QByteArray kSvg = QByteArrayLiteral(
            "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24'>"
            "<rect fill='#ffffff' x='7' y='1' width='10' height='8'/>"
            "<rect fill='#181818' x='9.2' y='3' width='1.8' height='3.2'/>"
            "<rect fill='#181818' x='13' y='3' width='1.8' height='3.2'/>"
            "<rect fill='#ffffff' x='5' y='9' width='14' height='14' rx='2.5'/>"
            "<rect fill='#181818' x='9' y='13' width='6' height='2' rx='1'/>"
            "</svg>");
    QSvgRenderer renderer(kSvg);
    QPixmap pixmap(96, 96);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    renderer.render(&painter);
    painter.end();
    return QIcon(pixmap);
}

} // namespace

void RekordboxFeature::onRekordboxDevicesFound() {
    const QList<TreeItem*> result = m_devicesFuture.result();
    auto foundDevices = std::vector<std::unique_ptr<TreeItem>>(result.cbegin(), result.cend());

    clearLastRightClickedIndex();

    TreeItem* root = m_pSidebarModel->getRootItem();
    QSqlDatabase database = m_pTrackCollection->database();

    if (foundDevices.size() == 0) {
        // No Rekordbox devices found
        if (root->childRows() == 0) {
            // Nothing was there before either: the (empty) tables are already fresh, so don't
            // drop and recreate them on the GUI thread every time Rekordbox is tapped.
            m_title = tr("Rekordbox");
            emit featureLoadingFinished(this);
            return;
        }
        ScopedTransaction transaction(database);

        dropTable(database, kRekordboxPlaylistTracksTable);
        dropTable(database, kRekordboxPlaylistsTable);
        dropTable(database, kRekordboxLibraryTable);

        // Create new temporary Rekordbox database tables
        createLibraryTable(database, kRekordboxLibraryTable);
        createPlaylistsTable(database, kRekordboxPlaylistsTable);
        createPlaylistTracksTable(database, kRekordboxPlaylistTracksTable);

        transaction.commit();

        if (root->childRows() > 0) {
            // Devices have since been unmounted
            m_pSidebarModel->removeRows(0, root->childRows());
            leaveViewOfRemovedDevices();
        }
    } else {
        bool removedAny = false;
        for (int deviceIndex = root->childRows() - 1; deviceIndex >= 0; deviceIndex--) {
            TreeItem* child = root->child(deviceIndex);
            bool removeChild = true;

            for (const auto& pDeviceFound : foundDevices) {
                if (pDeviceFound->getLabel() == child->getLabel()) {
                    removeChild = false;
                    break;
                }
            }

            if (removeChild) {
                // Device has since been unmounted, cleanup DB
                clearDeviceTables(database, child);

                m_pSidebarModel->removeRows(deviceIndex, 1);
                removedAny = true;
            }
        }
        if (removedAny) {
            leaveViewOfRemovedDevices();
        }

        std::vector<std::unique_ptr<TreeItem>> childrenToAdd;

        for (auto&& pDeviceFound : foundDevices) {
            bool addNewChild = true;
            for (int deviceIndex = 0; deviceIndex < root->childRows(); deviceIndex++) {
                TreeItem* child = root->child(deviceIndex);

                if (pDeviceFound->getLabel() == child->getLabel()) {
                    // This device already exists in the TreeModel, don't add or parse is again
                    addNewChild = false;
                }
            }

            if (addNewChild) {
                pDeviceFound->setIcon(usbDriveIcon());
                childrenToAdd.push_back(std::move(pDeviceFound));
            }
        }

        if (!childrenToAdd.empty()) {
            m_pSidebarModel->insertTreeItemRows(std::move(childrenToAdd), 0);
        }
    }

    // Start reading every new drive in the background right away, one at a time, so its
    // playlists are ready by the time it is tapped.
    for (int i = 0; i < root->childRows(); ++i) {
        const QList<QVariant> data = root->child(i)->getData().toList();
        if (data.size() >= 2 && data[1].toString() == IS_RECORDBOX_DEVICE) {
            const QString devicePath = data[0].toString();
            if (devicePath != m_parsingDevicePath && !m_parseQueue.contains(devicePath)) {
                m_parseQueue.append(devicePath);
            }
        }
    }
    startNextQueuedParse();

    // calls a slot in the sidebarmodel such that 'isLoading' is removed from the feature title.
    m_title = tr("Rekordbox");
    emit featureLoadingFinished(this);
}

void RekordboxFeature::leaveViewOfRemovedDevices() {
    // A drive went away (ejected, or pulled out). Its entries are gone from the sidebar and the
    // database, but if its track list is what is on screen, that list just stayed there until you
    // left the Rekordbox view and came back, and loading one of its tracks said "file not found".
    // The Library doesn't tell a feature when its view is replaced, so look at what is showing.
    if (!m_pLibraryWidget) {
        return;
    }
    const WTrackTableView* pTable = m_pLibraryWidget->getCurrentTrackTableView();
    const bool showingRekordbox = pTable &&
            pTable->model() == static_cast<const QAbstractItemModel*>(m_pRekordboxPlaylistModel.get());
    const bool showingLoadingPage =
            m_pLoadingView && m_pLibraryWidget->getActiveView() == m_pLoadingView.data();
    if (showingRekordbox || showingLoadingPage) {
        m_loadingTimer.stop();
        emit switchToView("REKORDBOXHOME");
    }
}

void RekordboxFeature::onTracksFound() {
    qDebug() << "onTracksFound";
    m_pSidebarModel->triggerRepaint();

    const QString finishedDevice = m_parsingDevicePath;
    m_parsingDevicePath.clear();

    QString devicePlaylist;
    bool ok = true;
    try {
        devicePlaylist = m_tracksFuture.result();
    } catch (const std::exception& e) {
        qWarning() << "Failed to load Rekordbox database:" << e.what();
        ok = false;
    }
    ok = ok && !devicePlaylist.isEmpty();

    if (!ok) {
        // Make the next tap on this drive try again instead of showing an empty list.
        if (TreeItem* pItem = findDeviceItem(finishedDevice)) {
            QList<QVariant> data = pItem->getData().toList();
            if (data.size() >= 2) {
                data[1] = QVariant(IS_RECORDBOX_DEVICE);
                pItem->setData(QVariant(data));
            }
        }
    }

    const bool wanted = !finishedDevice.isEmpty() && m_showWhenParsed == finishedDevice;
    if (wanted) {
        m_showWhenParsed.clear();
        m_loadingTimer.stop();
        // Only jump to the list if the loading page is still what the user is looking at.
        const bool stillWaiting = m_pLoadingView && m_pLoadingView->isVisible();
        if (ok) {
            if (stillWaiting) {
                // The drive is read and has opened in the sidebar: its tracks and playlists are
                // there to tap. (Don't load a list nobody asked for.)
                emit switchToView("REKORDBOXHOME");
            }
        } else if (stillWaiting) {
            showLoadingPage(tr("Couldn't read this drive"),
                    tr("Tap the drive to try again."),
                    false);
        }
    }
    startNextQueuedParse();
}
