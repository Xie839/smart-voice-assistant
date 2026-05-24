#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>

#include "TaskConfig.h"
#include "asr/AsrResult.h"
#include "asr/TranscriptAssembler.h"

class QLabel;
class QTextEdit;
class QPushButton;

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

    void aiOptimizeRequested(const QString &rawText);
    void customPromptOptimizeRequested(const QString &rawText);

    void copyRequested(const QString &text);
    void exportRequested(const QString &rawText, const QString &optimizedText);
    void saveRecordRequested(const QString &rawText, const QString &optimizedText);
    void clearRequested();

    void openSettingsRequested();
    void testAiConnectionRequested();

private:
    enum class PageMode
    {
        RealtimeInput,
        FileTranscription
    };

    void setupUi();
    void setupConnections();
    void setPageMode(PageMode mode);
    void updateStatusBar();
    void updateNavButtonStyles();
    void updateLeftActionButtons();
    void restoreStartButtonStyle();
    void restoreRunBadgeStyle();
    QString rawText() const;
    QString optimizedText() const;
    QString preferredOutputText() const;
    QString buildExportContent() const;
    void clearCurrentTexts();
    bool writeUtf8File(const QString &filePath, const QString &content, QString *errorMessage = nullptr) const;
    QString chooseLocalFile();

private:
    QLabel *pageTitleLabel = nullptr;
    QLabel *pageDescLabel = nullptr;
    QLabel *runStatusBadge = nullptr;
    QLabel *statusBarLabel = nullptr;
    QLabel *aiStatusLabel = nullptr;
    QLabel *asrInfoLabel = nullptr;

    QTextEdit *rawTextEdit = nullptr;
    QTextEdit *optimizedTextEdit = nullptr;

    QPushButton *realtimeNavButton = nullptr;
    QPushButton *fileNavButton = nullptr;
    QPushButton *startButton = nullptr;
    QPushButton *stopButton = nullptr;
    QPushButton *fileButton = nullptr;
    QPushButton *startFileTranscribeButton = nullptr;
    QPushButton *copyButton = nullptr;
    QPushButton *exportButton = nullptr;
    QPushButton *saveButton = nullptr;
    QPushButton *clearButton = nullptr;
    QPushButton *aiOptimizeButton = nullptr;
    QPushButton *customPromptButton = nullptr;
    QPushButton *settingsButton = nullptr;
    QPushButton *testAiButton = nullptr;

    PageMode currentPage = PageMode::RealtimeInput;
    QString currentStatus = "等待输入";
    QString selectedFilePath;
    qint64 lastAsrTimeMs = -1;
    qint64 lastResultEndTimeMs = -1;
    TranscriptAssembler transcriptAssembler;

    bool aiConfigured = false;
    QString aiProvider;
};

#endif
