#include "WavLoader.h"

#include <QDataStream>
#include <QDebug>
#include <QFile>

#include <cmath>

namespace
{
QString readFourcc(QDataStream &stream)
{
    char id[4] = {};
    if (stream.readRawData(id, 4) != 4)
    {
        return QString();
    }
    return QString::fromLatin1(id, 4);
}
}

WavLoader::WavData WavLoader::loadAsMonoFloat32(const QString &wavPath)
{
    WavData result;
    quint16 audioFormat = 0;
    quint32 dataSize = 0;
    bool didDownmix = false;
    bool didResample = false;

    QFile file(wavPath);
    if (!file.exists())
    {
        result.errorMessage = "WAV 文件不存在：" + wavPath;
        return result;
    }
    if (!file.open(QIODevice::ReadOnly))
    {
        result.errorMessage = "WAV 文件读取失败：" + wavPath;
        return result;
    }

    QDataStream in(&file);
    in.setByteOrder(QDataStream::LittleEndian);

    const QString riff = readFourcc(in);
    quint32 riffSize = 0;
    in >> riffSize;
    const QString wave = readFourcc(in);
    if (riff != "RIFF" || wave != "WAVE")
    {
        result.errorMessage = "WAV 文件缺少 RIFF/WAVE 标识：" + wavPath;
        return result;
    }

    qint64 dataOffset = 0;
    bool hasFmt = false;
    bool hasData = false;

    while (!in.atEnd())
    {
        const QString chunkId = readFourcc(in);
        if (chunkId.size() != 4)
        {
            break;
        }

        quint32 chunkSize = 0;
        in >> chunkSize;
        const qint64 chunkStart = file.pos();
        if (chunkSize > quint32(file.size() - chunkStart))
        {
            result.errorMessage = QString("WAV chunk 大小异常：%1, size=%2").arg(chunkId).arg(chunkSize);
            return result;
        }

        if (chunkId == "fmt ")
        {
            if (chunkSize < 16)
            {
                result.errorMessage = QString("fmt chunk 大小无效：%1").arg(chunkSize);
                return result;
            }

            quint16 channels = 0;
            quint32 sampleRate = 0;
            quint32 byteRate = 0;
            quint16 blockAlign = 0;
            quint16 bitsPerSample = 0;

            // WAV fmt 字段宽度固定：audioFormat/channels/bitsPerSample 是 16 位，
            // sampleRate/byteRate 是 32 位。不能直接读入 int，否则会造成字段错位。
            in >> audioFormat;
            in >> channels;
            in >> sampleRate;
            in >> byteRate;
            in >> blockAlign;
            in >> bitsPerSample;

            result.channels = int(channels);
            result.sampleRate = int(sampleRate);
            result.bitsPerSample = int(bitsPerSample);
            hasFmt = true;
        }
        else if (chunkId == "data")
        {
            dataOffset = file.pos();
            dataSize = chunkSize;
            hasData = true;
        }

        file.seek(chunkStart + chunkSize + (chunkSize % 2));
        if (hasFmt && hasData)
        {
            break;
        }
    }

    if (!hasFmt)
    {
        result.errorMessage = "未找到 fmt chunk：" + wavPath;
        return result;
    }
    if (!hasData)
    {
        result.errorMessage = "未找到 data chunk：" + wavPath;
        return result;
    }
    if (audioFormat != 1)
    {
        result.errorMessage = QString("当前 WAV 编码格式不是 PCM，audioFormat=%1").arg(audioFormat);
        return result;
    }
    if (result.bitsPerSample != 16)
    {
        result.errorMessage = QString("当前仅支持 16-bit PCM，bitsPerSample=%1").arg(result.bitsPerSample);
        return result;
    }
    if (result.sampleRate <= 0 || result.channels <= 0 || dataSize == 0)
    {
        result.errorMessage = QString("WAV 音频参数无效：sampleRate=%1, channels=%2, dataSize=%3")
                                  .arg(result.sampleRate)
                                  .arg(result.channels)
                                  .arg(dataSize);
        return result;
    }

    file.seek(dataOffset);
    const QByteArray pcm = file.read(dataSize);
    if (pcm.isEmpty())
    {
        result.errorMessage = "音频数据为空。";
        return result;
    }

    qDebug() << "WavLoader input:"
             << "wavPath=" << wavPath
             << "audioFormat=" << audioFormat
             << "sampleRate=" << result.sampleRate
             << "channels=" << result.channels
             << "bitsPerSample=" << result.bitsPerSample
             << "dataSize=" << dataSize;

    const int totalSamples = pcm.size() / int(sizeof(qint16));
    const int frameCount = totalSamples / result.channels;
    if (frameCount <= 0)
    {
        result.errorMessage = QString("音频数据不足以组成完整采样帧：totalSamples=%1, channels=%2")
                                  .arg(totalSamples)
                                  .arg(result.channels);
        return result;
    }

    didDownmix = result.channels > 1;
    result.monoFloat32.reserve(frameCount);

    const auto *samples = reinterpret_cast<const qint16 *>(pcm.constData());
    for (int frame = 0; frame < frameCount; ++frame)
    {
        float sum = 0.0f;
        for (int channel = 0; channel < result.channels; ++channel)
        {
            sum += float(samples[frame * result.channels + channel]) / 32768.0f;
        }
        result.monoFloat32.push_back(sum / float(result.channels));
    }

    if (result.sampleRate != 16000)
    {
        result.monoFloat32 = resampleLinear(result.monoFloat32, result.sampleRate, 16000);
        result.sampleRate = 16000;
        didResample = true;
    }

    if (result.monoFloat32.empty())
    {
        result.errorMessage = "WAV 解码后没有有效音频数据。";
        return result;
    }

    qDebug() << "WavLoader output:"
             << "downmix=" << didDownmix
             << "resampleTo16k=" << didResample
             << "monoFloat32.size=" << result.monoFloat32.size();

    result.success = true;
    return result;
}

std::vector<float> WavLoader::resampleLinear(const std::vector<float> &input,
                                             int sourceSampleRate,
                                             int targetSampleRate)
{
    if (input.empty() || sourceSampleRate <= 0 || targetSampleRate <= 0)
    {
        return {};
    }
    if (sourceSampleRate == targetSampleRate)
    {
        return input;
    }

    const double ratio = double(sourceSampleRate) / double(targetSampleRate);
    const int outputCount = qMax(1, int(std::floor(double(input.size()) / ratio)));
    std::vector<float> output;
    output.reserve(outputCount);

    for (int i = 0; i < outputCount; ++i)
    {
        const double sourceIndex = double(i) * ratio;
        const int left = int(std::floor(sourceIndex));
        const int right = qMin(left + 1, int(input.size()) - 1);
        const float fraction = float(sourceIndex - double(left));
        const float sample = input[left] * (1.0f - fraction) + input[right] * fraction;
        output.push_back(sample);
    }

    return output;
}
