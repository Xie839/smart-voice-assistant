#ifndef AUDIORECORDER_H
#define AUDIORECORDER_H

#include <QAudioFormat>
#include <QByteArray>
#include <QObject>
#include <QString>

#include "AudioChunker.h"
#include "VadDetector.h"

class QAudioSource;
class QIODevice;

// AudioRecorder 是录音链路的协调者：
// 1. 使用 Qt Multimedia 打开系统默认麦克风；
// 2. 根据设备实际 QAudioFormat 采集原始音频；
// 3. 将音频归一化为 VAD 需要的单声道 PCM16；
// 4. 驱动 VadDetector 判断语音端点；
// 5. 通过 AudioChunker 输出每一句话的 chunk WAV，同时保留完整录音用于调试。
class AudioRecorder : public QObject
{
    Q_OBJECT

public:
    explicit AudioRecorder(QObject *parent = nullptr);
    ~AudioRecorder() override;

    bool startRecording(const QString &sessionId);
    QString stopRecording();
    bool isRecording() const;

signals:
    void recordingStarted();
    void recordingStopped(const QString &wavPath);
    void recordingError(const QString &errorMessage);
    void recordingWarning(const QString &warningMessage);
    void audioLevelUpdated(float level);
    void vadStateChanged(const QString &stateText);
    void sentenceChunkReady(const AudioChunkInfo &info);

private:
    // 选择可用的录音格式。优先 16kHz/单声道/Int16，失败时回退到设备支持格式。
    QAudioFormat chooseRecordingFormat(QString *warningMessage) const;
    // 只转换采样格式，不改变声道数；完整录音保存时会保留设备原始声道布局。
    QByteArray convertToPcm16(const QByteArray &input, const QAudioFormat &format) const;
    // VAD 前的关键归一化：把任意支持的采样格式转为单声道 PCM16。
    QByteArray convertToMonoPcm16(const QByteArray &input, const QAudioFormat &format) const;
    QString buildOutputPath(const QString &sessionId) const;
    QString sanitizeFilePart(const QString &text) const;
    // QAudioSource readyRead 回调入口，负责读取一批麦克风数据并送入后续流程。
    void readAvailableAudio();
    void updateAudioLevel(const QByteArray &chunk);
    // 将单声道 PCM16 按 VadConfig::frameMs 切帧，再逐帧送入 VAD 状态机。
    void processPcm16Audio(const QByteArray &pcm16Data);
    void handleVadState(VadState state, const QByteArray &frame, qint64 frameStartMs, qint64 frameEndMs);
    void emitVadStatus(const QString &stateText);
    AudioChunkInfo saveCurrentChunk(const QString &splitReason, qint64 startTimeMs = -1, qint64 endTimeMs = -1);

private:
    QAudioSource *audioSource = nullptr;
    QIODevice *audioDevice = nullptr;
    QAudioFormat activeFormat;
    QAudioFormat pcm16Format;
    QByteArray pcmBuffer;
    QByteArray vadFrameBuffer;
    QByteArray preRollBuffer;
    QString outputPath;
    QString lastVadStatus;
    int preRollMs = 500;
    int preRollMaxBytes = 0;
    VadConfig vadConfig;
    VadDetector vadDetector;
    AudioChunker audioChunker;
    bool recording = false;
    qint64 sessionElapsedMs = 0;
    qint64 currentChunkStartMs = -1;
    qint64 currentChunkLastAudioMs = -1;
    bool chunkCollecting = false;
};

#endif
