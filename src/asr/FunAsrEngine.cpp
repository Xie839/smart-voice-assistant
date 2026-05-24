#include "FunAsrEngine.h"

#include "TextPostProcessor.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>
#include <QTimer>

FunAsrEngine::FunAsrEngine(QObject *parent)
    : QObject(parent)
{
    qRegisterMetaType<AsrResult>("AsrResult");
}

FunAsrEngine::~FunAsrEngine()
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

QString FunAsrEngine::resolveExistingPath(const QString &relativePath) const
{
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        QDir::current().absoluteFilePath(relativePath),
        QDir(appDir).absoluteFilePath(relativePath),
        QDir(appDir).absoluteFilePath("../" + relativePath),
        QDir(appDir).absoluteFilePath("../../" + relativePath)};

    for (const QString &candidate : candidates)
    {
        if (QFileInfo::exists(candidate))
        {
            return QFileInfo(candidate).absoluteFilePath();
        }
    }
    return QString();
}

QString FunAsrEngine::resolvePreferredPath(const QString &relativePath) const
{
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        QDir::current().absoluteFilePath(relativePath),
        QDir(appDir).absoluteFilePath(relativePath),
        QDir(appDir).absoluteFilePath("../" + relativePath),
        QDir(appDir).absoluteFilePath("../../" + relativePath)};
    return QFileInfo(candidates.first()).absoluteFilePath();
}

QString FunAsrEngine::findPythonExecutable() const
{
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        QDir::current().absoluteFilePath(".venv-funasr/Scripts/python.exe"),
        QDir(appDir).absoluteFilePath(".venv-funasr/Scripts/python.exe"),
        QDir(appDir).absoluteFilePath("../.venv-funasr/Scripts/python.exe"),
        QDir(appDir).absoluteFilePath("../../.venv-funasr/Scripts/python.exe"),
    };

    for (const QString &candidate : candidates)
    {
        if (QFileInfo::exists(candidate))
        {
            return QFileInfo(candidate).absoluteFilePath();
        }
    }

    QString fromPath = QStandardPaths::findExecutable("python");
    if (!fromPath.isEmpty())
    {
        return fromPath;
    }

    fromPath = QStandardPaths::findExecutable("python.exe");
    if (!fromPath.isEmpty())
    {
        return fromPath;
    }

    return QString();
}

QString FunAsrEngine::funAsrScriptPath() const
{
    return resolveExistingPath("tools/funasr_asr.py");
}

QString FunAsrEngine::modelIdFromConfig(const TaskConfig &config) const
{
    Q_UNUSED(config);
    // 第一版统一使用一个中文 Paraformer 模型，减少模型切换和首次加载开销。
    return "iic/speech_paraformer-large-vad-punc_asr_nat-zh-cn-16k-common-vocab8404-pytorch";
}

QString FunAsrEngine::buildCacheDirPath() const
{
    return resolvePreferredPath("models/funasr_cache");
}

bool FunAsrEngine::isAvailable(QString *reason) const
{
    const QString pythonPath = findPythonExecutable();
    if (pythonPath.isEmpty())
    {
        if (reason)
        {
            *reason = "未找到 Python 解释器（优先 .venv-funasr/Scripts/python.exe）。";
        }
        return false;
    }

    const QString scriptPath = funAsrScriptPath();
    if (scriptPath.isEmpty())
    {
        if (reason)
        {
            *reason = "未找到 tools/funasr_asr.py。";
        }
        return false;
    }

    QProcess probe;
    probe.setProgram(pythonPath);
    probe.setArguments({"-c", "import funasr"});
    probe.start();
    if (!probe.waitForStarted(3000))
    {
        if (reason)
        {
            *reason = "Python 启动失败，无法检测 FunASR。";
        }
        return false;
    }

    if (!probe.waitForFinished(10000))
    {
        probe.kill();
        if (reason)
        {
            *reason = "检测 FunASR 可用性超时。";
        }
        return false;
    }

    if (probe.exitCode() != 0)
    {
        const QString stderrText = QString::fromUtf8(probe.readAllStandardError()).trimmed();
        if (reason)
        {
            *reason = stderrText.isEmpty() ? "Python 无法 import funasr。" : stderrText;
        }
        return false;
    }

    return true;
}

void FunAsrEngine::transcribeAsync(const QString &wavPath, const TaskConfig &config)
{
    pendingQueue.enqueue({wavPath, config});
    startNextQueued();
}

void FunAsrEngine::startNextQueued()
{
    if (isTranscribing || pendingQueue.isEmpty())
    {
        return;
    }

    isTranscribing = true;
    const PendingAsrTask task = pendingQueue.dequeue();
    startTask(task);
}

void FunAsrEngine::startTask(const PendingAsrTask &task)
{
    activeTask = task;
    activeModelId = modelIdFromConfig(task.config);
    activeTimedOut = false;
    activeCompleted = false;
    activeElapsedTimer.start();

    emit transcribeStarted(task.wavPath);

    AsrResult baseResult;
    baseResult.wavPath = task.wavPath;
    baseResult.modelName = "funasr";

    if (!QFileInfo::exists(task.wavPath))
    {
        baseResult.errorMessage = "未找到音频片段文件：" + task.wavPath;
        finishTask(baseResult);
        return;
    }

    const QString pythonPath = findPythonExecutable();
    if (pythonPath.isEmpty())
    {
        baseResult.errorMessage = "未找到 Python 解释器（FunASR）。";
        finishTask(baseResult);
        return;
    }

    const QString scriptPath = funAsrScriptPath();
    if (scriptPath.isEmpty())
    {
        baseResult.errorMessage = "未找到 tools/funasr_asr.py。";
        finishTask(baseResult);
        return;
    }

    QDir().mkpath(buildCacheDirPath());

    QStringList args;
    args << scriptPath
         << "--wav" << task.wavPath
         << "--model" << activeModelId
         << "--device" << "cpu"
         << "--cache-dir" << buildCacheDirPath();

    activeProcess = new QProcess(this);
    activeProcess->setWorkingDirectory(QDir::currentPath());
    activeProcess->setProgram(pythonPath);
    activeProcess->setArguments(args);

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
                result.wavPath = activeTask.wavPath;
                result.modelName = "funasr";
                result.elapsedMs = activeElapsedTimer.elapsed();

                const QString stdoutText = QString::fromUtf8(activeProcess->readAllStandardOutput()).trimmed();
                const QString stderrText = QString::fromUtf8(activeProcess->readAllStandardError()).trimmed();

                if (activeTimedOut)
                {
                    result.errorMessage = "FunASR 识别超时。";
                    finishTask(result);
                    return;
                }

                if (exitStatus != QProcess::NormalExit || exitCode != 0)
                {
                    result.errorMessage = QString("FunASR 进程执行失败，exitCode=%1，stderr=%2")
                                              .arg(exitCode)
                                              .arg(stderrText.isEmpty() ? "无" : stderrText);
                    finishTask(result);
                    return;
                }

                if (stdoutText.isEmpty())
                {
                    result.errorMessage = "FunASR 输出为空。";
                    finishTask(result);
                    return;
                }

                QJsonParseError parseError;
                const QJsonDocument doc = QJsonDocument::fromJson(stdoutText.toUtf8(), &parseError);
                if (parseError.error != QJsonParseError::NoError || !doc.isObject())
                {
                    result.errorMessage = "FunASR 输出 JSON 解析失败：" + parseError.errorString();
                    finishTask(result);
                    return;
                }

                const QJsonObject obj = doc.object();
                const bool success = obj.value("success").toBool(false);
                const QString rawText = obj.value("text").toString().trimmed();
                const QString errorText = obj.value("error").toString().trimmed();

                if (!success)
                {
                    result.errorMessage = errorText.isEmpty() ? "FunASR 识别失败。" : errorText;
                    finishTask(result);
                    return;
                }

                const QString cleanedText = TextPostProcessor::normalize(rawText);
                if (cleanedText.isEmpty())
                {
                    result.errorMessage = "识别结果为空。";
                    finishTask(result);
                    return;
                }

                result.success = true;
                result.text = cleanedText;
                finishTask(result);
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
                    result.wavPath = activeTask.wavPath;
                    result.modelName = "funasr";
                    result.elapsedMs = activeElapsedTimer.elapsed();
                    result.errorMessage = "无法启动 FunASR Python 进程。";
                    finishTask(result);
                }
            });

    activeProcess->start();
    activeTimeoutTimer->start(kAsrTimeoutMs);
}

void FunAsrEngine::finishTask(const AsrResult &result)
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

    isTranscribing = false;
    QTimer::singleShot(0, this, &FunAsrEngine::startNextQueued);
}
