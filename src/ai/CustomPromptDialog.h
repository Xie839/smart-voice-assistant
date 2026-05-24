#ifndef CUSTOMPROMPTDIALOG_H
#define CUSTOMPROMPTDIALOG_H

#include <QDialog>
#include <QString>

class QComboBox;
class QTextEdit;

class CustomPromptDialog : public QDialog
{
    Q_OBJECT

public:
    explicit CustomPromptDialog(QWidget *parent = nullptr);

    QString promptText() const;

private:
    void applyTemplateByIndex(int index);

private:
    QComboBox *templateCombo = nullptr;
    QTextEdit *promptEdit = nullptr;
};

#endif
