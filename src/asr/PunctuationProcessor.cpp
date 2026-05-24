#include "PunctuationProcessor.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QTimer>

namespace
{
QString pickPreferredPath(const QStringList &paths)
{
    QString best;
    for (const QString &path : paths)
    {
        const QString normalized = path.toLower();
        if (normalized.contains("sherpa-onnx") && normalized.contains("/bin/"))
        {
            return QFileInfo(path).absoluteFilePath();
        }
        if (normalized.contains("sherpa-onnx") && normalized.contains("\\bin\\"))
        {
            return QFileInfo(path).absoluteFilePath();
        }
        if (best.isEmpty())
        {
            best = QFileInfo(path).absoluteFilePath();
        }
    }
    return best;
}

QString collapseSpaces(const QString &text)
{
    QString t = text;
    t.replace(QRegularExpression(R"([ \t]+)"), " ");
    return t.trimmed();
}
}

PunctuationProcessor::PunctuationProcessor(QObject *parent)
    : QObject(parent)
{
}

PunctuationProcessor::~PunctuationProcessor()
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

QString PunctuationProcessor::findProjectFile(const QString &relativePath) const
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

QString PunctuationProcessor::findPunctuationExe() const
{
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList roots = {
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

        QDirIterator it(root, QStringList() << "sherpa-onnx-offline-punctuation.exe", QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext())
        {
            matches.push_back(QFileInfo(it.next()).absoluteFilePath());
        }
    }
    return pickPreferredPath(matches);
}

QString PunctuationProcessor::findPunctuationModel() const
{
    const QString defaultRoot = findProjectFile("models/sherpa-onnx/punctuation");
    if (defaultRoot.isEmpty())
    {
        return QString();
    }

    const QStringList preferredNames = {
        "model.int8.onnx",
        "model.onnx",
        "cnn.onnx",
    };

    for (const QString &name : preferredNames)
    {
        QDirIterator it(defaultRoot, QStringList() << name, QDir::Files, QDirIterator::Subdirectories);
        if (it.hasNext())
        {
            return QFileInfo(it.next()).absoluteFilePath();
        }
    }

    QDirIterator anyOnnx(defaultRoot, QStringList() << "*.onnx", QDir::Files, QDirIterator::Subdirectories);
    if (anyOnnx.hasNext())
    {
        return QFileInfo(anyOnnx.next()).absoluteFilePath();
    }
    return QString();
}

bool PunctuationProcessor::isAvailable(QString *reason) const
{
    const QString exePath = findPunctuationExe();
    if (exePath.isEmpty())
    {
        if (reason)
        {
            *reason = "未找到 sherpa-onnx-offline-punctuation.exe。";
        }
        return false;
    }

    const QString modelPath = findPunctuationModel();
    if (modelPath.isEmpty())
    {
        if (reason)
        {
            *reason = "未找到标点模型（models/sherpa-onnx/punctuation）。";
        }
        return false;
    }

    return true;
}

void PunctuationProcessor::punctuateAsync(const QString &text)
{
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty())
    {
        emit punctuateFinished(QString());
        return;
    }

    pendingTexts.enqueue(trimmed);
    startNext();
}

void PunctuationProcessor::startNext()
{
    if (running || pendingTexts.isEmpty())
    {
        return;
    }

    running = true;
    activeText = pendingTexts.dequeue();
    activeCompleted = false;
    activeTimedOut = false;

    QString reason;
    if (!isAvailable(&reason))
    {
        finishCurrent(fallbackRulePunctuation(activeText), reason);
        return;
    }

    const QString exePath = findPunctuationExe();
    const QString modelPath = findPunctuationModel();
    const QString binDir = QFileInfo(exePath).absolutePath();

    QStringList args;
    args << QString("--ct-transformer=%1").arg(modelPath)
         << activeText;

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("PATH", binDir + ";" + env.value("PATH"));

    activeProcess = new QProcess(this);
    activeProcess->setProgram(exePath);
    activeProcess->setArguments(args);
    activeProcess->setWorkingDirectory(binDir);
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

                const QString stdoutText = QString::fromUtf8(activeProcess->readAllStandardOutput());
                const QString stderrText = QString::fromUtf8(activeProcess->readAllStandardError());

                QString parseError;
                QString punctuated = extractPunctuationText(stdoutText, &parseError);

                if (activeTimedOut)
                {
                    finishCurrent(fallbackRulePunctuation(activeText), "标点恢复超时");
                    return;
                }

                if (exitStatus != QProcess::NormalExit || exitCode != 0)
                {
                    const QString err = QString("标点恢复执行失败，exitCode=%1，stderr=%2")
                                            .arg(exitCode)
                                            .arg(stderrText.trimmed().isEmpty() ? "无" : stderrText.trimmed());
                    finishCurrent(fallbackRulePunctuation(activeText), err);
                    return;
                }

                punctuated = collapseSpaces(punctuated);
                if (punctuated.isEmpty())
                {
                    if (parseError.isEmpty())
                    {
                        parseError = "标点恢复输出为空";
                    }
                    finishCurrent(fallbackRulePunctuation(activeText), parseError);
                    return;
                }

                finishCurrent(punctuated);
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
                    finishCurrent(fallbackRulePunctuation(activeText), "无法启动标点恢复进程");
                }
            });

    activeProcess->start();
    activeTimeoutTimer->start(kPunctuationTimeoutMs);
}

QString PunctuationProcessor::extractPunctuationText(const QString &stdoutText, QString *errorMessage) const
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
        if (!text.isEmpty())
        {
            return text;
        }
    }

    QStringList candidates;
    const QStringList noisyWords = {
        "Elapsed", "Started", "Done", "Config", "Read", "Creating",
        "Options", "Usage", "provider", "num-threads", "print-args"};

    for (QString line : lines)
    {
        line = line.trimmed();
        if (line.isEmpty())
        {
            continue;
        }
        bool noisy = false;
        for (const QString &word : noisyWords)
        {
            if (line.contains(word, Qt::CaseInsensitive))
            {
                noisy = true;
                break;
            }
        }
        if (!noisy)
        {
            candidates.push_back(line);
        }
    }

    if (!candidates.isEmpty())
    {
        return candidates.last();
    }

    if (errorMessage)
    {
        *errorMessage = "未能从标点恢复输出中提取文本";
    }
    return QString();
}

QString PunctuationProcessor::fallbackRulePunctuation(const QString &text) const
{
    QString t = collapseSpaces(text);
    if (t.isEmpty())
    {
        return t;
    }

    // 在常见连接词前插入顿号，提升长句可读性。
    const QStringList connectors = {"但是", "然后", "所以", "因为", "如果", "另外", "同时", "不过", "而且", "其实"};
    for (const QString &word : connectors)
    {
        const QRegularExpression rx(QString("(?<![，。！？,.!?])%1").arg(QRegularExpression::escape(word)));
        t.replace(rx, "，" + word);
    }
    if (t.startsWith("，"))
    {
        t.remove(0, 1);
    }

    // 对较长句子粗粒度插入逗号，避免一整段无停顿文本。
    int countSinceComma = 0;
    QString punctuated;
    punctuated.reserve(t.size() + 8);
    for (int i = 0; i < t.size(); ++i)
    {
        const QChar c = t.at(i);
        punctuated.append(c);

        if (QString("，。！？,.!?").contains(c))
        {
            countSinceComma = 0;
            continue;
        }

        if (c.isLetterOrNumber() || (c.unicode() >= 0x4E00 && c.unicode() <= 0x9FFF))
        {
            ++countSinceComma;
        }

        if (countSinceComma >= 30 && i < t.size() - 1)
        {
            const QChar next = t.at(i + 1);
            if (!QString("，。！？,.!?").contains(next))
            {
                punctuated.append("，");
                countSinceComma = 0;
            }
        }
    }

    t = punctuated.trimmed();

    const QStringList questionWords = {"吗", "呢", "么", "为什么", "怎么", "如何", "有没有", "能不能", "是不是", "哪里", "什么", "谁"};
    const QChar last = t.isEmpty() ? QChar() : t.back();
    const bool hasEndPunc = QString("。！？!?").contains(last);
    if (!hasEndPunc)
    {
        bool looksLikeQuestion = false;
        for (const QString &w : questionWords)
        {
            if (t.contains(w))
            {
                looksLikeQuestion = true;
                break;
            }
        }
        t.append(looksLikeQuestion ? "？" : "。");
    }

    return t;
}

void PunctuationProcessor::finishCurrent(const QString &outputText, const QString &errorMessage)
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

    if (!errorMessage.isEmpty())
    {
        emit punctuateError(errorMessage);
    }
    emit punctuateFinished(outputText);

    running = false;
    activeText.clear();
    QTimer::singleShot(0, this, &PunctuationProcessor::startNext);
}
