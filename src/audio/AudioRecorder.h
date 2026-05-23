#ifndef AUDIORECORDER_H
#define AUDIORECORDER_H

#include <QAudioFormat>
#include <QByteArray>
#include <QObject>
#include <QString>

class QAudioSource;
class QIODevice;

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

private:
    QAudioFormat chooseRecordingFormat(QString *warningMessage) const;
    QByteArray convertToPcm16(const QByteArray &input, const QAudioFormat &format) const;
    QString buildOutputPath(const QString &sessionId) const;
    QString sanitizeFilePart(const QString &text) const;
    void readAvailableAudio();
    void updateAudioLevel(const QByteArray &chunk);

private:
    QAudioSource *audioSource = nullptr;
    QIODevice *audioDevice = nullptr;
    QAudioFormat activeFormat;
    QByteArray pcmBuffer;
    QString outputPath;
    bool recording = false;
};

#endif
