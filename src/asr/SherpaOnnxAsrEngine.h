#ifndef SHERPAONNXASRENGINE_H
#define SHERPAONNXASRENGINE_H

#include <QElapsedTimer>
#include <QObject>
#include <QQueue>
#include <QString>

#include "../TaskConfig.h"
#include "AsrResult.h"

class QProcess;
class QTimer;
class PunctuationProcessor;

class SherpaOnnxAsrEngine : public QObject
{
    Q_OBJECT

public:
    explicit SherpaOnnxAsrEngine(QObject *parent = nullptr);
    ~SherpaOnnxAsrEngine() override;

    bool isAvailable(QString *reason = nullptr) const;
    void transcribeAsync(const QString &wavPath, const TaskConfig &config);

signals:
    void transcribeStarted(const QString &wavPath);
    void transcribeFinished(const AsrResult &result);
    void transcribeError(const QString &wavPath, const QString &errorMessage);

private:
    struct PendingTask
    {
        QString wavPath;
        TaskConfig config;
    };

    static constexpr int kAsrTimeoutMs = 60000;

    QString findSherpaExecutable() const;
    QString findTokensPath() const;
    QString findParaformerPath() const;
    QString extractSherpaText(const QString &stdoutText, QString *errorMessage) const;
    void startPunctuation(const AsrResult &baseResult, const QString &normalizedText);

    void startNextQueued();
    void startTask(const PendingTask &task);
    void finishTask(const AsrResult &result);

private:
    QQueue<PendingTask> pendingQueue;
    bool running = false;

    PendingTask activeTask;
    QString activeExePath;
    QString activeTokensPath;
    QString activeParaformerPath;
    QElapsedTimer activeElapsedTimer;
    QProcess *activeProcess = nullptr;
    QTimer *activeTimeoutTimer = nullptr;
    bool activeTimedOut = false;
    bool activeCompleted = false;
    PunctuationProcessor *punctuationProcessor = nullptr;
    AsrResult pendingPunctuationResult;
    bool waitingPunctuation = false;
};

#endif
