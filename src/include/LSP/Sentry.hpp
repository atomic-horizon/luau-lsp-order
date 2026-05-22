#pragma once

#include <string>
#include <string_view>

namespace LspSentry
{

// Add a Sentry breadcrumb. Captured in the ring buffer (~100 entries) and attached
// to the next captured event (handled exception or crash). No-op when the binary
// was built without LSP_BUILD_WITH_SENTRY.
void addBreadcrumb(std::string_view category, const std::string& message);

// Same as above but with an extra `data` key/value pair. Use for context like
// arena pointers, type counts, module names.
void addBreadcrumb(std::string_view category, const std::string& message, std::string_view dataKey, const std::string& dataValue);

// Set a Sentry tag on the current scope. Tags appear on every captured event.
void setTag(std::string_view key, const std::string& value);

// Helper: format a pointer as "0x<hex>" for use as breadcrumb data.
std::string formatPointer(const void* p);

// Capture a handled exception event. Used when the LSP catches a normally-fatal
// condition (e.g. an SEH access violation around a third-party type-checker call)
// and recovers rather than letting the process die.
void captureHandledException(std::string_view exceptionType, const std::string& message);

} // namespace LspSentry
