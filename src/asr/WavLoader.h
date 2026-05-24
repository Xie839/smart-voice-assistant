#ifndef WAVLOADER_H
#define WAVLOADER_H

#include <QString>

#include <vector>

// WavLoader 用于读取 PCM16 WAV，并输出单声道 float PCM（可用于离线 ASR 预处理）。
class WavLoader
{
public:
    struct WavData
    {
        int sampleRate = 0;
        int channels = 0;
        int bitsPerSample = 0;
        std::vector<float> monoFloat32;
        QString errorMessage;
        bool success = false;
    };

    static WavData loadAsMonoFloat32(const QString &wavPath);

private:
    static std::vector<float> resampleLinear(const std::vector<float> &input,
                                             int sourceSampleRate,
                                             int targetSampleRate);
};

#endif
