#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include "TaskConfig.h"
#include "asr/AsrResult.h"
#include "asr/TranscriptAssembler.h"

class QLabel;
class QTextEdit;
class QComboBox;
class QPushButton;
class QFrame;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

    TaskConfig currentTaskConfig() const;

public slots:
    void appendRawText(const QString &text);
    void setRawText(const QString &text);
    void handleAsrResult(const AsrResult &result);
    void resetTranscript();
    void setOptimizedText(const QString &text);
    void setRunningStatus(const QString &status);
    void setLastAsrTime(qint64 ms);
    void setAiConfigured(bool configured, const QString &provider = "");
    void setAiOptimizeBusy(bool busy);

signals:
    void startVoiceInputRequested(TaskConfig config);
    void stopVoiceInputRequested();

    void fileTranscribeRequested(const QString &filePath, TaskConfig config);

    void offlineOptimizeRequested(const QString &rawText);
    void aiOptimizeRequested(const QString &rawText);
    void customPromptOptimizeRequested(const QString &rawText);

    void copyRequested(const QString &text);
    void exportRequested(const QString &rawText, const QString &optimizedText);
    void saveRecordRequested(const QString &rawText, const QString &optimizedText);
    void clearRequested();

    void openSettingsRequested();
    void testAiConnectionRequested();

private:
    void setupUi();
    void setupConnections();
    void updateStatusBar();
    QString chooseLocalFile();

private:
    QLabel *pageTitleLabel = nullptr;
    QLabel *pageDescLabel = nullptr;
    QLabel *runStatusBadge = nullptr;
    QLabel *statusBarLabel = nullptr;
    QLabel *aiStatusLabel = nullptr;

    QComboBox *modelComboBox = nullptr;
    QComboBox *textModeComboBox = nullptr;
    QComboBox *wordLibComboBox = nullptr;

    QTextEdit *rawTextEdit = nullptr;
    QTextEdit *optimizedTextEdit = nullptr;

    QPushButton *startButton = nullptr;
    QPushButton *stopButton = nullptr;
    QPushButton *fileButton = nullptr;
    QPushButton *copyButton = nullptr;
    QPushButton *exportButton = nullptr;
    QPushButton *saveButton = nullptr;
    QPushButton *clearButton = nullptr;
    QPushButton *aiOptimizeButton = nullptr;
    QPushButton *customPromptButton = nullptr;
    QPushButton *settingsButton = nullptr;
    QPushButton *testAiButton = nullptr;

    QString currentPage = "realtime";
    QString currentStatus = "等待输入";
    qint64 lastAsrTimeMs = -1;
    qint64 lastResultEndTimeMs = -1;
    TranscriptAssembler transcriptAssembler;

    bool aiConfigured = false;
    QString aiProvider = "";
};

#endif
