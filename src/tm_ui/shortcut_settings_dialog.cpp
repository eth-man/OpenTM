#include "shortcut_settings_dialog.h"

#include <QAction>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace opentm::tm_ui {

namespace {

constexpr int kKeyRole = Qt::UserRole + 1;

QString settings_key(const QString& action_key) {
    return QStringLiteral("shortcuts/") + action_key;
}

QString clean_label(const QAction* a) {
    QString t = a->text();
    t.remove(QLatin1Char('&'));
    if (t.endsWith(QLatin1String("..."))) t.chop(3);
    return t;
}

} // namespace

void shortcut_settings_dialog::save(const QString& key, const QKeySequence& seq) {
    QSettings s;
    if (seq.isEmpty()) {
        s.setValue(settings_key(key), QString());
    } else {
        s.setValue(settings_key(key), seq.toString(QKeySequence::PortableText));
    }
}

void shortcut_settings_dialog::apply_saved(const QHash<QString, QAction*>& actions) {
    QSettings s;
    for (auto it = actions.constBegin(); it!= actions.constEnd(); ++it) {
        const auto v = s.value(settings_key(it.key()));
        if (!v.isValid()) continue; // never rebound: keep the default
        it.value()->setShortcut(QKeySequence(v.toString(), QKeySequence::PortableText));
    }
}

shortcut_settings_dialog::shortcut_settings_dialog(const QHash<QString, QAction*>& actions, QWidget* parent) : QDialog(parent), actions_(actions)
{
    setWindowTitle(tr("Configure Shortcuts"));
    resize(560, 460);

    // captured before any edits so Reset has something to go back to
    for (auto it = actions_.constBegin(); it!= actions_.constEnd(); ++it) {
        defaults_.insert(it.key(), it.value()->property("default_shortcut").value<QKeySequence>());
    }

    tree_ = new QTreeWidget(this);
    tree_->setColumnCount(2);
    tree_->setHeaderLabels({tr("Action"), tr("Shortcut")});
    tree_->setRootIsDecorated(false);
    tree_->setAlternatingRowColors(true);
    tree_->header()->setStretchLastSection(true);
    tree_->setColumnWidth(0, 300);

    editor_ = new QKeySequenceEdit(this);
    auto* assign = new QPushButton(tr("&Assign"), this);
    auto* clear  = new QPushButton(tr("C&lear"), this);
    auto* reset  = new QPushButton(tr("&Reset"), this);

    auto* edit_row = new QHBoxLayout;
    edit_row->addWidget(new QLabel(tr("Press keys:"), this));
    edit_row->addWidget(editor_, 1);
    edit_row->addWidget(assign);
    edit_row->addWidget(clear);
    edit_row->addWidget(reset);

    notice_ = new QLabel(this);
    notice_->setStyleSheet(QStringLiteral("color:#c0392b;"));
    notice_->setWordWrap(true);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::accept);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(tree_, 1);
    layout->addLayout(edit_row);
    layout->addWidget(notice_);
    layout->addWidget(buttons);

    connect(assign, &QPushButton::clicked, this, &shortcut_settings_dialog::assign_from_editor);
    connect(clear,  &QPushButton::clicked, this, &shortcut_settings_dialog::clear_selected);
    connect(reset,  &QPushButton::clicked, this, &shortcut_settings_dialog::reset_selected);
    connect(tree_, &QTreeWidget::currentItemChanged, this, [this] {
        notice_->clear();
        if (auto* item = selected()) {
            if (auto* a = actions_.value(item->data(0, kKeyRole).toString())) {
                editor_->setKeySequence(a->shortcut());
            }
        }
    });

    rebuild();
}

void shortcut_settings_dialog::rebuild() {
    const QString keep = selected() ? selected()->data(0, kKeyRole).toString() : QString();
    tree_->clear();

    QStringList keys = actions_.keys();
    keys.sort();
    for (const auto& key : keys) {
        auto* a = actions_.value(key);
        if (!a) continue;
        auto* item = new QTreeWidgetItem(tree_);
        item->setText(0, clean_label(a));
        item->setIcon(0, a->icon());
        item->setText(1, a->shortcut().toString(QKeySequence::NativeText));
        item->setData(0, kKeyRole, key);
        if (key == keep) tree_->setCurrentItem(item);
    }
}

QTreeWidgetItem* shortcut_settings_dialog::selected() const {
    return tree_->currentItem();
}

QString shortcut_settings_dialog::conflict_for(const QKeySequence& seq, const QString& except_key) const
{
    if (seq.isEmpty()) return {};
    for (auto it = actions_.constBegin(); it!= actions_.constEnd(); ++it) {
        if (it.key() == except_key) continue;
        if (it.value()->shortcut() == seq) return clean_label(it.value());
    }
    return {};
}

void shortcut_settings_dialog::assign_from_editor() {
    auto* item = selected();
    if (!item) return;
    const auto key = item->data(0, kKeyRole).toString();
    auto* a = actions_.value(key);
    if (!a) return;

    const auto seq = editor_->keySequence();
    if (const auto clash = conflict_for(seq, key);!clash.isEmpty()) {
        notice_->setText(tr("%1 is already used by \"%2\".").arg(seq.toString(QKeySequence::NativeText), clash));
        return;
    }
    notice_->clear();
    a->setShortcut(seq);
    save(key, seq);
    rebuild();
}

void shortcut_settings_dialog::clear_selected() {
    auto* item = selected();
    if (!item) return;
    const auto key = item->data(0, kKeyRole).toString();
    if (auto* a = actions_.value(key)) {
        a->setShortcut(QKeySequence());
        save(key, QKeySequence());
        editor_->clear();
        notice_->clear();
        rebuild();
    }
}

void shortcut_settings_dialog::reset_selected() {
    auto* item = selected();
    if (!item) return;
    const auto key = item->data(0, kKeyRole).toString();
    auto* a = actions_.value(key);
    if (!a) return;
    const auto def = defaults_.value(key);
    a->setShortcut(def);
    save(key, def);
    editor_->setKeySequence(def);
    notice_->clear();
    rebuild();
}

} // namespace opentm::tm_ui
