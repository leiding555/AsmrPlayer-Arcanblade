#pragma once

#include "subtitle/SubtitleEntry.h"

#include <QStringList>

namespace asmr {

struct SubtitleFileLoadResult;
[[nodiscard]] QStringList fasterWhisperRecoverySrtWarnings(const SubtitleFileLoadResult &loaded);
[[nodiscard]] QStringList fasterWhisperRecoveryJsonWarnings(const QString &jsonPath,
    const QList<SubtitleEntry> &entries);

} // namespace asmr
