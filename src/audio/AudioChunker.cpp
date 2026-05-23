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
    // 每次点击“开始输入”都会建立一个新会话，chunkIndex 从 0 重新计数。
    sessionId = sanitizeFilePart(newSessionId).isEmpty()
                    ? QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss")
                    : sanitizeFilePart(newSessionId);
    format = newFormat;
    currentPcm16.clear();
    chunkIndex = 0;

    // 开始录音时立即创建 temp/chunks，便于确认 VAD 模块已经启动。
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
    // AudioRecorder 已经保证这里传入的是单声道 PCM16，因此可以直接拼接。
    currentPcm16.append(pcm16Data);
}

AudioChunkInfo AudioChunker::saveCurrentChunk(const QString &splitReason)
{
    // 保存时返回完整元信息，后续 whisper.cpp 可以直接使用 wavPath 做识别。
    AudioChunkInfo info;
    info.sessionId = sessionId;
    info.durationMs = currentDurationMs();
    info.splitReason = splitReason;

    if (!hasValidAudio())
    {
        // 没有有效音频时清空缓存即可，不生成空 WAV。
        clearCurrentChunk();
        return info;
    }

    ++chunkIndex;
    info.chunkIndex = chunkIndex;
    info.wavPath = buildChunkFilePath();

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
                 << "durationMs=" << info.durationMs
                 << "splitReason=" << info.splitReason;
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
    // chunk 是后续 ASR 的最小处理单元，固定保存到 temp/chunks。
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
