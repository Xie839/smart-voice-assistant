#ifndef FUNASRENGINE_H
#define FUNASRENGINE_H

#include <QElapsedTimer>
#include <QObject>
#include <QQueue>
#include <QString>

#include "../TaskConfig.h"
#include "AsrResult.h"

class QProcess;
class QTimer;

class FunAsrEngine : public QObject
{
    Q_OBJECT

public:
    explicit FunAsrEngine(QObject *parent = nullptr);
    ~FunAsrEngine() override;

    bool isAvailable(QString *reason = nullptr) const;
    void transcribeAsync(const QString &wavPath, const TaskConfig &config);

signals:
    void transcribeStarted(const QString &wavPath);
    void transcribeFinished(const AsrResult &result);
    void transcribeError(const QString &wavPath, const QString &errorMessage);

private:
    struct PendingAsrTask
    {
        QString wavPath;
        TaskConfig config;
    };

    static constexpr int kAsrTimeoutMs = 120000;

    QString resolveExistingPath(const QString &relativePath) const;
    QString resolvePreferredPath(const QString &relativePath) const;
    QString findPythonExecutable() const;
    QString funAsrScriptPath() const;
    QString modelIdFromConfig(const TaskConfig &config) const;
    QString buildCacheDirPath() const;

    void startNextQueued();
    void startTask(const PendingAsrTask &task);
    void finishTask(const AsrResult &result);

private:
    QQueue<PendingAsrTask> pendingQueue;
    bool isTranscribing = false;

    PendingAsrTask activeTask;
    QString activeModelId;
    QElapsedTimer activeElapsedTimer;
    QProcess *activeProcess = nullptr;
    QTimer *activeTimeoutTimer = nullptr;
    bool activeTimedOut = false;
    bool activeCompleted = false;
};

#endif
