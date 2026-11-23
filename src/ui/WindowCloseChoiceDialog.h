#pragma once

#include <QDialog>

namespace asmr {

class DialogFrame;

class WindowCloseChoiceDialog final : public QDialog {
    Q_OBJECT
public:
    enum Choice { ExitProgram = QDialog::Accepted + 1, HideToTray };
    explicit WindowCloseChoiceDialog(bool trayAvailable, QWidget *parent = nullptr);

protected:
    bool nativeEvent(const QByteArray &type, void *message, qintptr *result) override;

private:
    DialogFrame *m_frame = nullptr;
};

} // namespace asmr
