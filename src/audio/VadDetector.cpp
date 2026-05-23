#include "VadDetector.h"

#include <QDebug>
#include <QtMath>

VadDetector::VadDetector(const VadConfig &config)
    : config(config)
{
    reset();
}

void VadDetector::reset()
{
    state = VadState::Silence;
    level = 0.0f;
    threshold = config.minThreshold;
    noiseLevelSum = 0.0f;
    noiseFrames = 0;
    elapsedNoiseMs = 0;
    aboveThresholdMs = 0;
    silenceMs = 0;
    speechMs = 0;
    calibrating = true;
}

VadState VadDetector::processPcm16Frame(const QByteArray &pcmFrame)
{
    // 每次只处理固定长度的小帧，RMS 越高代表当前帧语音能量越强。
    level = computeRmsLevel(pcmFrame);

    if (calibrating)
    {
        // 开始录音时先用一段环境音估计底噪，不立即判定说话。
        // 这样在风扇声、键盘声等不同环境中，阈值可以自动适配。
        noiseLevelSum += level;
        ++noiseFrames;
        elapsedNoiseMs += config.frameMs;

        if (elapsedNoiseMs >= config.noiseEstimateMs)
        {
            const float averageNoise = noiseFrames > 0 ? noiseLevelSum / float(noiseFrames) : 0.0f;
            // 动态阈值取“底噪 * ratio”和最小阈值的较大值：
            // ratio 用来过滤环境噪声，minThreshold 避免极安静环境下阈值过低。
            threshold = qMax(averageNoise * config.thresholdRatio, config.minThreshold);
            calibrating = false;
            qDebug() << "VAD noise estimate completed:"
                     << "noiseLevel=" << averageNoise
                     << "threshold=" << threshold;
        }

        return VadState::Silence;
    }

    const bool voiceLike = level >= threshold;

    if (state == VadState::Silence)
    {
        // 静音状态下，需要连续 speechStartMs 高于阈值才认为真正开始说话，
        // 避免单个尖峰噪声误触发 SpeechStart。
        aboveThresholdMs = voiceLike ? aboveThresholdMs + config.frameMs : 0;
        silenceMs = 0;
        speechMs = 0;

        if (aboveThresholdMs >= config.speechStartMs)
        {
            state = VadState::Speaking;
            speechMs = aboveThresholdMs;
            silenceMs = 0;
            return VadState::SpeechStart;
        }

        return VadState::Silence;
    }

    if (state == VadState::Speaking)
    {
        speechMs += config.frameMs;

        if (speechMs >= config.maxSegmentMs)
        {
            // maxSegmentMs 是兜底切分：用户长时间不停顿时也要产出片段，避免 ASR 延迟过长。
            state = VadState::Silence;
            aboveThresholdMs = 0;
            silenceMs = 0;
            speechMs = 0;
            return VadState::SpeechEnd;
        }

        if (!voiceLike)
        {
            // 低于阈值不马上结束，因为一句话中间可能有自然停顿。
            state = VadState::Pause;
            silenceMs = config.frameMs;
            return VadState::Pause;
        }

        return VadState::Speaking;
    }

    if (state == VadState::Pause)
    {
        speechMs += config.frameMs;

        if (voiceLike)
        {
            // 短暂停顿后又检测到语音，说明仍属于同一句话。
            state = VadState::Speaking;
            silenceMs = 0;
            return VadState::Speaking;
        }

        silenceMs += config.frameMs;
        if (silenceMs >= config.speechEndSilenceMs || speechMs >= config.maxSegmentMs)
        {
            // 停顿超过 speechEndSilenceMs 才结束一句话，避免把短暂停顿切得太碎。
            const int completedSpeechMs = speechMs - silenceMs;
            state = VadState::Silence;
            aboveThresholdMs = 0;
            silenceMs = 0;
            speechMs = 0;

            // 太短的瞬态声音直接丢弃，不触发 chunk 保存。
            if (completedSpeechMs < config.minSpeechMs)
            {
                return VadState::Silence;
            }

            return VadState::SpeechEnd;
        }

        return VadState::Pause;
    }

    state = VadState::Silence;
    return VadState::Silence;
}

float VadDetector::currentLevel() const
{
    return level;
}

float VadDetector::currentThreshold() const
{
    return threshold;
}

bool VadDetector::isCalibratingNoise() const
{
    return calibrating;
}

float VadDetector::computeRmsLevel(const QByteArray &pcmFrame) const
{
    if (pcmFrame.size() < int(sizeof(qint16)))
    {
        return 0.0f;
    }

    // 输入约定为单声道 PCM16，因此可以直接按 qint16 读取并归一化到 [-1, 1]。
    const auto *samples = reinterpret_cast<const qint16 *>(pcmFrame.constData());
    const int sampleCount = pcmFrame.size() / int(sizeof(qint16));
    double sumSquares = 0.0;

    for (int i = 0; i < sampleCount; ++i)
    {
        const double normalized = double(samples[i]) / 32768.0;
        sumSquares += normalized * normalized;
    }

    return static_cast<float>(qSqrt(sumSquares / double(sampleCount)));
}
