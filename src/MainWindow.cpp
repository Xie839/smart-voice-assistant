#include "MainWindow.h"

#include "utils/PerfTracer.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QBoxLayout>
#include <QClipboard>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QGraphicsDropShadowEffect>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPalette>
#include <QPushButton>
#include <QSizePolicy>
#include <QStringConverter>
#include <QStringList>
#include <QTableWidget>
#include <QTextCursor>
#include <QTextEdit>
#include <QTextStream>
#include <QVBoxLayout>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setupUi();
    setupConnections();
    setPageMode(PageMode::RealtimeInput);
    updateStatusBar();
}

TaskConfig MainWindow::currentTaskConfig() const
{
    TaskConfig config;
    config.modelMode = "sherpa-onnx";
    config.modelText = "sherpa-onnx / Paraformer";
    config.textMode = "recognize";
    config.textModeText = "本地离线识别";
    config.wordLib = "none";
    config.wordLibText = "固定内置";
    config.aiConfigured = aiConfigured;
    config.aiProvider = aiProvider;
    return config;
}

void MainWindow::setupUi()
{
    setWindowTitle("VoiceFlow AI 智能语音助手");
    resize(1360, 860);
    setMinimumSize(1180, 760);
    setStyleSheet(
        "* { font-family: Inter, \"Microsoft YaHei\", \"Segoe UI\", Arial, sans-serif; }"
        "QToolTip { background-color: #1e293b; color: white; border: none; padding: 6px; border-radius: 6px; }");

    auto addSoftShadow = [](QFrame *frame)
    {
        auto *shadow = new QGraphicsDropShadowEffect(frame);
        shadow->setBlurRadius(8);
        shadow->setOffset(0, 2);
        shadow->setColor(QColor(0, 0, 0, 15));
        frame->setGraphicsEffect(shadow);
    };

    auto *central = new QWidget(this);
    setCentralWidget(central);

    auto *sideBar = new QFrame(this);
    sideBar->setFixedWidth(240);
    sideBar->setStyleSheet(
        "QFrame { background-color: #0f172a; }"
        "QLabel { color: white; font-size: 18px; font-weight: 700; }"
        "QPushButton { color: #cbd5e1; background: transparent; border: none;"
        "text-align: left; padding: 0 14px; min-height: 40px; font-size: 14px; border-radius: 8px; }"
        "QPushButton:hover { background-color: #1e293b; color: white; }");

    auto *logo = new QLabel("VoiceFlow AI", sideBar);
    logo->setAlignment(Qt::AlignCenter);
    logo->setStyleSheet("font-size: 18px; font-weight: 700; color: white;");

    auto *logoDivider = new QFrame(sideBar);
    logoDivider->setFrameShape(QFrame::HLine);
    logoDivider->setFixedHeight(1);
    logoDivider->setStyleSheet("background-color: #334155; border: none;");

    realtimeNavButton = new QPushButton("实时语音输入", sideBar);
    fileNavButton = new QPushButton("本地文件转写", sideBar);
    historyNavButton = new QPushButton("历史记录", sideBar);
    auto *settingTabButton = new QPushButton("设置", sideBar);

    auto *sideLayout = new QVBoxLayout(sideBar);
    sideLayout->setContentsMargins(20, 24, 20, 24);
    sideLayout->setSpacing(10);
    sideLayout->addWidget(logo);
    sideLayout->addSpacing(12);
    sideLayout->addWidget(logoDivider);
    sideLayout->addSpacing(16);
    sideLayout->addWidget(realtimeNavButton);
    sideLayout->addWidget(fileNavButton);
    sideLayout->addWidget(historyNavButton);
    sideLayout->addWidget(settingTabButton);
    sideLayout->addStretch();

    auto *mainPanel = new QFrame(this);
    mainPanel->setStyleSheet("QFrame { background-color: #f5f7fb; }");

    pageTitleLabel = new QLabel("实时语音输入", mainPanel);
    pageTitleLabel->setStyleSheet("font-size: 26px; font-weight: 700; color: #1f2937;");

    pageDescLabel = new QLabel("通过麦克风实时输入语音，自动识别为可编辑文本，并支持可选文本优化。", mainPanel);
    pageDescLabel->setStyleSheet("font-size: 14px; color: #64748b;");

    runStatusBadge = new QLabel("● 状态：未开始", mainPanel);
    runStatusBadge->setAlignment(Qt::AlignCenter);
    runStatusBadge->setMinimumSize(132, 36);
    restoreRunBadgeStyle();

    auto *titleTextLayout = new QVBoxLayout();
    titleTextLayout->setSpacing(4);
    titleTextLayout->addWidget(pageTitleLabel);
    titleTextLayout->addWidget(pageDescLabel);

    auto *titleLayout = new QHBoxLayout();
    titleLayout->addLayout(titleTextLayout);
    titleLayout->addStretch();
    titleLayout->addWidget(runStatusBadge);

    auto *configCard = new QFrame(mainPanel);
    configCard->setObjectName("configCard");
    configCard->setStyleSheet(
        "QFrame#configCard { background-color: white; border: 1px solid #e5e7eb; border-radius: 10px; }"
        "QLabel { font-size: 13px; color: #64748b; }"
        "QLabel#aiStateText { color: #166534; font-weight: 700; background-color: #dcfce7;"
        "border: 1px solid #bbf7d0; border-radius: 13px; padding: 4px 10px; }"
        "QPushButton { padding: 0 14px; border-radius: 7px; border: 1px solid #dbe3ef;"
        "background-color: white; color: #334155; font-size: 13px; min-height: 34px; }"
        "QPushButton:hover { background-color: #eff6ff; border-color: #2563eb; color: #1f2937; }");
    addSoftShadow(configCard);

    aiStatusLabel = new QLabel("AI：未配置", configCard);
    aiStatusLabel->setObjectName("aiStateText");
    asrInfoLabel = new QLabel("语音识别：sherpa-onnx / Paraformer 本地离线识别 | 本地处理：VAD 分句 + 标点恢复", configCard);

    settingsButton = new QPushButton("打开设置", configCard);
    testAiButton = new QPushButton("测试连接", configCard);

    auto *configLayout = new QVBoxLayout(configCard);
    configLayout->setContentsMargins(18, 14, 18, 14);
    configLayout->setSpacing(9);

    auto *configRow = new QHBoxLayout();
    configRow->setSpacing(8);
    configRow->addWidget(new QLabel("AI配置状态：", configCard));
    configRow->addWidget(aiStatusLabel);
    configRow->addSpacing(12);
    configRow->addWidget(settingsButton);
    configRow->addWidget(testAiButton);
    configRow->addStretch();

    auto *configDivider = new QFrame(configCard);
    configDivider->setFrameShape(QFrame::HLine);
    configDivider->setFixedHeight(1);
    configDivider->setStyleSheet("background-color: #eef2f7; border: none;");

    configLayout->addLayout(configRow);
    configLayout->addWidget(configDivider);
    configLayout->addWidget(asrInfoLabel);

    const QString cardStyle =
        "QFrame#textCard { background-color: white; border: 1px solid #e5e7eb; border-radius: 10px; }"
        "QLabel { font-size: 16px; font-weight: 700; color: #1f2937; }"
        "QTextEdit { border: 1px solid #dbe3ef; border-radius: 8px; font-size: 14px; color: #1f2937;"
        "line-height: 160%; padding: 14px; background-color: #f8fafc; selection-background-color: #bfdbfe; }"
        "QTextEdit:hover { border-color: #cbd5e1; }"
        "QTextEdit:focus { border-color: #2563eb; }"
        "QScrollBar:vertical { background: #f1f5f9; width: 6px; margin: 0; border-radius: 3px; }"
        "QScrollBar::handle:vertical { background: #cbd5e1; min-height: 24px; border-radius: 3px; }"
        "QScrollBar::handle:vertical:hover { background: #94a3b8; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; border: none; }"
        "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: none; }";

    rawTextCard = new QFrame(mainPanel);
    rawTextCard->setObjectName("textCard");
    rawTextCard->setStyleSheet(cardStyle);
    rawTextCard->setMinimumHeight(430);
    rawTextCard->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    addSoftShadow(rawTextCard);

    optimizedTextCard = new QFrame(mainPanel);
    optimizedTextCard->setObjectName("textCard");
    optimizedTextCard->setStyleSheet(cardStyle);
    optimizedTextCard->setMinimumHeight(430);
    optimizedTextCard->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    addSoftShadow(optimizedTextCard);

    auto *rawTitle = new QLabel("原始识别文本", rawTextCard);
    auto *optTitle = new QLabel("优化后文本", optimizedTextCard);

    rawTextEdit = new QTextEdit(rawTextCard);
    rawTextEdit->setPlaceholderText("语音识别结果将在这里逐句显示。");

    optimizedTextEdit = new QTextEdit(optimizedTextCard);
    optimizedTextEdit->setPlaceholderText("点击 AI智能优化 或 自定义提示词 后，结果将在这里显示。");

    QPalette placeholderPalette = rawTextEdit->palette();
    placeholderPalette.setColor(QPalette::PlaceholderText, QColor("#94a3b8"));
    rawTextEdit->setPalette(placeholderPalette);
    optimizedTextEdit->setPalette(placeholderPalette);

    auto *rawLayout = new QVBoxLayout(rawTextCard);
    rawLayout->setContentsMargins(18, 18, 18, 18);
    rawLayout->setSpacing(12);
    rawLayout->addWidget(rawTitle);
    rawLayout->addWidget(rawTextEdit);

    auto *optLayout = new QVBoxLayout(optimizedTextCard);
    optLayout->setContentsMargins(18, 18, 18, 18);
    optLayout->setSpacing(12);
    optLayout->addWidget(optTitle);
    optLayout->addWidget(optimizedTextEdit);

    textDivider = new QFrame(mainPanel);
    textDivider->setFixedWidth(1);
    textDivider->setStyleSheet("QFrame { background-color: #e2e8f0; border: none; }");

    textLayout = new QBoxLayout(QBoxLayout::LeftToRight);
    textLayout->setSpacing(16);
    textLayout->addWidget(rawTextCard, 1);
    textLayout->addWidget(textDivider);
    textLayout->addWidget(optimizedTextCard, 1);

    startButton = new QPushButton("开始输入", mainPanel);
    stopButton = new QPushButton("停止", mainPanel);
    fileButton = new QPushButton("选择文件", mainPanel);
    startFileTranscribeButton = new QPushButton("开始转写", mainPanel);
    copyButton = new QPushButton("复制", mainPanel);
    exportButton = new QPushButton("导出", mainPanel);
    saveButton = new QPushButton("保存", mainPanel);
    clearButton = new QPushButton("清空", mainPanel);
    aiOptimizeButton = new QPushButton("AI智能优化", mainPanel);
    customPromptButton = new QPushButton("自定义提示词", mainPanel);

    const QString primaryButtonStyle =
        "QPushButton { background-color: #2563eb; color: white; border: none;"
        "padding: 0 20px; border-radius: 6px; font-size: 14px; font-weight: 700; min-height: 36px; }"
        "QPushButton:hover { background-color: #1d4ed8; }"
        "QPushButton:pressed { background-color: #1e40af; }";

    const QString normalButtonStyle =
        "QPushButton { background-color: white; color: #475569; border: 1px solid #e2e8f0;"
        "padding: 0 16px; border-radius: 6px; font-size: 14px; min-height: 36px; }"
        "QPushButton:hover { background-color: #eff6ff; border-color: #2563eb; color: #1e293b; }"
        "QPushButton:pressed { background-color: #dbeafe; }";

    const QString aiButtonStyle =
        "QPushButton { background-color: #f97316; color: white; border: 1px solid #f97316;"
        "padding: 0 18px; border-radius: 6px; font-size: 14px; font-weight: 700; min-height: 36px; }"
        "QPushButton:hover { background-color: #ea580c; border-color: #ea580c; }"
        "QPushButton:pressed { background-color: #c2410c; border-color: #c2410c; }";

    startButton->setStyleSheet(primaryButtonStyle);
    startFileTranscribeButton->setStyleSheet(primaryButtonStyle);
    stopButton->setStyleSheet(normalButtonStyle);
    fileButton->setStyleSheet(normalButtonStyle);
    aiOptimizeButton->setStyleSheet(aiButtonStyle);
    customPromptButton->setStyleSheet(normalButtonStyle);
    copyButton->setStyleSheet(normalButtonStyle);
    exportButton->setStyleSheet(normalButtonStyle);
    saveButton->setStyleSheet(normalButtonStyle);
    clearButton->setStyleSheet(normalButtonStyle);

    auto *buttonLayout = new QHBoxLayout();
    buttonLayout->setSpacing(8);
    buttonLayout->setAlignment(Qt::AlignBottom);

    auto *leftButtonGroup = new QHBoxLayout();
    leftButtonGroup->setSpacing(8);
    leftButtonGroup->addWidget(startButton);
    leftButtonGroup->addWidget(stopButton);
    leftButtonGroup->addWidget(fileButton);
    leftButtonGroup->addWidget(startFileTranscribeButton);

    auto *middleButtonGroup = new QHBoxLayout();
    middleButtonGroup->setSpacing(8);
    middleButtonGroup->addWidget(aiOptimizeButton);
    middleButtonGroup->addWidget(customPromptButton);

    auto *rightButtonGroup = new QHBoxLayout();
    rightButtonGroup->setSpacing(8);
    rightButtonGroup->addWidget(copyButton);
    rightButtonGroup->addWidget(exportButton);
    rightButtonGroup->addWidget(saveButton);
    rightButtonGroup->addWidget(clearButton);

    buttonLayout->addLayout(leftButtonGroup);
    buttonLayout->addStretch(1);
    buttonLayout->addLayout(middleButtonGroup);
    buttonLayout->addStretch(1);
    buttonLayout->addLayout(rightButtonGroup);

    transcribeWorkArea = new QWidget(mainPanel);
    auto *transcribeLayout = new QVBoxLayout(transcribeWorkArea);
    transcribeLayout->setContentsMargins(0, 0, 0, 0);
    transcribeLayout->setSpacing(12);
    transcribeLayout->addLayout(textLayout, 1);
    transcribeLayout->addLayout(buttonLayout);

    historyWorkArea = new QWidget(mainPanel);
    historyWorkArea->setVisible(false);
    historyWorkArea->setMinimumWidth(560);
    auto *historyLayout = new QVBoxLayout(historyWorkArea);
    historyLayout->setContentsMargins(0, 0, 0, 0);
    historyLayout->setSpacing(0);

    auto *historyCard = new QFrame(historyWorkArea);
    historyCard->setObjectName("historyCard");
    historyCard->setStyleSheet(
        "QFrame#historyCard { background-color: white; border: 1px solid #e5e7eb; border-radius: 10px; }");
    addSoftShadow(historyCard);

    historyTable = new QTableWidget(historyCard);
    historyTable->setColumnCount(4);
    historyTable->setHorizontalHeaderLabels({"时间", "标题", "来源", "已优化"});
    historyTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    historyTable->setSelectionMode(QAbstractItemView::SingleSelection);
    historyTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    historyTable->setAlternatingRowColors(true);
    historyTable->setShowGrid(false);
    historyTable->setFrameShape(QFrame::NoFrame);
    historyTable->setFocusPolicy(Qt::NoFocus);
    historyTable->setWordWrap(false);
    historyTable->setStyleSheet(
        "QTableWidget { background-color: white; alternate-background-color: #f8fafc;"
        "border: none; gridline-color: #eef2f7; color: #1f2937; font-size: 13px;"
        "selection-background-color: #dbeafe; selection-color: #1f2937; }"
        "QTableWidget::item { border-bottom: 1px solid #eef2f7; padding: 0 8px; }"
        "QTableWidget::item:hover { background-color: #f1f5f9; }"
        "QHeaderView::section { background-color: #f8fafc; color: #334155; border: none;"
        "border-bottom: 1px solid #e5e7eb; padding: 0 8px; font-size: 13px; font-weight: 700; }"
        "QScrollBar:vertical { background: #f8fafc; width: 8px; margin: 0; border-radius: 4px; }"
        "QScrollBar::handle:vertical { background: #cbd5e1; min-height: 28px; border-radius: 4px; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; border: none; }"
        "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: none; }");
    historyTable->verticalHeader()->setVisible(false);
    historyTable->verticalHeader()->setDefaultSectionSize(40);
    historyTable->horizontalHeader()->setStretchLastSection(true);
    historyTable->horizontalHeader()->setHighlightSections(false);
    historyTable->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    historyTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    historyTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    historyTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    historyTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    historyTable->setMinimumHeight(230);

    auto *historyCardLayout = new QVBoxLayout(historyCard);
    historyCardLayout->setContentsMargins(14, 14, 14, 14);
    historyCardLayout->addWidget(historyTable);

    historyLoadButton = new QPushButton("加载到编辑区", historyWorkArea);
    historyCopyButton = new QPushButton("复制", historyWorkArea);
    historyExportButton = new QPushButton("导出", historyWorkArea);
    historyDeleteButton = new QPushButton("删除", historyWorkArea);
    historyClearAllButton = new QPushButton("清空历史", historyWorkArea);

    historyLoadButton->setStyleSheet(normalButtonStyle);
    historyCopyButton->setStyleSheet(normalButtonStyle);
    historyExportButton->setStyleSheet(normalButtonStyle);
    historyDeleteButton->setStyleSheet(normalButtonStyle);
    historyClearAllButton->setStyleSheet(normalButtonStyle);

    historyActionBar = new QFrame(mainPanel);
    historyActionBar->setVisible(false);
    historyActionBar->setStyleSheet(
        "QFrame { background-color: transparent; }"
        "QPushButton { min-height: 42px; min-width: 118px; padding: 0 18px; border-radius: 8px;"
        "border: 1px solid #dbe3ef; background-color: white; color: #334155;"
        "font-size: 14px; font-weight: 600; }"
        "QPushButton:hover { background-color: #eff6ff; border-color: #2563eb; color: #1f2937; }"
        "QPushButton:disabled { color: #94a3b8; background-color: #f8fafc; border-color: #e5e7eb; }"
        "QPushButton#historyPrimaryButton { background-color: #eff6ff; border-color: #93c5fd; color: #1d4ed8; }"
        "QPushButton#historyPrimaryButton:hover { background-color: #dbeafe; border-color: #2563eb; }"
        "QPushButton#historyDangerButton { color: #dc2626; }"
        "QPushButton#historyDangerButton:hover { background-color: #fef2f2; border-color: #fecaca; }");
    historyLoadButton->setStyleSheet(QString());
    historyCopyButton->setStyleSheet(QString());
    historyExportButton->setStyleSheet(QString());
    historyDeleteButton->setStyleSheet(QString());
    historyClearAllButton->setStyleSheet(QString());
    historyLoadButton->setObjectName("historyPrimaryButton");
    historyDeleteButton->setObjectName("historyDangerButton");
    historyActionBar->setStyleSheet(historyActionBar->styleSheet());

    auto *historyActionLayout = new QHBoxLayout(historyActionBar);
    historyActionLayout->setContentsMargins(0, 4, 0, 4);
    historyActionLayout->setSpacing(14);
    historyActionLayout->addWidget(historyLoadButton, 2);
    historyActionLayout->addWidget(historyCopyButton, 1);
    historyActionLayout->addWidget(historyExportButton, 1);
    historyActionLayout->addWidget(historyDeleteButton, 1);
    historyActionLayout->addWidget(historyClearAllButton, 1);

    historyLayout->addWidget(historyCard, 1);

    auto *contentArea = new QWidget(mainPanel);
    auto *contentLayout = new QHBoxLayout(contentArea);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(16);
    contentLayout->addWidget(historyWorkArea, 11);
    contentLayout->addWidget(transcribeWorkArea, 9);

    statusBarLabel = new QLabel(mainPanel);
    statusBarLabel->setTextFormat(Qt::RichText);
    statusBarLabel->setAlignment(Qt::AlignCenter);
    statusBarLabel->setMinimumHeight(40);
    statusBarLabel->setMaximumHeight(40);
    statusBarLabel->setStyleSheet(
        "background-color: #f8fafc; border: 1px solid #e5e7eb; border-radius: 8px;"
        "padding: 0 16px; color: #64748b; font-size: 13px;");

    auto *mainLayout = new QVBoxLayout(mainPanel);
    mainLayout->setContentsMargins(24, 20, 24, 20);
    mainLayout->setSpacing(14);
    mainLayout->addLayout(titleLayout);
    mainLayout->addWidget(configCard);
    mainLayout->addWidget(contentArea, 1);
    mainLayout->addWidget(historyActionBar);
    mainLayout->addWidget(statusBarLabel);

    auto *rootLayout = new QHBoxLayout(central);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);
    rootLayout->addWidget(sideBar);
    rootLayout->addWidget(mainPanel, 1);

    connect(settingTabButton, &QPushButton::clicked, this, [this]()
            { emit openSettingsRequested(); });
}

void MainWindow::setupConnections()
{
    connect(this, &MainWindow::clearRequested, this, &MainWindow::resetTranscript);

    connect(realtimeNavButton, &QPushButton::clicked, this, [this]()
            { setPageMode(PageMode::RealtimeInput); });
    connect(fileNavButton, &QPushButton::clicked, this, [this]()
            {
                clearStreamingPreview();
                setPageMode(PageMode::FileTranscription);
            });
    connect(historyNavButton, &QPushButton::clicked, this, [this]()
            {
                clearStreamingPreview();
                setPageMode(PageMode::History);
            });

    connect(historyTable, &QTableWidget::cellClicked, this, [this](int row, int)
            { showHistoryRecordByRow(row); });
    connect(historyTable, &QTableWidget::itemSelectionChanged, this, [this]()
            { updateHistoryButtonsEnabled(); });
    connect(historyLoadButton, &QPushButton::clicked, this, &MainWindow::handleHistoryLoadToEditor);
    connect(historyCopyButton, &QPushButton::clicked, this, &MainWindow::handleHistoryCopy);
    connect(historyExportButton, &QPushButton::clicked, this, &MainWindow::handleHistoryExport);
    connect(historyDeleteButton, &QPushButton::clicked, this, &MainWindow::handleHistoryDelete);
    connect(historyClearAllButton, &QPushButton::clicked, this, &MainWindow::handleHistoryClearAll);

    connect(startButton, &QPushButton::clicked, this, [this]()
            {
                currentStatus = "正在监听，请开始说话";
                runStatusBadge->setText("● 状态：监听中");
                runStatusBadge->setStyleSheet(
                    "background-color: #eff6ff; border: 1px solid #bfdbfe; border-radius: 18px;"
                    "color: #2563eb; font-size: 13px; padding: 0 12px;");

                startButton->setText("正在输入");
                startButton->setStyleSheet(
                    "QPushButton { background-color: #ef4444; color: white; border: none;"
                    "padding: 0 20px; border-radius: 6px; font-size: 14px; font-weight: 700; min-height: 36px; }"
                    "QPushButton:hover { background-color: #dc2626; }"
                    "QPushButton:pressed { background-color: #b91c1c; }");
                updateStatusBar();
                emit startVoiceInputRequested(currentTaskConfig());
            });

    connect(stopButton, &QPushButton::clicked, this, [this]()
            {
                currentStatus = "已停止输入";
                runStatusBadge->setText("● 状态：已停止");
                runStatusBadge->setStyleSheet(
                    "background-color: #fef2f2; border: 1px solid #fecaca; border-radius: 18px;"
                    "color: #dc2626; font-size: 13px; padding: 0 12px;");
                restoreStartButtonStyle();
                updateStatusBar();
                emit stopVoiceInputRequested();
            });

    connect(fileButton, &QPushButton::clicked, this, [this]()
            {
                const QString filePath = chooseLocalFile();
                if (filePath.isEmpty())
                {
                    return;
                }

                selectedFilePath = filePath;
                currentStatus = "已选择文件：" + QFileInfo(filePath).fileName();
                updateStatusBar();
            });

    connect(startFileTranscribeButton, &QPushButton::clicked, this, [this]()
            {
                if (fileTranscriptionBusy)
                {
                    setRunningStatus("文件转写正在进行，请稍候");
                    return;
                }

                if (selectedFilePath.trimmed().isEmpty())
                {
                    setRunningStatus("请先选择文件");
                    return;
                }

                if (!QFileInfo::exists(selectedFilePath))
                {
                    setRunningStatus("选择的文件不存在，请重新选择");
                    return;
                }

                if (!isSupportedMediaFile(selectedFilePath))
                {
                    setRunningStatus("当前文件格式暂不支持");
                    return;
                }

                setRunningStatus("正在转写本地文件...");
                emit fileTranscribeRequested(selectedFilePath, currentTaskConfig());
            });

    connect(copyButton, &QPushButton::clicked, this, [this]()
            {
                const QString text = preferredOutputText();
                if (text.trimmed().isEmpty())
                {
                    setRunningStatus("没有可复制的文本");
                    return;
                }

                QApplication::clipboard()->setText(text);
                setRunningStatus("已复制到剪贴板");
                emit copyRequested(text);
            });

    connect(exportButton, &QPushButton::clicked, this, [this]()
            {
                if (rawText().isEmpty() && optimizedText().isEmpty())
                {
                    setRunningStatus("没有可导出的文本");
                    return;
                }

                const QString defaultName =
                    QString("VoiceFlowAI_%1.txt").arg(QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss"));
                const QString filePath = QFileDialog::getSaveFileName(
                    this,
                    "导出文本",
                    QDir::currentPath() + "/" + defaultName,
                    "文本文件 (*.txt);;Markdown 文件 (*.md)");
                if (filePath.isEmpty())
                {
                    return;
                }

                QString errorMessage;
                if (!writeUtf8File(filePath, buildExportContent(), &errorMessage))
                {
                    setRunningStatus("导出失败：" + errorMessage);
                    return;
                }

                setRunningStatus("导出成功：" + QFileInfo(filePath).fileName());
                emit exportRequested(rawText(), optimizedText());
            });

    connect(saveButton, &QPushButton::clicked, this, [this]()
            {
                if (rawText().isEmpty() && optimizedText().isEmpty())
                {
                    setRunningStatus("没有可保存的文本");
                    return;
                }

                QDir dir(QDir::currentPath());
                if (!dir.exists("results") && !dir.mkpath("results"))
                {
                    setRunningStatus("保存失败：无法创建 results 目录");
                    return;
                }

                const QString fileName =
                    QString("voiceflow_result_%1.md").arg(QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss"));
                const QString filePath = dir.filePath("results/" + fileName);

                QString errorMessage;
                if (!writeUtf8File(filePath, buildExportContent(), &errorMessage))
                {
                    setRunningStatus("保存失败：" + errorMessage);
                    return;
                }

                setRunningStatus("已保存到：results/" + fileName);
                emit saveRecordRequested(rawText(), optimizedText());
            });

    connect(clearButton, &QPushButton::clicked, this, [this]()
            {
                clearCurrentTexts();
                setRunningStatus("已清空当前文本");
                emit clearRequested();
            });

    connect(aiOptimizeButton, &QPushButton::clicked, this, [this]()
            {
                if (!aiConfigured)
                {
                    QMessageBox::warning(this, "AI 未配置", "请先配置 DeepSeek API Key。");
                    emit openSettingsRequested();
                    return;
                }

                if (streamingPreviewActive)
                {
                    setRunningStatus("正在识别中，请停止后再优化");
                    return;
                }

                const QString inputText = confirmedRawText();
                if (inputText.isEmpty())
                {
                    QMessageBox::warning(this, "无文本", "请先输入或识别文本。");
                    return;
                }

                setRunningStatus("正在调用 AI 智能优化");
                emit aiOptimizeRequested(inputText);
            });

    connect(customPromptButton, &QPushButton::clicked, this, [this]()
            {
                if (!aiConfigured)
                {
                    QMessageBox::warning(this, "AI 未配置", "请先配置 DeepSeek API Key。");
                    emit openSettingsRequested();
                    return;
                }

                if (streamingPreviewActive)
                {
                    setRunningStatus("正在识别中，请停止后再优化");
                    return;
                }

                const QString inputText = confirmedRawText();
                if (inputText.isEmpty())
                {
                    QMessageBox::warning(this, "无文本", "没有可优化的文本。");
                    return;
                }

                emit customPromptOptimizeRequested(inputText);
            });

    connect(settingsButton, &QPushButton::clicked, this, [this]()
            { emit openSettingsRequested(); });
    connect(testAiButton, &QPushButton::clicked, this, [this]()
            { emit testAiConnectionRequested(); });
}

void MainWindow::setPageMode(PageMode mode)
{
    if (fileTranscriptionBusy && mode == PageMode::History)
    {
        setRunningStatus("文件转写正在进行，请稍候再查看历史记录");
        return;
    }

    const bool pageChanged = (mode != currentPage);
    currentPage = mode;

    if (pageChanged)
    {
        clearCurrentTexts();
    }

    if (currentPage == PageMode::RealtimeInput)
    {
        pageTitleLabel->setText("实时语音输入");
        pageDescLabel->setText("通过麦克风实时输入语音，自动识别为可编辑文本，并支持可选文本优化。");
        if (currentStatus.trimmed().isEmpty() || currentStatus.contains("转写"))
        {
            currentStatus = "等待输入";
        }
        runStatusBadge->setVisible(true);
        transcribeWorkArea->setVisible(true);
        historyWorkArea->setVisible(false);
        historyActionBar->setVisible(false);
        rawTextEdit->setReadOnly(false);
        optimizedTextEdit->setReadOnly(false);
        updateTextDetailLayout();
    }
    else if (currentPage == PageMode::FileTranscription)
    {
        pageTitleLabel->setText("本地文件转写");
        pageDescLabel->setText("选择本地音频或视频文件，自动转写为可编辑文本，并支持 AI 智能优化。");
        if (selectedFilePath.isEmpty())
        {
            currentStatus = "未选择文件";
        }
        else
        {
            currentStatus = "已选择文件：" + QFileInfo(selectedFilePath).fileName();
        }
        runStatusBadge->setVisible(true);
        transcribeWorkArea->setVisible(true);
        historyWorkArea->setVisible(false);
        historyActionBar->setVisible(false);
        rawTextEdit->setReadOnly(false);
        optimizedTextEdit->setReadOnly(false);
        updateTextDetailLayout();
    }
    else
    {
        pageTitleLabel->setText("历史记录");
        pageDescLabel->setText("查看、导出或恢复之前保存的语音识别与优化结果。");
        currentStatus = "已进入历史记录";
        runStatusBadge->setVisible(false);
        transcribeWorkArea->setVisible(true);
        historyWorkArea->setVisible(true);
        historyActionBar->setVisible(true);
        rawTextEdit->setReadOnly(true);
        optimizedTextEdit->setReadOnly(true);
        updateTextDetailLayout();
        reloadHistoryRecords();
    }

    updateNavButtonStyles();
    updateLeftActionButtons();
    updateStatusBar();
}

void MainWindow::updateNavButtonStyles()
{
    const QString activeStyle =
        "QPushButton { color: white; background-color: #2563eb; border: none;"
        "text-align: left; padding: 0 14px; min-height: 40px; font-size: 14px; border-radius: 8px; font-weight: 600; }"
        "QPushButton:hover { background-color: #1d4ed8; }";

    const QString normalStyle =
        "QPushButton { color: #cbd5e1; background: transparent; border: none;"
        "text-align: left; padding: 0 14px; min-height: 40px; font-size: 14px; border-radius: 8px; }"
        "QPushButton:hover { background-color: #1e293b; color: white; }";

    realtimeNavButton->setStyleSheet(currentPage == PageMode::RealtimeInput ? activeStyle : normalStyle);
    fileNavButton->setStyleSheet(currentPage == PageMode::FileTranscription ? activeStyle : normalStyle);
    historyNavButton->setStyleSheet(currentPage == PageMode::History ? activeStyle : normalStyle);
}

void MainWindow::updateLeftActionButtons()
{
    if (currentPage == PageMode::History)
    {
        startButton->setVisible(false);
        stopButton->setVisible(false);
        fileButton->setVisible(false);
        startFileTranscribeButton->setVisible(false);
        aiOptimizeButton->setVisible(false);
        customPromptButton->setVisible(false);
        copyButton->setVisible(false);
        exportButton->setVisible(false);
        saveButton->setVisible(false);
        clearButton->setVisible(false);
        return;
    }

    aiOptimizeButton->setVisible(true);
    customPromptButton->setVisible(true);
    copyButton->setVisible(true);
    exportButton->setVisible(true);
    saveButton->setVisible(true);
    clearButton->setVisible(true);

    const bool realtime = currentPage == PageMode::RealtimeInput;
    startButton->setVisible(realtime);
    stopButton->setVisible(realtime);
    fileButton->setVisible(!realtime);
    startFileTranscribeButton->setVisible(!realtime);
}

void MainWindow::updateTextDetailLayout()
{
    if (!textLayout || !textDivider || !rawTextCard || !optimizedTextCard)
    {
        return;
    }

    const bool history = currentPage == PageMode::History;
    textLayout->setDirection(history ? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight);
    textLayout->setSpacing(history ? 12 : 16);

    if (history)
    {
        rawTextCard->setMinimumHeight(240);
        optimizedTextCard->setMinimumHeight(240);
        rawTextEdit->setMinimumHeight(180);
        optimizedTextEdit->setMinimumHeight(180);
        textDivider->setMinimumWidth(0);
        textDivider->setMaximumWidth(QWIDGETSIZE_MAX);
        textDivider->setFixedHeight(1);
        return;
    }

    rawTextCard->setMinimumHeight(430);
    optimizedTextCard->setMinimumHeight(430);
    rawTextEdit->setMinimumHeight(0);
    optimizedTextEdit->setMinimumHeight(0);
    textDivider->setMinimumHeight(0);
    textDivider->setMaximumHeight(QWIDGETSIZE_MAX);
    textDivider->setFixedWidth(1);
}

void MainWindow::restoreStartButtonStyle()
{
    startButton->setText("开始输入");
    startButton->setStyleSheet(
        "QPushButton { background-color: #2563eb; color: white; border: none;"
        "padding: 0 20px; border-radius: 6px; font-size: 14px; font-weight: 700; min-height: 36px; }"
        "QPushButton:hover { background-color: #1d4ed8; }"
        "QPushButton:pressed { background-color: #1e40af; }");
}

void MainWindow::restoreRunBadgeStyle()
{
    runStatusBadge->setText("● 状态：未开始");
    runStatusBadge->setStyleSheet(
        "background-color: #f1f5f9; border: 1px solid #e2e8f0; border-radius: 18px;"
        "color: #64748b; font-size: 13px; padding: 0 12px;");
}

void MainWindow::appendRawText(const QString &text)
{
    if (text.trimmed().isEmpty())
    {
        return;
    }

    rawTextEdit->moveCursor(QTextCursor::End);
    rawTextEdit->insertPlainText(text);
    rawTextEdit->moveCursor(QTextCursor::End);
}

void MainWindow::setRawText(const QString &text)
{
    clearStreamingPreview();
    rawTextEdit->setPlainText(text);
    rawTextEdit->moveCursor(QTextCursor::End);
}

void MainWindow::handleAsrResult(const AsrResult &result)
{
    PerfTracer::markTrace("UI", result.traceId, "result_received",
                          QString("success=%1, text_len=%2").arg(result.success ? "true" : "false").arg(result.text.size()));
    if (!result.success || result.text.trimmed().isEmpty())
    {
        return;
    }
    clearStreamingPreview();

    QElapsedTimer uiTimer;
    uiTimer.start();
    PerfTracer::markTrace("UI", result.traceId, "transcript_assemble_start");
    qint64 gapMs = 0;
    if (lastResultEndTimeMs >= 0 && result.startTimeMs >= 0)
    {
        gapMs = qMax<qint64>(0, result.startTimeMs - lastResultEndTimeMs);
    }

    const QString assembledText = transcriptAssembler.appendSegment(result.text, gapMs);
    PerfTracer::markTrace("UI", result.traceId, "transcript_assemble_done",
                          QString("assembled_len=%1").arg(assembledText.size()));
    PerfTracer::markTrace("UI", result.traceId, "set_raw_text_start");
    setRawText(assembledText);
    const qint64 uiMs = uiTimer.elapsed();
    PerfTracer::markTrace("UI", result.traceId, "set_raw_text_done",
                          QString("ui_update_ms=%1").arg(uiMs));
    PerfTracer::warnIfSlow("UI", result.traceId, "ui_update", uiMs, 100,
                           "UI 更新耗时较长，可能文本过长或主线程阻塞");
    PerfTracer::markTrace("ASR", result.traceId, "ui_updated");

    if (result.endTimeMs >= 0)
    {
        lastResultEndTimeMs = result.endTimeMs;
    }
}

void MainWindow::onStreamingPartialResult(const QString &text)
{
    const QString partial = text.trimmed();
    if (partial.isEmpty())
    {
        return;
    }
    if (partial == streamingPartialText)
    {
        PerfTracer::markTrace("STREAM", "ui", "partial preview skipped same text",
                              QString("text_len=%1").arg(partial.size()));
        return;
    }

    streamingPartialText = partial;
    streamingPreviewActive = true;
    updateStreamingPreviewText(false);
}

void MainWindow::onStreamingFinalResult(const AsrResult &result)
{
    clearStreamingPreview();
    handleAsrResult(result);
    PerfTracer::markTrace("STREAM", result.traceId, "final result committed",
                          QString("text_len=%1").arg(result.text.size()));
}

bool MainWindow::commitStreamingPartialAsFinal(const QString &traceId)
{
    const QString partial = streamingPartialText.trimmed();
    if (partial.isEmpty())
    {
        clearStreamingPreview();
        return false;
    }

    PerfTracer::markTrace("STREAM", traceId, "final empty, use partial as fallback final",
                          QString("text_len=%1").arg(partial.size()));
    AsrResult result;
    result.traceId = traceId;
    result.text = partial;
    result.modelName = "sherpa-onnx online streaming";
    result.success = true;
    onStreamingFinalResult(result);
    return true;
}

void MainWindow::clearStreamingPreview()
{
    if (!streamingPreviewActive && streamingPartialText.isEmpty() && lastStreamingPreviewDisplayText.isEmpty())
    {
        return;
    }
    streamingPartialText.clear();
    streamingPreviewActive = false;
    lastStreamingPreviewDisplayText.clear();
    lastStreamingPreviewUiMs = -1;
    PerfTracer::markTrace("STREAM", "ui", "clear partial preview");
}

void MainWindow::updateStreamingPreviewText(bool force)
{
    if (!rawTextEdit)
    {
        return;
    }

    const QString confirmed = transcriptAssembler.currentText();
    const QString partial = streamingPartialText.trimmed();
    QString displayText = confirmed;
    if (!partial.isEmpty())
    {
        if (!displayText.isEmpty() && !displayText.endsWith('\n'))
        {
            displayText += '\n';
        }
        displayText += partial;
        displayText += "▌";
    }

    const qint64 nowMs = PerfTracer::nowMs();
    if (!force && lastStreamingPreviewUiMs >= 0 && nowMs - lastStreamingPreviewUiMs < 180)
    {
        return;
    }
    if (!force && displayText == lastStreamingPreviewDisplayText)
    {
        PerfTracer::markTrace("STREAM", "ui", "partial preview skipped same text",
                              QString("text_len=%1").arg(partial.size()));
        return;
    }

    rawTextEdit->setPlainText(displayText);
    rawTextEdit->moveCursor(QTextCursor::End);
    lastStreamingPreviewDisplayText = displayText;
    lastStreamingPreviewUiMs = nowMs;
    PerfTracer::markTrace("STREAM", "ui", "partial preview update",
                          QString("text_len=%1").arg(partial.size()));
}

void MainWindow::resetTranscript()
{
    transcriptAssembler.clear();
    lastResultEndTimeMs = -1;
    clearStreamingPreview();
}

void MainWindow::setOptimizedText(const QString &text)
{
    optimizedTextEdit->setPlainText(text);
}

void MainWindow::setRunningStatus(const QString &status)
{
    currentStatus = status;
    updateStatusBar();
}

void MainWindow::setLastAsrTime(qint64 ms)
{
    lastAsrTimeMs = ms;
    updateStatusBar();
}

void MainWindow::setAiConfigured(bool configured, const QString &provider)
{
    aiConfigured = configured;
    aiProvider = provider;
    aiStatusLabel->setStyleSheet(configured
                                     ? "color: #166534; font-weight: 700; background-color: #dcfce7; border: 1px solid #bbf7d0; border-radius: 13px; padding: 4px 10px;"
                                     : "color: #475569; font-weight: 700; background-color: #f1f5f9; border: 1px solid #e2e8f0; border-radius: 13px; padding: 4px 10px;");
    aiStatusLabel->setText(configured ? ("AI：已配置 " + provider) : "AI：未配置");
    updateStatusBar();
}

void MainWindow::setAiOptimizeBusy(bool busy)
{
    if (aiOptimizeButton)
    {
        aiOptimizeButton->setEnabled(!busy);
    }
    if (customPromptButton)
    {
        customPromptButton->setEnabled(!busy);
    }
}

void MainWindow::setFileTranscriptionBusy(bool busy)
{
    fileTranscriptionBusy = busy;
    if (fileButton)
    {
        fileButton->setEnabled(!busy);
    }
    if (startFileTranscribeButton)
    {
        startFileTranscribeButton->setEnabled(!busy);
    }
    if (realtimeNavButton)
    {
        realtimeNavButton->setEnabled(!busy);
    }
    if (fileNavButton)
    {
        fileNavButton->setEnabled(!busy);
    }
    if (historyNavButton)
    {
        historyNavButton->setEnabled(!busy);
    }
}

void MainWindow::updateStatusBar()
{
    const QString timeText = lastAsrTimeMs < 0 ? "--" : QString::number(lastAsrTimeMs) + " ms";
    const QString aiText = aiConfigured ? ("已配置 " + aiProvider) : "未配置";
    QString pageText = "实时语音输入";
    if (currentPage == PageMode::FileTranscription)
    {
        pageText = "本地文件转写";
    }
    else if (currentPage == PageMode::History)
    {
        pageText = "历史记录";
    }

    statusBarLabel->setText(
        "<span style=\"color:#2563eb;font-weight:600;\">当前状态：" + currentStatus.toHtmlEscaped() + "</span>"
        "<span style=\"color:#94a3b8;\"> &nbsp;|&nbsp; </span>"
        "<span>页面：" + pageText.toHtmlEscaped() + "</span>"
        "<span style=\"color:#94a3b8;\"> &nbsp;|&nbsp; </span>"
        "<span>模型：sherpa-onnx / Paraformer</span>"
        "<span style=\"color:#94a3b8;\"> &nbsp;|&nbsp; </span>"
        "<span>AI：" + aiText.toHtmlEscaped() + "</span>"
        "<span style=\"color:#94a3b8;\"> &nbsp;|&nbsp; </span>"
        "<span>最近一句识别耗时：" + timeText.toHtmlEscaped() + "</span>");
}

QString MainWindow::rawText() const
{
    if (streamingPreviewActive)
    {
        return confirmedRawText();
    }
    return rawTextEdit ? rawTextEdit->toPlainText().trimmed() : QString();
}

QString MainWindow::confirmedRawText() const
{
    const QString confirmed = transcriptAssembler.currentText().trimmed();
    if (!confirmed.isEmpty())
    {
        return confirmed;
    }
    if (!streamingPreviewActive && rawTextEdit)
    {
        return rawTextEdit->toPlainText().trimmed();
    }
    return QString();
}

QString MainWindow::optimizedText() const
{
    return optimizedTextEdit ? optimizedTextEdit->toPlainText().trimmed() : QString();
}

QString MainWindow::preferredOutputText() const
{
    const QString opt = optimizedText();
    if (!opt.isEmpty())
    {
        return opt;
    }
    return rawText();
}

QString MainWindow::buildExportContent() const
{
    const QString raw = rawText();
    const QString opt = optimizedText();

    QString content = "# VoiceFlow AI 转写结果\n\n";
    if (!raw.isEmpty())
    {
        content += "## 原始识别文本\n\n";
        content += raw + "\n\n";
    }

    if (!opt.isEmpty())
    {
        content += "## 优化后文本\n\n";
        content += opt + "\n";
    }

    return content.trimmed() + "\n";
}

bool MainWindow::writeUtf8File(const QString &filePath, const QString &content, QString *errorMessage) const
{
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        if (errorMessage)
        {
            *errorMessage = file.errorString();
        }
        return false;
    }

    QTextStream out(&file);
    out.setEncoding(QStringConverter::Utf8);
    out << content;
    out.flush();

    if (out.status() != QTextStream::Ok)
    {
        if (errorMessage)
        {
            *errorMessage = "文件写入失败";
        }
        return false;
    }
    return true;
}

QString MainWindow::chooseLocalFile()
{
    return QFileDialog::getOpenFileName(
        this,
        "选择本地音视频文件",
        "",
        "音视频文件 (*.wav *.mp3 *.m4a *.aac *.flac *.ogg *.mp4 *.mov *.avi *.mkv *.wmv);;所有文件 (*.*)");
}

bool MainWindow::isSupportedMediaFile(const QString &path) const
{
    const QString ext = QFileInfo(path).suffix().toLower();
    static const QStringList supported = {
        "wav", "mp3", "m4a", "aac", "flac", "ogg",
        "mp4", "mov", "avi", "mkv", "wmv"};
    return supported.contains(ext);
}

void MainWindow::clearCurrentTexts()
{
    clearStreamingPreview();
    if (rawTextEdit)
    {
        rawTextEdit->clear();
    }
    if (optimizedTextEdit)
    {
        optimizedTextEdit->clear();
    }
    transcriptAssembler.clear();
    lastResultEndTimeMs = -1;
    selectedFilePath.clear();
}

void MainWindow::reloadHistoryRecords()
{
    QString errorMessage;
    if (!historyManager.load(&errorMessage))
    {
        historyRecords.clear();
        historyTable->setRowCount(0);
        setRunningStatus("历史记录加载失败：" + errorMessage);
        updateHistoryButtonsEnabled();
        return;
    }

    historyRecords = historyManager.records();
    renderHistoryTable();
}

void MainWindow::renderHistoryTable()
{
    historyTable->setRowCount(historyRecords.size());
    for (int row = 0; row < historyRecords.size(); ++row)
    {
        const HistoryRecord &record = historyRecords.at(row);
        historyTable->setItem(row, 0, new QTableWidgetItem(record.createdAt));
        historyTable->setItem(row, 1, new QTableWidgetItem(record.title));
        historyTable->setItem(row, 2, new QTableWidgetItem(record.sourceType == "file" ? "本地文件转写" : "实时语音输入"));
        historyTable->setItem(row, 3, new QTableWidgetItem(record.optimizedText.trimmed().isEmpty() ? "否" : "是"));
    }

    for (int row = 0; row < historyTable->rowCount(); ++row)
    {
        historyTable->setRowHeight(row, 40);
        for (int column = 0; column < historyTable->columnCount(); ++column)
        {
            QTableWidgetItem *item = historyTable->item(row, column);
            if (!item)
            {
                continue;
            }
            item->setTextAlignment(column == 3 ? Qt::AlignCenter : (Qt::AlignLeft | Qt::AlignVCenter));
            if (column == 3 && !historyRecords.at(row).optimizedText.trimmed().isEmpty())
            {
                item->setForeground(QColor("#16a34a"));
            }
        }
    }

    if (!historyRecords.isEmpty())
    {
        historyTable->selectRow(0);
        showHistoryRecordByRow(0);
    }
    else
    {
        rawTextEdit->clear();
        optimizedTextEdit->clear();
    }

    updateHistoryButtonsEnabled();
}

void MainWindow::showHistoryRecordByRow(int row)
{
    if (row < 0 || row >= historyRecords.size())
    {
        return;
    }

    const HistoryRecord &record = historyRecords.at(row);
    setRawText(record.rawText);
    setOptimizedText(record.optimizedText);
}

int MainWindow::currentHistoryRow() const
{
    if (!historyTable)
    {
        return -1;
    }
    return historyTable->currentRow();
}

QString MainWindow::historySourceText(const HistoryRecord &record) const
{
    if (!record.optimizedText.trimmed().isEmpty())
    {
        return record.optimizedText;
    }
    return record.rawText;
}

QString MainWindow::buildHistoryExportContent(const HistoryRecord &record) const
{
    QString content = "# VoiceFlow AI 历史记录\n\n";
    content += "## 基本信息\n\n";
    content += "标题：" + record.title + "\n";
    content += "时间：" + record.createdAt + "\n";
    content += "来源：" + QString(record.sourceType == "file" ? "本地文件转写" : "实时语音输入") + "\n";
    if (!record.sourceFile.trimmed().isEmpty())
    {
        content += "文件：" + record.sourceFile + "\n";
    }
    content += "\n";

    if (!record.rawText.trimmed().isEmpty())
    {
        content += "## 原始识别文本\n\n";
        content += record.rawText + "\n\n";
    }
    if (!record.optimizedText.trimmed().isEmpty())
    {
        content += "## 优化后文本\n\n";
        content += record.optimizedText + "\n";
    }
    return content.trimmed() + "\n";
}

void MainWindow::updateHistoryButtonsEnabled()
{
    const bool hasSelection = currentHistoryRow() >= 0 && currentHistoryRow() < historyRecords.size();
    historyLoadButton->setEnabled(hasSelection);
    historyCopyButton->setEnabled(hasSelection);
    historyExportButton->setEnabled(hasSelection);
    historyDeleteButton->setEnabled(hasSelection);
    historyClearAllButton->setEnabled(!historyRecords.isEmpty());
}

void MainWindow::handleHistoryLoadToEditor()
{
    const int row = currentHistoryRow();
    if (row < 0 || row >= historyRecords.size())
    {
        setRunningStatus("请先选择历史记录");
        return;
    }

    const HistoryRecord record = historyRecords.at(row);
    setPageMode(PageMode::RealtimeInput);
    setRawText(record.rawText);
    setOptimizedText(record.optimizedText);
    setRunningStatus("已加载历史记录");
}

void MainWindow::handleHistoryCopy()
{
    const int row = currentHistoryRow();
    if (row < 0 || row >= historyRecords.size())
    {
        setRunningStatus("请先选择历史记录");
        return;
    }

    const QString text = historySourceText(historyRecords.at(row)).trimmed();
    if (text.isEmpty())
    {
        setRunningStatus("没有可复制的文本");
        return;
    }

    QApplication::clipboard()->setText(text);
    setRunningStatus("已复制历史记录文本");
}

void MainWindow::handleHistoryExport()
{
    const int row = currentHistoryRow();
    if (row < 0 || row >= historyRecords.size())
    {
        setRunningStatus("请先选择历史记录");
        return;
    }

    const HistoryRecord &record = historyRecords.at(row);
    const QString defaultName = QString("VoiceFlowAI_history_%1.md").arg(QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss"));
    const QString filePath = QFileDialog::getSaveFileName(
        this,
        "导出历史记录",
        QDir::currentPath() + "/" + defaultName,
        "文本文件 (*.txt);;Markdown 文件 (*.md)");
    if (filePath.isEmpty())
    {
        return;
    }

    QString errorMessage;
    if (!writeUtf8File(filePath, buildHistoryExportContent(record), &errorMessage))
    {
        setRunningStatus("导出失败：" + errorMessage);
        return;
    }

    setRunningStatus("导出成功：" + QFileInfo(filePath).fileName());
}

void MainWindow::handleHistoryDelete()
{
    const int row = currentHistoryRow();
    if (row < 0 || row >= historyRecords.size())
    {
        setRunningStatus("请先选择历史记录");
        return;
    }

    const HistoryRecord record = historyRecords.at(row);
    const auto reply = QMessageBox::question(
        this,
        "删除历史记录",
        "确定删除这条历史记录吗？",
        QMessageBox::Yes | QMessageBox::No);
    if (reply != QMessageBox::Yes)
    {
        return;
    }

    QString errorMessage;
    if (!historyManager.deleteRecord(record.id, &errorMessage))
    {
        setRunningStatus("删除失败：" + errorMessage);
        return;
    }

    setRunningStatus("历史记录已删除");
    reloadHistoryRecords();
}

void MainWindow::handleHistoryClearAll()
{
    if (historyRecords.isEmpty())
    {
        setRunningStatus("历史记录为空");
        return;
    }

    const auto reply = QMessageBox::warning(
        this,
        "清空历史记录",
        "确定清空全部历史记录吗？此操作不可恢复。",
        QMessageBox::Yes | QMessageBox::No);
    if (reply != QMessageBox::Yes)
    {
        return;
    }

    QString errorMessage;
    if (!historyManager.clearAll(&errorMessage))
    {
        setRunningStatus("清空失败：" + errorMessage);
        return;
    }

    setRunningStatus("历史记录已清空");
    reloadHistoryRecords();
}
