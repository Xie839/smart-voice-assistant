#ifndef WAVWRITER_H
#define WAVWRITER_H

#include <QByteArray>
#include <QString>

class WavWriter
{
public:
    static bool writePcm16ToWav(const QString &filePath,
                                const QByteArray &pcmData,
                                int sampleRate,
                                int channels);
};

#endif
