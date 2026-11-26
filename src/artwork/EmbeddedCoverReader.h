#pragma once

#include <QByteArray>
#include <QStringView>

#include <atomic>
#include <functional>
#include <memory>

class QFile;

namespace asmr {

enum class EmbeddedCoverFormat { Unsupported, Mp3, Wave, Flac };

[[nodiscard]] EmbeddedCoverFormat embeddedCoverFormat(QStringView extension);

// Read native FLAC PICTURE blocks or ID3 APIC pictures from MP3 and RIFF/WAVE.
// Try front covers first, keeping file order within each group.
[[nodiscard]] bool visitEmbeddedCoverImages(
    QFile &file, EmbeddedCoverFormat format, qint64 maxImageBytes,
    const std::shared_ptr<std::atomic_bool> &cancelled,
    const std::function<bool(const QByteArray &)> &visitor);

} // namespace asmr
