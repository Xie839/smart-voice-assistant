#ifndef ASRBACKENDMANAGER_H
#define ASRBACKENDMANAGER_H

#include <QHash>
#include <QObject>
#include <QQueue>
#include <QString>
#include <QVector>

#include "../TaskConfig.h"
#include "../audio/AudioChunker.h"
#include "AsrResult.h"

class SherpaOnnxAsrEngine;

class AsrBackendManager : public QObject
{
    Q_OBJECT

public:
    explicit AsrBackendManager(QObject *parent = nullptr);
    ~AsrBackendManager() override;

    void transcribeAsync(const AudioChunkInfo &chunkInfo, const TaskConfig &config);
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
        qint64 sequenceId = -1;
        qint64 startTimeMs = -1;
        qint64 endTimeMs = -1;
    };

    struct WorkerContext
    {
        SherpaOnnxAsrEngine *sherpaEngine = nullptr;
        bool busy = false;
        PendingTask activeTask;
    };

    void setupWorkers();
    void tryStartNextTasks();
    int findFreeWorkerIndex() const;
    void startTaskOnWorker(int workerIndex, const PendingTask &task);
    void completeWorkerTask(int workerIndex, const AsrResult &rawResult);
    void flushOrderedResults();
    AsrResult withTaskMetadata(const AsrResult &rawResult, const PendingTask &task) const;

private:
    int maxConcurrentAsr = 2;
    int runningCount = 0;
    qint64 nextOutputSequenceId = 0;
    qint64 sequenceSeed = 0;
    bool outputCursorInitialized = false;

    QQueue<PendingTask> pendingQueue;
    QHash<qint64, AsrResult> completedResults;
    QVector<WorkerContext> workers;
};

#endif
