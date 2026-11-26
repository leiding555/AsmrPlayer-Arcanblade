#pragma once

#include <QString>
#include <QStringView>

namespace asmr {

inline bool isSupportedAudioExtension(QStringView extension)
{
    return extension.compare(u"mp3", Qt::CaseInsensitive) == 0
        || extension.compare(u"wav", Qt::CaseInsensitive) == 0
        || extension.compare(u"flac", Qt::CaseInsensitive) == 0;
}

inline QString audioFileNameFilter()
{
    return QStringLiteral("音频文件 (*.mp3 *.wav *.flac)");
}

} // namespace asmr
