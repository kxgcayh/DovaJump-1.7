// logger.h
#pragma once

#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/msvc_sink.h>
#include <spdlog/spdlog.h>

namespace logger = spdlog;

static void SetupLog()
{
    // Получаем путь к папке с логами SKSE (Documents/My Games/Skyrim Special Edition/SKSE/)
    auto logsFolder = SKSE::log::log_directory();
    if (!logsFolder) {
        SKSE::stl::report_and_fail("Failed to find SKSE log directory");
    }

    // Формируем полный путь: .../SKSE/DovaJump.log
    auto logFilePath = *logsFolder / "DovaJump.log";

    // Создаём два "приёмника" (sinks):
    // 1. Файловый (запись на диск)
    // 2. Отладочный (вывод в окно Output Visual Studio)
    auto fileSink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(logFilePath.string(), true);
    auto debugSink = std::make_shared<spdlog::sinks::msvc_sink_mt>();

    // Собираем логгер с этими приёмниками
    auto log = std::make_shared<spdlog::logger>("global log", spdlog::sinks_init_list{ fileSink, debugSink });
    log->set_level(spdlog::level::info);
    log->flush_on(spdlog::level::info);

    // Регистрируем как глобальный (чтобы вызывать через logger::info())
    spdlog::set_default_logger(log);
    spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%n] [%l] %v");
}