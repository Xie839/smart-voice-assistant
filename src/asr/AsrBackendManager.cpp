#include "AsrBackendManager.h"

#include <QDebug>
#include <QFileInfo>
#include <QMetaObject>

#include "SherpaOnnxAsrEngine.h"
#include "../utils/PerfTracer.h"

AsrBackendManager::AsrBackendManager(QObject *parent)
    : QObject(parent)
{
    setupWorkers();
}

AsrBackendManager::~AsrBackendManager() = default;

void AsrBackendManager::setupWorkers()
{
    workers.resize(maxConcurrentAsr);

    for (int i = 0; i < workers.size(); ++i)
    {
        auto &worker = workers[i];
        worker.sherpaEngine = new SherpaOnnxAsrEngine(this);

        connect(worker.sherpaEngine, &SherpaOnnxAsrEngine::transcribeStarted, this,
                [this, i](const QString &wavPath)
                {
                    Q_UNUSED(wavPath);
                    emit transcribeStarted(workers[i].activeTask.wavPath);
                });

        connect(worker.sherpaEngine, &SherpaOnnxAsrEngine::transcribeFinished, this,
                [this, i](const AsrResult &result)
                {
                    completeWorkerTask(i, result);
                });

        connect(worker.sherpaEngine, &SherpaOnnxAsrEngine::transcribeError, this,
                [i](const QString &wavPath, const QString &errorMessage)
                {
                    Q_UNUSED(wavPath);
                    qWarning() << "[ASR] sherpa worker" << i << "failed:" << errorMessage;
                });
    }
}

void AsrBackendManager::transcribeAsync(const AudioChunkInfo &chunkInfo, const TaskConfig &config)
{
    PendingTask task;
    task.wavPath = chunkInfo.wavPath;
    task.traceId = chunkInfo.traceId.trimmed().isEmpty()
                       ? QString("chunk-%1").arg(chunkInfo.sequenceId, 5, 10, QLatin1Char('0'))
                       : chunkInfo.traceId;
    task.config = config;
    task.sequenceId = chunkInfo.sequenceId;
    task.startTimeMs = chunkInfo.startTimeMs;
    task.endTimeMs = chunkInfo.endTimeMs;
    task.enqueueTimeMs = PerfTracer::nowMs();

    if (task.sequenceId < 0)
    {
        task.sequenceId = sequenceSeed++;
    }
    else
    {
        sequenceSeed = qMax(sequenceSeed, task.sequenceId + 1);
    }

    if (runningCount == 0 && pendingQueue.isEmpty() && completedResults.isEmpty())
    {
        nextOutputSequenceId = task.sequenceId;
        outputCursorInitialized = true;
    }

    pendingQueue.enqueue(task);
    PerfTracer::markTrace("ASR", task.traceId, "asr_enqueue",
                          QString("queue_size=%1, running=%2/%3, file=%4")
                              .arg(pendingQueue.size())
                              .arg(runningCount)
                              .arg(maxConcurrentAsr)
                              .arg(QFileInfo(task.wavPath).fileName()));
    tryStartNextTasks();
}

void AsrBackendManager::transcribeAsync(const QString &wavPath, const TaskConfig &config)
{
    transcribeAsync(wavPath, config, QString("file-%1").arg(sequenceSeed, 5, 10, QLatin1Char('0')));
}

void AsrBackendManager::transcribeAsync(const QString &wavPath, const TaskConfig &config, const QString &traceId)
{
    AudioChunkInfo info;
    info.wavPath = wavPath;
    info.sequenceId = sequenceSeed++;
    info.traceId = traceId.trimmed().isEmpty()
                       ? QString("file-%1").arg(info.sequenceId, 5, 10, QLatin1Char('0'))
                       : traceId;
    transcribeAsync(info, config);
}

void AsrBackendManager::tryStartNextTasks()
{
    while (runningCount < maxConcurrentAsr && !pendingQueue.isEmpty())
    {
        const int workerIndex = findFreeWorkerIndex();
        if (workerIndex < 0)
        {
            return;
        }

        const PendingTask task = pendingQueue.dequeue();
        startTaskOnWorker(workerIndex, task);
    }
}

int AsrBackendManager::findFreeWorkerIndex() const
{
    for (int i = 0; i < workers.size(); ++i)
    {
        if (!workers[i].busy)
        {
            return i;
        }
    }
    return -1;
}

void AsrBackendManager::startTaskOnWorker(int workerIndex, const PendingTask &task)
{
    auto &worker = workers[workerIndex];
    worker.busy = true;
    worker.activeTask = task;
    ++runningCount;
    const qint64 queueWaitMs = task.enqueueTimeMs >= 0 ? PerfTracer::nowMs() - task.enqueueTimeMs : 0;
    PerfTracer::markTrace("ASR", task.traceId, "asr_start",
                          QString("queue_wait=%1 ms, worker=%2, running=%3/%4")
                              .arg(queueWaitMs)
                              .arg(workerIndex)
                              .arg(runningCount)
                              .arg(maxConcurrentAsr));
    PerfTracer::warnIfSlow("ASR", task.traceId, "queue_wait", queueWaitMs, 1000,
                           "ASR 队列等待过长，可能并发数不足或前序任务太慢");

    QString reason;
    if (!worker.sherpaEngine->isAvailable(&reason))
    {
        AsrResult unavailable;
        unavailable.success = false;
        unavailable.wavPath = task.wavPath;
        unavailable.errorMessage =
            "sherpa-onnx 识别后端不可用，请检查 sherpa-onnx-offline.exe、model.int8.onnx 和 tokens.txt 是否存在。";
        if (!reason.trimmed().isEmpty())
        {
            unavailable.errorMessage += " 详情：" + reason;
        }
        completeWorkerTask(workerIndex, unavailable);
        return;
    }

    qDebug() << "[ASR] worker" << workerIndex << "using sherpa-onnx";
    PerfTracer::markTrace("ASR", task.traceId, "asr_backend_type", "backend=exe");
    worker.sherpaEngine->transcribeAsync(task.wavPath, task.config, task.traceId);
}

AsrResult AsrBackendManager::withTaskMetadata(const AsrResult &rawResult, const PendingTask &task) const
{
    AsrResult result = rawResult;
    if (result.wavPath.isEmpty())
    {
        result.wavPath = task.wavPath;
    }
    result.sequenceId = task.sequenceId;
    result.traceId = task.traceId;
    result.startTimeMs = task.startTimeMs;
    result.endTimeMs = task.endTimeMs;
    return result;
}

void AsrBackendManager::completeWorkerTask(int workerIndex, const AsrResult &rawResult)
{
    auto &worker = workers[workerIndex];
    AsrResult result = withTaskMetadata(rawResult, worker.activeTask);

    if (!result.success && result.errorMessage.trimmed().isEmpty())
    {
        result.errorMessage = "识别失败";
    }

    completedResults.insert(result.sequenceId, result);

    worker.busy = false;
    worker.activeTask = PendingTask();
    runningCount = qMax(0, runningCount - 1);

    flushOrderedResults();
    QMetaObject::invokeMethod(this, [this]()
                              { tryStartNextTasks(); },
                              Qt::QueuedConnection);
}

void AsrBackendManager::flushOrderedResults()
{
    while (outputCursorInitialized && completedResults.contains(nextOutputSequenceId))
    {
        AsrResult result = completedResults.take(nextOutputSequenceId);

        if (!result.success)
        {
            emit transcribeError(result.wavPath, result.errorMessage);
        }
        emit transcribeFinished(result);

        ++nextOutputSequenceId;
    }
}
