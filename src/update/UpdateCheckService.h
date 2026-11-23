#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QObject>
#include <QPointer>
#include <QUrl>

QT_BEGIN_NAMESPACE
class QNetworkAccessManager;
class QNetworkReply;
class QTimer;
QT_END_NAMESPACE

namespace asmr {

struct UpdateRelease {
    QString version;
    QString title;
    QString notes;
    QUrl pageUrl;
    bool prerelease = false;
    // Empty transport metadata keeps older releases available via pageUrl.
    QUrl packageUrl;
    QUrl checksumUrl;
    qint64 packageSize = 0;
    QString packageSha256;
};

enum class UpdateCheckState { UpdateAvailable, UpToDate, Failed, Cancelled };

struct UpdateCheckResult {
    UpdateCheckState state = UpdateCheckState::Failed;
    UpdateRelease release;
    QString message;
    QDateTime retryAfterUtc;
};

struct UpdateCheckOptions {
    // Empty means the fixed GitHub Pages manifest. Other endpoints are
    // accepted only as HTTP loopback fixtures; there is no persisted URL setting.
    QUrl endpoint;
    int timeoutMs = 15000;
    qsizetype maximumResponseBytes = 2 * 1024 * 1024;
};

[[nodiscard]] bool isAllowedReleasePageUrl(const QUrl &url);
[[nodiscard]] bool isDownloadableUpdateRelease(const UpdateRelease &release);
[[nodiscard]] UpdateCheckResult selectUpdateRelease(
    const QByteArray &json, const QString &currentVersion, bool includePrereleases);

class UpdateCheckService final : public QObject {
    Q_OBJECT
public:
    explicit UpdateCheckService(QObject *parent = nullptr);
    explicit UpdateCheckService(const UpdateCheckOptions &options, QObject *parent = nullptr);
    ~UpdateCheckService() override;

    // A false return means an existing request (including queued completion) is
    // active. All other outcomes are delivered asynchronously via finished().
    bool start(const QString &currentVersion, bool includePrereleases);
    void cancel();
    [[nodiscard]] bool isBusy() const { return m_busy; }
    [[nodiscard]] UpdateCheckResult result() const { return m_result; }

signals:
    void finished(asmr::UpdateCheckResult result);
    void busyChanged(bool busy);

private:
    void requestManifest(quint64 generation);
    void readAvailable(QNetworkReply *reply, quint64 generation);
    void replyFinished(QNetworkReply *reply, quint64 generation);
    void complete(UpdateCheckResult result);

    UpdateCheckOptions m_options;
    QNetworkAccessManager *m_manager = nullptr;
    QTimer *m_timeout = nullptr;
    QPointer<QNetworkReply> m_reply;
    QByteArray m_response;
    QString m_currentVersion;
    UpdateCheckResult m_result;
    quint64 m_generation = 0;
    bool m_includePrereleases = false;
    bool m_busy = false;
    bool m_completing = false;
};
} // namespace asmr

Q_DECLARE_METATYPE(asmr::UpdateRelease)
Q_DECLARE_METATYPE(asmr::UpdateCheckState)
Q_DECLARE_METATYPE(asmr::UpdateCheckResult)
