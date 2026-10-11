// Host-path -> on-target DECI3 path mapping, shared by the executable load and the
// package install. Both must build the same wire path, or a loaded/installed file is
// requested at an address the console cannot open (ENOENT). Keeping the rule in one
// place is what stops the two from drifting: install once sent the full host path,
// which produced a malformed /app_home/<full path> the console could not open.
#pragma once

#include <QFileInfo>
#include <QString>

namespace opentm::tm_session {

// A kit-side path (already a console path) is used verbatim; any host path is served as
// /app_home/<basename> (the file must sit in the file-server directory). Forward slashes
// only -- never native separators, which on Windows would inject backslashes into the
// console path.
inline QString host_to_app_home_path(const QString& path) {
    if (path.startsWith(QStringLiteral("/app_home/"))
        || path.startsWith(QStringLiteral("/dev_"))
        || path.startsWith(QStringLiteral("/host_root/"))) {
        return path;
    }
    return QStringLiteral("/app_home/") + QFileInfo(path).fileName();
}

} // namespace opentm::tm_session
