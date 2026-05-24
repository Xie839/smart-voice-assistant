#include "TextPostProcessor.h"

#include <QRegularExpression>
#include <QStringList>

namespace
{
QString replaceTraditionalVariants(QString text)
{
    // 轻量级简繁归一化：覆盖 ASR 常见 UI 词汇与高频字，避免结果混杂影响阅读。
    static const QPair<QString, QString> kReplacements[] = {
        {"優化", "优化"},
        {"後", "后"},
        {"識別", "识别"},
        {"語音", "语音"},
        {"文本", "文本"},
        {"設置", "设置"},
        {"當前", "当前"},
        {"狀態", "状态"},
        {"請", "请"},
        {"為", "为"},
        {"與", "与"},
        {"輸入", "输入"},
        {"輸出", "输出"},
        {"這", "这"},
        {"個", "个"},
        {"測試", "测试"},
        {"電腦", "电脑"},
        {"網路", "网络"},
        {"離線", "离线"},
        {"實時", "实时"},
        {"長", "长"},
        {"開", "开"},
        {"關", "关"},
    };

    for (const auto &pair : kReplacements)
    {
        text.replace(pair.first, pair.second);
    }
    return text;
}
}

QString TextPostProcessor::normalize(const QString &text)
{
    QString normalized = text;
    normalized.replace("\r\n", "\n");
    normalized.replace('\r', '\n');

    const QRegularExpression multiSpaces(R"([ \t]+)");
    const QRegularExpression zeroWidthChars(R"([\u200B-\u200D\uFEFF])");
    normalized.remove(zeroWidthChars);

    QStringList lines;
    for (QString line : normalized.split('\n'))
    {
        line = line.trimmed();
        if (line.isEmpty())
        {
            continue;
        }
        line.replace(multiSpaces, " ");
        lines.push_back(line);
    }

    normalized = lines.join("\n").trimmed();
    normalized = replaceTraditionalVariants(normalized);
    return normalized.trimmed();
}
