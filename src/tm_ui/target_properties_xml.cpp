#include "target_properties_xml.h"

#include <QXmlStreamReader>
#include <QXmlStreamWriter>

namespace opentm::tm_ui {

namespace {

QString hex32(quint32 v) {
    return QStringLiteral("%1").arg(v, 8, 16, QChar('0')).toUpper();
}

QString hex64(quint64 v) {
    return QStringLiteral("%1").arg(v, 16, 16, QChar('0')).toUpper();
}

QString yn(bool v) { return v ? QStringLiteral("y") : QStringLiteral("n"); }

bool parse_yn(const QStringView s, bool fallback) {
    if (s.isEmpty()) return fallback;
    const QChar c = s.at(0).toLower();
    if (c == QLatin1Char('y')) return true;
    if (c == QLatin1Char('n')) return false;
    return fallback;
}

quint64 parse_num(const QStringView s, quint64 fallback) {
    if (s.isEmpty()) return fallback;
    const QString t = s.toString().trimmed();
    bool ok = false;
    quint64 v = t.startsWith(QLatin1String("0x"), Qt::CaseInsensitive) ? t.mid(2).toULongLong(&ok, 16) : t.toULongLong(&ok, 16);
    if (!ok) v = t.toULongLong(&ok, 10);
    return ok ? v : fallback;
}

struct attrs {
    const QXmlStreamAttributes& a;

    void str(const char* name, QString& out) const {
        if (a.hasAttribute(QLatin1String(name)))
            out = a.value(QLatin1String(name)).toString();
    }
    void flag(const char* name, bool& out) const {
        if (a.hasAttribute(QLatin1String(name)))
            out = parse_yn(a.value(QLatin1String(name)), out);
    }
    template <typename T>
    void num(const char* name, T& out) const {
        if (a.hasAttribute(QLatin1String(name)))
            out = static_cast<T>(parse_num(a.value(QLatin1String(name)), static_cast<quint64>(out)));
    }
};

} // namespace

QByteArray target_properties_to_xml(const target_record& r) {
    QByteArray out;
    QXmlStreamWriter w(&out);
    w.setAutoFormatting(true);
    w.setAutoFormattingIndent(1);
    w.writeStartDocument(QStringLiteral("1.0"));
    w.writeStartElement(QStringLiteral("Target"));
    w.writeStartElement(QStringLiteral("ServerSettings"));
    w.writeAttribute(QStringLiteral("HomeDir"), r.home_dir);
    w.writeAttribute(QStringLiteral("CaseSensitiveFileServing0"), yn(r.force_case_sensitive));
    w.writeAttribute(QStringLiteral("EnableEnvVarExpansion"), yn(r.env_var_expansion));
    w.writeAttribute(QStringLiteral("MaxLoadRetryTime"), hex32(static_cast<quint32>(r.timeouts.load_ms)));
    w.writeAttribute(QStringLiteral("DefaultELFLoadPriority"), hex32(r.load.priority));
    w.writeAttribute(QStringLiteral("DefaultELFStackSize"), hex32(r.load.stack_size));
    w.writeAttribute(QStringLiteral("DisplayResetSettings"), yn(r.display_reset_settings));
    w.writeEndElement();
    w.writeStartElement(QStringLiteral("UiSettings"));
    w.writeAttribute(QStringLiteral("FileServingHistoryLength"), hex32(static_cast<quint32>(r.file_serving_log_size)));
    w.writeAttribute(QStringLiteral("FileTraceMaxEventHistory"), hex32(static_cast<quint32>(r.file_trace_log_size)));
    w.writeStartElement(QStringLiteral("Options"));
    w.writeAttribute(QStringLiteral("LastElfArgs"), r.load.cmdline);
    w.writeAttribute(QStringLiteral("FSPath"), r.file_server_dir);
    w.writeAttribute(QStringLiteral("HomePath"), r.home_dir);
    w.writeAttribute(QStringLiteral("EnableDebugging"), yn(r.load.enable_debug_module));
    w.writeAttribute(QStringLiteral("DisablePPUDebugging"), yn(r.load.disable_ppu_debug));
    w.writeAttribute(QStringLiteral("DisableSPUDebugging"), yn(r.load.disable_spu_debug));
    w.writeEndElement();
    w.writeStartElement(QStringLiteral("ResetParameters"));
    w.writeAttribute(QStringLiteral("ResetType"), hex32(static_cast<quint32>(r.reset_mode)));
    w.writeAttribute(QStringLiteral("BootValue"),  hex64(r.reset_boot_value));
    w.writeAttribute(QStringLiteral("BootMask"),   hex64(r.reset_boot_mask));
    w.writeAttribute(QStringLiteral("SystemValue"), hex64(r.reset_system_value));
    w.writeAttribute(QStringLiteral("SystemMask"),  hex64(r.reset_system_mask));
    w.writeEndElement();
    w.writeEndElement();
    w.writeStartElement(QStringLiteral("OpenTM"));
    w.writeAttribute(QStringLiteral("Name"), r.name);
    {
        const auto sv = opentm::tm_core::target_type_string(r.type);
        w.writeAttribute(QStringLiteral("Type"), QString::fromLatin1(sv.data(), static_cast<int>(sv.size())));
    }
    w.writeAttribute(QStringLiteral("Host"), r.host);
    w.writeAttribute(QStringLiteral("Port"), hex32(r.port));
    w.writeAttribute(QStringLiteral("Mac"), r.mac);
    w.writeAttribute(QStringLiteral("EventsToLog"), r.events_to_log);
    w.writeAttribute(QStringLiteral("ConsoleCacheKB"), hex32(static_cast<quint32>(r.console_cache_kb)));

    w.writeStartElement(QStringLiteral("Timeouts"));
    w.writeAttribute(QStringLiteral("Default"),   hex32(static_cast<quint32>(r.timeouts.default_ms)));
    w.writeAttribute(QStringLiteral("Reset"),     hex32(static_cast<quint32>(r.timeouts.reset_ms)));
    w.writeAttribute(QStringLiteral("Connect"),   hex32(static_cast<quint32>(r.timeouts.connect_ms)));
    w.writeAttribute(QStringLiteral("Load"),      hex32(static_cast<quint32>(r.timeouts.load_ms)));
    w.writeAttribute(QStringLiteral("Status"),    hex32(static_cast<quint32>(r.timeouts.status_ms)));
    w.writeAttribute(QStringLiteral("Reconnect"), hex32(static_cast<quint32>(r.timeouts.reconnect_ms)));
    w.writeAttribute(QStringLiteral("GamePort"),  hex32(static_cast<quint32>(r.timeouts.game_port_ms)));
    w.writeAttribute(QStringLiteral("GameExit"),  hex32(static_cast<quint32>(r.timeouts.game_exit_ms)));
    w.writeEndElement();

    w.writeStartElement(QStringLiteral("LoadOptions"));
    w.writeAttribute(QStringLiteral("UseElfPriority"),   yn(r.load.use_elf_priority));
    w.writeAttribute(QStringLiteral("UseElfStack"),      yn(r.load.use_elf_stack));
    w.writeAttribute(QStringLiteral("WaitForBdvd"),      yn(r.load.wait_for_bdvd));
    w.writeAttribute(QStringLiteral("ResetTarget"),      yn(r.load.reset_target));
    w.writeAttribute(QStringLiteral("ClearStreams"),     yn(r.load.clear_streams));
    w.writeAttribute(QStringLiteral("EnableExtraOptions"), yn(r.load.enable_extra_options));
    w.writeAttribute(QStringLiteral("Lv2ExceptionHandler"), yn(r.load.lv2_exception_handler));
    w.writeAttribute(QStringLiteral("RemotePlay"),       yn(r.load.remote_play));
    w.writeAttribute(QStringLiteral("GcmDebug"),         yn(r.load.gcm_debug));
    w.writeAttribute(QStringLiteral("LoadLibprof"),      yn(r.load.load_libprof));
    w.writeAttribute(QStringLiteral("CoreDump"),         yn(r.load.core_dump));
    w.writeAttribute(QStringLiteral("RemotePlayAvc"),    yn(r.load.remote_play_avc));
    w.writeAttribute(QStringLiteral("SmartImageCapture"), yn(r.load.smart_image_capture));
    w.writeAttribute(QStringLiteral("MemoryAccessTrap"), yn(r.load.memory_access_trap));
    w.writeAttribute(QStringLiteral("GameAttribute"),    hex32(r.load.game_attribute));
    w.writeAttribute(QStringLiteral("PatchBoot"),        yn(r.load.patch_boot));
    w.writeAttribute(QStringLiteral("CoreDumpLocation"), hex64(r.load.core_dump_location));
    w.writeAttribute(QStringLiteral("RsxProfilingTool"), yn(r.load.rsx_profiling_tool));
    w.writeAttribute(QStringLiteral("HighMemoryFootprint"), yn(r.load.high_memory_footprint));
    w.writeAttribute(QStringLiteral("GcmCaptureMode"),   yn(r.load.gcm_capture_mode));
    w.writeEndElement();

    w.writeEndElement(); // OpenTM
    w.writeEndElement(); // Target
    w.writeEndDocument();
    return out;
}

bool target_properties_from_xml(const QByteArray& xml, target_record& out, QString* error) {
    
    QXmlStreamReader r(xml);
    bool saw_target = false;

    while (!r.atEnd()) {
        if (r.readNext() != QXmlStreamReader::StartElement) continue;
        const auto name = r.name();
        const attrs at{r.attributes()};

        if (name == QLatin1String("Target")) {
            saw_target = true;

        } else if (name == QLatin1String("ServerSettings")) {
            at.str("HomeDir", out.home_dir);
            at.flag("CaseSensitiveFileServing0", out.force_case_sensitive);
            at.flag("EnableEnvVarExpansion", out.env_var_expansion);
            at.num("MaxLoadRetryTime", out.timeouts.load_ms);
            at.num("DefaultELFLoadPriority", out.load.priority);
            at.num("DefaultELFStackSize", out.load.stack_size);
            at.flag("DisplayResetSettings", out.display_reset_settings);

        } else if (name == QLatin1String("UiSettings")) {
            at.num("FileServingHistoryLength", out.file_serving_log_size);
            at.num("FileTraceMaxEventHistory", out.file_trace_log_size);

        } else if (name == QLatin1String("Options")) {
            at.str("LastElfArgs", out.load.cmdline);
            at.str("FSPath", out.file_server_dir);
            at.str("HomePath", out.home_dir);
            at.flag("EnableDebugging", out.load.enable_debug_module);
            at.flag("DisablePPUDebugging", out.load.disable_ppu_debug);
            at.flag("DisableSPUDebugging", out.load.disable_spu_debug);

        } else if (name == QLatin1String("ResetParameters")) {
            at.num("ResetType", out.reset_mode);
            at.num("BootValue", out.reset_boot_value);
            at.num("BootMask", out.reset_boot_mask);
            at.num("SystemValue", out.reset_system_value);
            at.num("SystemMask", out.reset_system_mask);

        } else if (name == QLatin1String("OpenTM")) {
            at.str("Name", out.name);
            at.str("Host", out.host);
            at.str("Mac", out.mac);
            at.str("EventsToLog", out.events_to_log);
            at.num("Port", out.port);
            at.num("ConsoleCacheKB", out.console_cache_kb);
            if (at.a.hasAttribute(QLatin1String("Type"))) {
                const auto s = at.a.value(QLatin1String("Type")).toString().toStdString();
                if (const auto t = opentm::tm_core::target_type_from_string(s)) {
                    out.type = *t;
                }
            }

        } else if (name == QLatin1String("Timeouts")) {
            at.num("Default",   out.timeouts.default_ms);
            at.num("Reset",     out.timeouts.reset_ms);
            at.num("Connect",   out.timeouts.connect_ms);
            at.num("Load",      out.timeouts.load_ms);
            at.num("Status",    out.timeouts.status_ms);
            at.num("Reconnect", out.timeouts.reconnect_ms);
            at.num("GamePort",  out.timeouts.game_port_ms);
            at.num("GameExit",  out.timeouts.game_exit_ms);

        } else if (name == QLatin1String("LoadOptions")) {
            at.flag("UseElfPriority",      out.load.use_elf_priority);
            at.flag("UseElfStack",         out.load.use_elf_stack);
            at.flag("WaitForBdvd",         out.load.wait_for_bdvd);
            at.flag("ResetTarget",         out.load.reset_target);
            at.flag("ClearStreams",        out.load.clear_streams);
            at.flag("EnableExtraOptions",  out.load.enable_extra_options);
            at.flag("Lv2ExceptionHandler", out.load.lv2_exception_handler);
            at.flag("RemotePlay",          out.load.remote_play);
            at.flag("GcmDebug",            out.load.gcm_debug);
            at.flag("LoadLibprof",         out.load.load_libprof);
            at.flag("CoreDump",            out.load.core_dump);
            at.flag("RemotePlayAvc",       out.load.remote_play_avc);
            at.flag("SmartImageCapture",   out.load.smart_image_capture);
            at.flag("MemoryAccessTrap",    out.load.memory_access_trap);
            at.num ("GameAttribute",       out.load.game_attribute);
            at.flag("PatchBoot",           out.load.patch_boot);
            at.num ("CoreDumpLocation",    out.load.core_dump_location);
            at.flag("RsxProfilingTool",    out.load.rsx_profiling_tool);
            at.flag("HighMemoryFootprint", out.load.high_memory_footprint);
            at.flag("GcmCaptureMode",      out.load.gcm_capture_mode);
        }
    }

    if (r.hasError()) {
        if (error) *error = r.errorString();
        return false;
    }
    if (!saw_target) {
        if (error) *error = QStringLiteral("no <Target> element - not a ""Target Manager properties file");
        return false;
    }
    return true;
}

} // namespace opentm::tm_ui