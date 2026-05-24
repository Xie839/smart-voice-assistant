#ifndef FFMPEGAUDIOCONVERTER_H
#define FFMPEGAUDIOCONVERTER_H

#include <QObject>
#include <QString>

class QProcess;

class FfmpegAudioConverter : public QObject
{
    Q_OBJECT

public:
    explicit FfmpegAudioConverter(QObject *parent = nullptr);
    ~FfmpegAudioConverter() override;

    QString findFfmpegExecutable() const;
    bool isAvailable(QString *reason = nullptr) const;
    bool isConverting() const;

    void convertToWavAsync(const QString &inputFilePath, const QString &outputWavPath);

signals:
    void conversionStarted();
    void conversionFinished(const QString &outputWavPath);
    void conversionFailed(const QString &errorMessage);

private:
    void cleanupProcess();
    QString shortErrorDetail(const QString &stderrText, const QString &stdoutText) const;

private:
    QProcess *m_process = nullptr;
};

#endif
