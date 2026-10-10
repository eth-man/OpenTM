#include "target_properties_dialog.h"

#include <QCheckBox>

#include <algorithm>
#include <QComboBox>
#include <QDir>
#include <QHash>
#include <QTimer>
#include <QFile>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QSpinBox>
#include <QSplitter>
#include <QStackedWidget>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace opentm::tm_ui {

namespace {

constexpr std::uint32_t kStackSizes[] = {
    0x20, 0x40, 0x60, 0x80, 0x100, 0x200, 0x400
};

constexpr int kStackKb[] = { 32, 64, 96, 128, 256, 512, 1024 };

int stack_bytes_to_kb(std::uint32_t v) {
    for (int i = 0; i < 7; ++i) if (kStackSizes[i] == v) return kStackKb[i];
    return 64;
}
std::uint32_t stack_kb_to_bytes(int kb) {
    for (int i = 0; i < 7; ++i) if (kStackKb[i] >= kb) return kStackSizes[i];
    return 0x400;
}

int dump_loc_to_index(std::uint64_t v) {
    switch (v) {
    case 0x1: return 1; // /dev_ms
    case 0x4: return 2; // /dev_usb
    case 0x8: return 3; // /dev_hdd0
    default:  return 0; // /app_home (0x2)
    }
}
std::uint64_t index_to_dump_loc(int i) {
    switch (i) {
    case 1:  return 0x1;
    case 2:  return 0x4;
    case 3:  return 0x8;
    default: return 0x2;
    }
}

} // namespace

target_properties_dialog::target_properties_dialog(const target_record& r, QWidget* parent)
    : QDialog(parent), rec_(r)
{
    setWindowTitle(tr("Target Properties"));
    resize(680, 560);

    nav_ = new QTreeWidget(this);
    nav_->setHeaderHidden(true);
    nav_->setMaximumWidth(190);

    pages_ = new QStackedWidget(this);

    auto* target_root = new QTreeWidgetItem(nav_, {tr("Target")});
    auto* tm_props    = new QTreeWidgetItem(target_root, {tr("TM Properties")});
    auto* xmb         = new QTreeWidgetItem(target_root, {tr("XMB Settings")});
    auto* type_root   = new QTreeWidgetItem(nav_, {tr("Target Type")});
    auto* timeouts    = new QTreeWidgetItem(type_root, {tr("Time-outs")});

    pages_->addWidget(build_tm_properties_page()); // 0
    pages_->addWidget(build_xmb_page()); // 1
    pages_->addWidget(build_timeouts_page()); // 2
    tm_props->setData(0, Qt::UserRole, 0);
    xmb     ->setData(0, Qt::UserRole, 1);
    timeouts->setData(0, Qt::UserRole, 2);
    target_root->setData(0, Qt::UserRole, 0);
    type_root  ->setData(0, Qt::UserRole, 2);
    nav_->expandAll();
    nav_->setCurrentItem(tm_props);

    connect(nav_, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem* cur, QTreeWidgetItem*) {
        if (!cur) return;
        const auto v = cur->data(0, Qt::UserRole);
        if (!v.isValid()) return;
        pages_->setCurrentIndex(v.toInt());
        if (v.toInt() == kXmbPage) request_xmb_refresh();
    });

    setModal(false);
    setWindowFlag(Qt::Window);
    setAttribute(Qt::WA_DeleteOnClose);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Apply, this);
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        emit applied(record());
        apply_xmb_if_current();
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons->button(QDialogButtonBox::Apply), &QPushButton::clicked, this, [this] {
        emit applied(record());
        apply_xmb_if_current();
    });

    auto* row = new QHBoxLayout;
    row->addWidget(nav_);
    row->addWidget(pages_, 1);

    auto* top = new QVBoxLayout(this);
    top->addLayout(row, 1);
    top->addWidget(buttons);
}

target_properties_dialog::~target_properties_dialog() = default;

QTreeWidgetItem* target_properties_dialog::add_group(
    QTreeWidget* grid, QTreeWidgetItem* parent, const QString& title)
{
    auto* it = parent ? new QTreeWidgetItem(parent, {title}) : new QTreeWidgetItem(grid, {title});
    auto f = it->font(0);
    f.setBold(true);
    it->setFont(0, f);
    it->setExpanded(false);
    return it;
}

QCheckBox* target_properties_dialog::add_bool(
    QTreeWidget* grid, QTreeWidgetItem* parent, const QString& label, bool value)
{
    auto* it = new QTreeWidgetItem(parent, {label});
    auto* cb = new QCheckBox(grid);
    cb->setChecked(value);
    grid->setItemWidget(it, 1, cb);
    return cb;
}

QLineEdit* target_properties_dialog::add_text(
    QTreeWidget* grid, QTreeWidgetItem* parent, const QString& label,
    const QString& value, bool read_only)
{
    auto* it = new QTreeWidgetItem(parent, {label});
    auto* ed = new QLineEdit(value, grid);
    ed->setReadOnly(read_only);
    ed->setFrame(false);
    grid->setItemWidget(it, 1, ed);
    return ed;
}

QSpinBox* target_properties_dialog::add_int(
    QTreeWidget* grid, QTreeWidgetItem* parent, const QString& label,
    int value, int lo, int hi)
{
    auto* it = new QTreeWidgetItem(parent, {label});
    auto* sp = new QSpinBox(grid);
    sp->setRange(lo, hi);
    sp->setValue(value);
    sp->setFrame(false);
    sp->setButtonSymbols(QAbstractSpinBox::NoButtons);
    grid->setItemWidget(it, 1, sp);
    return sp;
}

QComboBox* target_properties_dialog::add_choice(
    QTreeWidget* grid, QTreeWidgetItem* parent, const QString& label,
    const QStringList& items, int index)
{
    auto* it = new QTreeWidgetItem(parent, {label});
    auto* cb = new QComboBox(grid);
    cb->addItems(items);
    cb->setCurrentIndex(index);
    grid->setItemWidget(it, 1, cb);
    return cb;
}

QWidget* target_properties_dialog::build_tm_properties_page() {
    auto* page = new QWidget(this);
    auto* v    = new QVBoxLayout(page);
    v->addWidget(new QLabel(tr("<b>Target properties for %1</b>").arg(rec_.name), page));

    auto* grid = new QTreeWidget(page);
    grid->setColumnCount(2);
    grid->setHeaderLabels({tr("Property"), tr("Value")});
    grid->setAlternatingRowColors(true);
    grid->setRootIsDecorated(true);
    grid->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    grid->header()->setStretchLastSection(true);
    v->addWidget(grid, 1);


    auto* general = add_group(grid, nullptr, tr("General"));
    name_ = add_text(grid, general, tr("Target name"), rec_.name);

    auto* conn = add_group(grid, nullptr, tr("Connection Properties"));
    host_ = add_text(grid, conn, tr("IP address or host name"), rec_.host);
    port_ = add_int(grid, conn, tr("Port"), rec_.port, 1, 65535);

    auto* serving = add_group(grid, nullptr, tr("File Serving"));
    file_serv_ = add_text(grid, serving, tr("File server directory (app_home/)"), rec_.file_server_dir);
    home_dir_  = add_text(grid, serving, tr("Home directory (~/)"), rec_.home_dir);
    case_sensitive_ = add_bool(grid, serving, tr("Force case sensitive"), rec_.force_case_sensitive);
    env_expansion_  = add_bool(grid, serving, tr("Environmental variable expansion"), rec_.env_var_expansion);
    events_to_log_  = add_text(grid, serving, tr("Events to log"), rec_.events_to_log);
    fs_log_size_    = add_int(grid, serving, tr("Log size"), rec_.file_serving_log_size, 0, 1 << 24);
    auto* trace = add_group(grid, nullptr, tr("File Trace"));
    ft_log_size_ = add_int(grid, trace, tr("Log size"), rec_.file_trace_log_size, 0, 1 << 24);

    const auto& L = rec_.load;
    auto* load = add_group(grid, nullptr, tr("Load Options"));
    use_elf_stack_    = add_bool(grid, load, tr("Use stack size from ELF"), L.use_elf_stack);
    stack_kb_         = add_int(grid, load, tr("Default ELF stack size (KB)"), stack_bytes_to_kb(L.stack_size), 32, 1024);
    use_elf_priority_ = add_bool(grid, load, tr("Use priority value from ELF"), L.use_elf_priority);
    priority_         = add_int(grid, load, tr("Default priority"), static_cast<int>(L.priority), 0, 0xfff);
    wait_bdvd_        = add_bool(grid, load, tr("Wait for BDVD"), L.wait_for_bdvd);
    paramsfo_map_     = add_bool(grid, load, tr("PARAM.SFO mapping"), L.paramsfo_mapping);
    paramsfo_elfdir_  = add_bool(grid, load, tr("Use ELF directory for PARAM.SFO"), L.paramsfo_use_elf_dir);
    paramsfo_path_    = add_text(grid, load, tr("PARAM.SFO path"), L.paramsfo_path);
    debug_module_     = add_bool(grid, load, tr("Enable debugging of module"), L.enable_debug_module);
    ppu_dis_          = add_bool(grid, load, tr("Disable PPU debugging"), L.disable_ppu_debug);
    spu_dis_          = add_bool(grid, load, tr("Disable SPU debugging"), L.disable_spu_debug);

    auto* extra = add_group(grid, load, tr("Extra Load Options"));
    extra_enable_ = add_bool(grid, extra, tr("Enable extra load options"), L.enable_extra_options);
    lv2_except_   = add_bool(grid, extra, tr("Enable lv2 exception handler"), L.lv2_exception_handler);
    remote_play_  = add_choice(grid, extra, tr("Remote play"), {tr("Disabled"), tr("M4V && ATRAC"), tr("AVC && AAC")}, L.remote_play ? (L.remote_play_avc ? 2 : 1) : 0);
    libprof_      = add_bool(grid, extra, tr("Load libprof module"), L.load_libprof);

    auto* gcm = add_group(grid, extra, tr("GCM Debug"));
    gcm_debug_     = add_bool(grid, gcm, tr("Enable GCM debug"), L.gcm_debug);
    smart_capture_ = add_bool(grid, gcm, tr("Enable smart image capture"), L.smart_image_capture);
    gcm_capture_   = add_bool(grid, gcm, tr("Enable GCM capture mode"), L.gcm_capture_mode);
    auto* rsx = add_group(grid, gcm, tr("RSX Profiling Tool"));
    rsx_prof_   = add_bool(grid, rsx, tr("Enable RSX profiling tool"), L.rsx_profiling_tool);
    rsx_hud_    = add_bool(grid, rsx, tr("Enable HUD"), L.rsx_hud);
    rsx_hud_on_ = add_bool(grid, rsx, tr("Start with HUD on"), L.rsx_hud_start_on);
    high_mem_   = add_bool(grid, rsx, tr("High memory footprint features"), L.high_memory_footprint);

    auto* dump = add_group(grid, extra, tr("Core Dump"));
    core_dump_     = add_bool(grid, dump, tr("Enable core dump"), L.core_dump);
    core_dump_loc_ = add_choice(grid, dump, tr("Core dump location"), {"app_home", "dev_ms", "dev_usb", "dev_hdd0"}, dump_loc_to_index(L.core_dump_location));

    auto* trig = add_group(grid, extra, tr("Trigger Option"));
    trig_ppu_  = add_bool(grid, trig, tr("Disable PPU exception detection"), L.trig_disable_ppu_exc);
    trig_spu_  = add_bool(grid, trig, tr("Disable SPU exception detection"), L.trig_disable_spu_exc);
    trig_rsx_  = add_bool(grid, trig, tr("Disable RSX exception detection"), L.trig_disable_rsx_exc);
    trig_foot_ = add_bool(grid, trig, tr("Disable Foot Switch detection"), L.trig_disable_footswitch);

    auto* corefile = add_group(grid, extra, tr("Corefile Generation Option"));
    corefile_nomem_ = add_bool(grid, corefile, tr("Disable Memory Dump"), L.corefile_disable_memdump);

    auto* execctl = add_group(grid, extra, tr("Execution Control Option"));
    exec_restart_ = add_bool(grid, execctl, tr("Enable restart process after core dumped"), L.exec_restart_after_dump);
    exec_dumpfn_  = add_bool(grid, execctl, tr("Enable core dump function after core dumped"), L.exec_dump_fn_after_dump);

    mat_        = add_bool(grid, extra, tr("Enable MAT"), L.memory_access_trap);
    game_attr_  = add_choice(grid, extra, tr("Game attribute"), {tr("No attribute"), tr("Invite Message"), tr("Custom Data Message")}, L.game_attribute <= 2 ? L.game_attribute : 0);
    boot_msg_map_    = add_bool(grid, extra, tr("Enable \"bootable_message_data.dat\" mapping"), L.bootable_msg_mapping);
    boot_msg_elfdir_ = add_bool(grid, extra, tr("Use ELF directory for \"bootable_message_data.dat\""), L.bootable_msg_use_elf_dir);
    boot_msg_dir_    = add_text(grid, extra, tr("\"bootable_message_data.dat\" mapping directory"), L.bootable_msg_dir);
    patch_boot_ = add_bool(grid, extra, tr("Patch boot"), L.patch_boot);

    auto* console = add_group(grid, nullptr, tr("Console Output"));
    console_kb_ = add_int(grid, console, tr("Console output cache size (KB)"), rec_.console_cache_kb, 0, 1 << 20);

    auto* capture = add_group(grid, nullptr, tr("Image Capture"));
    capture_auto_ = add_bool(grid, capture, tr("Save captures automatically"), rec_.image_capture_auto);
    capture_dir_  = add_text(grid, capture, tr("Image capture directory"), rec_.image_capture_dir);

    auto* reset = add_group(grid, nullptr, tr("Advanced Reset Settings"));
    reset_mode_ = add_choice(grid, reset, tr("Reset type"), {tr("Debug Mode"), tr("System Software Mode"), tr("Release Mode"), tr("Advanced (explicit)")}, rec_.reset_mode);
    reset_boot_val_  = add_text(grid, reset, tr("Boot parameters (value)"), QStringLiteral("0x%1").arg(rec_.reset_boot_value, 0, 16));
    reset_boot_mask_ = add_text(grid, reset, tr("Boot parameters (mask)"), QStringLiteral("0x%1").arg(rec_.reset_boot_mask, 0, 16));
    reset_sys_val_   = add_text(grid, reset, tr("System parameters (value)"), QStringLiteral("0x%1").arg(rec_.reset_system_value, 0, 16));
    reset_sys_mask_  = add_text(grid, reset, tr("System parameters (mask)"), QStringLiteral("0x%1").arg(rec_.reset_system_mask, 0, 16));
    reset_display_   = add_bool(grid, reset, tr("Display reset settings"), rec_.display_reset_settings);

    general->setExpanded(true);
    conn->setExpanded(true);
    return page;
}

QWidget* target_properties_dialog::build_timeouts_page() {
    auto* page = new QWidget(this);
    auto* form = new QFormLayout(page);
    const auto& T = rec_.timeouts;
    auto row = [&](const QString& label, int value) {
        auto* sp = new QSpinBox(page);
        sp->setRange(0, 600000);
        sp->setSingleStep(100);
        sp->setSuffix(tr(" ms"));
        sp->setValue(value);
        form->addRow(label, sp);
        return sp;
    };
    to_default_   = row(tr("Default time-out:"),   T.default_ms);
    to_reset_     = row(tr("Reset time-out:"),     T.reset_ms);
    to_connect_   = row(tr("Connect time-out:"),   T.connect_ms);
    to_load_      = row(tr("Load time-out:"),      T.load_ms);
    to_status_    = row(tr("Status time-out:"),    T.status_ms);
    to_reconnect_ = row(tr("Reconnect time-out:"), T.reconnect_ms);
    to_game_port_ = row(tr("Game port time-out:"), T.game_port_ms);
    to_game_exit_ = row(tr("Game exit time-out:"), T.game_exit_ms);
    return page;
}

QWidget* target_properties_dialog::build_xmb_page() {
    auto* page = new QWidget(this);
    auto* v = new QVBoxLayout(page);

    xmb_override_ = new QCheckBox(tr("Override XMB settings (applied after reset)"), page);
    v->addWidget(xmb_override_);

    xmb_status_ = new QLabel(page);
    xmb_status_->setWordWrap(true);
    v->addWidget(xmb_status_);

    xmb_grid_ = new QTreeWidget(page);
    xmb_grid_->setColumnCount(2);
    xmb_grid_->setHeaderLabels({tr("Setting"), tr("Value")});
    xmb_grid_->setAlternatingRowColors(true);
    xmb_grid_->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    xmb_grid_->header()->setStretchLastSection(true);
    v->addWidget(xmb_grid_, 1);

    auto* row = new QHBoxLayout;
    auto* refresh = new QPushButton(tr("Refresh from Target"), page);
    auto* apply   = new QPushButton(tr("Save Overrides and Reset"), page);
    row->addWidget(refresh);
    row->addWidget(apply);
    row->addStretch(1);
    v->addLayout(row);

    connect(refresh, &QPushButton::clicked, this, &target_properties_dialog::request_xmb_refresh);
    connect(apply, &QPushButton::clicked, this, &target_properties_dialog::apply_xmb);

    reload_xmb();
    return page;
}

void target_properties_dialog::request_xmb_refresh() {
    emit xmb_refresh_requested();
    xmb_status_->setText(tr("Requested a fresh dump from the target..."));
    QTimer::singleShot(1200, this, &target_properties_dialog::reload_xmb);
}

void target_properties_dialog::apply_xmb() {
    if (!write_xmb_overrides()) return;
    emit xmb_apply_requested(xmb_written_path_, static_cast<quint32>(xmb_written_size_));
}

void target_properties_dialog::apply_xmb_if_current() {
    if (pages_->currentIndex() != kXmbPage) return;
    if (!xmb_override_ || !xmb_override_->isChecked()) return;
    apply_xmb();
}

void target_properties_dialog::reload_xmb() {
    xmb_grid_->clear();
    xmb_editors_.clear();

    if (rec_.file_server_dir.isEmpty()) {
        xmb_status_->setText(tr("<i>No file-serving directory set for this target, so there is ""nowhere for the console to have written its settings dump. ""Set one under File Serving, reconnect, then reopen this ""page.</i>"));
        return;
    }

    const QString path =
        QDir(rec_.file_server_dir).filePath(QStringLiteral("PS3SETTINGS_OUT.SFT"));
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        xmb_status_->setText(tr("<i>No settings dump at <tt>%1</tt> yet. Press ""\"Refresh from Target\" while connected.</i>").arg(path));
        return;
    }
    xmb_baseline_ = opentm::tm_core::sft_settings::parse(f.readAll());
    f.close();

    QList<QString> order;
    QHash<QString, QTreeWidgetItem*> groups;
    int keys = 0, unknown = 0;

    for (const auto& s : xmb_baseline_.sections()) {
        if (s.name == QLatin1String("Version")) continue; // structural
        for (const auto& e : s.entries) {
            const auto d = opentm::tm_core::describe(s.name, e.key);
            if (d.group == QLatin1String("Other")) ++unknown;
            if (!groups.contains(d.group)) {
                groups.insert(d.group, add_group(xmb_grid_, nullptr, d.group));
                groups[d.group]->setExpanded(true);
                order.push_back(d.group);
            }
            auto* parent = groups[d.group];

            QString label = d.label;
            if (d.control_only) label += tr("  (control)");
            const QString tip = QStringLiteral("[%1] %2").arg(s.name, e.key);

            if (!d.choices.empty()) {
                auto choices = d.choices;
                const bool known = std::any_of(choices.begin(), choices.end(), [&](const auto& c) {
                    return c.value == e.value;
                });
                if (!known) {
                    choices.push_back({e.value, tr("%1 (unknown)").arg(e.value)});
                }

                QStringList items;
                int current = 0;
                for (int i = 0; i < static_cast<int>(choices.size()); ++i) {
                    items << choices[i].label;
                    if (choices[i].value == e.value) current = i;
                }
                auto* cb = add_choice(xmb_grid_, parent, label, items, current);
                xmb_editors_.push_back({s.name, e.key, nullptr, cb, nullptr, choices});
                cb->setToolTip(tip);
            } else if (d.type == opentm::tm_core::sft_type::boolean) {
                auto* bx = add_bool(xmb_grid_, parent, label, e.value.trimmed() != QLatin1String("0") && !e.value.trimmed().isEmpty());
                xmb_editors_.push_back({s.name, e.key, nullptr, nullptr, bx, {}});
                bx->setToolTip(tip);
            } else {
                auto* ed = add_text(xmb_grid_, parent, label, e.value);
                xmb_editors_.push_back({s.name, e.key, ed, nullptr, nullptr, {}});
                ed->setToolTip(tip);
            }
            ++keys;
        }
    }

    xmb_status_->setText(tr("Read %1 settings from <tt>%2</tt>%3.").arg(keys).arg(path).arg(unknown ? tr(" - %1 not in the known-key table and shown ""under \"Other\" with their raw names").arg(unknown) : QString()));
}

bool target_properties_dialog::write_xmb_overrides() {
    if (!xmb_override_->isChecked()) {
        xmb_status_->setText(tr("<i>\"Override XMB settings\" is off - nothing written.</i>"));
        return false;
    }
    opentm::tm_core::sft_settings edited = xmb_baseline_;
    for (const auto& ed : xmb_editors_) {
        if (ed.combo) {
            const int i = ed.combo->currentIndex();
            if (i >= 0 && i < static_cast<int>(ed.choices.size())) {
                edited.set(ed.section, ed.key, ed.choices[i].value);
            }
        } else if (ed.check) {
            edited.set(ed.section, ed.key, ed.check->isChecked() ? QStringLiteral("1") : QStringLiteral("0"));
        } else if (ed.editor) {
            edited.set(ed.section, ed.key, ed.editor->text());
        }
    }
    const auto overrides = edited.overrides_against(xmb_baseline_);
    if (overrides.empty()) {
        xmb_status_->setText(tr("<i>No settings changed - nothing to apply.</i>"));
        return false;
    }

    const QString path =
        QDir(rec_.file_server_dir).filePath(QStringLiteral("PS3SETTINGS.SFT"));
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        xmb_status_->setText(tr("<b>Could not write %1</b>").arg(path));
        return false;
    }
    const auto blob = overrides.serialise();
    f.write(blob);
    f.close();
    xmb_written_path_ = path;
    xmb_written_size_ = blob.size();

    int n = 0;
    for (const auto& s : overrides.sections()) n += static_cast<int>(s.entries.size());
    xmb_status_->setText(tr("Wrote %1 overridden setting(s) to <tt>%2</tt> (%3 bytes). ""The console picks these up on its next reset.").arg(n).arg(path).arg(blob.size()));
    return true;
}

target_record target_properties_dialog::record() const {
    target_record r = rec_;
    r.name            = name_->text();
    r.host            = host_->text();
    r.port            = static_cast<quint16>(port_->value());
    r.file_server_dir = file_serv_->text();
    r.home_dir        = home_dir_->text();
    r.force_case_sensitive  = case_sensitive_->isChecked();
    r.env_var_expansion     = env_expansion_->isChecked();
    r.events_to_log         = events_to_log_->text();
    r.file_serving_log_size = fs_log_size_->value();
    r.file_trace_log_size   = ft_log_size_->value();
    r.console_cache_kb      = console_kb_->value();
    r.image_capture_auto    = capture_auto_->isChecked();
    r.image_capture_dir     = capture_dir_->text();

    auto hex_or = [](const QString& s, quint64 fallback) {
        bool ok = false;
        const auto v = s.trimmed().startsWith(QStringLiteral("0x"), Qt::CaseInsensitive) ? s.trimmed().mid(2).toULongLong(&ok, 16) : s.trimmed().toULongLong(&ok, 16);
        return ok ? v : fallback;
    };
    r.reset_boot_value   = hex_or(reset_boot_val_->text(),  r.reset_boot_value);
    r.reset_boot_mask    = hex_or(reset_boot_mask_->text(), r.reset_boot_mask);
    r.reset_system_value = hex_or(reset_sys_val_->text(),   r.reset_system_value);
    r.reset_system_mask  = hex_or(reset_sys_mask_->text(),  r.reset_system_mask);
    r.display_reset_settings = reset_display_->isChecked();
    r.reset_mode             = reset_mode_->currentIndex();

    auto& L = r.load;
    L.use_elf_stack       = use_elf_stack_->isChecked();
    L.stack_size          = stack_kb_to_bytes(stack_kb_->value());
    L.use_elf_priority    = use_elf_priority_->isChecked();
    L.priority            = static_cast<std::uint32_t>(priority_->value());
    L.wait_for_bdvd       = wait_bdvd_->isChecked();
    L.enable_debug_module = debug_module_->isChecked();
    L.disable_ppu_debug   = ppu_dis_->isChecked();
    L.disable_spu_debug   = spu_dis_->isChecked();

    L.enable_extra_options  = extra_enable_->isChecked();
    L.lv2_exception_handler = lv2_except_->isChecked();
    const int rp            = remote_play_->currentIndex();
    L.remote_play           = rp != 0;
    L.remote_play_avc       = rp == 2;
    L.load_libprof          = libprof_->isChecked();
    L.gcm_debug             = gcm_debug_->isChecked();
    L.smart_image_capture   = smart_capture_->isChecked();
    L.gcm_capture_mode      = gcm_capture_->isChecked();
    L.rsx_profiling_tool    = rsx_prof_->isChecked();
    L.high_memory_footprint = high_mem_->isChecked();
    L.core_dump             = core_dump_->isChecked();
    L.core_dump_location    = index_to_dump_loc(core_dump_loc_->currentIndex());
    L.memory_access_trap    = mat_->isChecked();
    L.game_attribute        = static_cast<std::uint8_t>(game_attr_->currentIndex());
    L.patch_boot            = patch_boot_->isChecked();
    L.paramsfo_mapping         = paramsfo_map_->isChecked();
    L.paramsfo_use_elf_dir     = paramsfo_elfdir_->isChecked();
    L.paramsfo_path            = paramsfo_path_->text();
    L.rsx_hud                  = rsx_hud_->isChecked();
    L.rsx_hud_start_on         = rsx_hud_on_->isChecked();
    L.trig_disable_ppu_exc     = trig_ppu_->isChecked();
    L.trig_disable_spu_exc     = trig_spu_->isChecked();
    L.trig_disable_rsx_exc     = trig_rsx_->isChecked();
    L.trig_disable_footswitch  = trig_foot_->isChecked();
    L.corefile_disable_memdump = corefile_nomem_->isChecked();
    L.exec_restart_after_dump  = exec_restart_->isChecked();
    L.exec_dump_fn_after_dump  = exec_dumpfn_->isChecked();
    L.bootable_msg_mapping     = boot_msg_map_->isChecked();
    L.bootable_msg_use_elf_dir = boot_msg_elfdir_->isChecked();
    L.bootable_msg_dir         = boot_msg_dir_->text();

    auto& T = r.timeouts;
    T.default_ms   = to_default_->value();
    T.reset_ms     = to_reset_->value();
    T.connect_ms   = to_connect_->value();
    T.load_ms      = to_load_->value();
    T.status_ms    = to_status_->value();
    T.reconnect_ms = to_reconnect_->value();
    T.game_port_ms = to_game_port_->value();
    T.game_exit_ms = to_game_exit_->value();
    return r;
}

} // namespace opentm::tm_ui
