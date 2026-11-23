#include "update/UpdateCheckService.h"

#include "update/ApplicationVersion.h"
#include "update/SemanticVersion.h"

#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QTimer>

#include <cmath>

namespace asmr {
namespace {
using State = UpdateCheckState;
using Result = UpdateCheckResult;
const QString Repository = QStringLiteral("/leiding555/AsmrPlayer-Arcanblade");
const QUrl ManifestEndpoint(QStringLiteral(
    "https://leiding555.github.io/AsmrPlayer-Arcanblade/updates/windows-x64.json"));
constexpr qint64 MaximumPackageBytes = 2LL * 1024 * 1024 * 1024;

QString windowsPackageName(const QString &version)
{
    return QStringLiteral("Arcanblade-") + version + QStringLiteral("-windows-x64.zip");
}

bool noUrlExtras(const QUrl &url)
{
    return url.isValid() && !url.isRelative() && url.userInfo().isEmpty() && !url.hasFragment();
}

bool loopback(const QUrl &url)
{
    if (!noUrlExtras(url) || url.scheme() != QStringLiteral("http")) return false;
    QHostAddress address;
    return url.host().compare(QStringLiteral("localhost"), Qt::CaseInsensitive) == 0
        || (address.setAddress(url.host()) && address.isLoopback());
}

bool allowedEndpoint(const QUrl &url)
{
    return noUrlExtras(url) && !url.hasQuery()
        && (url == ManifestEndpoint || (loopback(url) && !url.path().isEmpty()));
}

Result failure(const QString &message)
{
    Result result;
    result.message = message;
    return result;
}

QDateTime retryTime(QNetworkReply *reply)
{
    const auto now = QDateTime::currentDateTimeUtc();
    const auto raw = reply->rawHeader("Retry-After").trimmed();
    QDateTime retry;
    bool ok = false;
    const auto seconds = raw.toLongLong(&ok);
    if (ok && seconds >= 0 && seconds <= 7 * 24 * 60 * 60) retry = now.addSecs(seconds);
    if (!retry.isValid() && !raw.isEmpty()) retry = QDateTime::fromString(QString::fromLatin1(raw), Qt::RFC2822Date).toUTC();
    if (!retry.isValid() || retry < now || retry > now.addDays(7)) return {};
    return retry;
}
} // namespace

bool isAllowedReleasePageUrl(const QUrl &url)
{
    if (!noUrlExtras(url) || url.scheme() != QStringLiteral("https")
        || url.host() != QStringLiteral("github.com") || url.hasQuery()
        || (url.port() != -1 && url.port() != 443)) return false;
    const QString prefix = Repository + QStringLiteral("/releases/tag/");
    const auto path = url.path(QUrl::FullyDecoded);
    return path.startsWith(prefix)
        && SemanticVersion::parse(path.mid(prefix.size()), true).has_value();
}

bool isDownloadableUpdateRelease(const UpdateRelease &release)
{
    const auto version = SemanticVersion::parse(release.version);
    if (!version || !isAllowedReleasePageUrl(release.pageUrl)
        || release.packageSize <= 0 || release.packageSize > MaximumPackageBytes)
        return false;
    const auto tag = release.pageUrl.path(QUrl::FullyDecoded).section(QLatin1Char('/'), -1);
    const auto tagged = SemanticVersion::parse(tag, true);
    if (!tagged || tagged->toString() != release.version) return false;
    const auto name = windowsPackageName(release.version);
    const auto allowed = [&](const QUrl &url, const QString &file) {
        return noUrlExtras(url) && url.scheme() == QStringLiteral("https")
            && url.host() == QStringLiteral("github.com") && !url.hasQuery()
            && (url.port() == -1 || url.port() == 443)
            && url.path(QUrl::FullyDecoded) == Repository + QStringLiteral("/releases/download/")
                + tag + QLatin1Char('/') + file;
    };
    static const QRegularExpression digest(QStringLiteral(R"(\A[0-9a-fA-F]{64}\z)"));
    return allowed(release.packageUrl, name)
        && allowed(release.checksumUrl, name + QStringLiteral(".sha256"))
        && (release.packageSha256.isEmpty() || digest.match(release.packageSha256).hasMatch());
}

namespace {
struct ManifestRelease {
    SemanticVersion version;
    UpdateRelease release;
};

std::optional<ManifestRelease> parseManifestRelease(const QJsonValue &value, bool prerelease)
{
    if (!value.isObject()) return std::nullopt;
    const auto entry = value.toObject();
    const auto tag = entry.value(QStringLiteral("tag")).toString();
    const auto version = SemanticVersion::parse(tag, true);
    const auto size = entry.value(QStringLiteral("size")).toDouble();
    const auto hash = entry.value(QStringLiteral("sha256")).toString();
    const auto notes = entry.value(QStringLiteral("notes"));
    if (!version || version->isPrerelease() != prerelease || !notes.isString()
        || !(size >= 1 && size <= MaximumPackageBytes) || size != std::floor(size)
        || hash.isEmpty())
        return std::nullopt;

    UpdateRelease release;
    release.version = version->toString();
    release.title = QStringLiteral("Arcanblade ") + tag;
    release.notes = notes.toString();
    release.prerelease = prerelease;
    release.packageSize = static_cast<qint64>(size);
    release.packageSha256 = hash.toLower();
    const auto base = QStringLiteral("https://github.com") + Repository;
    const auto file = windowsPackageName(release.version);
    const auto downloadBase = base + QStringLiteral("/releases/download/") + tag + QLatin1Char('/');
    release.pageUrl = QUrl(base + QStringLiteral("/releases/tag/") + tag);
    release.packageUrl = QUrl(downloadBase + file);
    release.checksumUrl = QUrl(downloadBase + file + QStringLiteral(".sha256"));
    if (!isDownloadableUpdateRelease(release)) return std::nullopt;
    return ManifestRelease{*version, release};
}
} // namespace

UpdateCheckResult selectUpdateRelease(const QByteArray &json, const QString &currentVersion,
                                      bool includePrereleases)
{
    const auto current = SemanticVersion::parse(currentVersion);
    if (!current) return failure(QStringLiteral("当前程序版本无效，无法检查更新。"));
    const auto manifest = QJsonDocument::fromJson(json).object();
    if (manifest.value(QStringLiteral("schema")).toDouble() != 1)
        return failure(QStringLiteral("更新清单格式无效或版本不受支持，请查看 GitHub 发行页面。"));

    const auto stable = parseManifestRelease(manifest.value(QStringLiteral("stable")), false);
    const auto prereleaseEntry = manifest.value(QStringLiteral("prerelease"));
    const auto preview = parseManifestRelease(prereleaseEntry, true);
    if (!stable || (!prereleaseEntry.isNull() && !preview))
        return failure(QStringLiteral("更新清单中的发行信息无效，请查看 GitHub 发行页面。"));

    const auto &selected = includePrereleases && preview
        && SemanticVersion::compare(preview->version, stable->version) > 0 ? *preview : *stable;
    Result result;
    if (SemanticVersion::compare(selected.version, *current) <= 0) {
        result.state = State::UpToDate;
        result.message = QStringLiteral("当前已是所选更新通道的最新版本。");
    } else {
        result.state = State::UpdateAvailable;
        result.release = selected.release;
        result.message = QStringLiteral("发现新版本 %1。").arg(selected.release.version);
    }
    return result;
}

UpdateCheckService::UpdateCheckService(QObject *parent)
    : UpdateCheckService(UpdateCheckOptions{}, parent)
{
}

UpdateCheckService::UpdateCheckService(const UpdateCheckOptions &options, QObject *parent)
    : QObject(parent), m_options(options), m_manager(new QNetworkAccessManager(this)),
      m_timeout(new QTimer(this))
{
    if (m_options.endpoint.isEmpty()) m_options.endpoint = ManifestEndpoint;
    if (loopback(m_options.endpoint)) m_manager->setProxy(QNetworkProxy::NoProxy);
    qRegisterMetaType<UpdateCheckResult>();
    m_timeout->setSingleShot(true);
    connect(m_timeout, &QTimer::timeout, this, [this] {
        if (m_busy) complete(failure(QStringLiteral("检查更新超时，请检查网络或代理后重试。")));
    });
}

UpdateCheckService::~UpdateCheckService()
{
    m_timeout->stop();
    if (m_reply) {
        disconnect(m_reply, nullptr, this, nullptr);
        m_reply->abort();
    }
}

bool UpdateCheckService::start(const QString &currentVersion, bool includePrereleases)
{
    if (m_busy) return false;
    const auto generation = ++m_generation;
    m_busy = true;
    m_completing = false;
    m_result = {};
    m_response.clear();
    m_currentVersion = currentVersion;
    m_includePrereleases = includePrereleases;
    QPointer<UpdateCheckService> guard(this);
    emit busyChanged(true);
    if (!guard || !m_busy || m_completing || generation != m_generation) return true;
    QString error;
    if (!SemanticVersion::parse(currentVersion))
        error = QStringLiteral("当前程序版本无效，无法检查更新。");
    else if (!allowedEndpoint(m_options.endpoint) || m_options.timeoutMs <= 0
        || m_options.timeoutMs > 120000 || m_options.maximumResponseBytes <= 0
        || m_options.maximumResponseBytes > 16 * 1024 * 1024)
        error = QStringLiteral("更新检查配置无效，未发送网络请求。");
    if (!error.isEmpty()) {
        QTimer::singleShot(0, this, [this, generation, error] {
            if (m_busy && !m_completing && generation == m_generation) complete(failure(error));
        });
        return true;
    }
    m_timeout->start(m_options.timeoutMs);
    requestManifest(generation);
    return true;
}

void UpdateCheckService::cancel()
{
    if (m_busy && !m_completing) {
        Result result;
        result.state = State::Cancelled;
        result.message = QStringLiteral("已取消检查更新。");
        complete(result);
    }
}

void UpdateCheckService::requestManifest(quint64 generation)
{
    if (!m_busy || m_completing || generation != m_generation) return;
    QNetworkRequest request(m_options.endpoint);
    request.setRawHeader("Accept", "application/json");
    request.setRawHeader("User-Agent", QByteArray("Arcanblade/") + applicationBuildVersion().toLatin1());
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
    auto *reply = m_manager->get(request);
    m_reply = reply;
    reply->setReadBufferSize(m_options.maximumResponseBytes + 1);
    connect(reply, &QNetworkReply::metaDataChanged, this, [this, reply, generation] {
        if (!m_busy || m_completing || m_reply != reply || generation != m_generation) return;
        bool ok = false;
        const qint64 length = reply->header(QNetworkRequest::ContentLengthHeader).toLongLong(&ok);
        if (ok && length > m_options.maximumResponseBytes)
            complete(failure(QStringLiteral("更新清单过大，已停止检查更新。")));
    });
    connect(reply, &QIODevice::readyRead, this,
        [this, reply, generation] { readAvailable(reply, generation); });
    connect(reply, &QNetworkReply::finished, this,
        [this, reply, generation] { replyFinished(reply, generation); });
}

void UpdateCheckService::readAvailable(QNetworkReply *reply, quint64 generation)
{
    if (!m_busy || m_completing || m_reply != reply || generation != m_generation) return;
    const auto remaining = m_options.maximumResponseBytes - m_response.size();
    m_response += reply->read(remaining + 1);
    if (m_response.size() > m_options.maximumResponseBytes)
        complete(failure(QStringLiteral("更新清单过大，已停止检查更新。")));
}

void UpdateCheckService::replyFinished(QNetworkReply *reply, quint64 generation)
{
    if (!m_busy || m_completing || m_reply != reply || generation != m_generation) return;
    QPointer<UpdateCheckService> guard(this);
    readAvailable(reply, generation);
    if (!guard || !m_busy || m_completing || m_reply != reply || generation != m_generation) return;
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (status == 429) {
        auto result = failure(QStringLiteral("更新清单请求受到限流，请稍后再试。"));
        result.retryAfterUtc = retryTime(reply);
        if (!result.retryAfterUtc.isValid())
            result.retryAfterUtc = QDateTime::currentDateTimeUtc().addSecs(60);
        complete(result);
        return;
    }
    if (status >= 300 && status < 400) {
        complete(failure(QStringLiteral("更新地址发生重定向，请直接查看 GitHub 发行页面。")));
        return;
    }
    if (reply->error() != QNetworkReply::NoError || status != 200) {
        complete(failure(QStringLiteral("无法读取更新清单，请稍后重试或查看 GitHub 发行页面。")));
        return;
    }
    complete(selectUpdateRelease(m_response, m_currentVersion, m_includePrereleases));
}

void UpdateCheckService::complete(UpdateCheckResult result)
{
    if (!m_busy || m_completing) return;
    m_completing = true;
    m_timeout->stop();
    if (m_reply) {
        auto *reply = m_reply.data();
        m_reply.clear();
        disconnect(reply, nullptr, this, nullptr);
        if (!reply->isFinished()) reply->abort();
        reply->deleteLater();
    }
    m_response.clear();
    m_result = result;
    const auto generation = m_generation;
    QTimer::singleShot(0, this, [this, generation, result] {
        if (!m_busy || !m_completing || generation != m_generation) return;
        m_busy = false;
        m_completing = false;
        QPointer<UpdateCheckService> guard(this);
        emit finished(result);
        if (guard && !m_busy && generation == m_generation) emit busyChanged(false);
    });
}
} // namespace asmr
