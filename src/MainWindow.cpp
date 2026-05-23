#include "MainWindow.h"

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QFileDialog>
#include <QFrame>
#include <QGraphicsDropShadowEffect>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPalette>
#include <QPushButton>
#include <QSizePolicy>
#include <QTextEdit>
#include <QVBoxLayout>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setupUi();
    setupConnections();
    updateStatusBar();
}

TaskConfig MainWindow::currentTaskConfig() const
{
    TaskConfig config;

    // 将当前 UI 控件状态整理成任务配置，作为所有业务信号的统一入参。
    int modelIndex = modelComboBox->currentIndex();
    if (modelIndex == 0)
    {
        config.modelMode = "tiny";
        config.modelText = "tiny 极速模式";
    }
    else if (modelIndex == 1)
    {
        config.modelMode = "base";
        config.modelText = "base 均衡模式";
    }
    else
    {
        config.modelMode = "small";
        config.modelText = "small 高准确模式";
    }

    int textIndex = textModeComboBox->currentIndex();
    if (textIndex == 0)
    {
        config.textMode = "recognize";
        config.textModeText = "仅识别";
    }
    else
    {
        config.textMode = "offline";
        config.textModeText = "离线优化";
    }

    int wordIndex = wordLibComboBox->currentIndex();
    if (wordIndex == 0)
    {
        config.wordLib = "general";
        config.wordLibText = "通用词库";
    }
    else if (wordIndex == 1)
    {
        config.wordLib = "code";
        config.wordLibText = "编程词库";
    }
    else if (wordIndex == 2)
    {
        config.wordLib = "academic";
        config.wordLibText = "学术词库";
    }
    else
    {
        config.wordLib = "custom";
        config.wordLibText = "自定义词库";
    }

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

    // 统一卡片阴影，避免每个卡片重复创建相同的 QGraphicsDropShadowEffect。
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

    // ================= 左侧导航栏 =================
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
    logo->setMinimumHeight(28);
    logo->setStyleSheet("font-size: 18px; font-weight: 700; color: white;");

    auto *logoDivider = new QFrame(sideBar);
    logoDivider->setFrameShape(QFrame::HLine);
    logoDivider->setFixedHeight(1);
    logoDivider->setStyleSheet("background-color: #334155; border: none;");

    auto *realtimeBtn = new QPushButton("实时语音输入", sideBar);
    auto *fileTabBtn = new QPushButton("本地文件转写", sideBar);
    auto *historyBtn = new QPushButton("历史记录", sideBar);
    auto *settingTabBtn = new QPushButton("设置", sideBar);

    realtimeBtn->setStyleSheet(
        "QPushButton { color: white; background-color: #2563eb; border: none;"
        "text-align: center; padding: 0 12px; min-height: 36px; font-size: 14px; border-radius: 6px; font-weight: 600; }"
        "QPushButton:hover { background-color: #1d4ed8; }");

    auto *sideLayout = new QVBoxLayout(sideBar);
    sideLayout->setContentsMargins(20, 24, 20, 24);
    sideLayout->setSpacing(8);
    sideLayout->addWidget(logo);
    sideLayout->addSpacing(12);
    sideLayout->addWidget(logoDivider);
    sideLayout->addSpacing(16);
    sideLayout->addWidget(realtimeBtn);
    sideLayout->addWidget(fileTabBtn);
    sideLayout->addWidget(historyBtn);
    sideLayout->addWidget(settingTabBtn);
    sideLayout->addStretch();

    // ================= 右侧主区域 =================
    auto *mainPanel = new QFrame(this);
    mainPanel->setStyleSheet("QFrame { background-color: #f8fafc; }");

    pageTitleLabel = new QLabel("实时语音输入", mainPanel);
    pageTitleLabel->setStyleSheet("font-size: 26px; font-weight: 700; color: #1e293b; line-height: 150%;");

    pageDescLabel = new QLabel("通过麦克风实时输入语音，自动识别为可编辑文本，并支持可选文本优化。", mainPanel);
    pageDescLabel->setStyleSheet("font-size: 14px; color: #64748b; line-height: 150%;");

    runStatusBadge = new QLabel("●  状态：未开始", mainPanel);
    runStatusBadge->setAlignment(Qt::AlignCenter);
    runStatusBadge->setMinimumSize(132, 36);
    runStatusBadge->setStyleSheet(
        "background-color: #f1f5f9; border: 1px solid #e2e8f0; border-radius: 18px;"
        "color: #64748b; font-size: 13px; padding: 0 12px;");

    auto *titleTextLayout = new QVBoxLayout();
    titleTextLayout->addWidget(pageTitleLabel);
    titleTextLayout->addWidget(pageDescLabel);

    auto *titleLayout = new QHBoxLayout();
    titleLayout->addLayout(titleTextLayout);
    titleLayout->addStretch();
    titleLayout->addWidget(runStatusBadge);

    // ================= 参数区 =================
    // 参数区使用两行布局：第一行放识别任务配置，第二行放 AI 连接相关操作。
    auto *configCard = new QFrame(mainPanel);
    configCard->setObjectName("configCard");
    configCard->setStyleSheet(
        "QFrame#configCard { background-color: white; border: 1px solid #e2e8f0; border-radius: 6px; }"
        "QLabel { font-size: 13px; color: #64748b; }"
        "QLabel#aiStateText { color: #94a3b8; min-width: 72px; }"
        "QComboBox { padding: 0 12px; border: 1px solid #e2e8f0; border-radius: 6px;"
        "font-size: 13px; min-width: 132px; min-height: 34px; background-color: white; color: #1e293b; }"
        "QComboBox:hover { border-color: #2563eb; }"
        "QComboBox::drop-down { width: 24px; border: none; }"
        "QPushButton { padding: 0 12px; border-radius: 6px; border: 1px solid #e2e8f0;"
        "background-color: white; color: #475569; font-size: 13px; min-height: 34px; }"
        "QPushButton:hover { background-color: #eff6ff; border-color: #2563eb; color: #1e293b; }");
    addSoftShadow(configCard);

    modelComboBox = new QComboBox(configCard);
    modelComboBox->addItems({"tiny 极速模式", "base 均衡模式", "small 高准确模式"});
    modelComboBox->setCurrentIndex(1);
    modelComboBox->setMinimumContentsLength(12);

    textModeComboBox = new QComboBox(configCard);
    textModeComboBox->addItems({"仅识别", "离线优化"});
    textModeComboBox->setMinimumContentsLength(8);

    wordLibComboBox = new QComboBox(configCard);
    wordLibComboBox->addItems({"通用词库", "编程词库", "学术词库", "自定义词库"});
    wordLibComboBox->setMinimumContentsLength(8);

    aiStatusLabel = new QLabel("AI：未配置", configCard);
    aiStatusLabel->setObjectName("aiStateText");

    auto *manageWordLibBtn = new QPushButton("管理词库", configCard);
    settingsButton = new QPushButton("打开设置", configCard);
    testAiButton = new QPushButton("测试连接", configCard);

    auto *configLayout = new QVBoxLayout(configCard);
    configLayout->setContentsMargins(16, 14, 16, 14);
    configLayout->setSpacing(10);

    auto *configFirstRow = new QHBoxLayout();
    configFirstRow->setSpacing(8);
    configFirstRow->setAlignment(Qt::AlignVCenter);

    auto *configSecondRow = new QHBoxLayout();
    configSecondRow->setSpacing(8);
    configSecondRow->setAlignment(Qt::AlignVCenter);

    // 第一行：模型、文本处理、词库，按使用频率从左到右排列。
    configFirstRow->addWidget(new QLabel("识别模型"));
    configFirstRow->addWidget(modelComboBox);
    configFirstRow->addSpacing(8);

    configFirstRow->addWidget(new QLabel("文本处理模式"));
    configFirstRow->addWidget(textModeComboBox);
    configFirstRow->addSpacing(8);

    configFirstRow->addWidget(new QLabel("当前词库"));
    configFirstRow->addWidget(wordLibComboBox);
    configFirstRow->addWidget(manageWordLibBtn);
    configFirstRow->addStretch();

    // 第二行：AI 配置状态和相关操作，和识别参数保持视觉分组。
    configSecondRow->addWidget(new QLabel("AI配置状态"));
    configSecondRow->addWidget(aiStatusLabel);
    configSecondRow->addWidget(settingsButton);
    configSecondRow->addWidget(testAiButton);
    configSecondRow->addStretch();

    configLayout->addLayout(configFirstRow);
    configLayout->addLayout(configSecondRow);

    // ================= 文本框区域 =================
    // 文本卡片只承担容器职责，标题保持普通文本，真正的输入边框只给 QTextEdit。
    QString cardStyle =
        "QFrame#textCard { background-color: white; border: 1px solid #e2e8f0; border-radius: 6px; }"
        "QLabel { font-size: 16px; font-weight: 700; color: #1e293b; line-height: 150%; }"
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
    optimizedTextEdit->setPlaceholderText("点击离线优化或 AI 智能优化后，结果将在这里显示。");

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
    textDivider->setStyleSheet(
        "QFrame { background-color: #e2e8f0; border: none; }"
        "QFrame:hover { background-color: #cbd5e1; }");

    auto *textLayout = new QHBoxLayout();
    textLayout->setSpacing(16);
    // 两个文本卡片使用相同伸缩权重，保持 1:1 等宽。
    textLayout->addWidget(rawCard, 1);
    textLayout->addWidget(textDivider);
    textLayout->addWidget(optCard, 1);

    // ================= 操作按钮区 =================
    startButton = new QPushButton("开始输入", mainPanel);
    stopButton = new QPushButton("停止", mainPanel);
    fileButton = new QPushButton("选择文件", mainPanel);
    copyButton = new QPushButton("复制", mainPanel);
    exportButton = new QPushButton("导出", mainPanel);
    saveButton = new QPushButton("保存", mainPanel);
    clearButton = new QPushButton("清空", mainPanel);
    // 离线优化按钮是局部按钮，不需要作为成员保存；AI 优化按钮已有后续槽函数使用。
    auto *offlineOptimizeButton = new QPushButton("离线优化", mainPanel);
    aiOptimizeButton = new QPushButton("AI智能优化", mainPanel);

    startButton->setStyleSheet(
        "QPushButton { background-color: #2563eb; color: white; border: none;"
        "padding: 0 20px; border-radius: 6px; font-size: 14px; font-weight: 700; min-height: 36px; }"
        "QPushButton:hover { background-color: #1d4ed8; }"
        "QPushButton:pressed { background-color: #1e40af; }");

    QString normalButtonStyle =
        "QPushButton { background-color: white; color: #475569; border: 1px solid #e2e8f0;"
        "padding: 0 16px; border-radius: 6px; font-size: 14px; min-height: 36px; }"
        "QPushButton:hover { background-color: #eff6ff; border-color: #2563eb; color: #1e293b; }"
        "QPushButton:pressed { background-color: #dbeafe; }";

    QString aiButtonStyle =
        "QPushButton { background-color: #f97316; color: white; border: 1px solid #f97316;"
        "padding: 0 18px; border-radius: 6px; font-size: 14px; font-weight: 700; min-height: 36px; }"
        "QPushButton:hover { background-color: #ea580c; border-color: #ea580c; }"
        "QPushButton:pressed { background-color: #c2410c; border-color: #c2410c; }";

    stopButton->setStyleSheet(normalButtonStyle);
    fileButton->setStyleSheet(normalButtonStyle);
    offlineOptimizeButton->setStyleSheet(normalButtonStyle);
    aiOptimizeButton->setStyleSheet(aiButtonStyle);
    copyButton->setStyleSheet(normalButtonStyle);
    exportButton->setStyleSheet(normalButtonStyle);
    saveButton->setStyleSheet(normalButtonStyle);
    clearButton->setStyleSheet(normalButtonStyle);

    auto *buttonLayout = new QHBoxLayout();
    buttonLayout->setSpacing(8);
    buttonLayout->setAlignment(Qt::AlignBottom);

    // 底部操作按业务意图分三组：输入来源、文本优化、结果处理。
    auto *leftButtonGroup = new QHBoxLayout();
    leftButtonGroup->setSpacing(8);
    leftButtonGroup->addWidget(startButton);
    leftButtonGroup->addWidget(stopButton);
    leftButtonGroup->addWidget(fileButton);

    auto *middleButtonGroup = new QHBoxLayout();
    middleButtonGroup->setSpacing(8);
    middleButtonGroup->addWidget(offlineOptimizeButton);
    middleButtonGroup->addWidget(aiOptimizeButton);

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

    // ================= 状态栏 =================
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

    connect(modelComboBox, &QComboBox::currentIndexChanged, this, [this]()
            { updateStatusBar(); });

    connect(textModeComboBox, &QComboBox::currentIndexChanged, this, [this]()
            { updateStatusBar(); });

    connect(wordLibComboBox, &QComboBox::currentIndexChanged, this, [this]()
            { updateStatusBar(); });

    connect(settingTabBtn, &QPushButton::clicked, this, [this]()
            {
        emit openSettingsRequested();
        QMessageBox::information(this, "设置", "后续将打开设置页面，用于配置 AI API 和场景词库。"); });

    connect(manageWordLibBtn, &QPushButton::clicked, this, [this]()
            {
        emit openSettingsRequested();
        QMessageBox::information(this, "词库管理", "后续将打开场景词库管理页面。"); });

    // 离线优化和 AI 优化使用相同的原文输入校验，但分别发出不同业务信号。
    connect(offlineOptimizeButton, &QPushButton::clicked, this, [this]()
            {
        QString rawText = rawTextEdit->toPlainText().trimmed();
        if (rawText.isEmpty()) {
            QMessageBox::warning(this, "无文本", "请先输入或识别文本。");
            return;
        }

        currentStatus = "正在进行离线优化";
        updateStatusBar();

        emit offlineOptimizeRequested(rawText); });
}

void MainWindow::setupConnections()
{
    // 开始/停止只更新当前界面的运行态；真正的识别逻辑交给外部业务层处理。
    connect(startButton, &QPushButton::clicked, this, [this]()
            {
        currentStatus = "正在监听，请开始说话";
        runStatusBadge->setText("●  状态：监听中");
        runStatusBadge->setStyleSheet(
            "background-color: #eff6ff; border: 1px solid #bfdbfe; border-radius: 18px;"
            "color: #2563eb; font-size: 13px; padding: 0 12px;"
        );
        startButton->setText("正在输入");
        startButton->setStyleSheet(
            "QPushButton { background-color: #ef4444; color: white; border: none;"
            "padding: 0 20px; border-radius: 6px; font-size: 14px; font-weight: 700; min-height: 36px; }"
            "QPushButton:hover { background-color: #dc2626; }"
            "QPushButton:pressed { background-color: #b91c1c; }"
        );
        updateStatusBar();

        emit startVoiceInputRequested(currentTaskConfig()); });

    connect(stopButton, &QPushButton::clicked, this, [this]()
            {
        currentStatus = "已停止输入";
        runStatusBadge->setText("●  状态：已完成");
        runStatusBadge->setStyleSheet(
            "background-color: #fef2f2; border: 1px solid #fecaca; border-radius: 18px;"
            "color: #dc2626; font-size: 13px; padding: 0 12px;"
        );
        startButton->setText("开始输入");
        startButton->setStyleSheet(
            "QPushButton { background-color: #2563eb; color: white; border: none;"
            "padding: 0 20px; border-radius: 6px; font-size: 14px; font-weight: 700; min-height: 36px; }"
            "QPushButton:hover { background-color: #1d4ed8; }"
            "QPushButton:pressed { background-color: #1e40af; }"
        );
        updateStatusBar();

        emit stopVoiceInputRequested(); });

    connect(fileButton, &QPushButton::clicked, this, [this]()
            {
        QString filePath = chooseLocalFile();
        if (filePath.isEmpty()) {
            return;
        }

        currentStatus = "已选择文件，等待转写";
        updateStatusBar();

        emit fileTranscribeRequested(filePath, currentTaskConfig()); });

    connect(copyButton, &QPushButton::clicked, this, [this]()
            {
        QString text = optimizedTextEdit->toPlainText().trimmed();
        if (text.isEmpty()) {
            text = rawTextEdit->toPlainText().trimmed();
        }

        QApplication::clipboard()->setText(text);
        currentStatus = "文本已复制到剪贴板";
        updateStatusBar();

        emit copyRequested(text); });

    connect(exportButton, &QPushButton::clicked, this, [this]()
            {
        emit exportRequested(rawTextEdit->toPlainText(), optimizedTextEdit->toPlainText());
        QMessageBox::information(this, "导出", "后续将接入 TXT / Markdown 导出模块。"); });

    connect(saveButton, &QPushButton::clicked, this, [this]()
            {
        emit saveRecordRequested(rawTextEdit->toPlainText(), optimizedTextEdit->toPlainText());
        QMessageBox::information(this, "保存", "后续将接入历史记录保存模块。"); });

    connect(clearButton, &QPushButton::clicked, this, [this]()
            {
        rawTextEdit->clear();
        optimizedTextEdit->clear();
        currentStatus = "内容已清空";
        updateStatusBar();

        emit clearRequested(); });

    connect(aiOptimizeButton, &QPushButton::clicked, this, [this]()
            {
        if (!aiConfigured) {
            QMessageBox::warning(this, "AI 未配置", "请先在设置中配置 AI API。");
            return;
        }

        QString rawText = rawTextEdit->toPlainText().trimmed();
        if (rawText.isEmpty()) {
            QMessageBox::warning(this, "无文本", "请先输入或识别文本。");
            return;
        }

        currentStatus = "正在调用 AI 智能优化";
        updateStatusBar();

        emit aiOptimizeRequested(rawText); });

    connect(settingsButton, &QPushButton::clicked, this, [this]()
            {
        emit openSettingsRequested();
        QMessageBox::information(this, "设置", "后续将打开设置页面。"); });

    connect(testAiButton, &QPushButton::clicked, this, [this]()
            {
        emit testAiConnectionRequested();
        QMessageBox::information(this, "测试连接", "后续将测试 AI API 是否可用。"); });
}

void MainWindow::appendRawText(const QString &text)
{
    if (text.trimmed().isEmpty())
    {
        return;
    }

    // 新识别结果追加到末尾，保留逐句显示的阅读节奏。
    rawTextEdit->moveCursor(QTextCursor::End);
    rawTextEdit->insertPlainText(text);
    rawTextEdit->insertPlainText("\n");
    rawTextEdit->moveCursor(QTextCursor::End);
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

    if (configured)
    {
        aiStatusLabel->setText("AI：已配置 " + provider);
    }
    else
    {
        aiStatusLabel->setText("AI：未配置");
    }

    updateStatusBar();
}

void MainWindow::updateStatusBar()
{
    TaskConfig config = currentTaskConfig();

    QString timeText = lastAsrTimeMs < 0
                           ? "--"
                           : QString::number(lastAsrTimeMs) + " ms";

    QString aiText = aiConfigured
                         ? "已配置 " + aiProvider
                         : "未配置";

    // 状态栏使用富文本高亮关键状态，所有动态内容先转义，避免特殊字符破坏 HTML。
    statusBarLabel->setText(
        "<span style=\"color:#2563eb;font-weight:600;\">当前状态：" + currentStatus.toHtmlEscaped() + "</span>"
        "<span style=\"color:#94a3b8;\"> &nbsp;|&nbsp; </span>"
        "<span>模型：" + config.modelText.toHtmlEscaped() + "</span>"
        "<span style=\"color:#94a3b8;\"> &nbsp;|&nbsp; </span>"
        "<span>文本处理：" + config.textModeText.toHtmlEscaped() + "</span>"
        "<span style=\"color:#94a3b8;\"> &nbsp;|&nbsp; </span>"
        "<span>词库：" + config.wordLibText.toHtmlEscaped() + "</span>"
        "<span style=\"color:#94a3b8;\"> &nbsp;|&nbsp; </span>"
        "<span>AI：" + aiText.toHtmlEscaped() + "</span>"
        "<span style=\"color:#94a3b8;\"> &nbsp;|&nbsp; </span>"
        "<span>最近一句识别耗时：" + timeText.toHtmlEscaped() + "</span>");
}

QString MainWindow::chooseLocalFile()
{
    QString filePath = QFileDialog::getOpenFileName(
        this,
        "选择音频或视频文件",
        "",
        "媒体文件 (*.wav *.mp3 *.m4a *.aac *.webm *.mp4 *.mov *.avi *.mkv);;所有文件 (*.*)");

    return filePath;
}
