#pragma once

#include "add_target_dialog.h"

#include <tm_core/sft_schema.h>
#include <tm_core/sft_settings.h>

#include <QDialog>

#include <vector>

class QCheckBox;
class QComboBox;
class QLineEdit;
class QLabel;
class QPushButton;
class QSpinBox;
class QStackedWidget;
class QTreeWidget;
class QTreeWidgetItem;

namespace opentm::tm_ui {

class target_properties_dialog : public QDialog {
    Q_OBJECT
public:
    explicit target_properties_dialog(const target_record& r, QWidget* parent = nullptr);
    ~target_properties_dialog() override;
    target_record record() const;

signals:
    void applied(target_record r);
    void xmb_apply_requested(QString host_path, quint32 file_size);
    void xmb_refresh_requested();

private:
    QWidget* build_tm_properties_page();
    QWidget* build_timeouts_page();
    QWidget* build_xmb_page();
    void reload_xmb();
    void request_xmb_refresh();
    void apply_xmb();
    void apply_xmb_if_current();
    bool write_xmb_overrides();

    static constexpr int kXmbPage = 1;

    QTreeWidgetItem* add_group(QTreeWidget* grid, QTreeWidgetItem* parent, const QString& title);
    QCheckBox* add_bool(QTreeWidget* grid, QTreeWidgetItem* parent, const QString& label, bool value);
    QLineEdit* add_text(QTreeWidget* grid, QTreeWidgetItem* parent, const QString& label, const QString& value, bool read_only = false);
    QSpinBox*  add_int(QTreeWidget* grid, QTreeWidgetItem* parent, const QString& label, int value,int lo, int hi);
    QComboBox* add_choice(QTreeWidget* grid, QTreeWidgetItem* parent, const QString& label, const QStringList& items, int index);

    target_record   rec_;
    QTreeWidget*    nav_   = nullptr;
    QStackedWidget* pages_ = nullptr;
    QLineEdit* name_        = nullptr;
    QLineEdit* host_        = nullptr;
    QSpinBox*  port_        = nullptr;
    QLineEdit* file_serv_   = nullptr;
    QLineEdit* home_dir_    = nullptr;
    QCheckBox* case_sensitive_ = nullptr;
    QCheckBox* env_expansion_  = nullptr;
    QLineEdit* events_to_log_  = nullptr;
    QSpinBox*  fs_log_size_    = nullptr;
    QSpinBox*  ft_log_size_    = nullptr;
    QCheckBox* use_elf_stack_    = nullptr;
    QSpinBox*  stack_kb_         = nullptr;
    QCheckBox* use_elf_priority_ = nullptr;
    QSpinBox*  priority_         = nullptr;
    QCheckBox* wait_bdvd_        = nullptr;
    QCheckBox* paramsfo_map_     = nullptr;
    QCheckBox* paramsfo_elfdir_  = nullptr;
    QLineEdit* paramsfo_path_    = nullptr;
    QCheckBox* extra_enable_  = nullptr;
    QCheckBox* lv2_except_    = nullptr;
    QComboBox* remote_play_   = nullptr;
    QCheckBox* libprof_       = nullptr;
    QCheckBox* gcm_debug_     = nullptr;
    QCheckBox* smart_capture_ = nullptr;
    QCheckBox* rsx_prof_      = nullptr;
    QCheckBox* high_mem_      = nullptr;
    QCheckBox* gcm_capture_   = nullptr;
    QCheckBox* core_dump_     = nullptr;
    QComboBox* core_dump_loc_ = nullptr;
    QCheckBox* mat_           = nullptr;
    QComboBox* game_attr_     = nullptr;
    QCheckBox* patch_boot_    = nullptr;
    QCheckBox* debug_module_  = nullptr;
    QCheckBox* ppu_dis_       = nullptr;
    QCheckBox* spu_dis_       = nullptr;
    QCheckBox* rsx_hud_       = nullptr;
    QCheckBox* rsx_hud_on_    = nullptr;
    QCheckBox* trig_ppu_      = nullptr;
    QCheckBox* trig_spu_      = nullptr;
    QCheckBox* trig_rsx_      = nullptr;
    QCheckBox* trig_foot_     = nullptr;
    QCheckBox* corefile_nomem_ = nullptr;
    QCheckBox* exec_restart_   = nullptr;
    QCheckBox* exec_dumpfn_    = nullptr;
    QCheckBox* boot_msg_map_    = nullptr;
    QCheckBox* boot_msg_elfdir_ = nullptr;
    QLineEdit* boot_msg_dir_    = nullptr;
    QSpinBox*  console_kb_      = nullptr;
    QCheckBox* capture_auto_    = nullptr;
    QLineEdit* capture_dir_     = nullptr;
    QLineEdit* reset_boot_val_  = nullptr;
    QLineEdit* reset_boot_mask_ = nullptr;
    QLineEdit* reset_sys_val_   = nullptr;
    QLineEdit* reset_sys_mask_  = nullptr;
    QCheckBox* reset_display_   = nullptr;
    QComboBox* reset_mode_      = nullptr;
    QSpinBox* to_default_   = nullptr;
    QSpinBox* to_reset_     = nullptr;
    QSpinBox* to_connect_   = nullptr;
    QSpinBox* to_load_      = nullptr;
    QSpinBox* to_status_    = nullptr;
    QSpinBox* to_reconnect_ = nullptr;
    QSpinBox* to_game_port_ = nullptr;
    QSpinBox* to_game_exit_ = nullptr;
    struct xmb_editor {
        QString    section;
        QString    key;
        QLineEdit* editor = nullptr; // set for free form values
        QComboBox* combo  = nullptr; // set for keys with named values
        QCheckBox* check  = nullptr; // set for plain 0/1 flags
        std::vector<opentm::tm_core::sft_choice> choices;
    };
    QCheckBox*                     xmb_override_ = nullptr;
    QLabel*                        xmb_status_   = nullptr;
    QTreeWidget*                   xmb_grid_     = nullptr;
    opentm::tm_core::sft_settings  xmb_baseline_;
    std::vector<xmb_editor>        xmb_editors_;
    QString                        xmb_written_path_;
    int                            xmb_written_size_ = 0;
};

} // namespace opentm::tm_ui
