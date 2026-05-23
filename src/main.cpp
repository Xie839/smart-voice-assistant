#include <QApplication>
#include <QDebug>
#include <QMessageBox>

#include "MainWindow.h"
#include "audio/AudioRecorder.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    MainWindow window;
    AudioRecorder recorder;

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

    QObject::connect(&recorder, &AudioRecorder::recordingStarted,
                     &window, [&window]()
                     {
                         window.setRunningStatus("正在录音，请开始说话");
                     });

    QObject::connect(&recorder, &AudioRecorder::recordingStopped,
                     &window, [&window](const QString &wavPath)
                     {
                         window.setRunningStatus("录音已保存：" + wavPath);
                         qDebug() << "Recording saved:" << wavPath;
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
