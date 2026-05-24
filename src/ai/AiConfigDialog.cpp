#include "AiConfigDialog.h"

#include "DeepSeekClient.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

AiConfigDialog::AiConfigDialog(const DeepSeekConfig &cfg, bool envApiKeyExists, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle("AI 配置");
    setModal(true);
    resize(520, 320);

    baseUrlEdit = new QLineEdit(this);
    baseUrlEdit->setText(cfg.baseUrl);

    apiKeyEdit = new QLineEdit(this);
    apiKeyEdit->setEchoMode(QLineEdit::Password);
    apiKeyEdit->setPlaceholderText("请输入你的 DeepSeek API Key");
    apiKeyEdit->setText(cfg.apiKey);

    showApiKeyCheck = new QCheckBox("显示 API Key", this);
    connect(showApiKeyCheck, &QCheckBox::toggled, this, [this](bool checked)
            { apiKeyEdit->setEchoMode(checked ? QLineEdit::Normal : QLineEdit::Password); });

    modelCombo = new QComboBox(this);
    modelCombo->setEditable(true);
    modelCombo->addItems({"deepseek-v4-flash", "deepseek-v4-pro"});
    modelCombo->setCurrentText(cfg.model.isEmpty() ? "deepseek-v4-flash" : cfg.model);

    temperatureSpin = new QDoubleSpinBox(this);
    temperatureSpin->setRange(0.0, 1.5);
    temperatureSpin->setSingleStep(0.1);
    temperatureSpin->setDecimals(2);
    temperatureSpin->setValue(cfg.temperature);

    maxTokensSpin = new QSpinBox(this);
    maxTokensSpin->setRange(64, 16384);
    maxTokensSpin->setValue(cfg.maxTokens > 0 ? cfg.maxTokens : 2048);

    timeoutSpin = new QSpinBox(this);
    timeoutSpin->setRange(5000, 180000);
    timeoutSpin->setSingleStep(5000);
    timeoutSpin->setValue(cfg.timeoutMs > 0 ? cfg.timeoutMs : 60000);

    envHintLabel = new QLabel(this);
    envHintLabel->setWordWrap(true);
    envHintLabel->setStyleSheet("color: #64748b;");
    envHintLabel->setText(envApiKeyExists
                              ? "已检测到环境变量 DEEPSEEK_API_KEY，将优先使用环境变量中的 Key。"
                              : "未检测到环境变量 DEEPSEEK_API_KEY。");

    auto *form = new QFormLayout();
    form->setLabelAlignment(Qt::AlignRight);
    form->addRow("Base URL", baseUrlEdit);
    form->addRow("API Key", apiKeyEdit);
    form->addRow("", showApiKeyCheck);
    form->addRow("模型", modelCombo);
    form->addRow("temperature", temperatureSpin);
    form->addRow("max_tokens", maxTokensSpin);
    form->addRow("timeout_ms", timeoutSpin);

    saveButton = new QPushButton("保存", this);
    testButton = new QPushButton("测试连接", this);
    cancelButton = new QPushButton("取消", this);

    connect(saveButton, &QPushButton::clicked, this, &AiConfigDialog::onSaveClicked);
    connect(testButton, &QPushButton::clicked, this, &AiConfigDialog::onTestClicked);
    connect(cancelButton, &QPushButton::clicked, this, &AiConfigDialog::reject);

    auto *buttons = new QHBoxLayout();
    buttons->addStretch();
    buttons->addWidget(saveButton);
    buttons->addWidget(testButton);
    buttons->addWidget(cancelButton);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(envHintLabel);
    layout->addSpacing(8);
    layout->addLayout(buttons);

    testClient = new DeepSeekClient(this);
    connect(testClient, &DeepSeekClient::requestStarted, this, [this]()
            { setTesting(true); });
    connect(testClient, &DeepSeekClient::testConnectionFinished, this, [this](bool success, const QString &message)
            {
        setTesting(false);
        if (success)
        {
            QMessageBox::information(this, "测试连接", message);
        }
        else
        {
            QMessageBox::warning(this, "测试连接", message);
        } });
    connect(testClient, &DeepSeekClient::requestError, this, [this](const QString &message)
            {
        if (testButton->isEnabled())
        {
            return;
        }
        Q_UNUSED(message); });
}

DeepSeekConfig AiConfigDialog::config() const
{
    DeepSeekConfig cfg;
    cfg.baseUrl = baseUrlEdit->text().trimmed();
    cfg.apiKey = apiKeyEdit->text().trimmed();
    cfg.model = modelCombo->currentText().trimmed();
    cfg.temperature = temperatureSpin->value();
    cfg.maxTokens = maxTokensSpin->value();
    cfg.timeoutMs = timeoutSpin->value();
    return cfg;
}

void AiConfigDialog::setTesting(bool testing)
{
    saveButton->setEnabled(!testing);
    testButton->setEnabled(!testing);
    cancelButton->setEnabled(!testing);
}

void AiConfigDialog::onSaveClicked()
{
    const DeepSeekConfig cfg = config();
    if (cfg.baseUrl.isEmpty())
    {
        QMessageBox::warning(this, "配置校验", "Base URL 不能为空。");
        return;
    }
    if (cfg.model.isEmpty())
    {
        QMessageBox::warning(this, "配置校验", "模型名称不能为空。");
        return;
    }
    accept();
}

void AiConfigDialog::onTestClicked()
{
    DeepSeekConfig cfg = config();
    testClient->setConfig(cfg);

    QString reason;
    if (!testClient->isConfigured(&reason))
    {
        QMessageBox::warning(this, "测试连接", reason);
        return;
    }

    testClient->testConnectionAsync();
}
