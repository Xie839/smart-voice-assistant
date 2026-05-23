#ifndef WAVWRITER_H
#define WAVWRITER_H

#include <QByteArray>
#include <QString>

// WavWriter 只负责把已经准备好的 PCM16 数据写成标准 WAV 文件。
// 采样格式转换、声道 downmix、VAD 切分等逻辑都放在 AudioRecorder/AudioChunker 中。
class WavWriter
{
public:
    // 输入：PCM16 裸数据、采样率、声道数；输出：带 RIFF/WAVE 头的可播放 WAV 文件。
    static bool writePcm16ToWav(const QString &filePath,
                                const QByteArray &pcmData,
                                int sampleRate,
                                int channels);
};

#endif
