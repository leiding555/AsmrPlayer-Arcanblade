#pragma once

#include <QString>
#include <QtGlobal>

namespace asmr {

QString formatTime(qint64 milliseconds);

// Directory and favorite track lists show hours for recordings of an hour or more.
inline QString formatTrackDuration(qint64 milliseconds)
{
    const qint64 seconds = milliseconds / 1000;
    const QString tail = QStringLiteral("%1:%2").arg(seconds / 60 % 60)
        .arg(seconds % 60, 2, 10, QLatin1Char('0'));
    return seconds >= 3600 ? QStringLiteral("%1:%2").arg(seconds / 3600)
        .arg(tail, 5, QLatin1Char('0')) : tail;
}

} // namespace asmr
