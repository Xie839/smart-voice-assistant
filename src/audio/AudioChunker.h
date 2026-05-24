#ifndef AUDIOCHUNKER_H
#define AUDIOCHUNKER_H

#include <QAudioFormat>
#include <QByteArray>
#include <QMetaType>
#include <QString>

// 一段 VAD 切分结果的元信息。后续 ASR 模块会使用 wavPath 进行识别，
// UI 只显示简短状态，不把路径写入“原始识别文本”框。
struct AudioChunkInfo
{
    QString sessionId;
    int chunkIndex = 0;
    qint64 sequenceId = -1;
    qint64 startTimeMs = -1;
    qint64 endTimeMs = -1;
    QString wavPath;
    int durationMs = 0;
    QString splitReason;
};

Q_DECLARE_METATYPE(AudioChunkInfo)

// AudioChunker 负责缓存“当前一句话”的单声道 PCM16 数据。
// 当 VAD 判断一句话结束或用户手动停止时，它将缓存写成 temp/chunks 下的 WAV。
class AudioChunker
{
public:
    AudioChunker();

    // 初始化一次录音会话，重置 chunk 序号，并确保 temp/chunks 目录存在。
    void startSession(const QString &sessionId, const QAudioFormat &format);
    // 追加 VAD 判定为有效语音或语音停顿的一帧 PCM16。
    void appendAudio(const QByteArray &pcm16Data);
    // 将当前缓存保存为 chunk WAV，并返回文件路径、时长和切分原因。
    AudioChunkInfo saveCurrentChunk(const QString &splitReason,
                                    qint64 startTimeMs = -1,
                                    qint64 endTimeMs = -1);
    void clearCurrentChunk();
    bool hasValidAudio() const;
    int currentDurationMs() const;

private:
    QString buildChunkFilePath() const;
    QString sanitizeFilePart(const QString &text) const;

private:
    QString sessionId;
    QAudioFormat format;
    QByteArray currentPcm16;
    int chunkIndex = 0;
    qint64 nextSequenceId = 0;
    qint64 sessionCursorMs = 0;
};

#endif
