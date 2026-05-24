#include "MainWindow.h"

#include <QApplication>
#include <QClipboard>
#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFile>
#include <QFrame>
#include <QGraphicsDropShadowEffect>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPalette>
#include <QPushButton>
#include <QSizePolicy>
#include <QTextCursor>
#include <QTextEdit>
#include <QTextStream>
#include <QStringConverter>
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
    setMinimumSize(1200, 800);
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
        "text-align: center; padding: 0 12px; min-height: 36px; font-size: 14px; border-radius: 6px; }"
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
    auto *historyBtn = new QPushButton("历史记录", sideBar);
    auto *settingTabBtn = new QPushButton("设置", sideBar);

    auto *sideLayout = new QVBoxLayout(sideBar);
    sideLayout->setContentsMargins(20, 24, 20, 24);
    sideLayout->setSpacing(8);
    sideLayout->addWidget(logo);
    sideLayout->addSpacing(12);
    sideLayout->addWidget(logoDivider);
    sideLayout->addSpacing(16);
    sideLayout->addWidget(realtimeNavButton);
    sideLayout->addWidget(fileNavButton);
    sideLayout->addWidget(historyBtn);
    sideLayout->addWidget(settingTabBtn);
    sideLayout->addStretch();

    auto *mainPanel = new QFrame(this);
    mainPanel->setStyleSheet("QFrame { background-color: #f8fafc; }");

    pageTitleLabel = new QLabel("实时语音输入", mainPanel);
    pageTitleLabel->setStyleSheet("font-size: 26px; font-weight: 700; color: #1e293b;");

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
        "QFrame#configCard { background-color: white; border: 1px solid #e2e8f0; border-radius: 6px; }"
        "QLabel { font-size: 13px; color: #64748b; }"
        "QLabel#aiStateText { color: #334155; font-weight: 600; }"
        "QPushButton { padding: 0 12px; border-radius: 6px; border: 1px solid #e2e8f0;"
        "background-color: white; color: #475569; font-size: 13px; min-height: 34px; }"
        "QPushButton:hover { background-color: #eff6ff; border-color: #2563eb; color: #1e293b; }");
    addSoftShadow(configCard);

    aiStatusLabel = new QLabel("AI：未配置", configCard);
    aiStatusLabel->setObjectName("aiStateText");
    asrInfoLabel = new QLabel("语音识别：sherpa-onnx / Paraformer 本地离线识别 | 本地处理：VAD 分句 + 标点恢复", configCard);

    settingsButton = new QPushButton("打开设置", configCard);
    testAiButton = new QPushButton("测试连接", configCard);

    auto *configLayout = new QVBoxLayout(configCard);
    configLayout->setContentsMargins(16, 14, 16, 14);
    configLayout->setSpacing(10);

    auto *configRow = new QHBoxLayout();
    configRow->setSpacing(8);
    configRow->addWidget(new QLabel("AI配置状态：", configCard));
    configRow->addWidget(aiStatusLabel);
    configRow->addSpacing(12);
    configRow->addWidget(settingsButton);
    configRow->addWidget(testAiButton);
    configRow->addStretch();

    configLayout->addLayout(configRow);
    configLayout->addWidget(asrInfoLabel);

    const QString cardStyle =
        "QFrame#textCard { background-color: white; border: 1px solid #e2e8f0; border-radius: 6px; }"
        "QLabel { font-size: 16px; font-weight: 700; color: #1e293b; }"
        "QTextEdit { border: 1px solid #e2e8f0; border-radius: 6px; font-size: 14px; color: #1e293b;"
        "line-height: 160%; padding: 12px; background-color: #f8fafc; selection-background-color: #bfdbfe; }"
        "QTextEdit:hover { border-color: #cbd5e1; }"
        "QTextEdit:focus { border-color: #2563eb; }"
        "QScrollBar:vertical { background: #f1f5f9; width: 6px; margin: 0; border-radius: 3px; }"
        "QScrollBar::handle:vertical { background: #cbd5e1; min-height: 24px; border-radius: 3px; }"
        "QScrollBar::handle:vertical:hover { background: #94a3b8; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; border: none; }"
        "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: none; }";

    auto *rawCard = new QFrame(mainPanel);
    rawCard->setObjectName("textCard");
    rawCard->setStyleSheet(cardStyle);
    rawCard->setMinimumHeight(430);
    rawCard->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    addSoftShadow(rawCard);

    auto *optCard = new QFrame(mainPanel);
    optCard->setObjectName("textCard");
    optCard->setStyleSheet(cardStyle);
    optCard->setMinimumHeight(430);
    optCard->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    addSoftShadow(optCard);

    auto *rawTitle = new QLabel("原始识别文本", rawCard);
    auto *optTitle = new QLabel("优化后文本", optCard);

    rawTextEdit = new QTextEdit(rawCard);
    rawTextEdit->setPlaceholderText("语音识别结果将在这里逐句显示。");

    optimizedTextEdit = new QTextEdit(optCard);
    optimizedTextEdit->setPlaceholderText("点击 AI智能优化 或 自定义提示词 后，结果将在这里显示。");

    QPalette placeholderPalette = rawTextEdit->palette();
    placeholderPalette.setColor(QPalette::PlaceholderText, QColor("#94a3b8"));
    rawTextEdit->setPalette(placeholderPalette);
    optimizedTextEdit->setPalette(placeholderPalette);

    auto *rawLayout = new QVBoxLayout(rawCard);
    rawLayout->setContentsMargins(16, 16, 16, 16);
    rawLayout->setSpacing(10);
    rawLayout->addWidget(rawTitle);
    rawLayout->addWidget(rawTextEdit);

    auto *optLayout = new QVBoxLayout(optCard);
    optLayout->setContentsMargins(16, 16, 16, 16);
    optLayout->setSpacing(10);
    optLayout->addWidget(optTitle);
    optLayout->addWidget(optimizedTextEdit);

    auto *textDivider = new QFrame(mainPanel);
    textDivider->setFixedWidth(1);
    textDivider->setStyleSheet("QFrame { background-color: #e2e8f0; border: none; }");

    auto *textLayout = new QHBoxLayout();
    textLayout->setSpacing(16);
    textLayout->addWidget(rawCard, 1);
    textLayout->addWidget(textDivider);
    textLayout->addWidget(optCard, 1);

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

    statusBarLabel = new QLabel(mainPanel);
    statusBarLabel->setTextFormat(Qt::RichText);
    statusBarLabel->setAlignment(Qt::AlignCenter);
    statusBarLabel->setMinimumHeight(40);
    statusBarLabel->setStyleSheet(
        "background-color: #f1f5f9; border: 1px solid #e2e8f0; border-radius: 6px;"
        "padding: 0 16px; color: #64748b; font-size: 13px;");

    auto *mainLayout = new QVBoxLayout(mainPanel);
    mainLayout->setContentsMargins(24, 20, 24, 20);
    mainLayout->setSpacing(12);
    mainLayout->addLayout(titleLayout);
    mainLayout->addWidget(configCard);
    mainLayout->addLayout(textLayout, 1);
    mainLayout->addLayout(buttonLayout);
    mainLayout->addWidget(statusBarLabel);

    auto *rootLayout = new QHBoxLayout(central);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);
    rootLayout->addWidget(sideBar);
    rootLayout->addWidget(mainPanel, 1);

    connect(settingTabBtn, &QPushButton::clicked, this, [this]()
            { emit openSettingsRequested(); });

    connect(historyBtn, &QPushButton::clicked, this, [this]()
            { setRunningStatus("历史记录功能正在规划中。"); });

}

void MainWindow::setupConnections()
{
    connect(this, &MainWindow::clearRequested, this, &MainWindow::resetTranscript);

    connect(realtimeNavButton, &QPushButton::clicked, this, [this]()
            { setPageMode(PageMode::RealtimeInput); });
    connect(fileNavButton, &QPushButton::clicked, this, [this]()
            { setPageMode(PageMode::FileTranscription); });

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
                if (selectedFilePath.trimmed().isEmpty())
                {
                    setRunningStatus("请先选择文件");
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

                const QString rawText = rawTextEdit->toPlainText().trimmed();
                if (rawText.isEmpty())
                {
                    QMessageBox::warning(this, "无文本", "请先输入或识别文本。");
                    return;
                }

                setRunningStatus("正在调用 AI 智能优化");
                emit aiOptimizeRequested(rawText);
            });

    connect(customPromptButton, &QPushButton::clicked, this, [this]()
            {
                if (!aiConfigured)
                {
                    QMessageBox::warning(this, "AI 未配置", "请先配置 DeepSeek API Key。");
                    emit openSettingsRequested();
                    return;
                }

                const QString rawText = rawTextEdit->toPlainText().trimmed();
                if (rawText.isEmpty())
                {
                    QMessageBox::warning(this, "无文本", "没有可优化的文本。");
                    return;
                }

                emit customPromptOptimizeRequested(rawText);
            });

    connect(settingsButton, &QPushButton::clicked, this, [this]()
            { emit openSettingsRequested(); });

    connect(testAiButton, &QPushButton::clicked, this, [this]()
            { emit testAiConnectionRequested(); });
}

void MainWindow::setPageMode(PageMode mode)
{
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
    }
    else
    {
        pageTitleLabel->setText("本地文件转写");
        pageDescLabel->setText("选择本地 WAV 音频文件，自动转写为可编辑文本，并支持 AI 智能优化。");
        if (selectedFilePath.isEmpty())
        {
            currentStatus = "未选择文件";
        }
        else
        {
            currentStatus = "已选择文件：" + QFileInfo(selectedFilePath).fileName();
        }
    }

    updateNavButtonStyles();
    updateLeftActionButtons();
    updateStatusBar();
}

void MainWindow::updateNavButtonStyles()
{
    const QString activeStyle =
        "QPushButton { color: white; background-color: #2563eb; border: none;"
        "text-align: center; padding: 0 12px; min-height: 36px; font-size: 14px; border-radius: 6px; font-weight: 600; }"
        "QPushButton:hover { background-color: #1d4ed8; }";

    const QString normalStyle =
        "QPushButton { color: #cbd5e1; background: transparent; border: none;"
        "text-align: center; padding: 0 12px; min-height: 36px; font-size: 14px; border-radius: 6px; }"
        "QPushButton:hover { background-color: #1e293b; color: white; }";

    realtimeNavButton->setStyleSheet(currentPage == PageMode::RealtimeInput ? activeStyle : normalStyle);
    fileNavButton->setStyleSheet(currentPage == PageMode::FileTranscription ? activeStyle : normalStyle);
}

void MainWindow::updateLeftActionButtons()
{
    const bool realtime = currentPage == PageMode::RealtimeInput;
    startButton->setVisible(realtime);
    stopButton->setVisible(realtime);
    fileButton->setVisible(!realtime);
    startFileTranscribeButton->setVisible(!realtime);
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
    rawTextEdit->setPlainText(text);
    rawTextEdit->moveCursor(QTextCursor::End);
}

void MainWindow::handleAsrResult(const AsrResult &result)
{
    if (!result.success || result.text.trimmed().isEmpty())
    {
        return;
    }

    qint64 gapMs = 0;
    if (lastResultEndTimeMs >= 0 && result.startTimeMs >= 0)
    {
        gapMs = qMax<qint64>(0, result.startTimeMs - lastResultEndTimeMs);
    }

    const QString assembledText = transcriptAssembler.appendSegment(result.text, gapMs);
    setRawText(assembledText);

    if (result.endTimeMs >= 0)
    {
        lastResultEndTimeMs = result.endTimeMs;
    }
}

void MainWindow::resetTranscript()
{
    transcriptAssembler.clear();
    lastResultEndTimeMs = -1;
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

void MainWindow::updateStatusBar()
{
    const QString timeText = lastAsrTimeMs < 0 ? "--" : QString::number(lastAsrTimeMs) + " ms";
    const QString aiText = aiConfigured ? ("已配置 " + aiProvider) : "未配置";
    const QString pageText = currentPage == PageMode::RealtimeInput ? "实时语音输入" : "本地文件转写";

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
    return rawTextEdit ? rawTextEdit->toPlainText().trimmed() : QString();
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

void MainWindow::clearCurrentTexts()
{
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
        "选择本地音频文件",
        "",
        "WAV 文件 (*.wav);;媒体文件 (*.wav *.mp3 *.m4a *.aac *.webm *.mp4 *.mov *.avi *.mkv);;所有文件 (*.*)");
}
