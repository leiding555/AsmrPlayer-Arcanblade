#include "ui/MainWindow.h"

#include "ui/LibraryPanel.h"
#include "ui/PlaybackControlsWidget.h"

#include <QFutureWatcher>
#include <QPromise>

#include <algorithm>

namespace asmr {
namespace {
const LibraryFavoriteTrack *findFavorite(const QVector<LibraryFavoriteTrack> &tracks, qint64 fileId)
{
    const auto found = std::find_if(tracks.cbegin(), tracks.cend(),
        [fileId](const auto &track) { return track.file.id == fileId; });
    return found == tracks.cend() ? nullptr : &*found;
}
}

void MainWindow::connectFavoriteTracks()
{
    connect(m_playbackControls, &PlaybackControlsWidget::favoriteToggleRequested, this, [this] {
        writeTrackFavorite(m_playingLibraryWorkId, m_playingLibraryFileId,
                           !findFavorite(m_favoriteTracks, m_playingLibraryFileId));
    });
    connect(m_libraryPanel, &LibraryPanel::favoriteTrackPlayRequested, this, &MainWindow::playFavoriteTrack);
    connect(m_libraryPanel, &LibraryPanel::directoryTrackFavoriteRequested, this, &MainWindow::writeTrackFavorite);
    connect(m_libraryPanel, &LibraryPanel::favoriteTrackRemovalRequested, this, [this](qint64 fileId) {
        if (const auto *track = findFavorite(m_favoriteTracks, fileId))
            writeTrackFavorite(track->work.id, fileId, false);
    });
    updateFavoriteButton();
}

void MainWindow::updateFavoriteButton()
{
    const bool available = m_libraryDatabaseReady && m_playingLibraryFileId > 0 && m_playingLibraryWorkId > 0
        && !m_removedLibraryWorkIds.contains(m_playingLibraryWorkId)
        && !m_pendingFavoriteFiles.contains(m_playingLibraryFileId);
    m_playbackControls->setFavoriteState(available, findFavorite(m_favoriteTracks, m_playingLibraryFileId) != nullptr);
}

void MainWindow::applyFavoriteTracks(QVector<LibraryFavoriteTrack> tracks)
{
    tracks.removeIf([this](const auto &track) { return m_removedLibraryWorkIds.contains(track.work.id); });
    if (tracks != m_favoriteTracks) {
        m_favoriteTracks = std::move(tracks);
        m_libraryPanel->setFavoriteTracks(m_favoriteTracks);
    }
    updateFavoriteButton();
}

void MainWindow::writeTrackFavorite(qint64 workId, qint64 fileId, bool liked)
{
    if (m_closing || !m_libraryDatabaseReady || fileId <= 0 || workId <= 0
        || m_removedLibraryWorkIds.contains(workId) || m_pendingFavoriteFiles.contains(fileId)) return;
    m_pendingFavoriteFiles.insert(fileId);
    updateFavoriteButton();
    auto promise = std::make_shared<QPromise<LibraryFavoriteTracksResult>>();
    auto *watcher = new QFutureWatcher<LibraryFavoriteTracksResult>(this);
    connect(watcher, &QFutureWatcher<LibraryFavoriteTracksResult>::finished, this, [this, watcher, fileId] {
        const auto result = watcher->result();
        watcher->deleteLater();
        m_pendingFavoriteFiles.remove(fileId);
        if (m_closing) {
            finishCloseIfReady();
            return;
        }
        if (result.success) {
            applyFavoriteTracks(result.tracks);
        } else {
            updateFavoriteButton();
            QStringList errors;
            for (const auto &diagnostic : result.diagnostics) errors.append(diagnostic.message);
            m_libraryPanel->setLibraryError(QStringLiteral("更新喜欢列表失败：%1").arg(errors.join(QLatin1Char('\n'))));
        }
    });
    watcher->setFuture(promise->future());
    m_libraryBrowsePool.start([promise, path = m_libraryDatabasePath, workId, fileId, liked] {
        promise->start();
        LibraryFavoriteTracksResult result;
        try {
            LibraryRepository repository(path);
            auto operation = repository.open();
            if (operation.success) operation = repository.setTrackFavorite(workId, fileId, liked);
            if (operation.success) result = repository.favoriteTracks();
            else result.diagnostics = std::move(operation.diagnostics);
        } catch (const std::exception &error) {
            result.diagnostics.append({LibraryDiagnosticCode::DatabaseError, path, QString::fromUtf8(error.what())});
        }
        promise->addResult(std::move(result));
        promise->finish();
    });
}

void MainWindow::playFavoriteTrack(qint64 fileId)
{
    if (m_closing || !m_libraryDatabaseReady) return;
    QVector<PlaybackQueueEntry> entries;
    int selected = -1;
    for (const auto &track : m_favoriteTracks) {
        if (track.file.missing || m_removedLibraryWorkIds.contains(track.work.id)) continue;
        if (track.file.id == fileId) selected = static_cast<int>(entries.size());
        entries.append({0, track.work.id, track.file.id, track.work.rootPath,
                        track.file.relativePath, track.work.displayName, track.file.fileName, true, {}});
    }
    if (selected < 0) return;
    cancelQueueTransition();
    m_queuePreparationRequest = {};
    auto &request = m_queuePreparationRequest;
    request.generation = m_audioOpenGeneration;
    request.workId = entries.at(selected).workId;
    request.entry = entries.at(selected);
    request.replacement.emplace();
    request.replacement->replace(std::move(entries), selected);
    m_queueReplacementPending = true;
    m_queuePreparationPending = true;
    if (!m_queuePreparationInProgress) startQueuePreparation();
}

} // namespace asmr
