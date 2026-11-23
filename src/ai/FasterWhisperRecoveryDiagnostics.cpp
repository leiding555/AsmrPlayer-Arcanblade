#include "ai/FasterWhisperRecoveryDiagnostics.h"
#include "subtitle/SrtParser.h"
#include "subtitle/SubtitleFileLoader.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>
#include <cmath>

namespace asmr {
namespace {
QString recoveredSrtDiagnostic(const SubtitleLoadDiagnostic &diagnostic)
{
    if (diagnostic.srtCode) {
        switch (*diagnostic.srtCode) {
        case SrtDiagnosticCode::MissingTimingLine: return QStringLiteral("编号后缺少时间行，该条无法显示。");
        case SrtDiagnosticCode::InvalidTimingLine: return QStringLiteral("时间行缺少有效的 --> 分隔符，该条无法显示。");
        case SrtDiagnosticCode::InvalidTimestamp: return QStringLiteral("时间戳格式无效，该条无法显示。");
        case SrtDiagnosticCode::EndBeforeStart: return QStringLiteral("结束时间早于开始时间，该条无法显示。");
        case SrtDiagnosticCode::EmptyText: return QStringLiteral("字幕正文为空，该条无法显示。");
        case SrtDiagnosticCode::ZeroDuration: return QStringLiteral("起止时间相同，该条时长为零，无法显示。");
        case SrtDiagnosticCode::MissingBlankLine: return QStringLiteral("字幕之间缺少空行，已按时间行识别分段。");
        case SrtDiagnosticCode::EmptyInput: return QStringLiteral("文件中没有字幕。");
        }
    }
    return diagnostic.message;
}
} // namespace

QStringList fasterWhisperRecoverySrtWarnings(const SubtitleFileLoadResult &loaded)
{
    constexpr qsizetype maxDetails = 20;
    QStringList warnings;
    for (const auto &diagnostic : loaded.diagnostics) {
        if (warnings.size() == maxDetails)
            break;
        warnings.append(QStringLiteral("SRT 第 %1 行：%2").arg(diagnostic.line).arg(recoveredSrtDiagnostic(diagnostic)));
    }
    if (loaded.diagnostics.size() > maxDetails)
        warnings.append(QStringLiteral("SRT 另有 %1 项解析提示。").arg(loaded.diagnostics.size() - maxDetails));
    return warnings;
}

QStringList fasterWhisperRecoveryJsonWarnings(const QString &jsonPath, const QList<SubtitleEntry> &entries)
{
    constexpr qint64 maxBytes = 32 * 1024 * 1024;
    const QFileInfo info(jsonPath);
    if (!info.exists())
        return {QStringLiteral("JSON 结果文件缺失。")};
    if (!info.isFile())
        return {QStringLiteral("JSON 结果路径不是普通文件。")};
    if (info.size() > maxBytes)
        return {QStringLiteral("JSON 结果超过 32 MiB 上限。")};

    QFile file(jsonPath);
    if (!file.open(QIODevice::ReadOnly) || file.isSequential())
        return {QStringLiteral("无法读取 JSON 结果文件。")};
    const QByteArray bytes = file.read(maxBytes + 1);
    if (bytes.size() > maxBytes)
        return {QStringLiteral("JSON 结果超过 32 MiB 上限。")};
    if (file.error() != QFileDevice::NoError || !file.atEnd())
        return {QStringLiteral("JSON 结果读取不完整。")};
    if (bytes.isEmpty())
        return {QStringLiteral("JSON 结果文件为空。")};
    if (!bytes.isValidUtf8())
        return {QStringLiteral("JSON 编码不是有效的 UTF-8。")};

    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError)
        return {QStringLiteral("JSON 格式无效（字节偏移 %1）。").arg(parseError.offset)};
    if (!document.isObject())
        return {QStringLiteral("JSON 格式无效，顶层应为对象。")};

    QStringList warnings;
    const auto root = document.object();
    if (root.value(QStringLiteral("language")).toString() != QStringLiteral("ja"))
        warnings.append(QStringLiteral("JSON 的 language 字段未标为日语（ja）。"));
    const auto segmentsValue = root.value(QStringLiteral("segments"));
    if (!segmentsValue.isArray()) {
        warnings.append(QStringLiteral("JSON 缺少有效的 segments 分段数组。"));
        return warnings;
    }

    const auto segments = segmentsValue.toArray();
    if (segments.size() != entries.size())
        warnings.append(QStringLiteral("分段数量不一致（SRT %1 条，JSON %2 条）。").arg(entries.size()).arg(segments.size()));
    const qsizetype commonSize = std::min(segments.size(), entries.size());
    for (qsizetype index = 0; index < commonSize; ++index) {
        const auto segment = segments.at(index).toObject();
        const auto &entry = entries.at(index);
        const double start = segment.value(QStringLiteral("start")).toDouble(-1);
        const double end = segment.value(QStringLiteral("end")).toDouble(-1);
        if (!std::isfinite(start) || !std::isfinite(end) || start < 0 || end <= start) {
            warnings.append(QStringLiteral("JSON 第 %1 段的起止时间缺失或区间无效。").arg(index + 1));
            break;
        }
        // The engine rounds timestamps to milliseconds when writing SRT.
        if (std::abs(start * 1000 - double(entry.startMs)) > 1.1
            || std::abs(end * 1000 - double(entry.endMs)) > 1.1) {
            warnings.append(QStringLiteral("JSON 第 %1 段的时间与 SRT 不一致（JSON %2–%3 秒，SRT %4–%5 毫秒）。")
                .arg(index + 1).arg(start, 0, 'g', 15).arg(end, 0, 'g', 15).arg(entry.startMs).arg(entry.endMs));
            break;
        }
        const auto text = segment.value(QStringLiteral("text"));
        if (!text.isString() || text.toString().simplified() != entry.text.simplified()) {
            warnings.append(QStringLiteral("JSON 第 %1 段的正文与 SRT 不一致。").arg(index + 1));
            break;
        }
    }
    return warnings;
}

} // namespace asmr
