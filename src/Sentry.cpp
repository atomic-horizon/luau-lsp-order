#include "LSP/Sentry.hpp"

#include <cstdio>

#ifdef LSP_BUILD_WITH_SENTRY
// sentry.h pulls in <windows.h>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif
#define SENTRY_BUILD_STATIC 1
#include <sentry.h>
#endif

namespace LspSentry
{

#ifdef LSP_BUILD_WITH_SENTRY

void addBreadcrumb(std::string_view category, const std::string& message)
{
    sentry_value_t crumb = sentry_value_new_breadcrumb("info", message.c_str());
    sentry_value_set_by_key(crumb, "category", sentry_value_new_string(std::string(category).c_str()));
    sentry_add_breadcrumb(crumb);
}

void addBreadcrumb(std::string_view category, const std::string& message, std::string_view dataKey, const std::string& dataValue)
{
    sentry_value_t crumb = sentry_value_new_breadcrumb("info", message.c_str());
    sentry_value_set_by_key(crumb, "category", sentry_value_new_string(std::string(category).c_str()));

    sentry_value_t data = sentry_value_new_object();
    sentry_value_set_by_key(data, std::string(dataKey).c_str(), sentry_value_new_string(dataValue.c_str()));
    sentry_value_set_by_key(crumb, "data", data);

    sentry_add_breadcrumb(crumb);
}

void setTag(std::string_view key, const std::string& value)
{
    sentry_set_tag(std::string(key).c_str(), value.c_str());
}

void captureHandledException(std::string_view exceptionType, const std::string& message)
{
    sentry_value_t event = sentry_value_new_event();
    sentry_value_t exc = sentry_value_new_exception(std::string(exceptionType).c_str(), message.c_str());
    sentry_value_set_stacktrace(exc, NULL, 0);
    sentry_value_set_by_key(exc, "handled", sentry_value_new_bool(true));
    sentry_event_add_exception(event, exc);
    sentry_capture_event(event);
}

#else

void addBreadcrumb(std::string_view, const std::string&) {}
void addBreadcrumb(std::string_view, const std::string&, std::string_view, const std::string&) {}
void setTag(std::string_view, const std::string&) {}
void captureHandledException(std::string_view, const std::string&) {}

#endif

std::string formatPointer(const void* p)
{
    char buf[2 + 2 * sizeof(void*) + 1];
    std::snprintf(buf, sizeof(buf), "0x%llx", static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(p)));
    return std::string(buf);
}

} // namespace LspSentry
