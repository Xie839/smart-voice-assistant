#include "WavWriter.h"

#include <QDataStream>
#include <QFile>

bool WavWriter::writePcm16ToWav(const QString &filePath,
                                const QByteArray &pcmData,
                                int sampleRate,
                                int channels)
{
    if (filePath.isEmpty() || sampleRate <= 0 || channels <= 0)
    {
        return false;
    }

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly))
    {
        return false;
    }

    constexpr quint16 audioFormatPcm = 1;
    constexpr quint16 bitsPerSample = 16;
    const quint16 blockAlign = static_cast<quint16>(channels * bitsPerSample / 8);
    const quint32 byteRate = static_cast<quint32>(sampleRate * blockAlign);
    const quint32 dataSize = static_cast<quint32>(pcmData.size());
    const quint32 riffSize = 36 + dataSize;

    QDataStream out(&file);
    out.setByteOrder(QDataStream::LittleEndian);

    // 标准 RIFF/WAVE 头，当前第一版只写入 16-bit PCM 裸数据。
    out.writeRawData("RIFF", 4);
    out << riffSize;
    out.writeRawData("WAVE", 4);

    out.writeRawData("fmt ", 4);
    out << quint32(16);
    out << audioFormatPcm;
    out << static_cast<quint16>(channels);
    out << static_cast<quint32>(sampleRate);
    out << byteRate;
    out << blockAlign;
    out << bitsPerSample;

    out.writeRawData("data", 4);
    out << dataSize;

    return file.write(pcmData) == pcmData.size();
}
