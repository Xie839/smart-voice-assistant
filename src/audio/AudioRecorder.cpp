#include "AudioRecorder.h"

#include "WavWriter.h"

#include <QAudioDevice>
#include <QAudioSource>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QMediaDevices>
#include <QRegularExpression>
#include <QtMath>

AudioRecorder::AudioRecorder(QObject *parent)
    : QObject(parent)
{
}

AudioRecorder::~AudioRecorder()
{
    if (recording)
    {
        stopRecording();
    }
}

bool AudioRecorder::startRecording(const QString &sessionId)
{
    if (recording)
    {
        emit recordingError("录音已经在进行中。");
        return false;
    }

    const QAudioDevice inputDevice = QMediaDevices::defaultAudioInput();
    if (inputDevice.isNull())
    {
        emit recordingError("未找到可用的系统默认麦克风。");
        return false;
    }

    QString formatWarning;
    activeFormat = chooseRecordingFormat(&formatWarning);
    if (!activeFormat.isValid())
    {
        emit recordingError("当前麦克风没有可用的录音格式。");
        return false;
    }

    outputPath = buildOutputPath(sessionId);
    if (outputPath.isEmpty())
    {
        emit recordingError("无法创建录音保存目录 temp/recordings。");
        return false;
    }

    pcmBuffer.clear();
    audioSource = new QAudioSource(inputDevice, activeFormat, this);
    audioSource->setBufferSize(activeFormat.bytesForDuration(500000));

    connect(audioSource, &QAudioSource::stateChanged, this, [this](QAudio::State state)
            {
        if (state == QAudio::StoppedState && recording && audioSource->error() != QAudio::NoError)
        {
            emit recordingError("麦克风录音中断，请检查设备权限或占用情况。");
        } });

    // QAudioSource 内部异步推送数据，readyRead 只把当前可读 PCM 追加到内存缓存，不阻塞 UI。
    audioDevice = audioSource->start();
    if (!audioDevice)
    {
        audioSource->deleteLater();
        audioSource = nullptr;
        emit recordingError("麦克风打开失败，请检查系统权限或默认输入设备。");
        return false;
    }

    connect(audioDevice, &QIODevice::readyRead, this, &AudioRecorder::readAvailableAudio);

    recording = true;
    emit recordingStarted();

    if (!formatWarning.isEmpty())
    {
        emit recordingWarning(formatWarning);
    }

    return true;
}

QString AudioRecorder::stopRecording()
{
    if (!recording)
    {
        return QString();
    }

    recording = false;

    if (audioDevice)
    {
        readAvailableAudio();
        audioDevice->disconnect(this);
        audioDevice = nullptr;
    }

    if (audioSource)
    {
        audioSource->stop();
        audioSource->deleteLater();
        audioSource = nullptr;
    }

    // 采集格式可能是 Int16/UInt8/Float；保存前统一转换成 WavWriter 支持的 PCM16。
    const QByteArray pcm16Data = convertToPcm16(pcmBuffer, activeFormat);
    if (pcm16Data.isEmpty() && !pcmBuffer.isEmpty())
    {
        emit recordingError("当前采样格式暂不支持转换");
        return QString();
    }

    const bool written = WavWriter::writePcm16ToWav(
        outputPath,
        pcm16Data,
        activeFormat.sampleRate(),
        activeFormat.channelCount());

    if (!written)
    {
        emit recordingError("WAV 文件写入失败：" + outputPath);
        return QString();
    }

    const QString savedPath = outputPath;
    pcmBuffer.clear();
    outputPath.clear();

    emit recordingStopped(savedPath);
    return savedPath;
}

bool AudioRecorder::isRecording() const
{
    return recording;
}

QAudioFormat AudioRecorder::chooseRecordingFormat(QString *warningMessage) const
{
    const QAudioDevice inputDevice = QMediaDevices::defaultAudioInput();

    QAudioFormat requestedFormat;
    requestedFormat.setSampleRate(16000);
    requestedFormat.setChannelCount(1);
    requestedFormat.setSampleFormat(QAudioFormat::Int16);

    if (inputDevice.isFormatSupported(requestedFormat))
    {
        return requestedFormat;
    }

    const QAudioFormat preferredFormat = inputDevice.preferredFormat();
    QAudioFormat requestedWithPreferredSampleFormat = requestedFormat;
    requestedWithPreferredSampleFormat.setSampleFormat(preferredFormat.sampleFormat());

    if (requestedWithPreferredSampleFormat.isValid() &&
        inputDevice.isFormatSupported(requestedWithPreferredSampleFormat))
    {
        if (warningMessage)
        {
            *warningMessage = "默认麦克风不支持 16000Hz 单声道 Int16，录音时使用设备支持的采样格式，保存时转换为 PCM16。";
        }
        return requestedWithPreferredSampleFormat;
    }

    if (preferredFormat.isValid())
    {
        if (warningMessage)
        {
            *warningMessage = QString("默认麦克风不支持 16000Hz 单声道 Int16，已回退为设备首选格式 %1Hz/%2声道，保存时转换为 PCM16。")
                                  .arg(preferredFormat.sampleRate())
                                  .arg(preferredFormat.channelCount());
        }
        return preferredFormat;
    }

    return QAudioFormat();
}

QByteArray AudioRecorder::convertToPcm16(const QByteArray &input, const QAudioFormat &format) const
{
    if (input.isEmpty())
    {
        return QByteArray();
    }

    switch (format.sampleFormat())
    {
    case QAudioFormat::Int16:
        // 设备已经输出 16-bit PCM，直接交给 WavWriter 写入。
        return input.left(input.size() - (input.size() % int(sizeof(qint16))));

    case QAudioFormat::UInt8:
    {
        QByteArray output;
        output.resize(input.size() * int(sizeof(qint16)));
        auto *out = reinterpret_cast<qint16 *>(output.data());

        for (int i = 0; i < input.size(); ++i)
        {
            const quint8 sample = static_cast<quint8>(input.at(i));
            out[i] = static_cast<qint16>((int(sample) - 128) << 8);
        }

        return output;
    }

    case QAudioFormat::Float:
    {
        const int sampleCount = input.size() / int(sizeof(float));
        QByteArray output;
        output.resize(sampleCount * int(sizeof(qint16)));
        const auto *in = reinterpret_cast<const float *>(input.constData());
        auto *out = reinterpret_cast<qint16 *>(output.data());

        for (int i = 0; i < sampleCount; ++i)
        {
            const float clamped = qBound(-1.0f, in[i], 1.0f);
            out[i] = static_cast<qint16>(clamped * 32767.0f);
        }

        return output;
    }

    default:
        return QByteArray();
    }
}

QString AudioRecorder::buildOutputPath(const QString &sessionId) const
{
    QDir dir(QCoreApplication::applicationDirPath());
    if (!dir.exists("temp/recordings") && !dir.mkpath("temp/recordings"))
    {
        return QString();
    }

    const QString timeText = QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss");
    const QString prefix = sanitizeFilePart(sessionId).isEmpty()
                               ? "recording"
                               : sanitizeFilePart(sessionId);
    return dir.filePath("temp/recordings/" + prefix + "_" + timeText + ".wav");
}

QString AudioRecorder::sanitizeFilePart(const QString &text) const
{
    QString result = text.trimmed();
    result.replace(QRegularExpression("[^A-Za-z0-9_-]"), "_");
    return result;
}

void AudioRecorder::readAvailableAudio()
{
    if (!audioDevice)
    {
        return;
    }

    const QByteArray chunk = audioDevice->readAll();
    if (chunk.isEmpty())
    {
        return;
    }

    pcmBuffer.append(chunk);
    updateAudioLevel(chunk);
}

void AudioRecorder::updateAudioLevel(const QByteArray &chunk)
{
    const QByteArray pcm16Chunk = convertToPcm16(chunk, activeFormat);
    if (pcm16Chunk.size() < 2)
    {
        emit audioLevelUpdated(0.0f);
        return;
    }

    const qint16 *samples = reinterpret_cast<const qint16 *>(pcm16Chunk.constData());
    const int sampleCount = pcm16Chunk.size() / int(sizeof(qint16));
    qint64 sumSquares = 0;

    for (int i = 0; i < sampleCount; ++i)
    {
        const qint64 sample = samples[i];
        sumSquares += sample * sample;
    }

    const double rms = qSqrt(double(sumSquares) / double(sampleCount));
    const float normalizedLevel = static_cast<float>(qBound(0.0, rms / 32768.0, 1.0));
    emit audioLevelUpdated(normalizedLevel);
}
