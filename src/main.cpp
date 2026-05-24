#include <QApplication>
#include <QDebug>
#include <QDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QProcessEnvironment>

#include "MainWindow.h"
#include "ai/AiConfigDialog.h"
#include "ai/CustomPromptDialog.h"
#include "ai/DeepSeekClient.h"
#include "asr/AsrBackendManager.h"
#include "audio/AudioRecorder.h"
#include "config/AppConfig.h"

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
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    qRegisterMetaType<AsrResult>("AsrResult");
    qRegisterMetaType<AudioChunkInfo>("AudioChunkInfo");

    MainWindow window;
    AudioRecorder recorder;
    AsrBackendManager asrManager;
    AppConfig appConfig;
    DeepSeekClient deepSeekClient;

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
                     &recorder, [&recorder](const TaskConfig &config)
                     {
                         Q_UNUSED(config);
                         recorder.startRecording("recording");
                     });

    QObject::connect(&window, &MainWindow::stopVoiceInputRequested,
                     &recorder, [&recorder]()
                     {
                         recorder.stopRecording();
                     });

    QObject::connect(&window, &MainWindow::fileTranscribeRequested,
                     &window, [&window, &asrManager](const QString &filePath, const TaskConfig &config)
                     {
                         if (filePath.trimmed().isEmpty())
                         {
                             window.setRunningStatus("请先选择文件");
                             return;
                         }
                         asrManager.transcribeAsync(filePath, config);
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
                     &window, [&window](const QString &wavPath)
                     {
                         Q_UNUSED(wavPath);
                         window.setRunningStatus("正在识别语音片段...");
                     });

    QObject::connect(&asrManager, &AsrBackendManager::transcribeFinished,
                     &window, [&window](const AsrResult &result)
                     {
                         if (!result.success)
                         {
                             return;
                         }

                         window.handleAsrResult(result);
                         window.setLastAsrTime(result.elapsedMs);
                         window.setRunningStatus(QString("识别完成，耗时 %1 ms").arg(result.elapsedMs));
                     });

    QObject::connect(&asrManager, &AsrBackendManager::transcribeError,
                     &window, [&window](const QString &wavPath, const QString &errorMessage)
                     {
                         Q_UNUSED(wavPath);
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
