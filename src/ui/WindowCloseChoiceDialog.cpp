#include "ui/WindowCloseChoiceDialog.h"

#include "ui/DialogFrame.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVariant>
#include <QVBoxLayout>

namespace asmr {

WindowCloseChoiceDialog::WindowCloseChoiceDialog(bool trayAvailable, QWidget *parent)
    : QDialog(parent)
{
    setObjectName(QStringLiteral("windowCloseChoiceDialog"));
    setWindowTitle(QStringLiteral("关闭主窗口"));
    setMinimumWidth(352);

    auto *body = new QVBoxLayout;
    body->setContentsMargins(18, 16, 18, 16);
    body->setSpacing(10);
    auto *question = new QLabel(QStringLiteral("关闭主窗口时，你希望 Arcanblade 怎么做？"), this);
    question->setWordWrap(true);
    body->addWidget(question);

    QString hint = trayAvailable ? QStringLiteral("隐藏到托盘后，音频播放和字幕任务会继续。")
                                : QStringLiteral("当前系统托盘不可用。你可以退出程序，也可关闭此对话框继续使用。");
    hint.append(QStringLiteral("\n本次选择会被记住，可在「设置 → 关闭主窗口时」更改。"));
    auto *description = new QLabel(hint, this);
    description->setProperty("uiRole", QStringLiteral("muted"));
    description->setWordWrap(true);
    body->addWidget(description);

    auto *exitButton = new QPushButton(QStringLiteral("关闭程序"), this);
    exitButton->setObjectName(QStringLiteral("windowCloseChoiceExitButton"));
    auto *trayButton = new QPushButton(QStringLiteral("隐藏到托盘"), this);
    trayButton->setObjectName(QStringLiteral("windowCloseChoiceTrayButton"));
    trayButton->setEnabled(trayAvailable);
    auto *buttons = new QHBoxLayout;
    buttons->addStretch();
    buttons->addWidget(exitButton);
    buttons->addWidget(trayButton);
    body->addLayout(buttons);
    m_frame = new DialogFrame(this, body, false);

    connect(exitButton, &QPushButton::clicked, this, [this] { done(ExitProgram); });
    connect(trayButton, &QPushButton::clicked, this, [this] { done(HideToTray); });
}

bool WindowCloseChoiceDialog::nativeEvent(const QByteArray &type, void *message, qintptr *result)
{
    return (m_frame && m_frame->handleNativeEvent(type, message, result))
        || QDialog::nativeEvent(type, message, result);
}

} // namespace asmr
