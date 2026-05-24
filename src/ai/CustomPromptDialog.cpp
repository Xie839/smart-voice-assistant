#include "CustomPromptDialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTextEdit>
#include <QVBoxLayout>

CustomPromptDialog::CustomPromptDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle("自定义优化提示词");
    setModal(true);
    resize(620, 420);
    setMinimumSize(560, 360);

    auto *hintLabel = new QLabel(
        "请输入你希望 AI 如何处理左侧原始识别文本。例如：整理成会议纪要、改写成正式文稿、提取要点、翻译成英文等。",
        this);
    hintLabel->setWordWrap(true);
    hintLabel->setStyleSheet("color: #64748b; font-size: 13px;");

    templateCombo = new QComboBox(this);
    templateCombo->addItems({
        "自定义",
        "整理成正式文稿",
        "整理成会议纪要",
        "提取要点",
        "翻译成英文",
        "改写得更口语化"});

    promptEdit = new QTextEdit(this);
    promptEdit->setMinimumHeight(180);
    promptEdit->setPlaceholderText("例如：请将下面的语音识别文本整理成一份结构清晰的课堂笔记，保留关键概念和结论。");

    connect(templateCombo, &QComboBox::currentIndexChanged, this, [this](int index)
            { applyTemplateByIndex(index); });

    auto *buttonBox = new QDialogButtonBox(this);
    auto *okButton = buttonBox->addButton("使用此提示词", QDialogButtonBox::AcceptRole);
    auto *cancelButton = buttonBox->addButton("取消", QDialogButtonBox::RejectRole);
    connect(okButton, &QPushButton::clicked, this, &QDialog::accept);
    connect(cancelButton, &QPushButton::clicked, this, &QDialog::reject);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(10);
    layout->addWidget(hintLabel);
    layout->addWidget(templateCombo);
    layout->addWidget(promptEdit, 1);
    layout->addWidget(buttonBox);
}

QString CustomPromptDialog::promptText() const
{
    return promptEdit->toPlainText().trimmed();
}

void CustomPromptDialog::applyTemplateByIndex(int index)
{
    switch (index)
    {
    case 1:
        promptEdit->setPlainText("请将下面的语音识别文本整理成一段正式、通顺、适合书面表达的中文文稿。要求不改变原意，不添加原文没有的信息。");
        break;
    case 2:
        promptEdit->setPlainText("请将下面的语音识别文本整理成会议纪要，包含会议要点、关键结论和待办事项。如果原文没有待办事项，请不要编造。");
        break;
    case 3:
        promptEdit->setPlainText("请从下面的语音识别文本中提取核心要点，使用简洁的项目符号列出，不要添加原文没有的信息。");
        break;
    case 4:
        promptEdit->setPlainText("请将下面的中文语音识别文本翻译成自然、准确的英文，保留原意。");
        break;
    case 5:
        promptEdit->setPlainText("请将下面的文本改写得更自然、口语化，适合日常表达，同时保持原意不变。");
        break;
    default:
        break;
    }
}
