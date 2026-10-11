#include "file_explorer_panel.h"

#include "../app_prefs.h"

#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QDateTime>
#include <QGuiApplication>
#include <QInputDialog>
#include <QMessageBox>
#include <QMenu>
#include <QFont>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QHash>
#include <QIcon>
#include <QMouseEvent>
#include <QPushButton>
#include <QShortcut>
#include <QStandardItem>
#include <QStandardItemModel>
#include <QStringLiteral>
#include <QStyle>
#include <QTableView>
#include <QVBoxLayout>

namespace opentm::tm_ui {

namespace {

constexpr int col_name = 0, col_mode = 3, col_modified = 4, col_created = 5, col_accessed = 6;


QString format_size(std::uint64_t bytes) {
    if (bytes == 0) return QString();

    const bool iec = size_units_are_binary();
    const double step = iec ? 1024.0 : 1000.0;
    static const char* const kBinary[]  = { "KiB", "MiB", "GiB", "TiB" };
    static const char* const kDecimal[] = { "kB",  "MB",  "GB",  "TB"  };
    const char* const* units = iec ? kBinary : kDecimal;

    if (double(bytes) < step) {
        return file_explorer_panel::tr("%1 bytes").arg(bytes);
    }
    double v = double(bytes);
    int tier = -1;
    while (v >= step && tier < 3) { v /= step; ++tier; }
    return QStringLiteral("%1 %2").arg(v, 0, 'f', 2).arg(QLatin1String(units[tier]));
}

QString format_mode(std::uint32_t mode) {
    if (mode == 0) return QString();
    QChar type = '?';
    const std::uint32_t fmt = mode & 0xF000u;
    switch (fmt) {
        case 0x4000u: type = 'd'; break; // S_IFDIR
        case 0x8000u: type = '-'; break; // S_IFREG
        case 0xA000u: type = 'l'; break; // S_IFLNK
        case 0x2000u: type = 'c'; break; // S_IFCHR
        case 0x6000u: type = 'b'; break; // S_IFBLK
        case 0x1000u: type = 'p'; break; // S_IFIFO
        default:      type = '-'; break;
    }
    QString s;
    s.reserve(10);
    s.append(type);
    const char rwx[9] = { 'r','w','x','r','w','x','r','w','x' };
    for (int bit = 8; bit >= 0; --bit) {
        s.append((mode & (1u << bit)) ? rwx[8 - bit] : '-');
    }
    return s;
}

QString format_timestamp(std::uint32_t secs) {
    if (secs == 0) return QString();
    return QDateTime::fromSecsSinceEpoch(static_cast<qint64>(secs))
        .toString(QStringLiteral("dd/MM/yyyy HH:mm:ss"));
}


QString icon_for_device(const QString& name) {
    const QString n = name.toLower();
    if (n.startsWith(QLatin1String("dev_hdd")))    return QStringLiteral("drive");
    if (n.startsWith(QLatin1String("dev_usb")))    return QStringLiteral("drive_usb");
    if (n.startsWith(QLatin1String("dev_flash")))  return QStringLiteral("drive_flash");
    if (n.startsWith(QLatin1String("dev_ms")))     return QStringLiteral("drive_ms");
    if (n.startsWith(QLatin1String("dev_sd")))     return QStringLiteral("drive_sd");
    if (n.startsWith(QLatin1String("dev_cf")))     return QStringLiteral("drive_cf");
    if (n == QLatin1String("dev_bdvd") || n == QLatin1String("dev_ps2disc")
        || n.startsWith(QLatin1String("dev_bdemu"))) {
        return QStringLiteral("drive_disk");
    }
    if (n == QLatin1String("app_home") || n == QLatin1String("host_root")) {
        return QStringLiteral("folder_explore");
    }
    return QString();
}

bool is_executable_name(const QString& name) {
    const QString n = name.toLower();
    if (n == QLatin1String("eboot.bin")) return true;
    for (const auto& suffix : {QLatin1String(".self"), QLatin1String(".fself"), QLatin1String(".elf"), QLatin1String(".sprx"), QLatin1String(".prx")}) {
        if (n.endsWith(suffix)) return true;
    }
    return false;
}

QString icon_for_file(const QString& ext) {
    static const QHash<QString, QString> kMap = {
        // executables and modules
        {QStringLiteral("self"),  QStringLiteral("application")},
        {QStringLiteral("fself"), QStringLiteral("application")},
        {QStringLiteral("elf"),   QStringLiteral("application")},
        {QStringLiteral("bin"),   QStringLiteral("box")},
        {QStringLiteral("sprx"),  QStringLiteral("plugin")},
        {QStringLiteral("prx"),   QStringLiteral("plugin")},
        // packages / images
        {QStringLiteral("pkg"),   QStringLiteral("package")},
        {QStringLiteral("iso"),   QStringLiteral("drive_disk")},
        {QStringLiteral("pup"),   QStringLiteral("package")},
        // metadata / config
        {QStringLiteral("sfo"),   QStringLiteral("page_white_gear")},
        {QStringLiteral("sft"),   QStringLiteral("page_white_gear")},
        {QStringLiteral("ini"),   QStringLiteral("page_white_gear")},
        {QStringLiteral("cfg"),   QStringLiteral("page_white_gear")},
        {QStringLiteral("xml"),   QStringLiteral("page_white_code")},
        {QStringLiteral("json"),  QStringLiteral("page_white_code")},
        // text / logs
        {QStringLiteral("txt"),   QStringLiteral("page_white_text")},
        {QStringLiteral("log"),   QStringLiteral("report")},
        // images
        {QStringLiteral("png"),   QStringLiteral("picture")},
        {QStringLiteral("jpg"),   QStringLiteral("picture")},
        {QStringLiteral("jpeg"),  QStringLiteral("picture")},
        {QStringLiteral("bmp"),   QStringLiteral("picture")},
        {QStringLiteral("gif"),   QStringLiteral("picture")},
        {QStringLiteral("dds"),   QStringLiteral("picture")},
        {QStringLiteral("gim"),   QStringLiteral("picture")},
        // video / audio
        {QStringLiteral("pam"),   QStringLiteral("film")},
        {QStringLiteral("mp4"),   QStringLiteral("film")},
        {QStringLiteral("m4v"),   QStringLiteral("film")},
        {QStringLiteral("bik"),   QStringLiteral("film")},
        {QStringLiteral("at3"),   QStringLiteral("music")},
        {QStringLiteral("msf"),   QStringLiteral("music")},
        {QStringLiteral("vag"),   QStringLiteral("music")},
        {QStringLiteral("wav"),   QStringLiteral("music")},
        {QStringLiteral("mp3"),   QStringLiteral("music")},
        // archives
        {QStringLiteral("zip"),   QStringLiteral("compress")},
        {QStringLiteral("gz"),    QStringLiteral("compress")},
        {QStringLiteral("psarc"), QStringLiteral("compress")},
        // fonts
        {QStringLiteral("ttf"),   QStringLiteral("font")},
        {QStringLiteral("pfd"),   QStringLiteral("font")},
    };
    const auto it = kMap.constFind(ext.toLower());
    return it == kMap.constEnd() ? QStringLiteral("page_white") : *it;
}

QString file_extension(const std::string& name) {
    const auto dot = name.find_last_of('.');
    if (dot == std::string::npos || dot == 0 || dot + 1 >= name.size()) {
        return QString();
    }
    return QString::fromStdString(name.substr(dot + 1));
}

} // namespace

file_explorer_panel::file_explorer_panel(QWidget* parent)
    : QWidget(parent)
{
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    auto* path_row = new QHBoxLayout();
    path_row->setContentsMargins(6, 6, 6, 6);
    path_row->setSpacing(4);
    auto* path_label = new QLabel(tr("Path:"), this);
    path_edit_ = new QLineEdit(QStringLiteral("/"), this);
    auto* go_btn = new QPushButton(tr("Go"), this);
    go_btn->setIcon(QIcon(QStringLiteral(":/icons/arrow_redo.bmp")));
    back_btn_ = new QPushButton(tr("Back"), this);
    fwd_btn_  = new QPushButton(tr("Forward"), this);
    back_btn_->setIcon(QIcon(QStringLiteral(":/icons/arrow_left.bmp")));
    fwd_btn_->setIcon(QIcon(QStringLiteral(":/icons/arrow_right.bmp")));
    back_btn_->setToolTip(tr("Back (mouse button 4)"));
    fwd_btn_->setToolTip(tr("Forward (mouse button 5)"));
    auto* up_btn = new QPushButton(tr("Up"), this);
    up_btn->setIcon(QIcon(QStringLiteral(":/icons/arrow_up.bmp")));
    auto* refresh_btn = new QPushButton(tr("Refresh"), this);
    refresh_btn->setIcon(QIcon(QStringLiteral(":/icons/arrow_refresh.bmp")));
    path_row->addWidget(path_label);
    path_row->addWidget(path_edit_, 1);
    path_row->addWidget(go_btn);
    path_row->addWidget(back_btn_);
    path_row->addWidget(fwd_btn_);
    path_row->addWidget(up_btn);
    path_row->addWidget(refresh_btn);
    outer->addLayout(path_row);

    model_ = new QStandardItemModel(0, 7, this);
    model_->setHeaderData(0, Qt::Horizontal, tr("Name"));
    model_->setHeaderData(1, Qt::Horizontal, tr("Extension"));
    model_->setHeaderData(2, Qt::Horizontal, tr("Size"));
    model_->setHeaderData(3, Qt::Horizontal, tr("Mode"));
    model_->setHeaderData(4, Qt::Horizontal, tr("Modified"));
    model_->setHeaderData(5, Qt::Horizontal, tr("Created"));
    model_->setHeaderData(6, Qt::Horizontal, tr("Accessed"));

    view_ = new QTableView(this);
    view_->setModel(model_);
    view_->setSelectionBehavior(QAbstractItemView::SelectRows);
    view_->setSelectionMode(QAbstractItemView::SingleSelection);
    view_->setAlternatingRowColors(true);
    view_->setSortingEnabled(true);
    view_->setShowGrid(false);
    view_->setEditTriggers(QAbstractItemView::NoEditTriggers);

    view_->verticalHeader()->setVisible(false);
    {
        QFont f = view_->font();
        f.setPointSize(qMax(8, f.pointSize() - 1));
        view_->setFont(f);
        const QFontMetrics fm(f);
        view_->verticalHeader()->setDefaultSectionSize(fm.height() + 4);
    }

    view_->horizontalHeader()->setStretchLastSection(true);
    view_->horizontalHeader()->setHighlightSections(false);
    view_->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    view_->setColumnWidth(0, 240); // Name (icon + text)
    view_->setColumnWidth(1, 70); // Extension
    view_->setColumnWidth(2, 90); // Size
    view_->setColumnWidth(3, 100); // Mode
    view_->setColumnWidth(4, 140); // Modified
    view_->setColumnWidth(5, 140); // Created
    view_->setColumnWidth(6, 140); // Accessed
    outer->addWidget(view_, 1);

    // nav hook
    auto navigate = [this]() {
        const auto p = path_edit_->text().trimmed();
        if (!p.isEmpty()) navigate_to(p);
    };
    connect(go_btn,      &QPushButton::clicked, this, navigate);

    connect(refresh_btn, &QPushButton::clicked, this, [this]() {
        if (!current_path_.isEmpty()) emit list_directory_requested(current_path_);
    });

    connect(up_btn, &QPushButton::clicked, this, [this]() {
        const auto cur = current_path_;
        if (cur.isEmpty() || cur == "/") return;
        const auto trimmed = cur.endsWith('/') ? cur.left(cur.size() - 1) : cur;
        const int slash = trimmed.lastIndexOf('/');
        const QString parent = (slash <= 0) ? "/" : trimmed.left(slash + 1);
        navigate_to(parent);
    });

    connect(path_edit_, &QLineEdit::returnPressed, this, navigate);
    connect(back_btn_, &QPushButton::clicked, this, &file_explorer_panel::go_back);
    connect(fwd_btn_,  &QPushButton::clicked, this, &file_explorer_panel::go_forward);
    // QTableView seems to consume back/forward itself, so watch its viewport too
    view_->installEventFilter(this);
    view_->viewport()->installEventFilter(this);
    update_nav_buttons();
    view_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(view_, &QTableView::customContextMenuRequested, this, &file_explorer_panel::show_context_menu);

    // widget scoped so it wins over the window's F5, which is Power On
    auto* refresh_sc = new QShortcut(QKeySequence(Qt::Key_F5), this);
    refresh_sc->setContext(Qt::WidgetWithChildrenShortcut);
    connect(refresh_sc, &QShortcut::activated, this, [this] {
        if (!current_path_.isEmpty()) emit list_directory_requested(current_path_);
    });

    connect(view_, &QTableView::doubleClicked, this, [this](const QModelIndex& idx) {
        if (!idx.isValid()) return;
        const auto* name_item = model_->item(idx.row(), 0);
        if (!name_item) {
            emit log_message(QStringLiteral("    -- double-click on row %1: no name item").arg(idx.row()));
            return;
        }
        // is_directory flag stashed on the name item in show_directory.
        if (!name_item->data(Qt::UserRole + 1).toBool()) {
            emit log_message(QStringLiteral("    -- double-click '%1' ignored: not flagged as a directory").arg(name_item->text()));
            return;
        }
        QString next = current_path_;
        if (!next.endsWith('/')) next += '/';
        next += name_item->text();
        if (!next.endsWith('/')) next += '/';
        navigate_to(next);
    });
}

file_explorer_panel::~file_explorer_panel() = default;

void file_explorer_panel::navigate_to(const QString& path) {
    if (path.isEmpty()) return;
    in_history_nav_ = false;
    emit list_directory_requested(path);
}

void file_explorer_panel::record_history(const QString& path) {
    if (path.isEmpty()) return;
    if (in_history_nav_) {
        in_history_nav_ = false;
        update_nav_buttons();
        return;
    }
    if (history_pos_ >= 0 && history_.value(history_pos_) == path) {
        update_nav_buttons();
        return;
    }
    history_.erase(history_.begin() + (history_pos_ + 1), history_.end());
    history_.push_back(path);
    history_pos_ = static_cast<int>(history_.size()) - 1;
    update_nav_buttons();
}

void file_explorer_panel::go_back() {
    if (history_pos_ <= 0) return;
    --history_pos_;
    in_history_nav_ = true; // cleared when the listing arrives
    emit list_directory_requested(history_.value(history_pos_));
    update_nav_buttons();
}

void file_explorer_panel::go_forward() {
    if (history_pos_ + 1 >= static_cast<int>(history_.size())) return;
    ++history_pos_;
    in_history_nav_ = true; // cleared when the listing arrives
    emit list_directory_requested(history_.value(history_pos_));
    update_nav_buttons();
}

void file_explorer_panel::update_nav_buttons() {
    if (back_btn_) back_btn_->setEnabled(history_pos_ > 0);
    if (fwd_btn_)  fwd_btn_->setEnabled(history_pos_ + 1 < static_cast<int>(history_.size()));
}

void file_explorer_panel::mousePressEvent(QMouseEvent* e) {
    if (e->button() == Qt::BackButton)         { go_back(); e->accept(); return; }
    if (e->button() == Qt::ForwardButton)      { go_forward(); e->accept(); return; }
    QWidget::mousePressEvent(e);
}

bool file_explorer_panel::eventFilter(QObject* obj, QEvent* e) {
    if (e->type() == QEvent::MouseButtonPress) {
        const auto btn = static_cast<QMouseEvent*>(e)->button();
        if (btn == Qt::BackButton)    { go_back(); return true; }
        if (btn == Qt::ForwardButton) { go_forward(); return true; }
    }
    return QWidget::eventFilter(obj, e);
}

QString file_explorer_panel::current_path() const {
    return path_edit_ ? path_edit_->text() : QString();
}

QString file_explorer_panel::absolute_path_for(int row) const {
    const auto* item = model_->item(row, 0);
    if (!item) return {};
    QString base = current_path_;
    if (!base.endsWith(QLatin1Char('/'))) base += QLatin1Char('/');
    return base + item->text();
}

void file_explorer_panel::show_context_menu(const QPoint& pos) {
    const auto idx = view_->indexAt(pos);
    QMenu menu(this);

    const bool at_mount_table = current_path_.isEmpty() || current_path_ == QLatin1String("/");

    if (at_mount_table) {
        auto* refresh = menu.addAction(QIcon(QStringLiteral(":/icons/arrow_refresh.bmp")), tr("Re&fresh"));
        connect(refresh, &QAction::triggered, this, [this] {
            if (!current_path_.isEmpty()) emit list_directory_requested(current_path_);
        });
        if (idx.isValid()) {
            const QString full = absolute_path_for(idx.row());
            auto* open = menu.addAction(QIcon(QStringLiteral(":/icons/folder_explore.bmp")), tr("&Open"));
            connect(open, &QAction::triggered, this, [this, full] {
                navigate_to(full.endsWith(QLatin1Char('/')) ? full : full + QLatin1Char('/'));
            });
            menu.setDefaultAction(open);
            auto* copy = menu.addAction(QIcon(QStringLiteral(":/icons/page_white_copy.bmp")), tr("&Copy Path"));
            connect(copy, &QAction::triggered, this, [full] {
                QGuiApplication::clipboard()->setText(full);
            });
        }
        menu.exec(view_->viewport()->mapToGlobal(pos));
        return;
    }

    if (idx.isValid()) {
        const int row = idx.row();
        const auto* name_item = model_->item(row, 0);
        const bool is_dir = name_item && name_item->data(Qt::UserRole + 1).toBool();
        const QString name = name_item ? name_item->text() : QString();
        const QString full = absolute_path_for(row);

        if (is_dir) {
            auto* open = menu.addAction(QIcon(QStringLiteral(":/icons/folder_explore.bmp")), tr("&Open"));
            connect(open, &QAction::triggered, this, [this, full] {
                navigate_to(full.endsWith(QLatin1Char('/')) ? full : full + QLatin1Char('/'));
            });
        } else if (is_executable_name(name)) {
            auto* run = menu.addAction(QIcon(QStringLiteral(":/icons/application_go.bmp")), tr("&Run on Target"));
            connect(run, &QAction::triggered, this, [this, full] {
                emit run_requested(full);
            });
            menu.setDefaultAction(run);
        }

        if (!is_dir) {
            auto* dl = menu.addAction(QIcon(QStringLiteral(":/icons/page_white_put.bmp")), tr("&Download..."));
            const auto* size_item = model_->item(row, 2);
            // the size column is formatted for humans, so carry the raw value
            const quint32 size = size_item ? size_item->data(Qt::UserRole + 1).toUInt() : 0;
            connect(dl, &QAction::triggered, this, [this, full, size] {
                emit download_requested(full, size);
            });
        }

        auto* del = menu.addAction(QIcon(QStringLiteral(":/icons/delete.bmp")), tr("De&lete"));
        connect(del, &QAction::triggered, this, [this, full, is_dir] {
            emit delete_requested(full, is_dir);
        });

        auto* ren = menu.addAction(QIcon(QStringLiteral(":/icons/pencil.bmp")), tr("Rena&me..."));
        connect(ren, &QAction::triggered, this, [this, full, name] {
            bool ok = false;
            const auto to = QInputDialog::getText(this, tr("Rename"), tr("New name for %1:").arg(name), QLineEdit::Normal, name, &ok).trimmed();
            if (!ok || to.isEmpty() || to == name) return;
            if (to.contains(QLatin1Char('/'))) {
                QMessageBox::warning(this, tr("Rename"), tr("A name cannot contain '/'."));
                return;
            }
            emit rename_requested(full, to);
        });

        auto* props = menu.addAction(QIcon(QStringLiteral(":/icons/cog.bmp")), tr("P&roperties..."));
        connect(props, &QAction::triggered, this, [this, full, row] {
            const auto at = [this, row](int col) -> quint64 {
                auto* it = model_->item(row, col);
                return it ? it->data(Qt::UserRole + 1).toULongLong() : 0;
            };
            auto* md = model_->item(row, col_mode);
            emit properties_requested(full, md ? md->data(Qt::UserRole + 1).toUInt() : 0, at(col_accessed), at(col_modified), at(col_created));
        });

        auto* copy = menu.addAction(QIcon(QStringLiteral(":/icons/page_white_copy.bmp")), tr("&Copy Path"));
        connect(copy, &QAction::triggered, this, [full] {
            QGuiApplication::clipboard()->setText(full);
        });
        menu.addSeparator();
    }

    auto* newdir = menu.addAction(QIcon(QStringLiteral(":/icons/add.bmp")), tr("&New Folder..."));
    connect(newdir, &QAction::triggered, this, [this] {
        bool ok = false;
        const auto name = QInputDialog::getText(this, tr("New Folder"), tr("Folder name:"), QLineEdit::Normal, QString(), &ok).trimmed();
        if (!ok || name.isEmpty()) return;
        if (name.contains(QLatin1Char('/'))) {
            QMessageBox::warning(this, tr("New Folder"), tr("A name cannot contain '/'."));
            return;
        }
        emit make_dir_requested(current_path_, name);
    });

    auto* up = menu.addAction(QIcon(QStringLiteral(":/icons/page_white_get.bmp")), tr("&Upload Here..."));
    connect(up, &QAction::triggered, this, [this] {
        emit upload_requested(current_path_);
    });

    auto* refresh = menu.addAction(QIcon(QStringLiteral(":/icons/arrow_refresh.bmp")), tr("Re&fresh"));
    connect(refresh, &QAction::triggered, this, [this] {
        if (!current_path_.isEmpty()) emit list_directory_requested(current_path_);
    });

    menu.exec(view_->viewport()->mapToGlobal(pos));
}

void file_explorer_panel::clear() {
    model_->removeRows(0, model_->rowCount());
}

void file_explorer_panel::show_directory(
    const QString& path,
    std::vector<opentm::tm_core::dfmp_file_entry> entries)
{
    model_->removeRows(0, model_->rowCount());
    current_path_ = path;
    record_history(path);
    if (path_edit_->text() != path) {
        path_edit_->setText(path);
    }

    const QIcon dir_icon(QStringLiteral(":/icons/folder.bmp"));
    QHash<QString, QIcon> icon_cache;

    for (const auto& e : entries) {
        if (e.name.empty() || e.name == "." || e.name == "..") continue;
        const bool is_dir = e.is_directory();

        const QString entry_name = QString::fromStdString(e.name);
        const QString ext = is_dir ? QString() : file_extension(e.name);
        auto* n  = new QStandardItem(entry_name);

        QString key;
        if (is_dir) {
            key = icon_for_device(entry_name);
        } else if (is_executable_name(entry_name)) {
            key = QStringLiteral("application");
        } else {
            key = icon_for_file(ext);
        }

        if (is_dir && key.isEmpty()) {
            n->setIcon(dir_icon);
        } else {
            auto it = icon_cache.constFind(key);
            if (it == icon_cache.constEnd()) {
                it = icon_cache.insert(
                    key, QIcon(QStringLiteral(":/icons/%1.bmp").arg(key)));
            }
            n->setIcon(*it);
        }
        n->setData(is_dir, Qt::UserRole + 1);

        auto* ex = new QStandardItem(ext);
        auto* sz = new QStandardItem(format_size(e.size));
        sz->setData(QVariant::fromValue<quint32>(static_cast<quint32>(e.size)), Qt::UserRole + 1);
        auto* md = new QStandardItem(format_mode(e.mode));
        md->setData(QVariant::fromValue<quint32>(e.mode), Qt::UserRole + 1);
        auto* mt = new QStandardItem(format_timestamp(e.mtime));
        mt->setData(QVariant::fromValue<quint64>(e.mtime), Qt::UserRole + 1);
        auto* ct = new QStandardItem(format_timestamp(e.ctime));
        ct->setData(QVariant::fromValue<quint64>(e.ctime), Qt::UserRole + 1);
        auto* at = new QStandardItem(format_timestamp(e.atime));
        at->setData(QVariant::fromValue<quint64>(e.atime), Qt::UserRole + 1);

        sz->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);

        for (auto* it : { n, ex, sz, md, mt, ct, at }) it->setEditable(false);
        model_->appendRow({ n, ex, sz, md, mt, ct, at });
    }
}

} // namespace opentm::tm_ui
