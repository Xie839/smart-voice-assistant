#include <QApplication>
#include <QDebug>
#include <QFileInfo>
#include <QMessageBox>

#include "MainWindow.h"
#include "asr/AsrBackendManager.h"
#include "audio/AudioRecorder.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    qRegisterMetaType<AsrResult>("AsrResult");
    qRegisterMetaType<AudioChunkInfo>("AudioChunkInfo");

    MainWindow window;
    AudioRecorder recorder;
    AsrBackendManager asrManager;

    // MainWindow 只负责发出“开始输入”意图；实际麦克风采集由 AudioRecorder 管理。
    QObject::connect(&window, &MainWindow::startVoiceInputRequested,
                     &recorder, [&recorder](const TaskConfig &config)
                     {
                         Q_UNUSED(config);
                         recorder.startRecording("recording");
                     });

    // 停止按钮只触发录音模块收尾：停止麦克风、保存未完成 chunk 和完整录音。
    QObject::connect(&window, &MainWindow::stopVoiceInputRequested,
                     &recorder, [&recorder]()
                     {
                         recorder.stopRecording();
                     });

    QObject::connect(&recorder, &AudioRecorder::recordingStarted,
                     &window, [&window]()
                     {
                         window.setRunningStatus("正在录音，请开始说话");
                     });

    // 完整录音用于调试回放，不写入原始识别文本框，避免和后续 ASR 文本混在一起。
    QObject::connect(&recorder, &AudioRecorder::recordingStopped,
                     &window, [](const QString &wavPath)
                     {
                         qDebug() << "Full recording saved:" << wavPath;
                     });

    // VAD 状态只更新状态栏，让用户知道当前是噪声估计、等待语音、录入中还是停顿。
    QObject::connect(&recorder, &AudioRecorder::vadStateChanged,
                     &window, [&window](const QString &stateText)
                     {
                         window.setRunningStatus(stateText);
                     });

    // chunk 路径交给 ASR 模块识别；界面只显示简短状态，不写入左侧原始文本框。
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

    window.show();

    return app.exec();
}
