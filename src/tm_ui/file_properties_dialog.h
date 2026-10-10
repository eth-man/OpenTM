#pragma once

#include <QDateTime>
#include <QDialog>

#include <cstdint>

class QCheckBox;
class QDateTimeEdit;
class QLineEdit;

namespace opentm::tm_ui {

class file_properties_dialog : public QDialog {
    Q_OBJECT
public:
    struct values {
        std::uint32_t mode  = 0; // full st_mode, file type bits included
        std::uint64_t atime = 0; // posix seconds
        std::uint64_t mtime = 0;
        std::uint64_t ctime = 0;
    };

    file_properties_dialog(const QString& kit_path, const values& current, QWidget* parent = nullptr);

    // res after accept(): mode keeps the original file type bits.
    values result() const;

private:
    void sync_numeric_from_boxes();
    void sync_boxes_from_numeric();
    std::uint32_t permission_bits() const;

    QString  path_;
    values   current_;
    bool     syncing_ = false;

    QCheckBox*     read_    = nullptr;
    QCheckBox*     write_   = nullptr;
    QCheckBox*     exec_    = nullptr;
    QLineEdit*     numeric_ = nullptr;
    QCheckBox*     set_atime_ = nullptr;
    QCheckBox*     set_mtime_ = nullptr;
    QDateTimeEdit* atime_   = nullptr;
    QDateTimeEdit* mtime_   = nullptr;
};

} // namespace opentm::tm_ui
