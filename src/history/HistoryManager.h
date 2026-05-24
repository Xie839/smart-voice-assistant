#ifndef HISTORYMANAGER_H
#define HISTORYMANAGER_H

#include <QList>
#include <QString>

#include "HistoryRecord.h"

class HistoryManager
{
public:
    HistoryManager();

    bool load(QString *errorMessage = nullptr);
    bool save(QString *errorMessage = nullptr);

    QList<HistoryRecord> records() const;

    bool addRecord(const HistoryRecord &record, QString *errorMessage = nullptr);
    bool deleteRecord(const QString &id, QString *errorMessage = nullptr);
    bool clearAll(QString *errorMessage = nullptr);

    QString historyPath() const;

private:
    bool ensureHistoryDir(QString *errorMessage = nullptr) const;

private:
    QList<HistoryRecord> m_records;
};

#endif
