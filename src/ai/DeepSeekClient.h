#ifndef DEEPSEEKCLIENT_H
#define DEEPSEEKCLIENT_H

#include <QObject>
#include <QPointer>
#include <QJsonObject>
#include <QString>
#include <QUrl>

#include "../config/AppConfig.h"

class QNetworkAccessManager;
class QNetworkReply;

class DeepSeekClient : public QObject
{
    Q_OBJECT

public:
    explicit DeepSeekClient(QObject *parent = nullptr);

    void setConfig(const DeepSeekConfig &config);

    bool isConfigured(QString *reason = nullptr) const;

    void optimizeTextAsync(const QString &rawText, const QString &mode = "polish");
    void optimizeTextWithCustomPromptAsync(const QString &rawText, const QString &customPrompt);
    void testConnectionAsync();

signals:
    void requestStarted();
    void optimizeFinished(const QString &optimizedText);
    void testConnectionFinished(bool success, const QString &message);
    void requestError(const QString &errorMessage);

private:
    QJsonObject buildOptimizeRequest(const QString &rawText, const QString &mode) const;
    QJsonObject buildCustomPromptRequest(const QString &rawText, const QString &customPrompt) const;
    QJsonObject buildTestRequest() const;
    QString buildSystemPrompt(const QString &mode) const;
    QString resolveApiKey() const;
    QUrl endpointUrl() const;
    QString normalizeBaseUrl(const QString &baseUrl) const;
    void sendRequest(const QJsonObject &payload, bool forTest);

private:
    DeepSeekConfig m_config;
    QNetworkAccessManager *m_network = nullptr;
    QPointer<QNetworkReply> m_activeReply;
};

#endif
