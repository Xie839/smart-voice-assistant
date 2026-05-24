#include <QApplication>
#include <QDateTime>
#include <QDebug>
#include <QDialog>
#include <QDir>
#include <QFileInfo>
#include <QMessageBox>
#include <QProcessEnvironment>
#include <QStringList>
#include <QUuid>

#include "MainWindow.h"
#include "ai/AiConfigDialog.h"
#include "ai/CustomPromptDialog.h"
#include "ai/DeepSeekClient.h"
#include "asr/AsrBackendManager.h"
#include "audio/AudioRecorder.h"
#include "config/AppConfig.h"
#include "history/HistoryManager.h"
#include "media/FfmpegAudioConverter.h"

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
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    qRegisterMetaType<AsrResult>("AsrResult");
    qRegisterMetaType<AudioChunkInfo>("AudioChunkInfo");

    MainWindow window;
    AudioRecorder recorder;
    AsrBackendManager asrManager;
    FfmpegAudioConverter ffmpegConverter;
    AppConfig appConfig;
    DeepSeekClient deepSeekClient;

    bool filePreparationRunning = false;
    bool fileTranscriptionRunning = false;
    QString pendingSourceFilePath;
    QString activeFileSourcePath;
    QString activeFileTranscribeWavPath;
    QString latestSavedSourceFilePath;
    TaskConfig pendingFileTaskConfig;

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
                     &recorder, [&recorder, &latestSavedSourceFilePath](const TaskConfig &config)
                     {
                         Q_UNUSED(config);
                         latestSavedSourceFilePath.clear();
                         recorder.startRecording("recording");
                     });

    QObject::connect(&window, &MainWindow::stopVoiceInputRequested,
                     &recorder, [&recorder]()
                     {
                         recorder.stopRecording();
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
                         window.setOptimizedText("");
                         window.setFileTranscriptionBusy(true);

                         if (isWavFile(filePath))
                         {
                             fileTranscriptionRunning = true;
                             activeFileSourcePath = filePath;
                             activeFileTranscribeWavPath = filePath;
                             pendingSourceFilePath.clear();
                             window.setRunningStatus("正在转写本地文件...");
                             asrManager.transcribeAsync(filePath, config);
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
                         ffmpegConverter.convertToWavAsync(filePath, convertedWavPath);
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
                         asrManager.transcribeAsync(outputWavPath, pendingFileTaskConfig);
                     });

    QObject::connect(&ffmpegConverter, &FfmpegAudioConverter::conversionFailed,
                     &window, [&window,
                               &filePreparationRunning,
                               &fileTranscriptionRunning,
                               &pendingSourceFilePath,
                               &activeFileSourcePath,
                               &activeFileTranscribeWavPath](const QString &errorMessage)
                     {
                         filePreparationRunning = false;
                         fileTranscriptionRunning = false;
                         pendingSourceFilePath.clear();
                         activeFileSourcePath.clear();
                         activeFileTranscribeWavPath.clear();
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

    QObject::connect(&recorder, &AudioRecorder::sentenceChunkReady,
                     &window, [&window, &asrManager](const AudioChunkInfo &info)
                     {
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
                               &activeFileTranscribeWavPath](const QString &wavPath, const QString &errorMessage)
                     {
                         if (fileTranscriptionRunning && wavPath == activeFileTranscribeWavPath)
                         {
                             filePreparationRunning = false;
                             fileTranscriptionRunning = false;
                             activeFileSourcePath.clear();
                             activeFileTranscribeWavPath.clear();
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
