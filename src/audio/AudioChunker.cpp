#include "AudioChunker.h"

#include "WavWriter.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QRegularExpression>

AudioChunker::AudioChunker()
{
}

void AudioChunker::startSession(const QString &newSessionId, const QAudioFormat &newFormat)
{
    sessionId = sanitizeFilePart(newSessionId).isEmpty()
                    ? QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss")
                    : sanitizeFilePart(newSessionId);
    format = newFormat;
    currentPcm16.clear();
    chunkIndex = 0;
    nextSequenceId = 0;
    sessionCursorMs = 0;

    QDir dir(QCoreApplication::applicationDirPath());
    if (!dir.exists("temp/chunks") && !dir.mkpath("temp/chunks"))
    {
        qDebug() << "Failed to create chunk directory:" << dir.filePath("temp/chunks");
    }
    else
    {
        qDebug() << "Chunk directory ready:" << dir.filePath("temp/chunks");
    }
}

void AudioChunker::appendAudio(const QByteArray &pcm16Data)
{
    currentPcm16.append(pcm16Data);
}

AudioChunkInfo AudioChunker::saveCurrentChunk(const QString &splitReason, qint64 startTimeMs, qint64 endTimeMs)
{
    AudioChunkInfo info;
    info.sessionId = sessionId;
    info.durationMs = currentDurationMs();
    info.splitReason = splitReason;

    if (!hasValidAudio())
    {
        clearCurrentChunk();
        return info;
    }

    ++chunkIndex;
    info.chunkIndex = chunkIndex;
    info.sequenceId = nextSequenceId++;
    info.wavPath = buildChunkFilePath();

    if (startTimeMs < 0 && endTimeMs < 0)
    {
        info.startTimeMs = sessionCursorMs;
        info.endTimeMs = sessionCursorMs + info.durationMs;
    }
    else if (startTimeMs >= 0 && endTimeMs < 0)
    {
        info.startTimeMs = startTimeMs;
        info.endTimeMs = startTimeMs + info.durationMs;
    }
    else if (startTimeMs < 0 && endTimeMs >= 0)
    {
        info.endTimeMs = endTimeMs;
        info.startTimeMs = qMax<qint64>(0, endTimeMs - info.durationMs);
    }
    else
    {
        info.startTimeMs = startTimeMs;
        info.endTimeMs = endTimeMs;
    }

    if (info.endTimeMs < info.startTimeMs)
    {
        info.endTimeMs = info.startTimeMs + info.durationMs;
    }

    if (!WavWriter::writePcm16ToWav(info.wavPath,
                                    currentPcm16,
                                    format.sampleRate(),
                                    format.channelCount()))
    {
        qDebug() << "Chunk save failed:" << info.wavPath;
        info.wavPath.clear();
    }
    else
    {
        qDebug() << "Chunk saved:" << info.wavPath
                 << "chunkIndex=" << info.chunkIndex
                 << "sequenceId=" << info.sequenceId
                 << "startTimeMs=" << info.startTimeMs
                 << "endTimeMs=" << info.endTimeMs
                 << "durationMs=" << info.durationMs
                 << "splitReason=" << info.splitReason;
        sessionCursorMs = qMax(sessionCursorMs, info.endTimeMs);
    }

    clearCurrentChunk();
    return info;
}

void AudioChunker::clearCurrentChunk()
{
    currentPcm16.clear();
}

bool AudioChunker::hasValidAudio() const
{
    return !currentPcm16.isEmpty() &&
           format.sampleRate() > 0 &&
           format.channelCount() > 0;
}

int AudioChunker::currentDurationMs() const
{
    const int bytesPerFrame = int(sizeof(qint16)) * format.channelCount();
    if (bytesPerFrame <= 0 || format.sampleRate() <= 0)
    {
        return 0;
    }

    const qint64 sampleFrames = currentPcm16.size() / bytesPerFrame;
    return int(sampleFrames * 1000 / format.sampleRate());
}

QString AudioChunker::buildChunkFilePath() const
{
    QDir dir(QCoreApplication::applicationDirPath());
    if (!dir.exists("temp/chunks"))
    {
        dir.mkpath("temp/chunks");
    }

    const QString timeText = QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss");
    return dir.filePath(QString("temp/chunks/%1_%2_chunk_%3.wav")
                            .arg(sessionId)
                            .arg(timeText)
                            .arg(chunkIndex, 4, 10, QLatin1Char('0')));
}

QString AudioChunker::sanitizeFilePart(const QString &text) const
{
    QString result = text.trimmed();
    result.replace(QRegularExpression("[^A-Za-z0-9_-]"), "_");
    return result;
}
