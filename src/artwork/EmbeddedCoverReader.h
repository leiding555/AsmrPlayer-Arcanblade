#pragma once

#include <QByteArray>

#include <atomic>
#include <functional>
#include <memory>

class QFile;

namespace asmr {

// Read ID3 APIC pictures from an open MP3 or RIFF/WAVE file. Try front covers
// first, keeping file order within each type. Stop once the visitor accepts one.
[[nodiscard]] bool visitEmbeddedCoverImages(
    QFile &file, bool wave, qint64 maxImageBytes,
    const std::shared_ptr<std::atomic_bool> &cancelled,
    const std::function<bool(const QByteArray &)> &visitor);

} // namespace asmr
