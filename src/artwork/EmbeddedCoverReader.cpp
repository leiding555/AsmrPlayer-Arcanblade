#include "artwork/EmbeddedCoverReader.h"

#include <QByteArrayView>
#include <QFile>
#include <QList>
#include <QtEndian>

namespace asmr {
namespace {

constexpr qint64 Id3HeaderBytes = 10;
constexpr qint64 MaximumTagBytes = 32 * 1024 * 1024;
constexpr qsizetype MaximumPicturesPerType = 32;
constexpr int MaximumFrames = 4096;
constexpr int MaximumWaveChunks = 4096;

struct PictureCandidates {
    qint64 maxImageBytes;
    const std::shared_ptr<std::atomic_bool> &cancelled;
    qint64 remainingTagBytes = MaximumTagBytes;
    int remainingFrames = MaximumFrames;
    QList<QByteArray> front;
    QList<QByteArray> other;

    [[nodiscard]] bool stopped() const
    {
        return cancelled && cancelled->load(std::memory_order_relaxed);
    }
};

// ID3 packs four 7-bit groups into four bytes to avoid MPEG sync markers.
qint64 synchsafeSize(const char *bytes)
{
    const auto raw = qFromBigEndian<quint32>(bytes);
    if (raw & 0x80808080u) return -1;
    return ((raw & 0x7f000000u) >> 3) | ((raw & 0x007f0000u) >> 2)
        | ((raw & 0x00007f00u) >> 1) | (raw & 0x7fu);
}

void undoUnsynchronisation(QByteArray &bytes)
{
    auto *data = bytes.data();
    qsizetype write = 0;
    for (qsizetype read = 0; read < bytes.size(); ++read) {
        const char current = data[read];
        data[write++] = current;
        if (static_cast<quint8>(current) == 0xff
            && read + 1 < bytes.size() && data[read + 1] == '\0')
            ++read;
    }
    bytes.truncate(write);
}

// The APIC description is terminated in its own encoding. Start at its
// first byte when looking for a UTF-16 terminator to keep code units aligned.
qsizetype imageOffset(QByteArrayView frame, qsizetype description)
{
    const auto encoding = static_cast<quint8>(frame[0]);
    if (encoding == 0 || encoding == 3) {
        const auto terminator = frame.sliced(description).indexOf('\0');
        return terminator < 0 ? -1 : description + terminator + 1;
    }
    if (encoding == 1 || encoding == 2) {
        for (auto i = description; i + 1 < frame.size(); i += 2) {
            if (frame[i] == '\0' && frame[i + 1] == '\0') return i + 2;
        }
    }
    return -1;
}

void collectPicture(QByteArrayView frame, PictureCandidates &pictures)
{
    if (frame.size() < 5) return;
    const auto mimeLength = frame.sliced(1).indexOf('\0');
    if (mimeLength < 0 || mimeLength >= frame.size() - 2
        || frame.sliced(1, mimeLength) == "-->")
        return;
    const auto type = mimeLength + 2;
    const auto offset = imageOffset(frame, type + 1);
    if (offset < 0 || offset == frame.size() || frame.size() - offset > pictures.maxImageBytes)
        return;
    auto &target = frame[type] == 3 ? pictures.front : pictures.other;
    if (target.size() < MaximumPicturesPerType)
        target.append(QByteArray(frame.data() + offset, frame.size() - offset));
}

void readId3Pictures(QFile &file, qint64 start, qint64 end, PictureCandidates &pictures)
{
    if (pictures.stopped() || end - start < Id3HeaderBytes || !file.seek(start)) return;
    const auto header = file.read(Id3HeaderBytes);
    if (header.size() != Id3HeaderBytes || !header.startsWith("ID3")) return;
    const auto version = static_cast<quint8>(header[3]);
    const auto flags = static_cast<quint8>(header[5]);
    if ((version != 3 && version != 4) || static_cast<quint8>(header[4]) == 0xff
        || (flags & (version == 3 ? 0x1f : 0x0f)))
        return;

    const qint64 bodySize = synchsafeSize(header.constData() + 6);
    const qint64 footerSize = version == 4 && (flags & 0x10) ? Id3HeaderBytes : 0;
    if (bodySize <= 0 || bodySize > pictures.remainingTagBytes
        || bodySize > end - start - Id3HeaderBytes - footerSize)
        return;
    pictures.remainingTagBytes -= bodySize;
    auto body = file.read(bodySize);
    if (body.size() != bodySize || pictures.stopped()) return;
    // v2.3 applies unsynchronisation to the entire body, including headers.
    // v2.4 lengths address the encoded frames and decoding happens per frame.
    if (version == 3 && (flags & 0x80)) undoUnsynchronisation(body);

    qsizetype offset = 0;
    if (flags & 0x40) {
        if (body.size() < 4) return;
        const qint64 extended = version == 3
            ? qint64(qFromBigEndian<quint32>(body.constData())) + 4
            : synchsafeSize(body.constData());
        if (extended < (version == 3 ? 10 : 6) || extended > body.size()) return;
        offset = extended;
    }

    const QByteArrayView data(body);
    while (data.size() - offset >= Id3HeaderBytes
           && pictures.remainingFrames > 0 && !pictures.stopped()) {
        const auto frameHeader = data.sliced(offset, Id3HeaderBytes);
        if (frameHeader[0] == '\0') break; // Padding.
        --pictures.remainingFrames;
        offset += Id3HeaderBytes;
        const qint64 length = version == 3
            ? qint64(qFromBigEndian<quint32>(frameHeader.data() + 4))
            : synchsafeSize(frameHeader.data() + 4);
        if (length <= 0 || length > data.size() - offset) break;

        auto frame = data.sliced(offset, length);
        offset += length;
        if (frameHeader.first(4) != "APIC") continue;

        const auto formatFlags = static_cast<quint8>(frameHeader[9]);
        // Skip compressed, encrypted and unknown frame formats.
        if (formatFlags & ~(version == 3 ? 0x20 : 0x43)) continue;
        QByteArray restored;
        if (version == 4 && ((flags & 0x80) || (formatFlags & 0x02))) {
            restored = frame.toByteArray();
            undoUnsynchronisation(restored);
            frame = restored;
        }
        const qsizetype groupBytes = (formatFlags & (version == 3 ? 0x20 : 0x40)) ? 1 : 0;
        const qsizetype lengthBytes = version == 4 && (formatFlags & 0x01) ? 4 : 0;
        if (frame.size() <= groupBytes + lengthBytes) continue;
        if (lengthBytes
            && synchsafeSize(frame.data() + groupBytes) != frame.size() - groupBytes - lengthBytes)
            continue;
        collectPicture(frame.sliced(groupBytes + lengthBytes), pictures);
    }
}

void readWavePictures(QFile &file, PictureCandidates &pictures)
{
    if (!file.seek(0)) return;
    const auto header = file.read(12);
    if (header.size() != 12 || !header.startsWith("RIFF")
        || QByteArrayView(header).sliced(8, 4) != "WAVE")
        return;
    const qint64 end = qint64(qFromLittleEndian<quint32>(header.constData() + 4)) + 8;
    if (end > file.size()) return;

    qint64 offset = 12;
    for (int chunk = 0; chunk < MaximumWaveChunks && end - offset >= 8
         && pictures.remainingTagBytes > 0 && pictures.remainingFrames > 0 && !pictures.stopped(); ++chunk) {
        if (!file.seek(offset)) return;
        const auto chunkHeader = file.read(8);
        if (chunkHeader.size() != 8) return;
        const qint64 length = qFromLittleEndian<quint32>(chunkHeader.constData() + 4);
        const qint64 payload = offset + 8;
        if (length > end - payload) return;
        if (chunkHeader.startsWith("ID3 ") || chunkHeader.startsWith("id3 "))
            readId3Pictures(file, payload, payload + length, pictures);
        // RIFF chunks are word-aligned. Seek past data without reading audio.
        offset = payload + length + (length & 1);
    }
}

} // namespace

bool visitEmbeddedCoverImages(
    QFile &file, bool wave, qint64 maxImageBytes,
    const std::shared_ptr<std::atomic_bool> &cancelled,
    const std::function<bool(const QByteArray &)> &visitor)
{
    PictureCandidates pictures{maxImageBytes, cancelled};
    if (pictures.stopped() || maxImageBytes <= 0 || !file.isReadable() || file.isSequential() || !visitor)
        return false;
    if (wave) readWavePictures(file, pictures);
    else readId3Pictures(file, 0, file.size(), pictures);
    for (const auto *list : {&pictures.front, &pictures.other}) {
        for (const auto &image : *list) {
            if (pictures.stopped()) return false;
            if (visitor(image)) return true;
        }
    }
    return false;
}

} // namespace asmr
