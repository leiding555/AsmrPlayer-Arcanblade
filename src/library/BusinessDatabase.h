#pragma once
#include <QSqlDatabase>
#include <QString>
#include <QVector>
namespace asmr {
struct BusinessTableSchema { QString name; QString sql; };
// v5 adds translations; v6 references; v7 categories; v8 category artwork; v9 favorite tracks.
inline constexpr int businessSchemaVersion = 9;
// Older supported versions select their immutable DDL for upgrade validation.
[[nodiscard]] QVector<BusinessTableSchema> businessTableSchemas(int version = businessSchemaVersion);
// Worker-owned scoped connection. Upgrades from v4 onward are additive only.
class BusinessDatabase final {
public:
    explicit BusinessDatabase(QString path);
    ~BusinessDatabase();
    BusinessDatabase(const BusinessDatabase &) = delete;
    BusinessDatabase &operator=(const BusinessDatabase &) = delete;
    bool open(QString *error = nullptr);
    QSqlDatabase &database() { return m_database; }
private:
    QString m_path, m_connectionName;
    QSqlDatabase m_database;
};
} // namespace asmr
