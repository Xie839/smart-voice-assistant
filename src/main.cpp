#include <QApplication>
#include <QDateTime>
#include <QDebug>
#include <QDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QMessageBox>
#include <QProcess>
#include <QProcessEnvironment>
#include <QFutureWatcher>
#include <QStringList>
#include <QTimer>
#include <QUuid>
#include <QtConcurrent>

#include <exception>

#include "MainWindow.h"
#include "ai/AiConfigDialog.h"
#include "ai/CustomPromptDialog.h"
#include "ai/DeepSeekClient.h"
#include "asr/AsrBackendManager.h"
#include "asr/SherpaOnnxStreamingAsrEngine.h"
#include "audio/AudioRecorder.h"
#include "config/AppConfig.h"
#include "history/HistoryManager.h"
#include "media/FfmpegAudioConverter.h"
#include "utils/PerfTracer.h"

namespace
{
QString maskedProviderText(const QString &apiKey)
{
    if (apiKey.size() <= 8)
    {
        return "DeepSeek";
    }
    return QString("DeepSeek(%1****%2)")
        .arg(apiKey.left(4), apiKey.right(4));
}

bool isWavFile(const QString &filePath)
{
    return QFileInfo(filePath).suffix().compare("wav", Qt::CaseInsensitive) == 0;
}

bool isSupportedMediaFile(const QString &filePath)
{
    static const QStringList supported = {
        "wav", "mp3", "m4a", "aac", "flac", "ogg",
        "mp4", "mov", "avi", "mkv", "wmv"};
    return supported.contains(QFileInfo(filePath).suffix().toLower());
}

QString buildConvertedWavPath(const QString &inputFilePath)
{
    QDir tempDir(QDir::current().filePath("temp/converted"));
    if (!tempDir.exists())
    {
        tempDir.mkpath(".");
    }

    const QFileInfo info(inputFilePath);
    const QString baseName = info.completeBaseName().isEmpty() ? "media" : info.completeBaseName();
    const QString stamp = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
    const QString fileName = QString("%1_%2_converted.wav").arg(baseName, stamp);
    return tempDir.filePath(fileName);
}

enum class StreamingMode
{
    Auto,
    ForceOn,
    ForceOff
};

enum class StreamingState
{
    Disabled,
    Uninitialized,
    Initializing,
    Ready,
    Streaming,
    Stopping,
    Error
};

struct StreamingInitResult
{
    bool success = false;
    bool smokePassed = false;
    qint64 elapsedMs = 0;
    QString reason;
    QString source;
};

QString streamingModeName(StreamingMode mode)
{
    switch (mode)
    {
    case StreamingMode::Auto:
        return "auto";
    case StreamingMode::ForceOn:
        return "force_on";
    case StreamingMode::ForceOff:
        return "force_off";
    }
    return "auto";
}

QString streamingStateName(StreamingState state)
{
    switch (state)
    {
    case StreamingState::Disabled:
        return "Disabled";
    case StreamingState::Uninitialized:
        return "Uninitialized";
    case StreamingState::Initializing:
        return "Initializing";
    case StreamingState::Ready:
        return "Ready";
    case StreamingState::Streaming:
        return "Streaming";
    case StreamingState::Stopping:
        return "Stopping";
    case StreamingState::Error:
        return "Error";
    }
    return "Unknown";
}

StreamingMode resolveStreamingMode(QString *rawEnv)
{
    const QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    if (!env.contains("VOICEFLOW_ENABLE_STREAMING_ASR"))
    {
        if (rawEnv)
        {
            *rawEnv = "<unset>";
        }
        return StreamingMode::Auto;
    }

    const QString raw = env.value("VOICEFLOW_ENABLE_STREAMING_ASR").trimmed();
    if (rawEnv)
    {
        *rawEnv = raw.isEmpty() ? "<empty>" : raw;
    }

    const QString value = raw.toLower();
    if (value == "1" || value == "true" || value == "yes" || value == "on")
    {
        return StreamingMode::ForceOn;
    }
    if (value == "0" || value == "false" || value == "no" || value == "off")
    {
        return StreamingMode::ForceOff;
    }

    qWarning() << "[STREAM] unknown VOICEFLOW_ENABLE_STREAMING_ASR value:" << raw
               << "use auto mode";
    return StreamingMode::Auto;
}

bool isStreamingSmokeProbeMode(const QStringList &arguments)
{
    return arguments.contains("--voiceflow-streaming-smoke-probe");
}

int runStreamingSmokeProbeProcess()
{
    SherpaOnnxStreamingAsrEngine probeEngine;
    QString reason;
    qWarning() << "[STREAM][PROBE] start";
    if (!probeEngine.isAvailable(&reason))
    {
        qWarning() << "[STREAM][PROBE] unavailable:" << reason;
        return 2;
    }
    if (!probeEngine.initialize(&reason))
    {
        qWarning() << "[STREAM][PROBE] init failed:" << reason;
        return 3;
    }
    if (!probeEngine.runSmokeTest(&reason))
    {
        qWarning() << "[STREAM][PROBE] smoke failed:" << reason;
        return 4;
    }
    qWarning() << "[STREAM][PROBE] passed";
    return 0;
}

bool runStreamingSmokeProbeOutOfProcess(QString *reason)
{
    QProcess process;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("VOICEFLOW_ENABLE_STREAMING_ASR", "1");
    process.setProcessEnvironment(env);
    process.setProgram(QCoreApplication::applicationFilePath());
    process.setArguments(QStringList() << "--voiceflow-streaming-smoke-probe");
    process.setProcessChannelMode(QProcess::MergedChannels);

    qWarning() << "[STREAM] smoke probe subprocess start";
    process.start();
    if (!process.waitForStarted(5000))
    {
        if (reason)
        {
            *reason = "streaming smoke probe 子进程启动失败：" + process.errorString();
        }
        return false;
    }

    if (!process.waitForFinished(90000))
    {
        process.kill();
        process.waitForFinished(3000);
        if (reason)
        {
            *reason = "streaming smoke probe 超时，已回退离线识别";
        }
        return false;
    }

    const QString output = QString::fromLocal8Bit(process.readAll());
    qWarning().noquote() << output.trimmed();

    if (process.exitStatus() != QProcess::NormalExit)
    {
        if (reason)
        {
            *reason = "streaming smoke probe 子进程崩溃，已回退离线识别";
        }
        return false;
    }

    if (process.exitCode() != 0)
    {
        if (reason)
        {
            *reason = QString("streaming smoke probe 失败，exitCode=%1").arg(process.exitCode());
            if (!output.trimmed().isEmpty())
            {
                *reason += "，日志：" + output.trimmed().right(500);
            }
        }
        return false;
    }

    if (reason)
    {
        *reason = "streaming smoke probe 通过";
    }
    qWarning() << "[STREAM] smoke probe subprocess passed";
    return true;
}
}

int main(int argc, char *argv[])
{
    QStringList rawArguments;
    for (int i = 0; i < argc; ++i)
    {
        rawArguments << QString::fromLocal8Bit(argv[i]);
    }
    if (isStreamingSmokeProbeMode(rawArguments))
    {
        QCoreApplication probeApp(argc, argv);
        qRegisterMetaType<AsrResult>("AsrResult");
        qRegisterMetaType<AudioChunkInfo>("AudioChunkInfo");
        return runStreamingSmokeProbeProcess();
    }

    QApplication app(argc, argv);
    qRegisterMetaType<AsrResult>("AsrResult");
    qRegisterMetaType<AudioChunkInfo>("AudioChunkInfo");

    MainWindow window;
    AudioRecorder recorder;
    AsrBackendManager asrManager;
    SherpaOnnxStreamingAsrEngine streamingAsr;
    FfmpegAudioConverter ffmpegConverter;
    AppConfig appConfig;
    DeepSeekClient deepSeekClient;

    bool filePreparationRunning = false;
    bool fileTranscriptionRunning = false;
    QString pendingSourceFilePath;
    QString activeFileSourcePath;
    QString activeFileTranscribeWavPath;
    QString activeFileTraceId;
    QString latestSavedSourceFilePath;
    TaskConfig pendingFileTaskConfig;
    bool streamingRealtimeActive = false;
    bool streamingLastSegmentHadFinal = false;
    bool streamingSmokeTestDone = false;
    bool streamingStartPending = false;
    bool streamingStartCancelled = false;
    bool streamingFirstAudioFrameLogged = false;
    bool streamingFirstPartialLogged = false;
    int streamingSessionCounter = 0;
    qint64 streamingStartClickedAtMs = -1;
    StreamingState streamingState = StreamingState::Uninitialized;
    QFutureWatcher<StreamingInitResult> streamingInitWatcher;

    enum class AiAction
    {
        None,
        TestConnection,
        Optimize,
        CustomOptimize
    };
    AiAction currentAiAction = AiAction::None;

    auto applyAiConfigToClient = [&]() -> bool
    {
        QString error;
        if (!appConfig.ensureConfigFile(&error))
        {
            window.setRunningStatus("配置文件创建失败：" + error);
            return false;
        }
        if (!appConfig.load(&error))
        {
            window.setRunningStatus("配置文件读取失败：" + error);
            return false;
        }

        DeepSeekConfig cfg = appConfig.deepSeekConfig();
        deepSeekClient.setConfig(cfg);

        QString reason;
        const bool configured = deepSeekClient.isConfigured(&reason);
        if (configured)
        {
            const QString envKey = QProcessEnvironment::systemEnvironment().value("DEEPSEEK_API_KEY").trimmed();
            if (!envKey.isEmpty())
            {
                window.setAiConfigured(true, "DeepSeek(ENV)");
            }
            else
            {
                window.setAiConfigured(true, maskedProviderText(cfg.apiKey));
            }
        }
        else
        {
            window.setAiConfigured(false, "");
        }
        return true;
    };

    auto openAiConfigDialog = [&]()
    {
        QString err;
        appConfig.ensureConfigFile(&err);
        appConfig.load(&err);

        const bool envExists = !QProcessEnvironment::systemEnvironment().value("DEEPSEEK_API_KEY").trimmed().isEmpty();
        AiConfigDialog dialog(appConfig.deepSeekConfig(), envExists, &window);
        if (dialog.exec() != QDialog::Accepted)
        {
            return;
        }

        appConfig.setDeepSeekConfig(dialog.config());
        if (!appConfig.save(&err))
        {
            QMessageBox::warning(&window, "保存失败", "AI 配置保存失败：" + err);
            return;
        }

        applyAiConfigToClient();
        window.setRunningStatus("AI 配置已保存。");
    };

    applyAiConfigToClient();

    auto startElapsedMs = [&]() -> qint64
    {
        return streamingStartClickedAtMs >= 0 ? PerfTracer::nowMs() - streamingStartClickedAtMs : 0;
    };

    auto logStartWarning = [](const QString &message, qint64 elapsedMs)
    {
        qWarning() << "[STREAM][START][WARN]" << message << "elapsed=" << elapsedMs << "ms";
    };

    auto startAudioRecorder = [&](const QString &sessionId, bool streamingEnabled, const QString &fallbackReason) -> bool
    {
        const qint64 elapsed = startElapsedMs();
        qWarning() << "[STREAM][START] audio recorder start elapsed=" << elapsed << "ms";
        if (streamingStartClickedAtMs >= 0 && elapsed > 300)
        {
            logStartWarning("start button clicked to audio recorder start exceeded 300ms", elapsed);
        }

        streamingRealtimeActive = streamingEnabled;
        const bool started = recorder.startRecording(sessionId);
        if (!started)
        {
            if (streamingEnabled)
            {
                streamingAsr.discardSession();
            }
            streamingRealtimeActive = false;
            streamingLastSegmentHadFinal = false;
            streamingState = streamingEnabled ? StreamingState::Ready : streamingState;
            return false;
        }

        if (streamingEnabled)
        {
            streamingState = StreamingState::Streaming;
            window.setAsrBackendText("sherpa-onnx streaming 实时识别");
            window.setRunningStatus("Streaming 实时识别已启动。");
            qWarning() << "[STREAM] streaming init success";
            qWarning() << "[STREAM] fallback offline=false";
            PerfTracer::markTrace("STREAM", "realtime", "streaming_runtime_enabled",
                                  "backend=streaming, fallback_offline=false");
        }
        else
        {
            window.setAsrBackendText("sherpa-onnx / Paraformer 本地离线识别");
            window.setRunningStatus(fallbackReason.trimmed().isEmpty()
                                        ? "Streaming 不可用，已回退离线识别。"
                                        : "Streaming 不可用，已回退离线识别。");
            qWarning() << "[STREAM] fallback offline=true reason=" << fallbackReason;
            PerfTracer::markTrace("ASR", "realtime", "fallback_backend",
                                  QString("backend=offline-exe, reason=%1").arg(fallbackReason));
        }
        return true;
    };

    auto startOfflineRealtime = [&](const QString &reason)
    {
        streamingStartPending = false;
        streamingStartCancelled = false;
        streamingRealtimeActive = false;
        streamingLastSegmentHadFinal = false;
        ++streamingSessionCounter;
        startAudioRecorder(QString("recording_%1").arg(streamingSessionCounter), false, reason);
    };

    auto startStreamingSession = [&]() -> bool
    {
        if (!streamingAsr.isInitialized())
        {
            startOfflineRealtime("streaming recognizer 尚未初始化");
            return false;
        }

        window.clearStreamingPreview();
        streamingLastSegmentHadFinal = false;
        streamingFirstAudioFrameLogged = false;
        streamingFirstPartialLogged = false;
        ++streamingSessionCounter;

        QElapsedTimer streamTimer;
        streamTimer.start();
        qWarning() << "[STREAM][START] create stream start";
        streamingAsr.startSession();
        const qint64 streamMs = streamTimer.elapsed();
        qWarning() << "[STREAM][START] create stream done elapsed=" << streamMs << "ms";

        streamingRealtimeActive = streamingAsr.isSessionActive();
        if (!streamingRealtimeActive)
        {
            streamingState = StreamingState::Error;
            startOfflineRealtime("streaming session 未能启动");
            return false;
        }

        streamingStartPending = false;
        streamingStartCancelled = false;
        const bool started = startAudioRecorder(QString("recording_%1").arg(streamingSessionCounter), true, QString());
        qWarning() << "[STREAM][LIFE] state after start=" << streamingStateName(streamingState)
                   << "elapsed=" << startElapsedMs() << "ms";
        return started;
    };

    auto startStreamingInitAsync = [&](const QString &source)
    {
        if (streamingInitWatcher.isRunning())
        {
            streamingState = StreamingState::Initializing;
            qWarning() << "[STREAM][START] init recognizer already running source=" << source;
            return;
        }
        if (streamingAsr.isInitialized())
        {
            streamingState = StreamingState::Ready;
            return;
        }

        streamingState = StreamingState::Initializing;
        const bool smokeAlreadyDone = streamingSmokeTestDone;
        qWarning() << (source == "start" ? "[STREAM][START]" : "[STREAM][PRELOAD]")
                   << "init recognizer start";

        streamingInitWatcher.setFuture(QtConcurrent::run([&streamingAsr, smokeAlreadyDone, source]() -> StreamingInitResult
        {
            StreamingInitResult result;
            result.source = source;
            QElapsedTimer initTimer;
            initTimer.start();
            QString reason;
            try
            {
                if (!streamingAsr.isAvailable(&reason))
                {
                    result.reason = reason;
                    result.elapsedMs = initTimer.elapsed();
                    return result;
                }
                if (!streamingAsr.initialize(&reason))
                {
                    result.reason = reason;
                    result.elapsedMs = initTimer.elapsed();
                    return result;
                }
                if (!smokeAlreadyDone && !streamingAsr.runSmokeTest(&reason))
                {
                    result.reason = reason;
                    result.elapsedMs = initTimer.elapsed();
                    return result;
                }

                result.success = true;
                result.smokePassed = true;
                result.reason = "streaming recognizer ready";
            }
            catch (const std::exception &ex)
            {
                result.reason = QString("streaming 初始化异常：%1").arg(ex.what());
            }
            catch (...)
            {
                result.reason = "streaming 初始化出现未知异常";
            }
            result.elapsedMs = initTimer.elapsed();
            return result;
        }));
    };

    QObject::connect(&streamingInitWatcher, &QFutureWatcher<StreamingInitResult>::finished,
                     &window, [&]()
                     {
                         const StreamingInitResult result = streamingInitWatcher.result();
                         const bool startWasWaiting = streamingStartPending && !streamingStartCancelled;
                         qWarning() << (startWasWaiting ? "[STREAM][START]" : "[STREAM][PRELOAD]")
                                    << "init recognizer done elapsed=" << result.elapsedMs << "ms"
                                    << "success=" << result.success
                                    << "reason=" << result.reason;
                         if (result.elapsedMs > 500)
                         {
                             logStartWarning("recognizer initialization exceeded 500ms", result.elapsedMs);
                         }

                         if (result.success)
                         {
                             streamingSmokeTestDone = streamingSmokeTestDone || result.smokePassed;
                             streamingState = StreamingState::Ready;
                             if (startWasWaiting)
                             {
                                 startStreamingSession();
                             }
                             return;
                         }

                         streamingState = StreamingState::Error;
                         PerfTracer::markTrace("STREAM", "realtime", "streaming_backend_init_fail", result.reason);
                         if (startWasWaiting)
                         {
                             window.setRunningStatus("Streaming 不可用，已回退离线识别。");
                             startOfflineRealtime(result.reason);
                         }
                     });

    QObject::connect(&window, &MainWindow::startVoiceInputRequested,
                     &recorder, [&](const TaskConfig &config)
                     {
                         {
                             Q_UNUSED(config);
                             latestSavedSourceFilePath.clear();
                             streamingStartClickedAtMs = PerfTracer::nowMs();
                             streamingFirstAudioFrameLogged = false;
                             streamingFirstPartialLogged = false;
                             streamingStartCancelled = false;
                             window.setRunningStatus("正在启动实时语音输入...");

                             QElapsedTimer startTimer;
                             startTimer.start();
                             qWarning() << "[STREAM][START] start button clicked";
                             qWarning() << "[STREAM][LIFE] state before start=" << streamingStateName(streamingState);

                             QString streamingEnvText;
                             const StreamingMode streamingMode = resolveStreamingMode(&streamingEnvText);
                             const bool tryStreaming = streamingMode != StreamingMode::ForceOff;
                             const bool recognizerReady = streamingState != StreamingState::Initializing &&
                                                          streamingAsr.isInitialized();
                             qWarning() << "[STREAM][START] resolve mode done elapsed=" << startTimer.elapsed() << "ms"
                                        << "env=" << streamingEnvText
                                        << "mode=" << streamingModeName(streamingMode)
                                        << "try_streaming=" << tryStreaming;
                             qWarning() << "[STREAM][START] recognizer ready=" << (recognizerReady ? "true" : "false");
                             PerfTracer::markTrace("STREAM", "realtime", "streaming_mode_resolved",
                                                   QString("env=%1, mode=%2, try_streaming=%3")
                                                       .arg(streamingEnvText,
                                                            streamingModeName(streamingMode),
                                                            tryStreaming ? "true" : "false"));

                             streamingRealtimeActive = false;
                             streamingLastSegmentHadFinal = false;

                             if (streamingState == StreamingState::Streaming)
                             {
                                 window.setRunningStatus("正在识别中");
                                 qWarning() << "[STREAM][LIFE] start ignored state=" << streamingStateName(streamingState);
                                 return;
                             }

                             if (!tryStreaming)
                             {
                                 streamingState = StreamingState::Disabled;
                                 startOfflineRealtime("Streaming 已关闭，使用离线识别");
                                 return;
                             }

                             if (recognizerReady)
                             {
                                 streamingState = StreamingState::Ready;
                                 startStreamingSession();
                                 return;
                             }

                             if (streamingState == StreamingState::Initializing || streamingInitWatcher.isRunning())
                             {
                                 streamingState = StreamingState::Initializing;
                                 streamingStartPending = true;
                                 window.setRunningStatus("Streaming 正在初始化...");
                                 qWarning() << "[STREAM][START] init recognizer already running";
                                 return;
                             }

                             if (streamingState == StreamingState::Error)
                             {
                                 startOfflineRealtime("Streaming 初始化失败，使用离线识别");
                                 return;
                             }

                             streamingStartPending = true;
                             window.setRunningStatus("Streaming 正在初始化...");
                             startStreamingInitAsync("start");
                         }
                     });

    QObject::connect(&window, &MainWindow::stopVoiceInputRequested,
                     &recorder, [&]()
                     {
                         QElapsedTimer stopTimer;
                         stopTimer.start();
                         qWarning() << "[STREAM][LIFE] stop clicked";
                         qWarning() << "[STREAM][LIFE] state before stop=" << streamingStateName(streamingState);
                         const bool wasStreamingActive = streamingRealtimeActive;
                         const bool wasInitializing = streamingState == StreamingState::Initializing;
                         streamingStartPending = false;
                         streamingStartCancelled = true;
                         streamingRealtimeActive = false;
                         streamingState = wasStreamingActive ? StreamingState::Stopping : streamingState;
                         QElapsedTimer recorderTimer;
                         recorderTimer.start();
                         recorder.stopRecording();
                         qWarning() << "[STREAM][LIFE] stop recorder elapsed=" << recorderTimer.elapsed() << "ms";
                         if (wasStreamingActive)
                         {
                             if (!streamingLastSegmentHadFinal)
                             {
                                 streamingLastSegmentHadFinal = window.commitStreamingPartialAsFinal("streaming-stop-partial");
                             }
                             qWarning() << "[STREAM][LIFE] finish stream async skipped, discard current stream";
                             streamingAsr.discardSession();
                             streamingState = StreamingState::Ready;
                             streamingLastSegmentHadFinal = false;
                         }
                         else if (!recorder.isRecording() && wasInitializing)
                         {
                             window.setRunningStatus("Streaming 初始化已在后台继续，已取消本次启动。");
                         }
                         qWarning() << "[STREAM][LIFE] state after stop=" << streamingStateName(streamingState)
                                    << "elapsed=" << stopTimer.elapsed() << "ms";
                         if (stopTimer.elapsed() > 300)
                         {
                             qWarning() << "[STREAM][WARN] stop took" << stopTimer.elapsed() << "ms";
                         }
                     });

    QObject::connect(&window, &MainWindow::fileTranscribeRequested,
                     &window, [&window,
                               &asrManager,
                               &ffmpegConverter,
                               &filePreparationRunning,
                               &fileTranscriptionRunning,
                               &pendingSourceFilePath,
                               &activeFileSourcePath,
                               &activeFileTranscribeWavPath,
                               &activeFileTraceId,
                               &pendingFileTaskConfig](const QString &filePath, const TaskConfig &config)
                     {
                         if (filePath.trimmed().isEmpty())
                         {
                             window.setRunningStatus("请先选择文件");
                             return;
                         }

                         if (!QFileInfo::exists(filePath))
                         {
                             window.setRunningStatus("选择的文件不存在，请重新选择");
                             return;
                         }

                         if (filePreparationRunning || fileTranscriptionRunning)
                         {
                             window.setRunningStatus("文件转写正在进行，请稍候");
                             return;
                         }

                         if (!isSupportedMediaFile(filePath))
                         {
                             window.setRunningStatus("当前文件格式暂不支持");
                             return;
                         }

                         pendingSourceFilePath = filePath;
                         pendingFileTaskConfig = config;
                         const QFileInfo selectedInfo(filePath);
                         activeFileTraceId = "file-" + QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss-zzz");
                         PerfTracer::startTrace("FILE", activeFileTraceId, "file_selected",
                                                QString("name=%1, suffix=%2, size=%3 MB")
                                                    .arg(selectedInfo.fileName(), selectedInfo.suffix().toLower())
                                                    .arg(selectedInfo.size() / 1024.0 / 1024.0, 0, 'f', 2));
                         PerfTracer::markTrace("FILE", activeFileTraceId, "file_prepare_start");
                         window.setOptimizedText("");
                         window.setFileTranscriptionBusy(true);

                         if (isWavFile(filePath))
                         {
                             fileTranscriptionRunning = true;
                             activeFileSourcePath = filePath;
                             activeFileTranscribeWavPath = filePath;
                             pendingSourceFilePath.clear();
                             window.setRunningStatus("正在转写本地文件...");
                             PerfTracer::markTrace("FILE", activeFileTraceId, "file_is_wav_skip_convert");
                             PerfTracer::markTrace("FILE", activeFileTraceId, "file_asr_start");
                             asrManager.transcribeAsync(filePath, config, activeFileTraceId);
                             return;
                         }

                         QString ffmpegReason;
                         if (!ffmpegConverter.isAvailable(&ffmpegReason))
                         {
                             window.setRunningStatus("文件转换失败：" + ffmpegReason);
                             window.setFileTranscriptionBusy(false);
                             return;
                         }

                         const QString convertedWavPath = buildConvertedWavPath(filePath);
                         filePreparationRunning = true;
                         window.setRunningStatus("正在提取音频并转换格式...");
                         ffmpegConverter.convertToWavAsync(filePath, convertedWavPath, activeFileTraceId);
                     });

    QObject::connect(&ffmpegConverter, &FfmpegAudioConverter::conversionStarted,
                     &window, [&window]()
                     {
                         window.setRunningStatus("正在提取音频并转换格式...");
                     });

    QObject::connect(&ffmpegConverter, &FfmpegAudioConverter::conversionFinished,
                     &window, [&window,
                               &asrManager,
                               &filePreparationRunning,
                               &fileTranscriptionRunning,
                               &pendingSourceFilePath,
                               &activeFileSourcePath,
                               &activeFileTranscribeWavPath,
                               &activeFileTraceId,
                               &pendingFileTaskConfig](const QString &outputWavPath)
                     {
                         filePreparationRunning = false;

                         if (!QFileInfo::exists(outputWavPath))
                         {
                             fileTranscriptionRunning = false;
                             window.setFileTranscriptionBusy(false);
                             window.setRunningStatus("文件转换失败：未生成可用 WAV 文件");
                             return;
                         }

                         activeFileSourcePath = pendingSourceFilePath;
                         pendingSourceFilePath.clear();
                         activeFileTranscribeWavPath = outputWavPath;
                         fileTranscriptionRunning = true;
                         window.setRunningStatus("音频预处理完成，开始转写...");
                         PerfTracer::markTrace("FILE", activeFileTraceId, "file_asr_start",
                                               QString("output=%1").arg(QFileInfo(outputWavPath).fileName()));
                         asrManager.transcribeAsync(outputWavPath, pendingFileTaskConfig, activeFileTraceId);
                     });

    QObject::connect(&ffmpegConverter, &FfmpegAudioConverter::conversionFailed,
                     &window, [&window,
                               &filePreparationRunning,
                               &fileTranscriptionRunning,
                               &pendingSourceFilePath,
                               &activeFileSourcePath,
                               &activeFileTranscribeWavPath,
                               &activeFileTraceId](const QString &errorMessage)
                     {
                         filePreparationRunning = false;
                         fileTranscriptionRunning = false;
                         pendingSourceFilePath.clear();
                         activeFileSourcePath.clear();
                         activeFileTranscribeWavPath.clear();
                         PerfTracer::markTrace("FILE", activeFileTraceId, "ffmpeg_failed");
                         activeFileTraceId.clear();
                         window.setFileTranscriptionBusy(false);
                         window.setRunningStatus("文件转换失败：" + errorMessage);
                     });

    QObject::connect(&recorder, &AudioRecorder::recordingStarted,
                     &window, [&window]()
                     {
                         window.setRunningStatus("正在监听，请开始说话...");
                     });

    QObject::connect(&recorder, &AudioRecorder::recordingStopped,
                     &window, [](const QString &wavPath)
                     {
                         qDebug() << "Full recording saved:" << wavPath;
                     });

    QObject::connect(&recorder, &AudioRecorder::vadStateChanged,
                     &window, [&window](const QString &stateText)
                     {
                         window.setRunningStatus(stateText);
                     });

    QObject::connect(&recorder, &AudioRecorder::streamingAudioFrameReady,
                     &streamingAsr, [&](const QByteArray &pcm16MonoData,
                                         int sampleRate,
                                         int channels,
                                         int bitsPerSample,
                                         qint64 frameStartMs)
                     {
                         if (!streamingRealtimeActive)
                         {
                             return;
                         }
                         if (!streamingFirstAudioFrameLogged)
                         {
                             streamingFirstAudioFrameLogged = true;
                             const qint64 elapsed = startElapsedMs();
                             qWarning() << "[STREAM][START] first audio frame received elapsed=" << elapsed << "ms";
                             if (streamingStartClickedAtMs >= 0 && elapsed > 1000)
                             {
                                 logStartWarning("first audio frame exceeded 1000ms", elapsed);
                             }
                         }
                         streamingAsr.acceptAudioFrame(pcm16MonoData, sampleRate, channels, bitsPerSample, frameStartMs);
                     });

    QObject::connect(&recorder, &AudioRecorder::speechSegmentEnded,
                     &streamingAsr, [&window,
                                     &streamingAsr,
                                     &streamingRealtimeActive,
                                     &streamingLastSegmentHadFinal](const QString &traceId, qint64 speechEndMs)
                     {
                         Q_UNUSED(traceId);
                         Q_UNUSED(speechEndMs);
                         if (!streamingRealtimeActive)
                         {
                             return;
                         }
                         streamingLastSegmentHadFinal = false;
                         streamingAsr.finishSession();
                         if (!streamingLastSegmentHadFinal)
                         {
                             streamingLastSegmentHadFinal = window.commitStreamingPartialAsFinal(traceId);
                         }
                         streamingAsr.resetSession();
                         streamingAsr.startSession();
                     });

    QObject::connect(&streamingAsr, &SherpaOnnxStreamingAsrEngine::partialResultReady,
                     &window, [&](const QString &text)
                     {
                         if (!streamingRealtimeActive)
                         {
                             qWarning() << "[STREAM][LIFE] ignore stale partial";
                             return;
                         }
                         if (!streamingFirstPartialLogged)
                         {
                             streamingFirstPartialLogged = true;
                             const qint64 elapsed = startElapsedMs();
                             qWarning() << "[STREAM][START] first partial result elapsed=" << elapsed << "ms";
                             if (streamingStartClickedAtMs >= 0 && elapsed > 1500)
                             {
                                 logStartWarning("first partial result exceeded 1500ms", elapsed);
                             }
                         }
                         window.onStreamingPartialResult(text);
                         window.setRunningStatus("正在识别：" + text.left(32));
                     });

    QObject::connect(&streamingAsr, &SherpaOnnxStreamingAsrEngine::finalResultReady,
                     &window, [&window, &streamingRealtimeActive, &streamingLastSegmentHadFinal](const AsrResult &result)
                     {
                         if (!streamingRealtimeActive)
                         {
                             qWarning() << "[STREAM][LIFE] ignore stale final";
                             return;
                         }
                         if (!result.success)
                         {
                             return;
                         }
                         streamingLastSegmentHadFinal = !result.text.trimmed().isEmpty();
                         window.onStreamingFinalResult(result);
                         window.setLastAsrTime(result.elapsedMs);
                     });

    QObject::connect(&streamingAsr, &SherpaOnnxStreamingAsrEngine::streamingError,
                     &window, [&window,
                               &streamingRealtimeActive,
                               &streamingLastSegmentHadFinal,
                               &streamingState](const QString &errorMessage)
                     {
                          qDebug() << "[STREAM] unavailable:" << errorMessage;
                          streamingRealtimeActive = false;
                          streamingLastSegmentHadFinal = false;
                          streamingState = StreamingState::Error;
                         window.setRunningStatus("streaming 后端不可用，已使用离线分段识别");
                     });

    QObject::connect(&recorder, &AudioRecorder::sentenceChunkReady,
                     &window, [&window,
                               &asrManager,
                               &streamingRealtimeActive,
                               &streamingLastSegmentHadFinal](const AudioChunkInfo &info)
                     {
                         if (streamingRealtimeActive && streamingLastSegmentHadFinal)
                         {
                             PerfTracer::markTrace("ASR", info.traceId, "offline_chunk_skipped", "backend=streaming");
                             return;
                         }
                         PerfTracer::markTrace("ASR", info.traceId, "asr_task_created",
                                               QString("duration=%1 ms, sequence=%2")
                                                   .arg(info.durationMs)
                                                   .arg(info.sequenceId));
                         const QString fileName = QFileInfo(info.wavPath).fileName();
                         window.setRunningStatus(
                             QString("已保存第 %1 个语音片段：%2（%3 ms，%4）")
                                 .arg(info.chunkIndex)
                                 .arg(fileName)
                                 .arg(info.durationMs)
                                 .arg(info.splitReason));
                         qDebug() << "Sentence chunk saved:" << info.wavPath
                                  << "durationMs=" << info.durationMs
                                  << "splitReason=" << info.splitReason;
                         asrManager.transcribeAsync(info, window.currentTaskConfig());
                     });

    QObject::connect(&asrManager, &AsrBackendManager::transcribeStarted,
                     &window, [&window, &fileTranscriptionRunning, &activeFileTranscribeWavPath](const QString &wavPath)
                     {
                         if (fileTranscriptionRunning && wavPath == activeFileTranscribeWavPath)
                         {
                             window.setRunningStatus("正在转写本地文件...");
                             return;
                         }

                         window.setRunningStatus("正在识别语音片段...");
                     });

    QObject::connect(&asrManager, &AsrBackendManager::transcribeFinished,
                     &window, [&window,
                               &filePreparationRunning,
                               &fileTranscriptionRunning,
                               &activeFileSourcePath,
                               &activeFileTranscribeWavPath,
                               &activeFileTraceId,
                               &latestSavedSourceFilePath](const AsrResult &result)
                     {
                         if (!result.success)
                         {
                             if (fileTranscriptionRunning && result.wavPath == activeFileTranscribeWavPath)
                             {
                                 filePreparationRunning = false;
                                 fileTranscriptionRunning = false;
                                 activeFileSourcePath.clear();
                                 activeFileTranscribeWavPath.clear();
                                 PerfTracer::markTrace("FILE", activeFileTraceId, "file_asr_failed");
                                 activeFileTraceId.clear();
                                 window.setFileTranscriptionBusy(false);
                             }
                             return;
                         }

                         window.handleAsrResult(result);
                         window.setLastAsrTime(result.elapsedMs);

                         if (fileTranscriptionRunning && result.wavPath == activeFileTranscribeWavPath)
                         {
                             latestSavedSourceFilePath = activeFileSourcePath;
                             filePreparationRunning = false;
                             fileTranscriptionRunning = false;
                             activeFileSourcePath.clear();
                             activeFileTranscribeWavPath.clear();
                             PerfTracer::markTrace("FILE", activeFileTraceId, "ui_updated",
                                                   QString("total_asr_elapsed=%1 ms").arg(result.elapsedMs));
                             activeFileTraceId.clear();
                             window.setFileTranscriptionBusy(false);
                             window.setRunningStatus(QString("文件转写完成，耗时 %1 ms").arg(result.elapsedMs));
                             return;
                         }

                         window.setRunningStatus(QString("识别完成，耗时 %1 ms").arg(result.elapsedMs));
                     });

    QObject::connect(&asrManager, &AsrBackendManager::transcribeError,
                     &window, [&window,
                               &filePreparationRunning,
                               &fileTranscriptionRunning,
                               &activeFileSourcePath,
                               &activeFileTranscribeWavPath,
                               &activeFileTraceId](const QString &wavPath, const QString &errorMessage)
                     {
                         if (fileTranscriptionRunning && wavPath == activeFileTranscribeWavPath)
                         {
                             filePreparationRunning = false;
                             fileTranscriptionRunning = false;
                             activeFileSourcePath.clear();
                             activeFileTranscribeWavPath.clear();
                             PerfTracer::markTrace("FILE", activeFileTraceId, "file_asr_error");
                             activeFileTraceId.clear();
                             window.setFileTranscriptionBusy(false);
                             window.setRunningStatus("文件转写失败：" + errorMessage);
                             qDebug() << "[ASR] file transcribe error:" << errorMessage;
                             return;
                         }

                         window.setRunningStatus("识别错误：" + errorMessage);
                         qDebug() << "[ASR] transcribeError:" << errorMessage;
                     });

    QObject::connect(&recorder, &AudioRecorder::recordingWarning,
                     &window, [&window](const QString &warningMessage)
                     {
                         window.setRunningStatus(warningMessage);
                     });

    QObject::connect(&recorder, &AudioRecorder::recordingError,
                     &window, [&window](const QString &errorMessage)
                     {
                         window.setRunningStatus("录音错误：" + errorMessage);
                         QMessageBox::warning(&window, "录音错误", errorMessage);
                     });

    QObject::connect(&window, &MainWindow::clearRequested,
                     &window, [&latestSavedSourceFilePath]()
                     {
                         latestSavedSourceFilePath.clear();
                     });

    QObject::connect(&window, &MainWindow::saveRecordRequested,
                     &window, [&window,
                               &appConfig,
                               &latestSavedSourceFilePath](const QString &rawText, const QString &optimizedText)
                     {
                         if (rawText.trimmed().isEmpty() && optimizedText.trimmed().isEmpty())
                         {
                             return;
                         }

                         HistoryRecord record;
                         record.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
                         record.createdAt = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
                         record.sourceType = latestSavedSourceFilePath.trimmed().isEmpty() ? "realtime" : "file";
                         record.sourceFile = latestSavedSourceFilePath;
                         record.rawText = rawText;
                         record.optimizedText = optimizedText;
                         record.model = "sherpa-onnx / Paraformer";
                         record.aiModel = optimizedText.trimmed().isEmpty() ? QString() : appConfig.deepSeekConfig().model.trimmed();

                         const QString timeTag = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm");
                         if (record.sourceType == "file")
                         {
                             record.title = QString("本地文件转写 %1 %2")
                                                .arg(QFileInfo(record.sourceFile).fileName(), timeTag);
                         }
                         else
                         {
                             record.title = "实时语音输入 " + timeTag;
                         }

                         HistoryManager historyManager;
                         QString loadError;
                         if (!historyManager.load(&loadError))
                         {
                             qWarning() << "[History] load before add failed:" << loadError;
                         }

                         QString error;
                         if (!historyManager.addRecord(record, &error))
                         {
                             window.setRunningStatus("文件已保存，但写入历史记录失败：" + error);
                             return;
                         }

                         window.setRunningStatus("已保存到 results，并写入历史记录");
                     });

    QObject::connect(&window, &MainWindow::openSettingsRequested, &window, [&]()
                     { openAiConfigDialog(); });

    QTimer::singleShot(500, &window, [&]()
                       {
                           QString streamingEnvText;
                           const StreamingMode streamingMode = resolveStreamingMode(&streamingEnvText);
                           if (streamingMode == StreamingMode::ForceOff)
                           {
                               streamingState = StreamingState::Disabled;
                               qWarning() << "[STREAM][PRELOAD] skipped because streaming is disabled";
                               return;
                           }
                           if (streamingState == StreamingState::Initializing || streamingInitWatcher.isRunning())
                           {
                               qWarning() << "[STREAM][PRELOAD] skipped because init is already running";
                               return;
                           }
                           if (streamingAsr.isInitialized())
                           {
                               streamingState = StreamingState::Ready;
                               qWarning() << "[STREAM][PRELOAD] recognizer already ready";
                               return;
                           }
                           qWarning() << "[STREAM][PRELOAD] schedule async preload";
                           startStreamingInitAsync("preload");
                       });

    QObject::connect(&window, &MainWindow::testAiConnectionRequested, &window, [&]()
                     {
                         if (!applyAiConfigToClient())
                         {
                             return;
                         }
                         currentAiAction = AiAction::TestConnection;
                         deepSeekClient.testConnectionAsync(); });

    QObject::connect(&window, &MainWindow::aiOptimizeRequested, &window, [&](const QString &rawText)
                     {
                         if (rawText.trimmed().isEmpty())
                         {
                             window.setRunningStatus("没有可优化的文本。");
                             return;
                         }

                         if (!applyAiConfigToClient())
                         {
                             return;
                         }

                         QString reason;
                         if (!deepSeekClient.isConfigured(&reason))
                         {
                             window.setRunningStatus(reason);
                             openAiConfigDialog();
                             return;
                         }

                         currentAiAction = AiAction::Optimize;
                         deepSeekClient.optimizeTextAsync(rawText, "polish"); });

    QObject::connect(&window, &MainWindow::customPromptOptimizeRequested, &window, [&](const QString &rawText)
                     {
                         if (rawText.trimmed().isEmpty())
                         {
                             window.setRunningStatus("没有可优化的文本。");
                             return;
                         }

                         if (!applyAiConfigToClient())
                         {
                             return;
                         }

                         QString reason;
                         if (!deepSeekClient.isConfigured(&reason))
                         {
                             window.setRunningStatus(reason);
                             openAiConfigDialog();
                             return;
                         }

                         CustomPromptDialog dialog(&window);
                         if (dialog.exec() != QDialog::Accepted)
                         {
                             return;
                         }

                         const QString customPrompt = dialog.promptText();
                         if (customPrompt.trimmed().isEmpty())
                         {
                             window.setRunningStatus("请输入自定义提示词。");
                             return;
                         }

                         currentAiAction = AiAction::CustomOptimize;
                         window.setRunningStatus("正在根据自定义提示词优化文本...");
                         deepSeekClient.optimizeTextWithCustomPromptAsync(rawText, customPrompt); });

    QObject::connect(&deepSeekClient, &DeepSeekClient::requestStarted,
                     &window, [&window]()
                     {
                         window.setAiOptimizeBusy(true);
                     });

    QObject::connect(&deepSeekClient, &DeepSeekClient::optimizeFinished,
                     &window, [&](const QString &optimizedText)
                     {
                         const AiAction finishedAction = currentAiAction;
                         currentAiAction = AiAction::None;
                         window.setAiOptimizeBusy(false);
                         window.setOptimizedText(optimizedText);
                         if (finishedAction == AiAction::CustomOptimize)
                         {
                             window.setRunningStatus("自定义优化完成");
                         }
                         else
                         {
                             window.setRunningStatus("AI 优化完成");
                         }
                     });

    QObject::connect(&deepSeekClient, &DeepSeekClient::testConnectionFinished,
                     &window, [&](bool success, const QString &message)
                     {
                         Q_UNUSED(message);
                         if (currentAiAction == AiAction::TestConnection)
                         {
                             currentAiAction = AiAction::None;
                             window.setAiOptimizeBusy(false);
                         }

                         if (success)
                         {
                             window.setAiConfigured(true, "DeepSeek");
                             window.setRunningStatus("AI 连接成功");
                         }
                         else
                         {
                             window.setAiConfigured(false, "");
                             window.setRunningStatus("AI 配置错误");
                         }
                     });

    QObject::connect(&deepSeekClient, &DeepSeekClient::requestError,
                     &window, [&](const QString &errorMessage)
                     {
                         if (currentAiAction == AiAction::Optimize || currentAiAction == AiAction::CustomOptimize || currentAiAction == AiAction::TestConnection)
                         {
                             currentAiAction = AiAction::None;
                             window.setAiOptimizeBusy(false);
                         }

                         window.setRunningStatus("AI 请求失败：" + errorMessage);
                     });

    window.show();
    return app.exec();
}
