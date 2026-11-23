#pragma once

#include "library/LibraryRepository.h"

#include <QElapsedTimer>
#include <QIcon>
#include <QPointer>
#include <QTableView>
#include <functional>
#include <optional>
#include <utility>

class QContextMenuEvent;
class QKeyEvent;
class QMenu;
class QMessageBox;
class QMouseEvent;
class QResizeEvent;

namespace asmr {

class FavoriteTracksModel;

class FavoriteTracksView final : public QTableView {
    Q_OBJECT
public:
    enum Column { TrackColumn, HeartColumn, WorkColumn, DurationColumn, ColumnCount };

    explicit FavoriteTracksView(QWidget *parent);
    void setTracks(const QVector<LibraryFavoriteTrack> &tracks);
    void setWorkCoverProvider(std::function<QIcon(qint64)> provider);
    void refreshWorkCover(qint64 workId);
    QVector<qint64> visibleWorkIds() const;
    QVector<qint64> visibleAudioFileIds() const;
    void setTrackDuration(qint64 workId, qint64 fileId, std::optional<qint64> milliseconds);
    void clearWorkDurations(qint64 workId);

signals:
    void playRequested(qint64 fileId);
    void removalRequested(qint64 fileId);

protected:
    void resizeEvent(QResizeEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;

private:
    void openMenu(const QModelIndex &index, const QPoint &globalPosition);
    void confirmRemoval(qint64 fileId, std::optional<QPoint> heartPosition = {});
    void play(const QModelIndex &index);
    std::pair<int, int> visibleRows() const;

    FavoriteTracksModel *m_tracks;
    QMenu *m_menu;
    qint64 m_menuFileId = 0;
    qint64 m_pressedHeartFileId = 0;
    QPointer<QMessageBox> m_confirmation;
    QElapsedTimer m_confirmedClickTime;
    QPoint m_confirmedClickPosition;
};

} // namespace asmr
