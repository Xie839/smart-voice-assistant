#ifndef HISTORYRECORD_H
#define HISTORYRECORD_H

#include <QString>

struct HistoryRecord
{
    QString id;
    QString createdAt;
    QString sourceType;
    QString sourceFile;
    QString title;
    QString rawText;
    QString optimizedText;
    QString model;
    QString aiModel;
    qint64 durationMs = 0;
    QString note;
};

#endif
