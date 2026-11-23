#pragma once

#include "ui/PlayerMessageBox.h"

#include <QPushButton>

namespace asmr {

inline PlayerMessageBox *makeFavoriteRemovalConfirmation(QWidget *parent, const QString &work,
                                                         const QString &track)
{
    auto *dialog = new PlayerMessageBox(QMessageBox::Question, QStringLiteral("取消喜欢"),
        QStringLiteral("将《%1》中的「%2」移出喜欢列表？").arg(work, track), QMessageBox::NoButton, parent);
    dialog->setObjectName(QStringLiteral("favoriteRemovalConfirmation"));
    dialog->setTextFormat(Qt::PlainText);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    auto *remove = dialog->addButton(QStringLiteral("取消喜欢"), QMessageBox::DestructiveRole);
    remove->setObjectName(QStringLiteral("confirmFavoriteRemoval"));
    auto *keep = dialog->addButton(QStringLiteral("保留喜欢"), QMessageBox::RejectRole);
    keep->setObjectName(QStringLiteral("cancelFavoriteRemoval"));
    dialog->setDefaultButton(keep);
    dialog->setEscapeButton(keep);
    return dialog;
}

} // namespace asmr
