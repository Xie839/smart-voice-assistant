#include "DeepSeekClient.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcessEnvironment>
#include <QTimer>

DeepSeekClient::DeepSeekClient(QObject *parent)
    : QObject(parent)
{
    m_network = new QNetworkAccessManager(this);
}

void DeepSeekClient::setConfig(const DeepSeekConfig &config)
{
    m_config = config;
}

QString DeepSeekClient::resolveApiKey() const
{
    const QString envKey = QProcessEnvironment::systemEnvironment().value("DEEPSEEK_API_KEY").trimmed();
    if (!envKey.isEmpty())
    {
        return envKey;
    }
    return m_config.apiKey.trimmed();
}

QString DeepSeekClient::normalizeBaseUrl(const QString &baseUrl) const
{
    QString url = baseUrl.trimmed();
    if (url.isEmpty())
    {
        url = "https://api.deepseek.com";
    }
    while (url.endsWith('/'))
    {
        url.chop(1);
    }
    if (!url.endsWith("/chat/completions"))
    {
        url += "/chat/completions";
    }
    return url;
}

QUrl DeepSeekClient::endpointUrl() const
{
    return QUrl(normalizeBaseUrl(m_config.baseUrl));
}

bool DeepSeekClient::isConfigured(QString *reason) const
{
    const QString key = resolveApiKey();
    if (key.isEmpty())
    {
        if (reason)
        {
            *reason = "未配置 DeepSeek API Key，请在 AI 配置中填写。";
        }
        return false;
    }
    if (m_config.model.trimmed().isEmpty())
    {
        if (reason)
        {
            *reason = "未配置 DeepSeek 模型名称。";
        }
        return false;
    }
    if (!endpointUrl().isValid())
    {
        if (reason)
        {
            *reason = "DeepSeek Base URL 无效。";
        }
        return false;
    }
    return true;
}

QString DeepSeekClient::buildSystemPrompt(const QString &mode) const
{
    if (mode == "summary")
    {
        return "你是一个中文语音转文字后的文本整理助手。请在不改变原意的前提下，整理为简洁摘要。只输出摘要正文。";
    }
    if (mode == "meeting")
    {
        return "你是一个中文语音转文字后的文本整理助手。请整理为会议纪要格式：议题、要点、结论。不要编造信息。";
    }

    return "你是一个中文语音转文字后的文本整理助手。"
           "你的任务是把语音识别得到的原始文本整理成更通顺、清晰、适合阅读的中文。"
           "要求："
           "1. 不改变原意；"
           "2. 不编造原文没有的信息；"
           "3. 修正常见语音识别错别字；"
           "4. 合并过碎的短句；"
           "5. 调整标点和段落；"
           "6. 去掉明显重复的口头语；"
           "7. 保留必要的关键词和专有名词；"
           "8. 输出优化后的正文，不要解释你的修改过程。";
}

QJsonObject DeepSeekClient::buildOptimizeRequest(const QString &rawText, const QString &mode) const
{
    QJsonArray messages;
    messages.append(QJsonObject{
        {"role", "system"},
        {"content", buildSystemPrompt(mode)}});

    messages.append(QJsonObject{
        {"role", "user"},
        {"content", QString("请整理下面这段语音识别文本，直接输出优化后的正文，不要输出分析过程：\n\n%1").arg(rawText)}});

    QJsonObject body;
    body.insert("model", m_config.model);
    body.insert("messages", messages);
    body.insert("temperature", m_config.temperature);
    body.insert("max_tokens", m_config.maxTokens);
    return body;
}

QJsonObject DeepSeekClient::buildCustomPromptRequest(const QString &rawText, const QString &customPrompt) const
{
    QJsonArray messages;
    messages.append(QJsonObject{
        {"role", "system"},
        {"content", "你是一个可靠的中文文本处理助手。请严格按照用户给定的处理要求，对语音识别文本进行整理。不要编造原文没有的信息。"}});

    messages.append(QJsonObject{
        {"role", "user"},
        {"content", QString("【用户处理要求】\n%1\n\n【原始语音识别文本】\n%2\n\n请根据“用户处理要求”处理“原始语音识别文本”，直接输出处理后的正文，不要解释过程。")
                        .arg(customPrompt, rawText)}});

    QJsonObject body;
    body.insert("model", m_config.model);
    body.insert("messages", messages);
    body.insert("temperature", m_config.temperature);
    body.insert("max_tokens", m_config.maxTokens);
    return body;
}

QJsonObject DeepSeekClient::buildTestRequest() const
{
    QJsonArray messages;
    messages.append(QJsonObject{
        {"role", "system"},
        {"content", "你是连接测试助手。"}});
    messages.append(QJsonObject{
        {"role", "user"},
        {"content", "请只回复：连接成功"}});

    QJsonObject body;
    body.insert("model", m_config.model);
    body.insert("messages", messages);
    body.insert("temperature", 0.0);
    body.insert("max_tokens", 64);
    return body;
}

void DeepSeekClient::optimizeTextAsync(const QString &rawText, const QString &mode)
{
    if (rawText.trimmed().isEmpty())
    {
        emit requestError("没有可优化的文本。");
        return;
    }
    sendRequest(buildOptimizeRequest(rawText, mode), false);
}

void DeepSeekClient::optimizeTextWithCustomPromptAsync(const QString &rawText, const QString &customPrompt)
{
    if (rawText.trimmed().isEmpty())
    {
        emit requestError("没有可优化的文本。");
        return;
    }
    if (customPrompt.trimmed().isEmpty())
    {
        emit requestError("请输入自定义提示词。");
        return;
    }
    sendRequest(buildCustomPromptRequest(rawText, customPrompt), false);
}

void DeepSeekClient::testConnectionAsync()
{
    sendRequest(buildTestRequest(), true);
}

void DeepSeekClient::sendRequest(const QJsonObject &payload, bool forTest)
{
    QString reason;
    if (!isConfigured(&reason))
    {
        if (forTest)
        {
            emit testConnectionFinished(false, reason);
        }
        emit requestError(reason);
        return;
    }

    if (m_activeReply && m_activeReply->isRunning())
    {
        const QString msg = "已有请求正在进行，请稍后重试。";
        if (forTest)
        {
            emit testConnectionFinished(false, msg);
        }
        emit requestError(msg);
        return;
    }

    QNetworkRequest request(endpointUrl());
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("Authorization", QByteArray("Bearer ") + resolveApiKey().toUtf8());

    const QByteArray body = QJsonDocument(payload).toJson(QJsonDocument::Compact);
    QNetworkReply *reply = m_network->post(request, body);
    m_activeReply = reply;
    reply->setProperty("for_test", forTest);
    reply->setProperty("timed_out", false);

    emit requestStarted();

    const int timeoutMs = m_config.timeoutMs > 0 ? m_config.timeoutMs : 60000;
    QTimer::singleShot(timeoutMs, this, [reply]()
                       {
        if (reply && reply->isRunning())
        {
            reply->setProperty("timed_out", true);
            reply->abort();
        } });

    connect(reply, &QNetworkReply::finished, this, [this, reply]()
            {
        const bool forTest = reply->property("for_test").toBool();
        const bool timedOut = reply->property("timed_out").toBool();
        const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray bytes = reply->readAll();

        if (timedOut)
        {
            const QString msg = "DeepSeek 请求超时。";
            if (forTest)
            {
                emit testConnectionFinished(false, msg);
            }
            emit requestError(msg);
            reply->deleteLater();
            return;
        }

        if (reply->error() != QNetworkReply::NoError)
        {
            const QString msg = QString("DeepSeek 请求失败：HTTP %1").arg(statusCode > 0 ? statusCode : -1);
            if (forTest)
            {
                emit testConnectionFinished(false, msg);
            }
            emit requestError(msg);
            reply->deleteLater();
            return;
        }

        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(bytes, &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject())
        {
            const QString msg = "DeepSeek 响应解析失败";
            if (forTest)
            {
                emit testConnectionFinished(false, msg);
            }
            emit requestError(msg);
            reply->deleteLater();
            return;
        }

        const QJsonObject root = doc.object();
        const QJsonArray choices = root.value("choices").toArray();
        if (choices.isEmpty())
        {
            const QString msg = "DeepSeek 响应中没有可用结果";
            if (forTest)
            {
                emit testConnectionFinished(false, msg);
            }
            emit requestError(msg);
            reply->deleteLater();
            return;
        }

        const QJsonObject first = choices.first().toObject();
        const QJsonObject messageObj = first.value("message").toObject();
        const QString content = messageObj.value("content").toString().trimmed();
        if (content.isEmpty())
        {
            const QString msg = "AI 优化结果为空";
            if (forTest)
            {
                emit testConnectionFinished(false, msg);
            }
            emit requestError(msg);
            reply->deleteLater();
            return;
        }

        if (forTest)
        {
            emit testConnectionFinished(true, "连接成功");
        }
        else
        {
            emit optimizeFinished(content);
        }

        reply->deleteLater(); });
}
