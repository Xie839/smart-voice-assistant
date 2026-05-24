#ifndef VADDETECTOR_H
#define VADDETECTOR_H

#include <QByteArray>

// VAD 状态机的对外状态。调用方根据这些状态决定是否开始缓存音频、
// 是否继续追加、以及是否保存当前句子的 chunk WAV。
enum class VadState
{
    Silence,
    SpeechStart,
    Speaking,
    Pause,
    SpeechEnd
};

struct VadConfig
{
    int sampleRate = 16000;
    int frameMs = 30;
    int noiseEstimateMs = 1000;
    int speechStartMs = 120;
    int speechEndSilenceMs = 1000;
    int minSpeechMs = 400;
    int maxSegmentMs = 15000;
    float thresholdRatio = 2.0f;
    float minThreshold = 0.005f;
};

// VadDetector 只负责“判断声音状态”，不关心麦克风设备和文件保存。
// 输入必须是单声道 PCM16 小帧；内部用 RMS 能量和动态噪声阈值判断
// Silence / SpeechStart / Speaking / Pause / SpeechEnd。
class VadDetector
{
public:
    explicit VadDetector(const VadConfig &config = VadConfig());

    void reset();
    // 处理一帧 PCM16 音频，返回当前 VAD 状态；调用方应按固定 frameMs 连续喂入。
    VadState processPcm16Frame(const QByteArray &pcmFrame);
    float currentLevel() const;
    float currentThreshold() const;
    bool isCalibratingNoise() const;

private:
    // 计算归一化 RMS 音量，返回范围大致为 0.0 ~ 1.0。
    float computeRmsLevel(const QByteArray &pcmFrame) const;

private:
    VadConfig config;
    VadState state = VadState::Silence;
    float level = 0.0f;
    float threshold = 0.01f;
    float noiseLevelSum = 0.0f;
    int noiseFrames = 0;
    int elapsedNoiseMs = 0;
    int aboveThresholdMs = 0;
    int silenceMs = 0;
    int speechMs = 0;
    bool calibrating = true;
};

#endif
