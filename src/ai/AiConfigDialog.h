#ifndef AICONFIGDIALOG_H
#define AICONFIGDIALOG_H

#include <QDialog>

#include "../config/AppConfig.h"

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;

class DeepSeekClient;

class AiConfigDialog : public QDialog
{
    Q_OBJECT

public:
    explicit AiConfigDialog(const DeepSeekConfig &config,
                            bool envApiKeyExists,
                            QWidget *parent = nullptr);

    DeepSeekConfig config() const;

private:
    void onSaveClicked();
    void onTestClicked();
    void setTesting(bool testing);

private:
    QLineEdit *baseUrlEdit = nullptr;
    QLineEdit *apiKeyEdit = nullptr;
    QCheckBox *showApiKeyCheck = nullptr;
    QComboBox *modelCombo = nullptr;
    QDoubleSpinBox *temperatureSpin = nullptr;
    QSpinBox *maxTokensSpin = nullptr;
    QSpinBox *timeoutSpin = nullptr;
    QLabel *envHintLabel = nullptr;
    QPushButton *saveButton = nullptr;
    QPushButton *testButton = nullptr;
    QPushButton *cancelButton = nullptr;

    DeepSeekClient *testClient = nullptr;
};

#endif
