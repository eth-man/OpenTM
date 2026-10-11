#include "cp_panel.h"

#include <QButtonGroup>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QVBoxLayout>

namespace opentm::tm_ui {

using opentm::tm_core::cp_boot_params;

namespace {

struct choice { const char* token; const char* label; };
struct group {
    const char* form_name;
    const char* label;
    QString cp_boot_params::* member;
    choice choices[3];
};

const group kGroups[] = {
    {"sysutil", QT_TRANSLATE_NOOP("cp_panel", "Boot Mode"), &cp_boot_params::boot_mode,
     {{"dbg", QT_TRANSLATE_NOOP("cp_panel", "Debugger Mode")},
      {"sys", QT_TRANSLATE_NOOP("cp_panel", "System Software Mode")},
      {"rel", QT_TRANSLATE_NOOP("cp_panel", "Release Mode")}}},

    {"memmode", QT_TRANSLATE_NOOP("cp_panel", "User Process Memory Size"), &cp_boot_params::memory_size,
     {{"tool", QT_TRANSLATE_NOOP("cp_panel", "Tool Mode")},
      {"console", QT_TRANSLATE_NOOP("cp_panel", "Console Mode")},
      {nullptr, nullptr}}},

    {"bd", QT_TRANSLATE_NOOP("cp_panel", "Blu-ray Disc Access"), &cp_boot_params::bd_access,
     {{"emu_dev", QT_TRANSLATE_NOOP("cp_panel", "BD Emulator (DEV)")},
      {"emu_usb", QT_TRANSLATE_NOOP("cp_panel", "BD Emulator (USB)")},
      {"drive", QT_TRANSLATE_NOOP("cp_panel", "BD Drive")}}},

    {"hddspeed", QT_TRANSLATE_NOOP("cp_panel", "Transfer Rate Pacing for BD Emulator"), &cp_boot_params::hdd_speed,
     {{"native", QT_TRANSLATE_NOOP("cp_panel", "HDD Native")},
      {"emulated", QT_TRANSLATE_NOOP("cp_panel", "Equiv. to BD Drive")},
      {nullptr, nullptr}}},

    {"relchk", QT_TRANSLATE_NOOP("cp_panel", "Release Check Mode"), &cp_boot_params::release_check,
     {{"dev", QT_TRANSLATE_NOOP("cp_panel", "Development Mode")},
      {"rel", QT_TRANSLATE_NOOP("cp_panel", "Release Mode")},
      {nullptr, nullptr}}},

    {"hostfs", QT_TRANSLATE_NOOP("cp_panel", "HOSTFS Network"), &cp_boot_params::hostfs,
     {{"dev", QT_TRANSLATE_NOOP("cp_panel", "DEV LAN")},
      {"target", QT_TRANSLATE_NOOP("cp_panel", "LAN")},
      {nullptr, nullptr}}},

    {"model", QT_TRANSLATE_NOOP("cp_panel", "Target Model"), &cp_boot_params::model,
     {{"ps3-hdd60", QT_TRANSLATE_NOOP("cp_panel", "PS3 HDD 60GB Model")},
      {"ps3-hdd20", QT_TRANSLATE_NOOP("cp_panel", "PS3 HDD 20GB Model")},
      {nullptr, nullptr}}},

    {"bootbeep", QT_TRANSLATE_NOOP("cp_panel", "Power On Beep"), &cp_boot_params::boot_beep,
     {{"beep", QT_TRANSLATE_NOOP("cp_panel", "Yes")},
      {"silent", QT_TRANSLATE_NOOP("cp_panel", "No")},
      {nullptr, nullptr}}},
};

} // namespace

cp_panel::cp_panel(QWidget* parent) : QWidget(parent) {
    connect(&client_, &opentm::tm_core::cp_client::log_message, this, &cp_panel::log_message);
    build_ui();
    apply_host_state();
}

void cp_panel::set_host(const QString& host) {
    if (host_ == host) return;
    host_ = host;
    client_.set_host(host);
    apply_host_state();
    if (!host_.isEmpty()) reload();
}

void cp_panel::apply_host_state() {
    const bool have = !host_.isEmpty();
    if (reload_btn_) reload_btn_->setEnabled(have);
    if (apply_btn_)  apply_btn_->setEnabled(have);
    if (status_ && !have) {
        status_->setText(tr("Select a connected DECR target to reach its communications processor."));
    }
}

void cp_panel::build_ui() {
    auto* outer = new QVBoxLayout(this);

    auto* intro = new QLabel(tr("These are the communications processor's own settings. They apply when the ""target next boots, and are reachable even while it is powered off."), this);
    intro->setWordWrap(true);
    outer->addWidget(intro);

    auto* cred_box = new QGroupBox(tr("Credentials"), this);
    auto* cred_form = new QFormLayout(cred_box);
    user_edit_ = new QLineEdit(QStringLiteral("Administrator"), this);
    pass_edit_ = new QLineEdit(QStringLiteral("Administrator"), this);
    pass_edit_->setEchoMode(QLineEdit::Password);
    cred_form->addRow(tr("&User:"), user_edit_);
    cred_form->addRow(tr("&Password:"), pass_edit_);
    outer->addWidget(cred_box);

    auto* params_box = new QGroupBox(tr("Boot Parameters"), this);
    auto* form = new QFormLayout(params_box);
    for (const auto& g : kGroups) {
        auto* row = new QHBoxLayout;
        auto* bg  = new QButtonGroup(this);
        for (const auto& c : g.choices) {
            if (!c.token) break;
            auto* rb = new QRadioButton(tr(c.label), this);
            bg->addButton(rb);
            row->addWidget(rb);
            buttons_[QLatin1String(g.form_name)].insert(QLatin1String(c.token), rb);
        }
        row->addStretch(1);
        auto* holder = new QWidget(this);
        holder->setLayout(row);
        form->addRow(tr(g.label), holder);
    }
    outer->addWidget(params_box);

    status_ = new QLabel(this);
    status_->setWordWrap(true);
    outer->addWidget(status_);

    reload_btn_ = new QPushButton(tr("Re&load"), this);
    apply_btn_  = new QPushButton(tr("&Apply"), this);
    connect(reload_btn_, &QPushButton::clicked, this, &cp_panel::reload);
    connect(apply_btn_,  &QPushButton::clicked, this, &cp_panel::apply);

    auto* btn_row = new QHBoxLayout;
    btn_row->addStretch(1);
    btn_row->addWidget(reload_btn_);
    btn_row->addWidget(apply_btn_);
    outer->addLayout(btn_row);
    outer->addStretch(1);
}

void cp_panel::set_busy(bool busy, const QString& what) {
    const bool have = !host_.isEmpty();
    reload_btn_->setEnabled(!busy && have);
    apply_btn_->setEnabled(!busy && have);
    if (busy) status_->setText(what);
}

void cp_panel::show_params(const cp_boot_params& p) {
    for (const auto& g : kGroups) {
        const auto& by_token = buttons_[QLatin1String(g.form_name)];
        const auto value = p.*g.member;
        if (auto* rb = by_token.value(value)) {
            rb->setChecked(true);
        } else {
            // the CP reported something this build does not model; leave the
            // group blank rather than silently showing the wrong option
            for (auto* b : by_token) b->setChecked(false);
            emit log_message(tr("    !! CP: unknown value '%1' for %2").arg(value, QLatin1String(g.form_name)));
        }
    }
}

cp_boot_params cp_panel::current_params() const {
    cp_boot_params p;
    for (const auto& g : kGroups) {
        const auto& by_token = buttons_[QLatin1String(g.form_name)];
        for (auto it = by_token.constBegin(); it!= by_token.constEnd(); ++it) {
            if (it.value()->isChecked()) { p.*g.member = it.key(); break; }
        }
    }
    return p;
}

void cp_panel::reload() {
    client_.set_credentials(user_edit_->text(), pass_edit_->text());
    set_busy(true, tr("Reading from %1...").arg(host_));
    client_.fetch_boot_params([this](bool ok, const cp_boot_params& p, const QString& err) {
        set_busy(false);
        if (!ok) {
            status_->setText(tr("Could not read the CP: %1").arg(err));
            return;
        }
        show_params(p);
        status_->setText(tr("Read from %1.").arg(host_));
    });
}

void cp_panel::apply() {
    client_.set_credentials(user_edit_->text(), pass_edit_->text());
    set_busy(true, tr("Writing to %1...").arg(host_));
    client_.apply_boot_params(current_params(), [this](bool ok, const QString& err) {
        set_busy(false);
        if (!ok) {
            status_->setText(tr("Could not write to the CP: %1").arg(err));
            return;
        }
        status_->setText(tr("Written. The target picks these up on its next boot."));
        reload();
    });
}

} // namespace opentm::tm_ui
