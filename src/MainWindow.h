#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QList>
#include <QMainWindow>

#include "TaskConfig.h"
#include "asr/AsrResult.h"
#include "asr/TranscriptAssembler.h"
#include "history/HistoryManager.h"
#include "history/HistoryRecord.h"

class QLabel;
class QBoxLayout;
class QFrame;
class QPushButton;
class QTableWidget;
class QTextEdit;
class QWidget;

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
    void onStreamingPartialResult(const QString &text);
    void onStreamingFinalResult(const AsrResult &result);
    bool commitStreamingPartialAsFinal(const QString &traceId = "streaming-partial-fallback");
    void clearStreamingPreview();
    void resetTranscript();
    void setOptimizedText(const QString &text);
    void setRunningStatus(const QString &status);
    void setAsrBackendText(const QString &text);
    void setLastAsrTime(qint64 ms);
    void setAiConfigured(bool configured, const QString &provider = "");
    void setAiOptimizeBusy(bool busy);
    void setFileTranscriptionBusy(bool busy);

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
        FileTranscription,
        History
    };

    void setupUi();
    void setupConnections();
    void setPageMode(PageMode mode);
    void updateStatusBar();
    void updateNavButtonStyles();
    void updateLeftActionButtons();
    void updateTextDetailLayout();
    void restoreStartButtonStyle();
    void restoreRunBadgeStyle();

    QString rawText() const;
    QString confirmedRawText() const;
    QString optimizedText() const;
    QString preferredOutputText() const;
    QString buildExportContent() const;
    bool writeUtf8File(const QString &filePath, const QString &content, QString *errorMessage = nullptr) const;
    QString chooseLocalFile();
    bool isSupportedMediaFile(const QString &path) const;
    void clearCurrentTexts();
    void updateStreamingPreviewText(bool force = false);

    void reloadHistoryRecords();
    void renderHistoryTable();
    void showHistoryRecordByRow(int row);
    int currentHistoryRow() const;
    QString historySourceText(const HistoryRecord &record) const;
    QString buildHistoryExportContent(const HistoryRecord &record) const;
    void updateHistoryButtonsEnabled();
    void handleHistoryLoadToEditor();
    void handleHistoryCopy();
    void handleHistoryExport();
    void handleHistoryDelete();
    void handleHistoryClearAll();

private:
    QLabel *pageTitleLabel = nullptr;
    QLabel *pageDescLabel = nullptr;
    QLabel *runStatusBadge = nullptr;
    QLabel *statusBarLabel = nullptr;
    QLabel *aiStatusLabel = nullptr;
    QLabel *asrInfoLabel = nullptr;

    QTextEdit *rawTextEdit = nullptr;
    QTextEdit *optimizedTextEdit = nullptr;
    QFrame *rawTextCard = nullptr;
    QFrame *optimizedTextCard = nullptr;
    QFrame *textDivider = nullptr;
    QBoxLayout *textLayout = nullptr;

    QPushButton *realtimeNavButton = nullptr;
    QPushButton *fileNavButton = nullptr;
    QPushButton *historyNavButton = nullptr;
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

    QPushButton *historyLoadButton = nullptr;
    QPushButton *historyCopyButton = nullptr;
    QPushButton *historyExportButton = nullptr;
    QPushButton *historyDeleteButton = nullptr;
    QPushButton *historyClearAllButton = nullptr;

    QWidget *transcribeWorkArea = nullptr;
    QWidget *historyWorkArea = nullptr;
    QWidget *historyActionBar = nullptr;
    QTableWidget *historyTable = nullptr;

    PageMode currentPage = PageMode::RealtimeInput;
    QString currentStatus = "等待输入";
    QString asrBackendText = "sherpa-onnx / Paraformer 本地离线识别";
    QString selectedFilePath;
    qint64 lastAsrTimeMs = -1;
    qint64 lastResultEndTimeMs = -1;
    TranscriptAssembler transcriptAssembler;
    QString streamingPartialText;
    QString lastStreamingPreviewDisplayText;
    bool streamingPreviewActive = false;
    qint64 lastStreamingPreviewUiMs = -1;

    bool aiConfigured = false;
    QString aiProvider;
    bool fileTranscriptionBusy = false;

    HistoryManager historyManager;
    QList<HistoryRecord> historyRecords;
};

#endif
