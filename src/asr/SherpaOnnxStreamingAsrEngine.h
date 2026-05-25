#ifndef SHERPAONNXSTREAMINGASRENGINE_H
#define SHERPAONNXSTREAMINGASRENGINE_H

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QStringList>
#include <memory>

#include "AsrResult.h"

class SherpaOnnxStreamingAsrEngine : public QObject
{
    Q_OBJECT

public:
    enum class ModelKind
    {
        Unknown,
        Paraformer,
        Transducer,
        Ctc
    };

    explicit SherpaOnnxStreamingAsrEngine(QObject *parent = nullptr);
    ~SherpaOnnxStreamingAsrEngine() override;

    bool isAvailable(QString *reason = nullptr) const;
    bool initialize(QString *errorMessage = nullptr);
    bool runSmokeTest(QString *errorMessage = nullptr);
    void startSession();
    void acceptAudioFrame(const QByteArray &pcmData,
                          int sampleRate,
                          int channels,
                          int bitsPerSample,
                          qint64 frameStartMs);
    void finishSession();
    void resetSession();
    void discardSession();
    bool isSessionActive() const;
    bool isInitialized() const;

signals:
    void partialResultReady(const QString &text);
    void finalResultReady(const AsrResult &result);
    void streamingError(const QString &errorMessage);

private:
    struct ModelFiles
    {
        QString modelDir;
        QString tokensPath;
        QString encoderPath;
        QString decoderPath;
        QString joinerPath;
        QString ctcModelPath;
        ModelKind kind = ModelKind::Unknown;
    };

    struct Impl;

    bool findStreamingSdk(QString *reason = nullptr) const;
    QString findStreamingDll() const;
    QString findSherpaRuntimeDir(QString *reason = nullptr) const;
    bool findStreamingModel(ModelFiles *files = nullptr, QString *reason = nullptr) const;
    QStringList candidateBaseDirs() const;
    QString firstMatch(const QString &dirPath, const QStringList &patterns) const;
    QVector<float> pcm16ToFloat32Mono(const QByteArray &pcmData, int channels, int bitsPerSample) const;
    QString currentResultText() const;
    bool prepareRuntimeDlls(const QString &runtimeDir, QString *errorMessage);
    QString loadedModulePath(const QString &moduleName) const;

    bool m_initialized = false;
    bool m_sessionActive = false;
    bool m_unavailableLogged = false;
    QString m_activeTraceId = "streaming";
    QString m_lastPartialText;
    QString m_selectedRuntimeDir;
    ModelFiles m_selectedModelFiles;
    QByteArray m_encoderPathUtf8;
    QByteArray m_decoderPathUtf8;
    QByteArray m_joinerPathUtf8;
    QByteArray m_ctcModelPathUtf8;
    QByteArray m_tokensPathUtf8;
    qint64 m_sessionStartedAtMs = -1;
    qint64 m_lastFrameLogMs = -1;
    std::unique_ptr<Impl> m_impl;
};

#endif
