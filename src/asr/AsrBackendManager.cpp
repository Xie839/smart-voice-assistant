#include "AsrBackendManager.h"

#include <QDebug>
#include <QMetaObject>

#include "SherpaOnnxAsrEngine.h"

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
    task.config = config;
    task.sequenceId = chunkInfo.sequenceId;
    task.startTimeMs = chunkInfo.startTimeMs;
    task.endTimeMs = chunkInfo.endTimeMs;

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
    tryStartNextTasks();
}

void AsrBackendManager::transcribeAsync(const QString &wavPath, const TaskConfig &config)
{
    AudioChunkInfo info;
    info.wavPath = wavPath;
    info.sequenceId = sequenceSeed++;
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
    worker.sherpaEngine->transcribeAsync(task.wavPath, task.config);
}

AsrResult AsrBackendManager::withTaskMetadata(const AsrResult &rawResult, const PendingTask &task) const
{
    AsrResult result = rawResult;
    if (result.wavPath.isEmpty())
    {
        result.wavPath = task.wavPath;
    }
    result.sequenceId = task.sequenceId;
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
