#include "mge/core/log.h"

#include <cstdio>

#if defined(__ANDROID__)
#include <android/log.h>
#endif

namespace mge {

void logMessage(LogLevel level, const char* tag, const char* fmt, ...) {
    char buffer[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

#if defined(__ANDROID__)
    int prio = ANDROID_LOG_INFO;
    switch (level) {
        case LogLevel::Debug: prio = ANDROID_LOG_DEBUG; break;
        case LogLevel::Info: prio = ANDROID_LOG_INFO; break;
        case LogLevel::Warn: prio = ANDROID_LOG_WARN; break;
        case LogLevel::Error: prio = ANDROID_LOG_ERROR; break;
    }
    __android_log_print(prio, tag, "%s", buffer);
#else
    const char* levelName = "INFO";
    switch (level) {
        case LogLevel::Debug: levelName = "DEBUG"; break;
        case LogLevel::Info: levelName = "INFO"; break;
        case LogLevel::Warn: levelName = "WARN"; break;
        case LogLevel::Error: levelName = "ERROR"; break;
    }
    fprintf(level >= LogLevel::Warn ? stderr : stdout, "[%s] %s: %s\n", levelName, tag, buffer);
#endif
}

}  // namespace mge
