#include "AudioRecorder.h"

#include "WavWriter.h"

#include <QAudioDevice>
#include <QAudioSource>
#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QMediaDevices>
#include <QRegularExpression>
#include <QMetaType>
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

    // Windows 下不同麦克风可能只暴露 48kHz、多声道或 Float 等格式。
    // 这里不强行假设设备一定支持 16kHz 单声道，而是记录实际格式，
    // 后续再为 VAD 和 WAV 保存分别做格式归一化。
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

    // VAD 的帧长计算必须使用实际采样率，否则 48kHz 设备会被按 16kHz 错误切帧。
    vadConfig.sampleRate = activeFormat.sampleRate();
    vadDetector = VadDetector(vadConfig);
    vadDetector.reset();

    // VAD 和 chunk 保存统一使用“实际采样率 + 单声道 + PCM16”。
    // 第一版暂不做重采样，避免引入复杂 DSP；只保证帧大小和音频数据格式一致。
    pcm16Format = activeFormat;
    pcm16Format.setSampleFormat(QAudioFormat::Int16);
    pcm16Format.setChannelCount(1);
    audioChunker.startSession(sessionId, pcm16Format);

    pcmBuffer.clear();
    vadFrameBuffer.clear();
    lastVadStatus.clear();
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

    // 用户停止时可能正处在一句话中间，此时没有等到 SpeechEnd，
    // 仍然把当前有效缓存保存为 manual_stop chunk，避免丢掉最后一句。
    if (audioChunker.hasValidAudio())
    {
        saveCurrentChunk("manual_stop");
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
    vadFrameBuffer.clear();
    outputPath.clear();
    lastVadStatus.clear();

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

    // 理想格式适合后续 whisper.cpp：16kHz、单声道、Int16。
    // 如果设备支持就直接用它；如果不支持，就回退到设备可用格式。
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
        // UInt8 音频以 128 为零点，转换到有符号 Int16 时需要先平移到 [-128, 127]。
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
        // Qt 的 Float 音频通常是 [-1.0, 1.0]，先裁剪再映射到 Int16 范围。
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
    // VAD 只关心“整体说话能量”，多声道会让帧大小和 RMS 计算复杂化。
    // 因此先把设备格式转成 PCM16，再将每个采样点的多声道平均成单声道。
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
    // 完整录音用于调试和回放，和 VAD chunk 分开保存，避免后续 ASR 只处理片段时混淆。
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

    // 完整录音缓存保留设备原始格式，停止时再转换为 WAV 可写的 PCM16。
    pcmBuffer.append(chunk);
    updateAudioLevel(chunk);

    // VAD 流程必须使用单声道 PCM16：QAudioSource 可能返回 48kHz/4声道/Float 等格式。
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
    // audioLevelUpdated 预留给后续 UI 音量条；这里复用 VAD 的单声道 PCM16 数据口径。
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

    // 输入已经是单声道 PCM16，因此一帧字节数只等于 frameSamples * sizeof(qint16)。
    // 这里不能再乘设备原始 channelCount，否则多声道设备会导致 VAD 帧过长。
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

        const VadState state = vadDetector.processPcm16Frame(frame);
        qDebug() << "VAD frame:"
                 << "state=" << vadStateName(state)
                 << "level=" << vadDetector.currentLevel()
                 << "threshold=" << vadDetector.currentThreshold();
        handleVadState(state, frame);
    }
}

void AudioRecorder::handleVadState(VadState state, const QByteArray &frame)
{
    if (vadDetector.isCalibratingNoise())
    {
        // 录音开始的第一秒只估计环境噪声，不缓存 chunk，避免开头静音写入片段。
        emitVadStatus("正在进行环境噪声估计");
        return;
    }

    switch (state)
    {
    case VadState::Silence:
        if (audioChunker.hasValidAudio())
        {
            // 太短或未达到起始条件的声音会回到 Silence，清空缓存防止保存误触发。
            audioChunker.clearCurrentChunk();
        }
        emitVadStatus("等待语音输入");
        break;

    case VadState::SpeechStart:
        qDebug() << "VAD SpeechStart triggered";
        // SpeechStart 表示连续超过阈值已满足起始时长，开始缓存当前句子。
        audioChunker.appendAudio(frame);
        emitVadStatus("检测到语音，正在录入");
        break;

    case VadState::Speaking:
        audioChunker.appendAudio(frame);
        emitVadStatus("检测到语音，正在录入");
        break;

    case VadState::Pause:
        // Pause 是句中短暂停顿，仍然追加音频，保留自然语句中的空隙。
        audioChunker.appendAudio(frame);
        emitVadStatus("检测到停顿");
        break;

    case VadState::SpeechEnd:
    {
        qDebug() << "VAD SpeechEnd triggered";
        audioChunker.appendAudio(frame);
        emitVadStatus("一句话已结束，正在保存片段");
        // SpeechEnd 立即保存 chunk，然后继续监听下一句话；max_duration 是防止超长句子的兜底切分。
        const QString reason = audioChunker.currentDurationMs() >= vadConfig.maxSegmentMs
                                   ? "max_duration"
                                   : "vad_silence";
        saveCurrentChunk(reason);
        emitVadStatus("等待语音输入");
        break;
    }
    }
}

void AudioRecorder::emitVadStatus(const QString &stateText)
{
    // 状态文本去重，避免 30ms 一帧时频繁刷新状态栏。
    if (stateText == lastVadStatus)
    {
        return;
    }

    lastVadStatus = stateText;
    emit vadStateChanged(stateText);
}

AudioChunkInfo AudioRecorder::saveCurrentChunk(const QString &splitReason)
{
    qDebug() << "Saving current chunk:"
             << "reason=" << splitReason
             << "durationMs=" << audioChunker.currentDurationMs();

    // chunk 写入 temp/chunks，完整 recording 写入 temp/recordings，二者用途不同：
    // chunk 给后续 ASR 逐句识别，完整录音只作为调试回放。
    AudioChunkInfo info = audioChunker.saveCurrentChunk(splitReason);
    if (info.wavPath.isEmpty())
    {
        if (info.durationMs > 0)
        {
            emit recordingError("语音片段保存失败。");
        }
        return info;
    }

    emit sentenceChunkReady(info);
    qDebug() << "sentenceChunkReady emitted:" << info.wavPath;
    return info;
}
