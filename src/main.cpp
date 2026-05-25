#include <QApplication>
#include <QDateTime>
#include <QDebug>
#include <QDialog>
#include <QDir>
#include <QFileInfo>
#include <QMessageBox>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStringList>
#include <QUuid>

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

bool isStreamingAsrEnabledByEnv()
{
    const QString value = QProcessEnvironment::systemEnvironment()
                              .value("VOICEFLOW_ENABLE_STREAMING_ASR")
                              .trimmed()
                              .toLower();
    return value == "1" || value == "true" || value == "yes" || value == "on";
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

    QObject::connect(&window, &MainWindow::startVoiceInputRequested,
                     &recorder, [&window,
                                 &recorder,
                                 &streamingAsr,
                                 &streamingRealtimeActive,
                                 &streamingLastSegmentHadFinal,
                                 &latestSavedSourceFilePath](const TaskConfig &config)
                     {
                         Q_UNUSED(config);
                         latestSavedSourceFilePath.clear();
                         QString streamingReason;
                         const bool streamingEnabled = isStreamingAsrEnabledByEnv();
                         qWarning() << "[STREAM] enable_streaming=" << streamingEnabled;
                         PerfTracer::markTrace("STREAM", "realtime", "streaming_enable_check",
                                               QString("enable_streaming=%1").arg(streamingEnabled ? "true" : "false"));

                         streamingRealtimeActive = false;
                         streamingLastSegmentHadFinal = false;
                         if (streamingEnabled)
                         {
                             try
                             {
                                 if (!runStreamingSmokeProbeOutOfProcess(&streamingReason))
                                 {
                                     qWarning() << "[STREAM] smoke probe failed:" << streamingReason;
                                     PerfTracer::markTrace("STREAM", "realtime", "streaming_smoke_probe_failed", streamingReason);
                                 }
                                 else if (!streamingAsr.isAvailable(&streamingReason))
                                 {
                                     qWarning() << "[STREAM] backend unavailable before init:" << streamingReason;
                                     PerfTracer::markTrace("STREAM", "realtime", "streaming_backend_unavailable", streamingReason);
                                 }
                                 else
                                 {
                                     qWarning() << "[STREAM] backend init start";
                                     if (!streamingAsr.initialize(&streamingReason))
                                     {
                                         qWarning() << "[STREAM] backend init fail:" << streamingReason;
                                         PerfTracer::markTrace("STREAM", "realtime", "streaming_backend_init_fail", streamingReason);
                                     }
                                     else
                                     {
                                         qWarning() << "[STREAM] backend init success";
                                         PerfTracer::markTrace("STREAM", "realtime", "streaming_backend_init_success");
                                         qWarning() << "[STREAM] smoke test start";
                                         if (!streamingAsr.runSmokeTest(&streamingReason))
                                         {
                                             qWarning() << "[STREAM] smoke test fail:" << streamingReason;
                                             PerfTracer::markTrace("STREAM", "realtime", "streaming_smoke_test_fail", streamingReason);
                                             streamingRealtimeActive = false;
                                         }
                                         else
                                         {
                                             qWarning() << "[STREAM] smoke test passed";
                                             streamingAsr.startSession();
                                             streamingRealtimeActive = streamingAsr.isSessionActive();
                                             qWarning() << "[STREAM] realtime active=" << streamingRealtimeActive;
                                             PerfTracer::markTrace("STREAM", "realtime", "streaming_realtime_active",
                                                                   QString("active=%1").arg(streamingRealtimeActive ? "true" : "false"));
                                             if (!streamingRealtimeActive)
                                             {
                                                 streamingReason = "streaming session 未能启动";
                                                 qWarning() << "[STREAM] session start fail:" << streamingReason;
                                             }
                                             else
                                             {
                                                 streamingReason = "streaming 后端已启用";
                                             }
                                         }
                                     }
                                 }
                             }
                             catch (const std::exception &ex)
                             {
                                 streamingReason = QString("streaming 初始化异常：%1").arg(ex.what());
                                 qWarning() << "[STREAM] exception:" << streamingReason;
                                 streamingRealtimeActive = false;
                             }
                             catch (...)
                             {
                                 streamingReason = "streaming 初始化出现未知异常";
                                 qWarning() << "[STREAM] unknown exception";
                                 streamingRealtimeActive = false;
                             }
                         }
                         else
                         {
                             streamingReason = "VOICEFLOW_ENABLE_STREAMING_ASR 未开启，默认使用离线识别";
                         }
                         const bool started = recorder.startRecording("recording");
                         if (!started)
                         {
                             if (streamingRealtimeActive)
                             {
                                 streamingAsr.resetSession();
                             }
                             streamingRealtimeActive = false;
                             streamingLastSegmentHadFinal = false;
                             return;
                         }
                         if (streamingRealtimeActive)
                         {
                             window.setRunningStatus("streaming 后端已启用，正在实时识别");
                             qWarning() << "[STREAM] fallback offline=false";
                             PerfTracer::markTrace("STREAM", "realtime", "streaming_runtime_enabled",
                                                   "backend=streaming, fallback_offline=false");
                         }
                         else
                         {
                             const QString statusText = streamingEnabled
                                                            ? streamingReason + "；当前仍使用离线识别"
                                                            : "Streaming 后端未启用，已使用离线分段识别";
                             window.setRunningStatus(statusText);
                             qWarning() << "[STREAM] fallback offline=true reason=" << streamingReason;
                             PerfTracer::markTrace("ASR", "realtime", "fallback_backend",
                                                   QString("backend=offline-exe, reason=%1").arg(streamingReason));
                         }
                     });

    QObject::connect(&window, &MainWindow::stopVoiceInputRequested,
                     &recorder, [&window,
                                 &recorder,
                                 &streamingAsr,
                                 &streamingRealtimeActive,
                                 &streamingLastSegmentHadFinal]()
                     {
                         const bool wasStreamingActive = streamingRealtimeActive;
                         recorder.stopRecording();
                         if (wasStreamingActive)
                         {
                             streamingAsr.finishSession();
                             if (!streamingLastSegmentHadFinal)
                             {
                                 streamingLastSegmentHadFinal = window.commitStreamingPartialAsFinal("streaming-stop-partial");
                             }
                             streamingAsr.resetSession();
                             streamingRealtimeActive = false;
                             streamingLastSegmentHadFinal = false;
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
                         window.setRunningStatus("正在录音，请开始说话");
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
                     &streamingAsr, [&streamingAsr, &streamingRealtimeActive](const QByteArray &pcm16MonoData,
                                                                               int sampleRate,
                                                                               int channels,
                                                                               int bitsPerSample,
                                                                               qint64 frameStartMs)
                     {
                         if (!streamingRealtimeActive)
                         {
                             return;
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
                     &window, [&window](const QString &text)
                     {
                         window.onStreamingPartialResult(text);
                         window.setRunningStatus("正在识别");
                     });

    QObject::connect(&streamingAsr, &SherpaOnnxStreamingAsrEngine::finalResultReady,
                     &window, [&window, &streamingLastSegmentHadFinal](const AsrResult &result)
                     {
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
                               &streamingLastSegmentHadFinal](const QString &errorMessage)
                     {
                         qDebug() << "[STREAM] unavailable:" << errorMessage;
                         streamingRealtimeActive = false;
                         streamingLastSegmentHadFinal = false;
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
