#include "AppConfig.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>

namespace
{
QJsonObject defaultConfigObject()
{
    QJsonObject deepSeek;
    deepSeek.insert("base_url", "https://api.deepseek.com");
    deepSeek.insert("api_key", "");
    deepSeek.insert("model", "deepseek-v4-flash");
    deepSeek.insert("temperature", 0.3);
    deepSeek.insert("max_tokens", 2048);
    deepSeek.insert("timeout_ms", 60000);

    QJsonObject root;
    root.insert("deepseek", deepSeek);
    return root;
}
}

QString AppConfig::findExistingPath(const QString &relativePath) const
{
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        QDir::current().absoluteFilePath(relativePath),
        QDir(appDir).absoluteFilePath(relativePath),
        QDir(appDir).absoluteFilePath("../" + relativePath),
        QDir(appDir).absoluteFilePath("../../" + relativePath)};

    for (const QString &path : candidates)
    {
        if (QFileInfo::exists(path))
        {
            return QFileInfo(path).absoluteFilePath();
        }
    }

    return QString();
}

QString AppConfig::preferredPath(const QString &relativePath) const
{
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        QDir::current().absoluteFilePath(relativePath),
        QDir(appDir).absoluteFilePath(relativePath),
        QDir(appDir).absoluteFilePath("../" + relativePath),
        QDir(appDir).absoluteFilePath("../../" + relativePath)};

    return QFileInfo(candidates.first()).absoluteFilePath();
}

QString AppConfig::examplePath() const
{
    const QString existing = findExistingPath("config/config.example.json");
    if (!existing.isEmpty())
    {
        return existing;
    }
    return preferredPath("config/config.example.json");
}

QString AppConfig::configPath() const
{
    if (!m_configPath.isEmpty())
    {
        return m_configPath;
    }

    const QString existing = findExistingPath("config/config.json");
    if (!existing.isEmpty())
    {
        return existing;
    }

    return preferredPath("config/config.json");
}

bool AppConfig::ensureConfigFile(QString *errorMessage)
{
    const QString cfgPath = configPath();
    const QFileInfo cfgInfo(cfgPath);
    QDir().mkpath(cfgInfo.absolutePath());

    if (cfgInfo.exists())
    {
        m_configPath = cfgInfo.absoluteFilePath();
        return true;
    }

    const QString exPath = examplePath();
    if (QFileInfo::exists(exPath))
    {
        if (!QFile::copy(exPath, cfgPath))
        {
            if (errorMessage)
            {
                *errorMessage = "无法从 config.example.json 创建 config.json。";
            }
            return false;
        }
        m_configPath = QFileInfo(cfgPath).absoluteFilePath();
        return true;
    }

    QFile out(cfgPath);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        if (errorMessage)
        {
            *errorMessage = "无法创建 config/config.json。";
        }
        return false;
    }

    const QJsonDocument doc(defaultConfigObject());
    out.write(doc.toJson(QJsonDocument::Indented));
    out.close();

    m_configPath = QFileInfo(cfgPath).absoluteFilePath();
    return true;
}

bool AppConfig::load(QString *errorMessage)
{
    if (!ensureConfigFile(errorMessage))
    {
        return false;
    }

    QFile file(configPath());
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        if (errorMessage)
        {
            *errorMessage = "无法读取配置文件。";
        }
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    file.close();

    if (parseError.error != QJsonParseError::NoError || !doc.isObject())
    {
        if (errorMessage)
        {
            *errorMessage = "配置文件 JSON 格式无效。";
        }
        return false;
    }

    const QJsonObject root = doc.object();
    const QJsonObject deep = root.value("deepseek").toObject();

    DeepSeekConfig cfg;
    cfg.baseUrl = deep.value("base_url").toString("https://api.deepseek.com").trimmed();
    cfg.apiKey = deep.value("api_key").toString().trimmed();
    cfg.model = deep.value("model").toString("deepseek-v4-flash").trimmed();
    cfg.temperature = deep.value("temperature").toDouble(0.3);
    cfg.maxTokens = deep.value("max_tokens").toInt(2048);
    cfg.timeoutMs = deep.value("timeout_ms").toInt(60000);

    if (cfg.baseUrl.isEmpty())
    {
        cfg.baseUrl = "https://api.deepseek.com";
    }
    if (cfg.model.isEmpty())
    {
        cfg.model = "deepseek-v4-flash";
    }
    if (cfg.maxTokens <= 0)
    {
        cfg.maxTokens = 2048;
    }
    if (cfg.timeoutMs <= 0)
    {
        cfg.timeoutMs = 60000;
    }

    m_deepSeekConfig = cfg;
    return true;
}

bool AppConfig::save(QString *errorMessage)
{
    if (!ensureConfigFile(errorMessage))
    {
        return false;
    }

    QFile file(configPath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        if (errorMessage)
        {
            *errorMessage = "无法写入配置文件。";
        }
        return false;
    }

    QJsonObject deep;
    deep.insert("base_url", m_deepSeekConfig.baseUrl);
    deep.insert("api_key", m_deepSeekConfig.apiKey);
    deep.insert("model", m_deepSeekConfig.model);
    deep.insert("temperature", m_deepSeekConfig.temperature);
    deep.insert("max_tokens", m_deepSeekConfig.maxTokens);
    deep.insert("timeout_ms", m_deepSeekConfig.timeoutMs);

    QJsonObject root;
    root.insert("deepseek", deep);

    const QJsonDocument doc(root);
    file.write(doc.toJson(QJsonDocument::Indented));
    file.close();
    return true;
}

DeepSeekConfig AppConfig::deepSeekConfig() const
{
    return m_deepSeekConfig;
}

void AppConfig::setDeepSeekConfig(const DeepSeekConfig &config)
{
    m_deepSeekConfig = config;
}
