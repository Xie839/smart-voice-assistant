#include "FfmpegAudioConverter.h"

#include "../utils/PerfTracer.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QStringList>

FfmpegAudioConverter::FfmpegAudioConverter(QObject *parent)
    : QObject(parent)
{
}

FfmpegAudioConverter::~FfmpegAudioConverter()
{
    if (m_process)
    {
        if (m_process->state() != QProcess::NotRunning)
        {
            m_process->kill();
            m_process->waitForFinished(2000);
        }
        m_process->deleteLater();
        m_process = nullptr;
    }
}

QString FfmpegAudioConverter::findFfmpegExecutable() const
{
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        QDir::current().absoluteFilePath("tools/ffmpeg/bin/ffmpeg.exe"),
        QDir::current().absoluteFilePath("third_party/ffmpeg/bin/ffmpeg.exe"),
        QDir(appDir).absoluteFilePath("ffmpeg.exe"),
        QDir(appDir).absoluteFilePath("tools/ffmpeg/bin/ffmpeg.exe"),
        QDir(appDir).absoluteFilePath("../tools/ffmpeg/bin/ffmpeg.exe"),
        QDir(appDir).absoluteFilePath("../../tools/ffmpeg/bin/ffmpeg.exe"),
        QDir(appDir).absoluteFilePath("../third_party/ffmpeg/bin/ffmpeg.exe"),
        QDir(appDir).absoluteFilePath("../../third_party/ffmpeg/bin/ffmpeg.exe"),
    };

    for (const QString &candidate : candidates)
    {
        if (QFileInfo::exists(candidate))
        {
            return QFileInfo(candidate).absoluteFilePath();
        }
    }

    return QStandardPaths::findExecutable("ffmpeg");
}

bool FfmpegAudioConverter::isAvailable(QString *reason) const
{
    const QString ffmpegPath = findFfmpegExecutable();
    if (ffmpegPath.trimmed().isEmpty())
    {
        if (reason)
        {
            *reason = "未找到 ffmpeg.exe，请将其放在 tools/ffmpeg/bin/ 或 third_party/ffmpeg/bin/。";
        }
        return false;
    }
    return true;
}

bool FfmpegAudioConverter::isConverting() const
{
    return m_process && m_process->state() != QProcess::NotRunning;
}

QString FfmpegAudioConverter::shortErrorDetail(const QString &stderrText, const QString &stdoutText) const
{
    QString detail = stderrText.trimmed();
    if (detail.isEmpty())
    {
        detail = stdoutText.trimmed();
    }
    if (detail.isEmpty())
    {
        return "未知错误";
    }

    const QStringList lines = detail.split('\n', Qt::SkipEmptyParts);
    return lines.isEmpty() ? detail : lines.last().trimmed();
}

void FfmpegAudioConverter::cleanupProcess()
{
    if (!m_process)
    {
        return;
    }

    m_process->deleteLater();
    m_process = nullptr;
    m_activeTraceId.clear();
    m_startedAtMs = -1;
}

void FfmpegAudioConverter::convertToWavAsync(const QString &inputFilePath, const QString &outputWavPath, const QString &traceId)
{
    if (isConverting())
    {
        emit conversionFailed("文件转换正在进行，请稍候。");
        return;
    }

    if (!QFileInfo::exists(inputFilePath))
    {
        emit conversionFailed("待转换文件不存在，请重新选择。");
        return;
    }

    const QString ffmpegPath = findFfmpegExecutable();
    if (ffmpegPath.trimmed().isEmpty())
    {
        emit conversionFailed("未找到 ffmpeg.exe，请检查工具目录配置。");
        return;
    }

    const QFileInfo outInfo(outputWavPath);
    QDir outDir = outInfo.dir();
    if (!outDir.exists() && !outDir.mkpath("."))
    {
        emit conversionFailed("无法创建临时转换目录：" + outDir.absolutePath());
        return;
    }

    m_process = new QProcess(this);
    m_activeTraceId = traceId;
    m_startedAtMs = PerfTracer::nowMs();
    PerfTracer::markTrace("FFMPEG", m_activeTraceId, "ffmpeg_start",
                          "input=" + QFileInfo(inputFilePath).fileName());
    const QStringList args = {
        "-y",
        "-i",
        inputFilePath,
        "-vn",
        "-ac",
        "1",
        "-ar",
        "16000",
        "-sample_fmt",
        "s16",
        outputWavPath};

    m_process->setProgram(ffmpegPath);
    m_process->setArguments(args);
    m_process->setWorkingDirectory(QFileInfo(ffmpegPath).absolutePath());

    connect(m_process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this, outputWavPath](int exitCode, QProcess::ExitStatus exitStatus)
            {
                if (!m_process)
                {
                    return;
                }

                const QString stdoutText = QString::fromUtf8(m_process->readAllStandardOutput());
                const QString stderrText = QString::fromUtf8(m_process->readAllStandardError());

                const bool ok = (exitStatus == QProcess::NormalExit && exitCode == 0 && QFileInfo::exists(outputWavPath));
                const qint64 elapsedMs = m_startedAtMs >= 0 ? PerfTracer::nowMs() - m_startedAtMs : 0;
                if (ok)
                {
                    const double outputMb = QFileInfo(outputWavPath).size() / 1024.0 / 1024.0;
                    PerfTracer::markTrace("FFMPEG", m_activeTraceId, "ffmpeg_finished",
                                          QString("elapsed=%1 ms, output_wav_size=%2 MB")
                                              .arg(elapsedMs)
                                              .arg(outputMb, 0, 'f', 2));
                    emit conversionFinished(outputWavPath);
                }
                else
                {
                    PerfTracer::markTrace("FFMPEG", m_activeTraceId, "ffmpeg_failed",
                                          QString("elapsed=%1 ms, exitCode=%2").arg(elapsedMs).arg(exitCode));
                    emit conversionFailed("ffmpeg 转换失败：" + shortErrorDetail(stderrText, stdoutText));
                }

                cleanupProcess();
            });

    connect(m_process, &QProcess::errorOccurred, this,
            [this](QProcess::ProcessError error)
            {
                if (!m_process)
                {
                    return;
                }

                if (error == QProcess::FailedToStart)
                {
                    emit conversionFailed("无法启动 ffmpeg.exe，请检查文件和依赖库是否完整。");
                    cleanupProcess();
                }
            });

    connect(m_process, &QProcess::started, this, [this]()
            {
                PerfTracer::markTrace("FFMPEG", m_activeTraceId, "ffmpeg_started");
            });
    emit conversionStarted();
    m_process->start();
    PerfTracer::markTrace("FFMPEG", m_activeTraceId, "ffmpeg_process_start");
}
