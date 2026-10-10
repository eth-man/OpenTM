#include "console_panel.h"

#include "../app_prefs.h"
#include "../find_dialog.h"

#include <QAction>
#include <QColor>
#include <QDateTime>
#include <QFont>
#include <QIcon>
#include <QLineEdit>
#include <QList>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPoint>
#include <QShortcut>
#include <QStringLiteral>
#include <QTabWidget>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextEdit>
#include <QVBoxLayout>

namespace opentm::tm_ui {

namespace {

void highlight_matches(QPlainTextEdit* view, const QString& needle) {
    if (!view) return;
    QList<QTextEdit::ExtraSelection> selections;
    if (!needle.isEmpty()) {
        QTextCharFormat fmt;
        fmt.setBackground(QColor(255, 230, 0, 120));
        fmt.setForeground(QColor(0, 0, 0));
        QTextCursor cursor(view->document());
        while (true) {
            cursor = view->document()->find(needle, cursor);
            if (cursor.isNull()) break;
            QTextEdit::ExtraSelection sel;
            sel.cursor = cursor;
            sel.format = fmt;
            selections.append(sel);
        }
    }
    view->setExtraSelections(selections);
}

int blocks_for_kb(int kb) {
    constexpr int kAssumedBytesPerLine = 64;
    const int blocks = (kb * 1024) / kAssumedBytesPerLine;
    return blocks < 1000 ? 1000 : blocks;
}

} // namespace

console_panel::console_panel(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    filter_ = new QLineEdit(this);
    filter_->setPlaceholderText(tr("Filter (highlights matches) - Ctrl+F to search"));
    filter_->setClearButtonEnabled(true);

    auto* find_sc = new QShortcut(QKeySequence::Find, this);
    find_sc->setContext(Qt::WidgetWithChildrenShortcut);
    connect(find_sc, &QShortcut::activated, this, &console_panel::focus_find);


    tabs_ = new QTabWidget(this);
    tabs_->setDocumentMode(true);
    tabs_->setTabPosition(QTabWidget::North);

    layout->addWidget(filter_);
    layout->addWidget(tabs_);

    connect(filter_, &QLineEdit::textChanged, this, [this](const QString& s) {
        if (auto* v = qobject_cast<QPlainTextEdit*>(tabs_->currentWidget()))
            highlight_matches(v, s);
    });
    connect(tabs_, &QTabWidget::currentChanged, this, [this](int) {
        if (auto* v = qobject_cast<QPlainTextEdit*>(tabs_->currentWidget()))
            highlight_matches(v, filter_->text());
    });

    set_channels(default_tty_channels());
}

console_panel::~console_panel() = default;

void console_panel::set_channels(const QVector<tty_channel>& channels) {
    channels_ = channels;
    views_.clear();
    routes_.clear();

    while (tabs_->count() > 0) {
        QWidget* w = tabs_->widget(0);
        tabs_->removeTab(0);
        w->deleteLater();
    }

    views_.resize(channels_.size());
    for (int i = 0; i < channels_.size(); ++i) {
        const tty_channel& c = channels_[i];
        if (!c.enabled) continue;

        auto* view = new QPlainTextEdit(tabs_);
        view->setReadOnly(true);
        view->setMaximumBlockCount(blocks_for_kb(c.buffer_kb));
        view->setFont(QFont(console_font_family(), console_font_point_size()));
        view->setLineWrapMode(c.word_wrap ? QPlainTextEdit::WidgetWidth : QPlainTextEdit::NoWrap);
        install_context_menu(view);
        views_[i].view = view;
        views_[i].at_line_start = true;
        tabs_->addTab(view, c.name);

        for (std::uint8_t s : c.streams) routes_[s].push_back(i);
    }
}

void console_panel::append_stream(std::uint8_t stream, const QString& text) {
    append_stream_at(stream, text, QDateTime::currentDateTime());
}

void console_panel::append_stream_at(std::uint8_t stream, const QString& text, const QDateTime& when) {
    const auto it = routes_.constFind(stream);
    if (it == routes_.constEnd()) return; // nothing listening
    for (int idx : *it) append_to(idx, text, when);
}

void console_panel::append_to(int channel_index, const QString& text, const QDateTime& when) {
    if (channel_index < 0 || channel_index >= views_.size()) return;
    channel_view& cv = views_[channel_index];
    if (!cv.view) return;
    const tty_channel& c = channels_[channel_index];

    QString out = text;
    if (c.timestamps) {
        const QString stamp = c.date_in_timestamp ? when.toString(QStringLiteral("[yyyy-MM-dd HH:mm:ss.zzz] ")) : when.toString(QStringLiteral("[HH:mm:ss.zzz] "));

        QString stamped;
        stamped.reserve(out.size() + stamp.size() * 2);
        for (const QChar ch : out) {
            if (cv.at_line_start && ch != QLatin1Char('\n')) {
                stamped += stamp;
                cv.at_line_start = false;
            }
            stamped += ch;
            if (ch == QLatin1Char('\n')) cv.at_line_start = true;
        }
        out = stamped;
    } else {
        cv.at_line_start = out.endsWith(QLatin1Char('\n'));
    }

    auto cursor = cv.view->textCursor();
    cursor.movePosition(QTextCursor::End);
    cursor.insertText(out);
    cv.view->setTextCursor(cursor);
    cv.view->ensureCursorVisible();
}

void console_panel::clear() {
    for (auto& cv : views_) {
        if (cv.view) cv.view->clear();
        cv.at_line_start = true;
    }
}

QPlainTextEdit* console_panel::current_view() const {
    return tabs_ ? qobject_cast<QPlainTextEdit*>(tabs_->currentWidget()) : nullptr;
}

void console_panel::clear_current() {
    auto* v = current_view();
    if (!v) return;
    v->clear();
    for (int i = 0; i < views_.size(); ++i) {
        if (views_[i].view == v) { views_[i].at_line_start = true; break; }
    }
}

QString console_panel::current_channel_name() const {
    return tabs_ && tabs_->currentIndex() >= 0 ? tabs_->tabText(tabs_->currentIndex()) : QString();
}

QString console_panel::current_text() const {
    auto* v = current_view();
    return v ? v->toPlainText() : QString();
}

void console_panel::apply_font_from_prefs() {
    const QFont f(console_font_family(), console_font_point_size());
    for (auto& cv : views_) {
        if (cv.view) cv.view->setFont(f);
    }
}

void console_panel::focus_find() {
    if (!find_) {
        // resolved lazily so it follows whichever channel tab is in front
        find_ = new find_dialog([this] { return current_view(); }, this);
    }
    auto* v = current_view();
    find_->focus_input(v ? v->textCursor().selectedText() : QString());
}

void console_panel::install_context_menu(QPlainTextEdit* view) {
    view->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(view, &QPlainTextEdit::customContextMenuRequested, this,
            [this, view](const QPoint& pos) {
        // start from the built-in menu so copy / select-all / undo stay put
        QMenu* menu = view->createStandardContextMenu(view->mapToGlobal(pos));
        menu->addSeparator();

        // setShortcut rather than the addAction overload that takes one:
        // that overload is Qt 6.3, and the release bundle is built against
        // the oldest Qt we support.
        auto* find = menu->addAction(tr("&Find..."), this, &console_panel::focus_find);
        find->setShortcut(QKeySequence::Find);
        find->setIcon(QIcon(QStringLiteral(":/icons/folder_explore.bmp")));

        auto* clear_this = menu->addAction(tr("C&lear \"%1\"").arg(current_channel_name()), this, &console_panel::clear_current);
        clear_this->setIcon(QIcon(QStringLiteral(":/icons/delete.bmp")));

        menu->addAction(tr("Clear &All Channels"), this, &console_panel::clear);
        menu->addSeparator();
        menu->addAction(tr("&Save Channel to File..."), this, [this] { emit save_requested(); });
        menu->addAction(tr("&Console Settings..."), this, [this] { emit settings_requested(); });

        menu->setAttribute(Qt::WA_DeleteOnClose);
        menu->popup(view->mapToGlobal(pos));
    });
}

} // namespace opentm::tm_ui
