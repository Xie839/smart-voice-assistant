#include "AudioRecorder.h"

#include "WavWriter.h"
#include "../utils/PerfTracer.h"

#include <QAudioDevice>
#include <QAudioSource>
#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QMediaDevices>
#include <QMetaType>
#include <QRegularExpression>
#include <QtMath>

namespace
{
QString sampleFormatName(QAudioFormat::SampleFormat format)
{
    switch (format)
    {
    case QAudioFormat::UInt8:
        return "UInt8";
    case QAudioFormat::Int16:
        return "Int16";
    case QAudioFormat::Int32:
        return "Int32";
    case QAudioFormat::Float:
        return "Float";
    default:
        return "Unknown";
    }
}

QString vadStateName(VadState state)
{
    switch (state)
    {
    case VadState::Silence:
        return "Silence";
    case VadState::SpeechStart:
        return "SpeechStart";
    case VadState::Speaking:
        return "Speaking";
    case VadState::Pause:
        return "Pause";
    case VadState::SpeechEnd:
        return "SpeechEnd";
    }
    return "Unknown";
}
}

AudioRecorder::AudioRecorder(QObject *parent)
    : QObject(parent),
      vadDetector(vadConfig)
{
    qRegisterMetaType<AudioChunkInfo>("AudioChunkInfo");
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

    qDebug() << "Audio input format:"
             << "sampleRate=" << activeFormat.sampleRate()
             << "channelCount=" << activeFormat.channelCount()
             << "sampleFormat=" << sampleFormatName(activeFormat.sampleFormat());

    vadConfig.sampleRate = activeFormat.sampleRate();
    vadDetector = VadDetector(vadConfig);
    vadDetector.reset();

    pcm16Format = activeFormat;
    pcm16Format.setSampleFormat(QAudioFormat::Int16);
    pcm16Format.setChannelCount(1);

    preRollMaxBytes = (pcm16Format.sampleRate() * preRollMs / 1000) * int(sizeof(qint16));
    preRollBuffer.clear();
    audioChunker.startSession(sessionId, pcm16Format);

    pcmBuffer.clear();
    vadFrameBuffer.clear();
    sessionElapsedMs = 0;
    currentChunkStartMs = -1;
    currentChunkLastAudioMs = -1;
    currentChunkLastSpeechMs = -1;
    currentChunkTraceId.clear();
    currentChunkSpeechEndDetectedMs = -1;
    currentChunkSilenceWaitMs = -1;
    chunkCollecting = false;
    lastVadStatus.clear();

    audioSource = new QAudioSource(inputDevice, activeFormat, this);
    audioSource->setBufferSize(activeFormat.bytesForDuration(500000));

    connect(audioSource, &QAudioSource::stateChanged, this, [this](QAudio::State state)
            {
        if (state == QAudio::StoppedState && recording && audioSource->error() != QAudio::NoError)
        {
            emit recordingError("麦克风录音中断，请检查设备权限或占用情况。");
        } });

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
    emitVadStatus("正在进行环境噪声估计");

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

    const auto clearSessionBuffers = [this]()
    {
        pcmBuffer.clear();
        vadFrameBuffer.clear();
        preRollBuffer.clear();
        outputPath.clear();
        lastVadStatus.clear();
        preRollMaxBytes = 0;
        sessionElapsedMs = 0;
        currentChunkStartMs = -1;
        currentChunkLastAudioMs = -1;
        currentChunkLastSpeechMs = -1;
        currentChunkTraceId.clear();
        currentChunkSpeechEndDetectedMs = -1;
        currentChunkSilenceWaitMs = -1;
        chunkCollecting = false;
    };

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

    if (audioChunker.hasValidAudio())
    {
        const qint64 chunkEndMs = currentChunkLastAudioMs >= 0 ? currentChunkLastAudioMs : sessionElapsedMs;
        saveCurrentChunk("manual_stop", currentChunkStartMs, chunkEndMs);
    }

    const QByteArray pcm16Data = convertToPcm16(pcmBuffer, activeFormat);
    if (pcm16Data.isEmpty() && !pcmBuffer.isEmpty())
    {
        emit recordingError("当前采样格式暂不支持转换");
        clearSessionBuffers();
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
        clearSessionBuffers();
        return QString();
    }

    const QString savedPath = outputPath;
    clearSessionBuffers();

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
            *warningMessage = "默认麦克风不支持 16000Hz 单声道 Int16，录音时使用设备支持格式，保存时转换为 PCM16。";
        }
        return requestedWithPreferredSampleFormat;
    }

    if (preferredFormat.isValid())
    {
        if (warningMessage)
        {
            *warningMessage = QString("默认麦克风不支持 16000Hz 单声道 Int16，已回退为设备首选格式 %1Hz/%2 声道，保存时转换为 PCM16。")
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

QByteArray AudioRecorder::convertToMonoPcm16(const QByteArray &input, const QAudioFormat &format) const
{
    const QByteArray pcm16 = convertToPcm16(input, format);
    if (pcm16.isEmpty())
    {
        return QByteArray();
    }

    const int channels = qMax(1, format.channelCount());
    if (channels == 1)
    {
        return pcm16;
    }

    const int totalSamples = pcm16.size() / int(sizeof(qint16));
    const int frameCount = totalSamples / channels;
    QByteArray mono;
    mono.resize(frameCount * int(sizeof(qint16)));

    const auto *in = reinterpret_cast<const qint16 *>(pcm16.constData());
    auto *out = reinterpret_cast<qint16 *>(mono.data());

    for (int frame = 0; frame < frameCount; ++frame)
    {
        qint64 sum = 0;
        for (int channel = 0; channel < channels; ++channel)
        {
            sum += in[frame * channels + channel];
        }
        out[frame] = static_cast<qint16>(sum / channels);
    }

    return mono;
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

    const QByteArray monoPcm16Chunk = convertToMonoPcm16(chunk, activeFormat);
    if (monoPcm16Chunk.isEmpty() && !chunk.isEmpty())
    {
        emit recordingError("当前采样格式暂不支持转换");
        return;
    }

    processPcm16Audio(monoPcm16Chunk);
}

void AudioRecorder::updateAudioLevel(const QByteArray &chunk)
{
    const QByteArray pcm16Chunk = convertToMonoPcm16(chunk, activeFormat);
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

void AudioRecorder::processPcm16Audio(const QByteArray &pcm16Data)
{
    if (pcm16Data.isEmpty() || pcm16Format.sampleRate() <= 0 || pcm16Format.channelCount() <= 0)
    {
        return;
    }

    vadFrameBuffer.append(pcm16Data);

    const int frameSamples = pcm16Format.sampleRate() * vadConfig.frameMs / 1000;
    const int frameBytes = frameSamples * int(sizeof(qint16));
    if (frameBytes <= 0)
    {
        return;
    }

    while (vadFrameBuffer.size() >= frameBytes)
    {
        const QByteArray frame = vadFrameBuffer.left(frameBytes);
        vadFrameBuffer.remove(0, frameBytes);
        const qint64 frameStartMs = sessionElapsedMs;
        const qint64 frameEndMs = sessionElapsedMs + vadConfig.frameMs;

        const VadState state = vadDetector.processPcm16Frame(frame);
        qDebug() << "VAD frame:"
                 << "state=" << vadStateName(state)
                 << "level=" << vadDetector.currentLevel()
                 << "threshold=" << vadDetector.currentThreshold();
        handleVadState(state, frame, frameStartMs, frameEndMs);
        sessionElapsedMs = frameEndMs;

        if (!frame.isEmpty() && preRollMaxBytes > 0)
        {
            preRollBuffer.append(frame);
            if (preRollBuffer.size() > preRollMaxBytes)
            {
                preRollBuffer.remove(0, preRollBuffer.size() - preRollMaxBytes);
            }
        }
    }
}

void AudioRecorder::handleVadState(VadState state, const QByteArray &frame, qint64 frameStartMs, qint64 frameEndMs)
{
    if (vadDetector.isCalibratingNoise())
    {
        emitVadStatus("正在进行环境噪声估计");
        return;
    }

    switch (state)
    {
    case VadState::Silence:
        if (audioChunker.hasValidAudio())
        {
            audioChunker.clearCurrentChunk();
        }
        chunkCollecting = false;
        currentChunkStartMs = -1;
        currentChunkLastAudioMs = -1;
        emitVadStatus("等待语音输入");
        break;

    case VadState::SpeechStart:
    {
        qDebug() << "VAD SpeechStart triggered";
        chunkCollecting = true;
        currentChunkTraceId = audioChunker.nextTraceId();
        currentChunkSpeechEndDetectedMs = -1;
        currentChunkSilenceWaitMs = -1;
        PerfTracer::startTrace("VAD", currentChunkTraceId, "vad_speech_start",
                               QString("frame_start=%1 ms").arg(frameStartMs));
        PerfTracer::startTrace("ASR", currentChunkTraceId, "vad_speech_start");

        const qint64 preRollSamples = preRollBuffer.size() / int(sizeof(qint16));
        const qint64 preRollDurationMs = pcm16Format.sampleRate() > 0
                                             ? (preRollSamples * 1000 / pcm16Format.sampleRate())
                                             : 0;
        currentChunkStartMs = qMax<qint64>(0, frameStartMs - preRollDurationMs);

        if (!preRollBuffer.isEmpty())
        {
            audioChunker.appendAudio(preRollBuffer);
        }
        audioChunker.appendAudio(frame);
        currentChunkLastAudioMs = frameEndMs;
        currentChunkLastSpeechMs = frameEndMs;

        emitVadStatus("检测到语音，正在录入");
        break;
    }

    case VadState::Speaking:
        if (!chunkCollecting)
        {
            chunkCollecting = true;
            currentChunkStartMs = frameStartMs;
        }
        audioChunker.appendAudio(frame);
        currentChunkLastAudioMs = frameEndMs;
        currentChunkLastSpeechMs = frameEndMs;
        emitVadStatus("检测到语音，正在录入");
        break;

    case VadState::Pause:
        if (!chunkCollecting)
        {
            chunkCollecting = true;
            currentChunkStartMs = frameStartMs;
        }
        audioChunker.appendAudio(frame);
        currentChunkLastAudioMs = frameEndMs;
        emitVadStatus("检测到停顿");
        break;

    case VadState::SpeechEnd:
    {
        qDebug() << "VAD SpeechEnd triggered";
        currentChunkSpeechEndDetectedMs = frameEndMs;
        currentChunkSilenceWaitMs = currentChunkLastSpeechMs >= 0 ? qMax<qint64>(0, frameEndMs - currentChunkLastSpeechMs) : 0;
        PerfTracer::markTrace("VAD", currentChunkTraceId, "vad_speech_end_detected",
                              QString("silence_wait=%1 ms").arg(currentChunkSilenceWaitMs));
        PerfTracer::warnIfSlow("VAD", currentChunkTraceId, "vad_silence_wait", currentChunkSilenceWaitMs, 1500,
                               "VAD 等待静音过长，可能导致响应慢");
        audioChunker.appendAudio(frame);
        currentChunkLastAudioMs = frameEndMs;
        emitVadStatus("一句话已结束，正在保存片段");

        const QString reason = audioChunker.currentDurationMs() >= vadConfig.maxSegmentMs
                                   ? "max_duration"
                                   : "vad_silence";
        saveCurrentChunk(reason, currentChunkStartMs, currentChunkLastAudioMs);
        chunkCollecting = false;
        currentChunkStartMs = -1;
        currentChunkLastAudioMs = -1;
        emitVadStatus("等待语音输入");
        break;
    }
    }
}

void AudioRecorder::emitVadStatus(const QString &stateText)
{
    if (stateText == lastVadStatus)
    {
        return;
    }

    lastVadStatus = stateText;
    emit vadStateChanged(stateText);
}

AudioChunkInfo AudioRecorder::saveCurrentChunk(const QString &splitReason, qint64 startTimeMs, qint64 endTimeMs)
{
    qDebug() << "Saving current chunk:"
             << "reason=" << splitReason
             << "durationMs=" << audioChunker.currentDurationMs()
             << "startTimeMs=" << startTimeMs
             << "endTimeMs=" << endTimeMs;

    const int chunkDurationMs = audioChunker.currentDurationMs();
    const QString traceId = currentChunkTraceId.isEmpty() ? audioChunker.nextTraceId() : currentChunkTraceId;
    if (chunkDurationMs < vadConfig.minSpeechMs)
    {
        PerfTracer::markTrace("CHUNK", traceId, "chunk_skipped_too_short",
                              QString("skipped_too_short=true, chunk_duration_ms=%1, minSpeechMs=%2")
                                  .arg(chunkDurationMs)
                                  .arg(vadConfig.minSpeechMs));
        audioChunker.clearCurrentChunk();
        AudioChunkInfo skipped;
        skipped.traceId = traceId;
        skipped.durationMs = chunkDurationMs;
        skipped.splitReason = "too_short";
        return skipped;
    }

    AudioChunkInfo info = audioChunker.saveCurrentChunk(splitReason, startTimeMs, endTimeMs);
    info.vadSpeechEndDetectedMs = currentChunkSpeechEndDetectedMs;
    info.vadSilenceWaitMs = currentChunkSilenceWaitMs;
    if (info.wavPath.isEmpty())
    {
        if (info.durationMs > 0)
        {
            emit recordingError("语音片段保存失败。");
        }
        return info;
    }

    emit sentenceChunkReady(info);
    qDebug() << "sentenceChunkReady emitted:" << info.wavPath
             << "sequenceId=" << info.sequenceId;
    return info;
}
