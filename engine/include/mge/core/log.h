#pragma once

#include <cstdarg>

namespace mge {

enum class LogLevel { Debug, Info, Warn, Error };

// Leveled, tagged logging. Goes to logcat on Android, stdout elsewhere.
void logMessage(LogLevel level, const char* tag, const char* fmt, ...);

}  // namespace mge

#define MGE_LOGD(tag, ...) ::mge::logMessage(::mge::LogLevel::Debug, tag, __VA_ARGS__)
#define MGE_LOGI(tag, ...) ::mge::logMessage(::mge::LogLevel::Info, tag, __VA_ARGS__)
#define MGE_LOGW(tag, ...) ::mge::logMessage(::mge::LogLevel::Warn, tag, __VA_ARGS__)
#define MGE_LOGE(tag, ...) ::mge::logMessage(::mge::LogLevel::Error, tag, __VA_ARGS__)
