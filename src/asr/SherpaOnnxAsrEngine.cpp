#include "SherpaOnnxAsrEngine.h"

#include "PunctuationProcessor.h"
#include "TextPostProcessor.h"
#include "../utils/PerfTracer.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTimer>

namespace
{
QString preferredPath(const QStringList &paths)
{
    QString bestPath;
    for (const QString &path : paths)
    {
        if (path.contains("no-tts/bin", Qt::CaseInsensitive))
        {
            return QFileInfo(path).absoluteFilePath();
        }
        if (bestPath.isEmpty())
        {
            bestPath = QFileInfo(path).absoluteFilePath();
        }
    }
    return bestPath;
}

QString resolveModelsBaseDir()
{
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        QDir::current().absoluteFilePath("models"),
        QDir(appDir).absoluteFilePath("models"),
        QDir(appDir).absoluteFilePath("../models"),
        QDir(appDir).absoluteFilePath("../../models"),
    };

    for (const QString &candidate : candidates)
    {
        if (QFileInfo::exists(candidate))
        {
            return QFileInfo(candidate).absoluteFilePath();
        }
    }
    return QFileInfo(candidates.first()).absoluteFilePath();
}
}

SherpaOnnxAsrEngine::SherpaOnnxAsrEngine(QObject *parent)
    : QObject(parent)
{
    qRegisterMetaType<AsrResult>("AsrResult");
    punctuationProcessor = new PunctuationProcessor(this);

    connect(punctuationProcessor, &PunctuationProcessor::punctuateError, this,
            [](const QString &errorMessage)
            {
                qDebug() << "[PUNC] fallback reason =" << errorMessage;
            });

    connect(punctuationProcessor, &PunctuationProcessor::punctuateFinished, this,
            [this](const QString &punctuatedText)
            {
                if (!waitingPunctuation)
                {
                    return;
                }

                waitingPunctuation = false;
                AsrResult result = pendingPunctuationResult;
                result.text = TextPostProcessor::normalize(punctuatedText);

                qDebug() << "[PUNC] punctuated text =" << result.text;

                if (result.text.trimmed().isEmpty())
                {
                    result.success = false;
                    result.errorMessage = "识别结果为空";
                    finishTask(result);
                    return;
                }

                result.success = true;
                finishTask(result);
            });
}

SherpaOnnxAsrEngine::~SherpaOnnxAsrEngine()
{
    if (activeTimeoutTimer)
    {
        activeTimeoutTimer->stop();
        activeTimeoutTimer->deleteLater();
        activeTimeoutTimer = nullptr;
    }

    if (activeProcess)
    {
        if (activeProcess->state() != QProcess::NotRunning)
        {
            activeProcess->kill();
            activeProcess->waitForFinished(2000);
        }
        activeProcess->deleteLater();
        activeProcess = nullptr;
    }
}

QString SherpaOnnxAsrEngine::findSherpaExecutable() const
{
    const QString appDir = QCoreApplication::applicationDirPath();
    QStringList roots = {
        QDir::current().absoluteFilePath("third_party"),
        QDir(appDir).absoluteFilePath("third_party"),
        QDir(appDir).absoluteFilePath("../third_party"),
        QDir(appDir).absoluteFilePath("../../third_party"),
    };

    QStringList matches;
    for (const QString &root : roots)
    {
        if (!QFileInfo::exists(root))
        {
            continue;
        }
        QDirIterator it(root, QStringList() << "sherpa-onnx-offline.exe", QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext())
        {
            const QString candidate = QFileInfo(it.next()).absoluteFilePath();
            if (candidate.contains("/bin/", Qt::CaseInsensitive) || candidate.contains("\\bin\\", Qt::CaseInsensitive))
            {
                matches.push_back(candidate);
            }
        }
    }
    return preferredPath(matches);
}

QString SherpaOnnxAsrEngine::findTokensPath() const
{
    const QString baseDir = resolveModelsBaseDir();
    const QString defaultPath = QDir(baseDir).filePath("sherpa-onnx/paraformer-zh/tokens.txt");
    if (QFileInfo::exists(defaultPath))
    {
        return QFileInfo(defaultPath).absoluteFilePath();
    }

    const QString sherpaModelsRoot = QDir(baseDir).filePath("sherpa-onnx");
    if (!QFileInfo::exists(sherpaModelsRoot))
    {
        return QString();
    }

    QDirIterator it(sherpaModelsRoot, QStringList() << "tokens.txt", QDir::Files, QDirIterator::Subdirectories);
    if (it.hasNext())
    {
        return QFileInfo(it.next()).absoluteFilePath();
    }
    return QString();
}

QString SherpaOnnxAsrEngine::findParaformerPath() const
{
    const QString baseDir = resolveModelsBaseDir();
    const QString defaultPath = QDir(baseDir).filePath("sherpa-onnx/paraformer-zh/model.int8.onnx");
    if (QFileInfo::exists(defaultPath))
    {
        return QFileInfo(defaultPath).absoluteFilePath();
    }

    const QString sherpaModelsRoot = QDir(baseDir).filePath("sherpa-onnx");
    if (!QFileInfo::exists(sherpaModelsRoot))
    {
        return QString();
    }

    QDirIterator it(sherpaModelsRoot, QStringList() << "model.int8.onnx", QDir::Files, QDirIterator::Subdirectories);
    if (it.hasNext())
    {
        return QFileInfo(it.next()).absoluteFilePath();
    }
    return QString();
}

bool SherpaOnnxAsrEngine::isAvailable(QString *reason) const
{
    const QString exePath = findSherpaExecutable();
    if (exePath.isEmpty())
    {
        if (reason)
        {
            *reason = "未找到 sherpa-onnx-offline.exe。";
        }
        return false;
    }

    const QString tokensPath = findTokensPath();
    if (tokensPath.isEmpty())
    {
        if (reason)
        {
            *reason = "未找到 tokens.txt。";
        }
        return false;
    }

    const QString modelPath = findParaformerPath();
    if (modelPath.isEmpty())
    {
        if (reason)
        {
            *reason = "未找到 model.int8.onnx。";
        }
        return false;
    }

    return true;
}

void SherpaOnnxAsrEngine::transcribeAsync(const QString &wavPath, const TaskConfig &config, const QString &traceId)
{
    Q_UNUSED(config);
    pendingQueue.enqueue({traceId, wavPath, config});
    startNextQueued();
}

void SherpaOnnxAsrEngine::startNextQueued()
{
    if (running || pendingQueue.isEmpty())
    {
        return;
    }

    running = true;
    const PendingTask task = pendingQueue.dequeue();
    startTask(task);
}

QString SherpaOnnxAsrEngine::extractSherpaText(const QString &stdoutText, QString *errorMessage) const
{
    const QStringList lines = stdoutText.split('\n');
    for (QString line : lines)
    {
        line = line.trimmed();
        if (!line.startsWith('{') || !line.contains("\"text\""))
        {
            continue;
        }

        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(line.toUtf8(), &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject())
        {
            continue;
        }

        const QString text = doc.object().value("text").toString().trimmed();
        if (text.isEmpty())
        {
            if (errorMessage)
            {
                *errorMessage = "sherpa-onnx 识别结果为空";
            }
            return QString();
        }
        return text;
    }

    if (errorMessage)
    {
        *errorMessage = "未找到 sherpa-onnx JSON 输出";
    }
    return QString();
}

void SherpaOnnxAsrEngine::startPunctuation(const AsrResult &baseResult, const QString &normalizedText)
{
    qDebug() << "[PUNC] text_len =" << normalizedText.size();

    waitingPunctuation = true;
    pendingPunctuationResult = baseResult;
    punctuationProcessor->punctuateAsync(normalizedText, baseResult.traceId);
}

void SherpaOnnxAsrEngine::startTask(const PendingTask &task)
{
    activeTask = task;
    activeTimedOut = false;
    activeCompleted = false;
    activeElapsedTimer.start();
    PerfTracer::markTrace("SHERPA", task.traceId, "sherpa_prepare_start",
                          "file=" + QFileInfo(task.wavPath).fileName());

    emit transcribeStarted(task.wavPath);

    AsrResult baseResult;
    baseResult.traceId = task.traceId;
    baseResult.wavPath = task.wavPath;
    baseResult.modelName = "sherpa-onnx-paraformer";

    if (!QFileInfo::exists(task.wavPath))
    {
        baseResult.errorMessage = "未找到音频片段文件：" + task.wavPath;
        finishTask(baseResult);
        return;
    }

    activeExePath = findSherpaExecutable();
    activeTokensPath = findTokensPath();
    activeParaformerPath = findParaformerPath();
    PerfTracer::markTrace("SHERPA", task.traceId, "sherpa_prepare_done");

    if (activeExePath.isEmpty())
    {
        baseResult.errorMessage = "未找到 sherpa-onnx-offline.exe。";
        finishTask(baseResult);
        return;
    }
    if (activeTokensPath.isEmpty())
    {
        baseResult.errorMessage = "未找到 tokens.txt。";
        finishTask(baseResult);
        return;
    }
    if (activeParaformerPath.isEmpty())
    {
        baseResult.errorMessage = "未找到 model.int8.onnx。";
        finishTask(baseResult);
        return;
    }

    QStringList args;
    args << QString("--tokens=%1").arg(activeTokensPath)
         << QString("--paraformer=%1").arg(activeParaformerPath)
         << task.wavPath;

    const QString sherpaBinDir = QFileInfo(activeExePath).absolutePath();
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("PATH", sherpaBinDir + ";" + env.value("PATH"));

    activeProcess = new QProcess(this);
    activeProcess->setProgram(activeExePath);
    activeProcess->setArguments(args);
    activeProcess->setWorkingDirectory(sherpaBinDir);
    activeProcess->setProcessEnvironment(env);

    activeTimeoutTimer = new QTimer(this);
    activeTimeoutTimer->setSingleShot(true);
    connect(activeTimeoutTimer, &QTimer::timeout, this, [this]()
            {
                if (!activeProcess || activeCompleted)
                {
                    return;
                }
                activeTimedOut = true;
                activeProcess->kill();
            });

    connect(activeProcess, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this](int exitCode, QProcess::ExitStatus exitStatus)
            {
                if (activeCompleted)
                {
                    return;
                }
                activeCompleted = true;
                if (activeTimeoutTimer)
                {
                    activeTimeoutTimer->stop();
                }

                AsrResult result;
                result.traceId = activeTask.traceId;
                result.wavPath = activeTask.wavPath;
                result.modelName = "sherpa-onnx-paraformer";
                result.elapsedMs = activeElapsedTimer.elapsed();
                PerfTracer::markTrace("SHERPA", activeTask.traceId, "sherpa_process_finished",
                                      QString("elapsed=%1 ms, exitCode=%2").arg(result.elapsedMs).arg(exitCode));
                PerfTracer::warnIfSlow("SHERPA", activeTask.traceId, "sherpa_process", result.elapsedMs, 3000,
                                       "sherpa 识别耗时较长，可能是模型加载/音频过长/CPU压力");

                const QString stdoutText = QString::fromUtf8(activeProcess->readAllStandardOutput());
                const QString stderrText = QString::fromUtf8(activeProcess->readAllStandardError());

                PerfTracer::markTrace("SHERPA", activeTask.traceId, "sherpa_output_read",
                                      QString("stdout=%1 bytes, stderr=%2 bytes")
                                          .arg(stdoutText.toUtf8().size())
                                          .arg(stderrText.toUtf8().size()));
                qDebug() << "当前输入音频可能为多声道，sherpa-onnx 默认使用第一个声道，后续可优化为 mono downmix。";

                if (activeTimedOut)
                {
                    result.errorMessage = "sherpa-onnx 识别超时。";
                    finishTask(result);
                    return;
                }

                if (exitStatus != QProcess::NormalExit || exitCode != 0)
                {
                    result.errorMessage = QString("sherpa-onnx 执行失败，exitCode=%1，stderr=%2")
                                              .arg(exitCode)
                                              .arg(stderrText.trimmed().isEmpty() ? "无" : stderrText.trimmed());
                    finishTask(result);
                    return;
                }

                QString parseError;
                const QString rawText = extractSherpaText(stdoutText, &parseError);
                PerfTracer::markTrace("SHERPA", activeTask.traceId, "parse_json_done",
                                      QString("json_line_found=%1, extracted_text_length=%2")
                                          .arg(rawText.isEmpty() ? "no" : "yes")
                                          .arg(rawText.size()));
                if (rawText.isEmpty())
                {
                    result.errorMessage = parseError;
                    finishTask(result);
                    return;
                }

                PerfTracer::markTrace("TEXT", activeTask.traceId, "normalize_start",
                                      QString("text_len_before=%1").arg(rawText.size()));
                const QString cleaned = TextPostProcessor::normalize(rawText);
                PerfTracer::markTrace("TEXT", activeTask.traceId, "normalize_done",
                                      QString("text_len_after=%1").arg(cleaned.size()));
                if (cleaned.isEmpty())
                {
                    result.errorMessage = "识别结果为空";
                    finishTask(result);
                    return;
                }

                startPunctuation(result, cleaned);
            });

    connect(activeProcess, &QProcess::errorOccurred, this,
            [this](QProcess::ProcessError error)
            {
                if (activeCompleted)
                {
                    return;
                }

                if (error == QProcess::FailedToStart)
                {
                    activeCompleted = true;
                    if (activeTimeoutTimer)
                    {
                        activeTimeoutTimer->stop();
                    }

                    AsrResult result;
                    result.traceId = activeTask.traceId;
                    result.wavPath = activeTask.wavPath;
                    result.modelName = "sherpa-onnx-paraformer";
                    result.elapsedMs = activeElapsedTimer.elapsed();
                    result.errorMessage = "无法启动 sherpa-onnx-offline.exe。";
                    finishTask(result);
                }
            });

    connect(activeProcess, &QProcess::started, this, [this]()
            {
                PerfTracer::markTrace("SHERPA", activeTask.traceId, "sherpa_process_started");
            });
    activeProcess->start();
    PerfTracer::markTrace("SHERPA", task.traceId, "sherpa_process_start");
    activeTimeoutTimer->start(kAsrTimeoutMs);
}

void SherpaOnnxAsrEngine::finishTask(const AsrResult &result)
{
    if (activeTimeoutTimer)
    {
        activeTimeoutTimer->stop();
        activeTimeoutTimer->deleteLater();
        activeTimeoutTimer = nullptr;
    }

    if (activeProcess)
    {
        activeProcess->deleteLater();
        activeProcess = nullptr;
    }

    if (result.success)
    {
        emit transcribeFinished(result);
    }
    else
    {
        emit transcribeError(result.wavPath, result.errorMessage);
        emit transcribeFinished(result);
    }

    running = false;
    waitingPunctuation = false;
    QTimer::singleShot(0, this, &SherpaOnnxAsrEngine::startNextQueued);
}
