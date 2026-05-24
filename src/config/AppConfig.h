#ifndef APPCONFIG_H
#define APPCONFIG_H

#include <QString>

struct DeepSeekConfig
{
    QString baseUrl = "https://api.deepseek.com";
    QString apiKey;
    QString model = "deepseek-v4-flash";
    double temperature = 0.3;
    int maxTokens = 2048;
    int timeoutMs = 60000;
};

class AppConfig
{
public:
    bool load(QString *errorMessage = nullptr);
    bool save(QString *errorMessage = nullptr);

    DeepSeekConfig deepSeekConfig() const;
    void setDeepSeekConfig(const DeepSeekConfig &config);

    QString configPath() const;
    bool ensureConfigFile(QString *errorMessage = nullptr);

private:
    QString findExistingPath(const QString &relativePath) const;
    QString preferredPath(const QString &relativePath) const;
    QString examplePath() const;

private:
    QString m_configPath;
    DeepSeekConfig m_deepSeekConfig;
};

#endif
