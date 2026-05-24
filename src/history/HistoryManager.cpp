#include "HistoryManager.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

namespace
{
QJsonObject toJson(const HistoryRecord &record)
{
    QJsonObject obj;
    obj.insert("id", record.id);
    obj.insert("created_at", record.createdAt);
    obj.insert("source_type", record.sourceType);
    obj.insert("source_file", record.sourceFile);
    obj.insert("title", record.title);
    obj.insert("raw_text", record.rawText);
    obj.insert("optimized_text", record.optimizedText);
    obj.insert("model", record.model);
    obj.insert("ai_model", record.aiModel);
    obj.insert("duration_ms", static_cast<qint64>(record.durationMs));
    obj.insert("note", record.note);
    return obj;
}

HistoryRecord fromJson(const QJsonObject &obj)
{
    HistoryRecord record;
    record.id = obj.value("id").toString().trimmed();
    record.createdAt = obj.value("created_at").toString().trimmed();
    record.sourceType = obj.value("source_type").toString().trimmed();
    record.sourceFile = obj.value("source_file").toString().trimmed();
    record.title = obj.value("title").toString().trimmed();
    record.rawText = obj.value("raw_text").toString();
    record.optimizedText = obj.value("optimized_text").toString();
    record.model = obj.value("model").toString().trimmed();
    record.aiModel = obj.value("ai_model").toString().trimmed();
    record.durationMs = obj.value("duration_ms").toInteger(0);
    record.note = obj.value("note").toString();
    return record;
}
} // namespace

HistoryManager::HistoryManager() = default;

QString HistoryManager::historyPath() const
{
    return QDir::current().filePath("data/history/history.json");
}

bool HistoryManager::ensureHistoryDir(QString *errorMessage) const
{
    QDir dir(QDir::currentPath());
    if (!dir.exists("data/history") && !dir.mkpath("data/history"))
    {
        if (errorMessage)
        {
            *errorMessage = "无法创建 data/history 目录";
        }
        return false;
    }
    return true;
}

bool HistoryManager::load(QString *errorMessage)
{
    if (!ensureHistoryDir(errorMessage))
    {
        return false;
    }

    QFile file(historyPath());
    if (!file.exists())
    {
        m_records.clear();
        return save(errorMessage);
    }

    if (!file.open(QIODevice::ReadOnly))
    {
        if (errorMessage)
        {
            *errorMessage = file.errorString();
        }
        return false;
    }

    const QByteArray bytes = file.readAll();
    file.close();

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isArray())
    {
        if (errorMessage)
        {
            *errorMessage = QString("history.json 解析失败：%1").arg(parseError.errorString());
        }
        return false;
    }

    QList<HistoryRecord> loaded;
    const QJsonArray array = doc.array();
    for (const QJsonValue &value : array)
    {
        if (!value.isObject())
        {
            continue;
        }
        HistoryRecord record = fromJson(value.toObject());
        if (record.id.isEmpty())
        {
            continue;
        }
        loaded.append(record);
    }

    m_records = loaded;
    return true;
}

bool HistoryManager::save(QString *errorMessage)
{
    if (!ensureHistoryDir(errorMessage))
    {
        return false;
    }

    QJsonArray array;
    for (const HistoryRecord &record : m_records)
    {
        array.append(toJson(record));
    }

    QSaveFile file(historyPath());
    if (!file.open(QIODevice::WriteOnly))
    {
        if (errorMessage)
        {
            *errorMessage = file.errorString();
        }
        return false;
    }

    const QByteArray content = QJsonDocument(array).toJson(QJsonDocument::Indented);
    if (file.write(content) != content.size())
    {
        if (errorMessage)
        {
            *errorMessage = "history.json 写入失败";
        }
        file.cancelWriting();
        return false;
    }

    if (!file.commit())
    {
        if (errorMessage)
        {
            *errorMessage = file.errorString();
        }
        return false;
    }
    return true;
}

QList<HistoryRecord> HistoryManager::records() const
{
    return m_records;
}

bool HistoryManager::addRecord(const HistoryRecord &record, QString *errorMessage)
{
    m_records.prepend(record);
    return save(errorMessage);
}

bool HistoryManager::deleteRecord(const QString &id, QString *errorMessage)
{
    for (int i = 0; i < m_records.size(); ++i)
    {
        if (m_records.at(i).id == id)
        {
            m_records.removeAt(i);
            return save(errorMessage);
        }
    }

    if (errorMessage)
    {
        *errorMessage = "未找到要删除的历史记录";
    }
    return false;
}

bool HistoryManager::clearAll(QString *errorMessage)
{
    m_records.clear();
    return save(errorMessage);
}
