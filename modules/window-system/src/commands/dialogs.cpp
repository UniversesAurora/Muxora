// SPDX-License-Identifier: MIT
// Dialogs over windows: the manager's side, and the Widgets presentation's.
#include "dialogs.hpp"
#include "../window_manager_p.hpp"

#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>

namespace window_system {
namespace {

// A QDialog built from the request: the text, a form of its fields and the
// buttons. It deletes itself when closed.
class WidgetDialogs final : public DialogPresenter {
public:
    explicit WidgetDialogs(WindowManager* manager) : manager_(manager) {}

    void open(quint64 id, WindowId window, const DialogRequest& request, std::function<void()> finished) override
    {
        auto* dialog = new QDialog(manager_->windowWidget(window));
        dialogs_.insert(id, dialog);
        dialog->setObjectName(request.name);
        dialog->setWindowTitle(request.title);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->setWindowModality(request.modal ? Qt::WindowModal : Qt::NonModal);
        auto* layout = new QVBoxLayout(dialog);
        if (!request.text.isEmpty()) {
            auto* text = new QLabel(request.text, dialog);
            text->setTextFormat(Qt::PlainText);
            text->setWordWrap(true);
            layout->addWidget(text);
        }
        auto* form = new QFormLayout;
        layout->addLayout(form, 1);
        // Each field's current value, by its ID.
        const auto readers = std::make_shared<QVector<std::pair<QString, std::function<QVariant()>>>>();
        const auto values = [readers] {
            QVariantMap result;
            for (const auto& [key, read] : std::as_const(*readers)) result.insert(key, read());
            return result;
        };
        auto* buttons = new QDialogButtonBox(request.cancellable ? QDialogButtonBox::Ok | QDialogButtonBox::Cancel : QDialogButtonBox::Ok, dialog);
        auto* ok = buttons->button(QDialogButtonBox::Ok);
        if (!request.acceptText.isEmpty()) ok->setText(request.acceptText);
        if (request.cancellable && !request.cancelText.isEmpty()) buttons->button(QDialogButtonBox::Cancel)->setText(request.cancelText);
        const auto validity = [ok, values, acceptable = request.acceptable] {
            ok->setEnabled(!acceptable || acceptable(values()));
        };
        for (const auto& field : request.fields) {
            QWidget* control = nullptr;
            switch (field.kind) {
            case DialogField::Kind::Text: {
                auto* edit = new QLineEdit(field.value.toString(), dialog);
                QObject::connect(edit, &QLineEdit::textChanged, dialog, validity);
                readers->append({field.id, [edit] { return QVariant(edit->text()); }});
                control = edit;
                break;
            }
            case DialogField::Kind::Lines: {
                auto* edit = new QPlainTextEdit(field.value.toString(), dialog);
                QObject::connect(edit, &QPlainTextEdit::textChanged, dialog, validity);
                readers->append({field.id, [edit] { return QVariant(edit->toPlainText()); }});
                control = edit;
                break;
            }
            case DialogField::Kind::Integer: {
                auto* box = new QSpinBox(dialog);
                box->setRange(field.minimum, field.maximum);
                box->setSuffix(field.suffix);
                box->setSpecialValueText(field.emptyText);
                box->setValue(field.value.toInt());
                QObject::connect(box, &QSpinBox::valueChanged, dialog, validity);
                readers->append({field.id, [box] { return QVariant(box->value()); }});
                control = box;
                break;
            }
            }
            control->setObjectName(field.id);
            if (field.label.isEmpty()) form->addRow(control);
            else form->addRow(field.label, control);
        }
        layout->addWidget(buttons);
        validity();
        QObject::connect(buttons, &QDialogButtonBox::accepted, dialog, &QDialog::accept);
        QObject::connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
        QObject::connect(dialog, &QDialog::accepted, dialog, [values, accepted = request.accepted] {
            if (accepted) accepted(values());
        });
        QObject::connect(dialog, &QObject::destroyed, [this, id, finished = std::move(finished)] {
            dialogs_.remove(id);
            finished();
        });
        if (request.timeoutMs > 0) QTimer::singleShot(request.timeoutMs, dialog, &QDialog::reject);
        if (!request.minimumSize.isEmpty()) dialog->resize(request.minimumSize);
        applyTheme(dialog, manager_->interfaceTheme());
        dialog->show();
    }
    void close(quint64 id) override { if (auto* dialog = dialogs_.value(id).data()) dialog->reject(); }
    void raise(quint64 id) override
    {
        if (auto* dialog = dialogs_.value(id).data()) { dialog->raise(); dialog->activateWindow(); }
    }

private:
    WindowManager* manager_;
    QHash<quint64, QPointer<QDialog>> dialogs_;
};

} // namespace

bool askForText(WindowManager& manager, WindowId window, const QString& name, const QString& title, const QString& label,
                const QString& text, std::function<bool()> applies, std::function<void(const QString&)> accepted)
{
    DialogRequest request;
    request.name = name;
    request.title = title;
    request.fields = {{QStringLiteral("text"), label, DialogField::Kind::Text, text}};
    request.applies = std::move(applies);
    request.accepted = [accepted = std::move(accepted)](const QVariantMap& values) { accepted(values.value(QStringLiteral("text")).toString()); };
    return manager.openDialog(window, std::move(request)) != 0;
}

DialogPresenter* WindowManager::dialogPresenter()
{
    if (d->presentation == Presentation::External) return d->dialogPresenter;
    if (!d->widgetDialogs) d->widgetDialogs = std::make_unique<WidgetDialogs>(this);
    return d->widgetDialogs.get();
}

quint64 WindowManager::openDialog(WindowId window, DialogRequest request)
{
    auto* presenter = dialogPresenter();
    if (!d->model.window(window) || !presenter) return 0;
    const auto id = ++d->nextDialog;
    d->openDialogs.insert(id, [this, window, applies = request.applies] { return d->model.window(window) && (!applies || applies()); });
    if (!d->watchingDialogs) {
        d->watchingDialogs = true;
        connect(this, &WindowManager::layoutChanged, this, [this] {
            QVector<quint64> ended;
            for (auto it = d->openDialogs.cbegin(); it != d->openDialogs.cend(); ++it) if (!it.value()()) ended.append(it.key());
            for (const auto dialog : std::as_const(ended)) closeDialog(dialog);
        });
    }
    const QPointer<WindowManager> self(this);
    presenter->open(id, window, request, [self, id, finished = request.finished] {
        if (self) self->d->openDialogs.remove(id);
        if (finished) finished();
    });
    return id;
}

void WindowManager::closeDialog(quint64 dialog)
{
    if (d->openDialogs.contains(dialog)) if (auto* presenter = dialogPresenter()) presenter->close(dialog);
}

bool WindowManager::raiseDialog(quint64 dialog)
{
    if (!d->openDialogs.contains(dialog)) return false;
    if (auto* presenter = dialogPresenter()) presenter->raise(dialog);
    return true;
}

} // namespace window_system
