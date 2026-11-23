#include "ui/FavoriteTracksView.h"
#include "ui/FavoriteIcon.h"
#include "ui/FavoriteRemovalConfirmation.h"
#include "ui/LibraryTrackTreeModel.h"
#include "ui/TimeFormat.h"

#include <QAbstractTableModel>
#include <QAction>
#include <QApplication>
#include <QBrush>
#include <QContextMenuEvent>
#include <QHeaderView>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>
#include <QSet>
#include <QStyledItemDelegate>
#include <QStyleHints>

#include <algorithm>

namespace asmr {

namespace {
constexpr int coverSize = 40;
constexpr int coverInset = 8;
constexpr int coverTextGap = 10;

Qt::Alignment favoriteTextAlignment(int column)
{
    return (column == FavoriteTracksView::DurationColumn ? Qt::AlignRight : Qt::AlignLeft)
        | Qt::AlignVCenter;
}

void drawFavoriteText(QPainter *painter, const QRect &cell, int column, const QString &text)
{
    const int left = column == FavoriteTracksView::TrackColumn ? coverInset + coverSize + coverTextGap
        : column == FavoriteTracksView::WorkColumn ? 16 : 4;
    const int right = column == FavoriteTracksView::DurationColumn ? 14 : 6;
    const QRect bounds = cell.adjusted(left, 0, -right, 0);
    if (bounds.isEmpty()) return;
    painter->drawText(bounds, int(favoriteTextAlignment(column)) | Qt::TextSingleLine,
        painter->fontMetrics().elidedText(text, Qt::ElideRight, bounds.width()));
}

class FavoriteTracksHeader final : public QHeaderView {
public:
    explicit FavoriteTracksHeader(QWidget *parent) : QHeaderView(Qt::Horizontal, parent) {}

    QSize sizeHint() const override
    {
        QSize size = QHeaderView::sizeHint();
        size.setHeight(std::max(32, fontMetrics().height() + 6));
        return size;
    }

protected:
    void paintSection(QPainter *painter, const QRect &rect, int column) const override
    {
        painter->save();
        painter->fillRect(rect, QColor(QStringLiteral("#1F232A")));
        painter->setPen(QColor(QStringLiteral("#30363D")));
        painter->drawLine(rect.bottomLeft(), rect.bottomRight());
        painter->setFont(font());
        painter->setPen(QColor(QStringLiteral("#A6AFBA")));
        drawFavoriteText(painter, rect, column, model()->headerData(column, Qt::Horizontal).toString());
        painter->restore();
    }
};

class FavoriteTrackDelegate final : public QStyledItemDelegate {
public:
    explicit FavoriteTrackDelegate(FavoriteTracksView *view) : QStyledItemDelegate(view), m_view(view) {}

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        QStyleOptionViewItem styled(option);
        initStyleOption(&styled, index);
        painter->save();
        painter->setClipRect(option.rect, Qt::IntersectClip);
        painter->setRenderHint(QPainter::Antialiasing);
        painter->fillRect(option.rect, QColor(QStringLiteral("#1F232A")));
        if (option.state & (QStyle::State_Selected | QStyle::State_MouseOver)) {
            // Each cell clips the same rounded row, so no seams appear between columns.
            const QRectF row(0, option.rect.y() + 1, m_view->viewport()->width(), option.rect.height() - 2);
            painter->setPen(Qt::NoPen);
            painter->setBrush(option.state.testFlag(QStyle::State_Selected)
                ? QColor(QStringLiteral("#293D39")) : QColor(QStringLiteral("#2B3239")));
            painter->drawRoundedRect(row, 7, 7);
        }
        if (index.column() == FavoriteTracksView::HeartColumn) {
            painter->translate(QRectF(option.rect).center());
            painter->scale(0.85, 0.85);
            painter->translate(-12, -12);
            painter->fillPath(favoriteHeartPath(), QColor(QStringLiteral("#FF696E")));
        } else {
            if (index.column() == FavoriteTracksView::TrackColumn) {
                const QRect cover(option.rect.x() + coverInset,
                    option.rect.y() + (option.rect.height() - coverSize) / 2, coverSize, coverSize);
                styled.icon.paint(painter, cover);
            }
            painter->setFont(styled.font);
            const auto group = option.state.testFlag(QStyle::State_Enabled) ? QPalette::Normal : QPalette::Disabled;
            painter->setPen(styled.palette.color(group, QPalette::Text));
            drawFavoriteText(painter, option.rect, index.column(), styled.text);
        }
        painter->restore();
    }
private:
    FavoriteTracksView *m_view;
};
}

class FavoriteTracksModel final : public QAbstractTableModel {
public:
    using QAbstractTableModel::QAbstractTableModel;
    int rowCount(const QModelIndex &parent = {}) const override
    {
        return parent.isValid() ? 0 : static_cast<int>(m_tracks.size());
    }
    int columnCount(const QModelIndex &parent = {}) const override
    {
        return parent.isValid() ? 0 : FavoriteTracksView::ColumnCount;
    }
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override
    {
        if (orientation == Qt::Horizontal && role == Qt::TextAlignmentRole)
            return int(favoriteTextAlignment(section));
        if (orientation == Qt::Horizontal && role == Qt::DisplayRole) {
            switch (section) {
            case FavoriteTracksView::TrackColumn: return QStringLiteral("音轨名");
            case FavoriteTracksView::WorkColumn: return QStringLiteral("所属作品");
            case FavoriteTracksView::DurationColumn: return QStringLiteral("时长");
            }
        }
        if (orientation == Qt::Horizontal && section == FavoriteTracksView::HeartColumn
            && role == Qt::AccessibleTextRole)
            return QStringLiteral("喜欢");
        return {};
    }
    QVariant data(const QModelIndex &index, int role) const override
    {
        if (!index.isValid() || index.row() < 0 || index.row() >= rowCount()) return {};
        const auto &track = m_tracks.at(index.row());
        switch (role) {
        case LibraryTrackTreeModel::FileIdRole: return track.file.id;
        case LibraryTrackTreeModel::WorkIdRole: return track.work.id;
        case LibraryTrackTreeModel::MissingRole: return track.file.missing;
        case Qt::TextAlignmentRole: return int(favoriteTextAlignment(index.column()));
        case Qt::DecorationRole:
            if (index.column() == FavoriteTracksView::TrackColumn && m_workCoverIcon)
                return m_workCoverIcon(track.work.id);
            return {};
        case Qt::ForegroundRole:
            if (track.file.missing) return QBrush(QColor(QStringLiteral("#838B96")));
            if (index.column() == FavoriteTracksView::WorkColumn) return QBrush(QColor(QStringLiteral("#AFB8C2")));
            return QBrush(QColor(QStringLiteral("#F0F2F5")));
        case Qt::ToolTipRole:
            if (index.column() == FavoriteTracksView::HeartColumn) return QStringLiteral("取消喜欢");
            return QStringLiteral("<qt>%1<br/>%2</qt>").arg(track.work.displayName.toHtmlEscaped(),
                track.file.relativePath.toHtmlEscaped());
        case Qt::DisplayRole:
        case Qt::AccessibleTextRole:
            switch (index.column()) {
            case FavoriteTracksView::TrackColumn:
                return track.file.missing ? QStringLiteral("%1（缺失）").arg(track.file.fileName)
                                          : track.file.fileName;
            case FavoriteTracksView::HeartColumn:
                if (role == Qt::AccessibleTextRole) return QStringLiteral("取消喜欢");
                return {};
            case FavoriteTracksView::WorkColumn: return track.work.displayName;
            case FavoriteTracksView::DurationColumn: {
                const auto found = m_durations.constFind(track.file.id);
                return found == m_durations.cend() ? QStringLiteral("--:--") : formatTrackDuration(*found);
            }
            }
        }
        return {};
    }
    int rowForFile(qint64 id) const { return m_rows.value(id, -1); }
    void setWorkCoverProvider(std::function<QIcon(qint64)> provider)
    {
        m_workCoverIcon = std::move(provider);
        if (rowCount() > 0)
            emit dataChanged(index(0, FavoriteTracksView::TrackColumn),
                             index(rowCount() - 1, FavoriteTracksView::TrackColumn), {Qt::DecorationRole});
    }
    void refreshWorkCover(qint64 id, int first, int end)
    {
        for (int row = first; row < end; ++row) {
            if (m_tracks.at(row).work.id == id) {
                const auto cell = index(row, FavoriteTracksView::TrackColumn);
                emit dataChanged(cell, cell, {Qt::DecorationRole});
            }
        }
    }
    bool setTracks(const QVector<LibraryFavoriteTrack> &tracks)
    {
        if (m_tracks == tracks) return false;
        beginResetModel();
        QHash<qint64, int> rows;
        QHash<qint64, qint64> durations;
        for (int row = 0; row < tracks.size(); ++row) {
            const auto &track = tracks.at(row);
            rows.insert(track.file.id, row);
            const int previous = rowForFile(track.file.id);
            if (previous >= 0 && m_tracks.at(previous).file == track.file
                && m_tracks.at(previous).work.rootPath == track.work.rootPath
                && m_durations.contains(track.file.id))
                durations.insert(track.file.id, m_durations.value(track.file.id));
        }
        m_tracks = tracks;
        m_rows.swap(rows);
        m_durations.swap(durations);
        endResetModel();
        return true;
    }
    void setDuration(qint64 workId, qint64 fileId, std::optional<qint64> milliseconds)
    {
        const int row = rowForFile(fileId);
        if (row < 0 || m_tracks.at(row).work.id != workId || m_tracks.at(row).file.missing) return;
        if (milliseconds && *milliseconds >= 0) {
            if (m_durations.value(fileId, -1) == *milliseconds) return;
            m_durations.insert(fileId, *milliseconds);
        } else if (!m_durations.remove(fileId)) return;
        const auto cell = index(row, FavoriteTracksView::DurationColumn);
        emit dataChanged(cell, cell, {Qt::DisplayRole, Qt::AccessibleTextRole});
    }
    void clearWorkDurations(qint64 workId)
    {
        for (const auto &track : m_tracks)
            if (track.work.id == workId) setDuration(workId, track.file.id, {});
    }
private:
    QVector<LibraryFavoriteTrack> m_tracks;
    QHash<qint64, int> m_rows;
    QHash<qint64, qint64> m_durations;
    std::function<QIcon(qint64)> m_workCoverIcon;
};

FavoriteTracksView::FavoriteTracksView(QWidget *parent)
    : QTableView(parent), m_tracks(new FavoriteTracksModel(this)), m_menu(new QMenu(this))
{
    setObjectName(QStringLiteral("libraryFavoritesView"));
    setAccessibleName(QStringLiteral("喜欢的音轨"));
    setModel(m_tracks);
    setFrameShape(QFrame::NoFrame);
    setShowGrid(false);
    setWordWrap(false);
    setMinimumSize(0, 0);
    setEditTriggers(QAbstractItemView::NoEditTriggers);
    setSelectionBehavior(QAbstractItemView::SelectRows);
    setSelectionMode(QAbstractItemView::SingleSelection);
    setTabKeyNavigation(false);
    verticalHeader()->hide();
    verticalHeader()->setDefaultSectionSize(56);
    setIconSize(QSize(coverSize, coverSize));
    setItemDelegate(new FavoriteTrackDelegate(this));
    setHorizontalHeader(new FavoriteTracksHeader(this));
    horizontalHeader()->setMinimumSectionSize(42);
    horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    horizontalHeader()->setSectionResizeMode(TrackColumn, QHeaderView::Stretch);
    horizontalHeader()->setSectionResizeMode(HeartColumn, QHeaderView::Fixed);
    horizontalHeader()->setSectionResizeMode(WorkColumn, QHeaderView::Fixed);
    horizontalHeader()->setSectionResizeMode(DurationColumn, QHeaderView::Fixed);
    setColumnWidth(HeartColumn, 48);
    setColumnWidth(DurationColumn, 80);
    m_menu->setObjectName(QStringLiteral("libraryFavoriteTrackMenu"));
    auto *remove = m_menu->addAction(QStringLiteral("取消喜欢"));
    remove->setObjectName(QStringLiteral("libraryRemoveFavoriteAction"));
    connect(remove, &QAction::triggered, this, [this] {
        confirmRemoval(std::exchange(m_menuFileId, 0));
    });
    connect(this, &QTableView::doubleClicked, this, [this](const QModelIndex &index) {
        if (index.column() != HeartColumn) play(index);
    });
}

void FavoriteTracksView::setTracks(const QVector<LibraryFavoriteTrack> &tracks)
{
    const auto current = currentIndex();
    const auto id = current.data(LibraryTrackTreeModel::FileIdRole).toLongLong();
    const int oldRow = current.row(), oldColumn = current.column();
    const int scroll = verticalScrollBar()->value();
    if (!m_tracks->setTracks(tracks)) return;
    m_menu->close();
    m_menuFileId = 0;
    if (id > 0 && !tracks.isEmpty()) {
        const int retained = m_tracks->rowForFile(id);
        setCurrentIndex(m_tracks->index(retained >= 0 ? retained : std::min(oldRow, m_tracks->rowCount() - 1),
                                        oldColumn));
    }
    verticalScrollBar()->setValue(scroll);
}

void FavoriteTracksView::setWorkCoverProvider(std::function<QIcon(qint64)> provider)
{
    m_tracks->setWorkCoverProvider(std::move(provider));
}

std::pair<int, int> FavoriteTracksView::visibleRows() const
{
    if (!isVisible() || viewport()->height() <= 0) return {0, 0};
    const int first = rowAt(0);
    if (first < 0) return {0, 0};
    const int last = rowAt(viewport()->height() - 1);
    return {first, last < 0 ? m_tracks->rowCount() : last + 1};
}

void FavoriteTracksView::refreshWorkCover(qint64 workId)
{
    const auto [first, end] = visibleRows();
    m_tracks->refreshWorkCover(workId, first, end);
}

QVector<qint64> FavoriteTracksView::visibleWorkIds() const
{
    QVector<qint64> ids;
    QSet<qint64> seen;
    const auto [first, end] = visibleRows();
    for (int row = first; row < end; ++row) {
        const auto id = m_tracks->index(row, TrackColumn).data(LibraryTrackTreeModel::WorkIdRole).toLongLong();
        if (!seen.contains(id)) {
            seen.insert(id);
            ids.append(id);
        }
    }
    return ids;
}

QVector<qint64> FavoriteTracksView::visibleAudioFileIds() const
{
    QVector<qint64> ids;
    const auto [first, end] = visibleRows();
    if (first == end) return ids;
    const int prefetchEnd = std::min(m_tracks->rowCount(), end + 1);
    for (int row = first; row < prefetchEnd; ++row) {
        const auto index = m_tracks->index(row, TrackColumn);
        if (!index.data(LibraryTrackTreeModel::MissingRole).toBool())
            ids.append(index.data(LibraryTrackTreeModel::FileIdRole).toLongLong());
    }
    return ids;
}

void FavoriteTracksView::setTrackDuration(qint64 workId, qint64 fileId, std::optional<qint64> milliseconds)
{
    m_tracks->setDuration(workId, fileId, milliseconds);
}

void FavoriteTracksView::clearWorkDurations(qint64 workId) { m_tracks->clearWorkDurations(workId); }

void FavoriteTracksView::resizeEvent(QResizeEvent *event)
{
    QTableView::resizeEvent(event);
    const int textColumnsWidth = viewport()->width() - columnWidth(HeartColumn) - columnWidth(DurationColumn);
    // The stretch section gives the track name the remaining two thirds.
    setColumnWidth(WorkColumn, std::max(horizontalHeader()->minimumSectionSize(), textColumnsWidth / 3));
}

void FavoriteTracksView::play(const QModelIndex &index)
{
    if (isVisible() && isEnabled() && index.isValid() && !index.data(LibraryTrackTreeModel::MissingRole).toBool())
        emit playRequested(index.data(LibraryTrackTreeModel::FileIdRole).toLongLong());
}

void FavoriteTracksView::confirmRemoval(qint64 fileId, std::optional<QPoint> heartPosition)
{
    const int row = m_tracks->rowForFile(fileId);
    if (row < 0 || m_confirmation || !isVisible() || !isEnabled()) return;
    const auto name = m_tracks->index(row, TrackColumn).data().toString();
    const auto work = m_tracks->index(row, WorkColumn).data().toString();
    auto *dialog = makeFavoriteRemovalConfirmation(this, work, name);
    m_confirmation = dialog;
    connect(m_tracks, &QAbstractItemModel::modelReset, dialog, [this, dialog, fileId] {
        if (m_tracks->rowForFile(fileId) < 0) dialog->reject();
    });
    connect(dialog, &QDialog::finished, this, [this, dialog, fileId, heartPosition] {
        if (m_confirmation != dialog) return;
        m_confirmation.clear();
        if (!dialog->clickedButton() || dialog->buttonRole(dialog->clickedButton()) != QMessageBox::DestructiveRole
            || !isVisible() || !isEnabled()
            || m_tracks->rowForFile(fileId) < 0) return;
        if (heartPosition) {
            m_confirmedClickPosition = *heartPosition;
            m_confirmedClickTime.start();
        }
        emit removalRequested(fileId);
    });
    dialog->open();
}

void FavoriteTracksView::mousePressEvent(QMouseEvent *event)
{
    m_pressedHeartFileId = 0;
    const QPoint pos = event->position().toPoint();
    const auto index = indexAt(pos);
    if (event->button() == Qt::LeftButton && index.column() == HeartColumn) {
        // Opening a modal confirmation can split a native double-click into
        // two single clicks. The second one may now land on a different file.
        const auto *hints = QApplication::styleHints();
        const QPoint delta = pos - m_confirmedClickPosition;
        const int distance = hints->mouseDoubleClickDistance();
        if (m_confirmedClickTime.isValid() && m_confirmedClickTime.elapsed() <= hints->mouseDoubleClickInterval()
            && qAbs(delta.x()) <= distance && qAbs(delta.y()) <= distance) {
            event->accept();
            return;
        }
        m_pressedHeartFileId = index.data(LibraryTrackTreeModel::FileIdRole).toLongLong();
        setCurrentIndex(index);
        event->accept();
        return;
    }
    QTableView::mousePressEvent(event);
}

void FavoriteTracksView::mouseReleaseEvent(QMouseEvent *event)
{
    const QPoint pos = event->position().toPoint();
    const auto index = indexAt(pos);
    const auto pressedId = std::exchange(m_pressedHeartFileId, 0);
    if (event->button() == Qt::LeftButton && (pressedId > 0 || index.column() == HeartColumn)) {
        if (pressedId > 0 && index.column() == HeartColumn
            && index.data(LibraryTrackTreeModel::FileIdRole).toLongLong() == pressedId)
            confirmRemoval(pressedId, pos);
        event->accept();
        return;
    }
    QTableView::mouseReleaseEvent(event);
}

void FavoriteTracksView::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && indexAt(event->position().toPoint()).column() == HeartColumn) {
        m_pressedHeartFileId = 0;
        event->accept();
        return;
    }
    QTableView::mouseDoubleClickEvent(event);
}

void FavoriteTracksView::openMenu(const QModelIndex &index, const QPoint &globalPosition)
{
    if (!isEnabled() || !isVisible() || !index.isValid()) return;
    setCurrentIndex(index);
    m_menuFileId = index.data(LibraryTrackTreeModel::FileIdRole).toLongLong();
    m_menu->popup(globalPosition);
}

void FavoriteTracksView::contextMenuEvent(QContextMenuEvent *event)
{
    if (event->reason() == QContextMenuEvent::Keyboard) {
        openMenu(currentIndex(), viewport()->mapToGlobal(visualRect(currentIndex()).center()));
    } else {
        openMenu(indexAt(viewport()->mapFromGlobal(event->globalPos())), event->globalPos());
    }
    event->accept();
}

void FavoriteTracksView::keyPressEvent(QKeyEvent *event)
{
    if ((event->key() == Qt::Key_Enter || event->key() == Qt::Key_Return)
        && (event->modifiers() == Qt::NoModifier || event->modifiers() == Qt::KeypadModifier)) {
        if (!event->isAutoRepeat()) play(currentIndex());
        return;
    }
    if ((event->key() == Qt::Key_Menu && event->modifiers() == Qt::NoModifier)
        || (event->key() == Qt::Key_F10 && event->modifiers() == Qt::ShiftModifier)) {
        if (!event->isAutoRepeat())
            openMenu(currentIndex(), viewport()->mapToGlobal(visualRect(currentIndex()).center()));
        return;
    }
    QTableView::keyPressEvent(event);
}

} // namespace asmr
