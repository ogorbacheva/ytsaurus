// Copyright 2017 The Abseil Authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "y_absl/base/internal/raw_logging.h"

#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <util/generic/string.h>

#include "y_absl/base/attributes.h"
#include "y_absl/base/config.h"
#include "y_absl/base/internal/atomic_hook.h"
#include "y_absl/base/internal/errno_saver.h"
#include "y_absl/base/log_severity.h"

// We know how to perform low-level writes to stderr in POSIX and Windows.  For
// these platforms, we define the token Y_ABSL_LOW_LEVEL_WRITE_SUPPORTED.
// Much of raw_logging.cc becomes a no-op when we can't output messages,
// although a FATAL Y_ABSL_RAW_LOG message will still abort the process.

// Y_ABSL_HAVE_POSIX_WRITE is defined when the platform provides posix write()
// (as from unistd.h)
//
// This preprocessor token is also defined in raw_io.cc.  If you need to copy
// this, consider moving both to config.h instead.
#if defined(__linux__) || defined(__APPLE__) || defined(__FreeBSD__) || \
    defined(__Fuchsia__) || defined(__native_client__) ||               \
    defined(__OpenBSD__) || defined(__EMSCRIPTEN__) || defined(__ASYLO__)

#include <unistd.h>

#define Y_ABSL_HAVE_POSIX_WRITE 1
#define Y_ABSL_LOW_LEVEL_WRITE_SUPPORTED 1
#else
#undef Y_ABSL_HAVE_POSIX_WRITE
#endif

// Y_ABSL_HAVE_SYSCALL_WRITE is defined when the platform provides the syscall
//   syscall(SYS_write, /*int*/ fd, /*char* */ buf, /*size_t*/ len);
// for low level operations that want to avoid libc.
#if (defined(__linux__) || defined(__FreeBSD__) || defined(__OpenBSD__)) && \
    !defined(__ANDROID__)
#include <sys/syscall.h>
#define Y_ABSL_HAVE_SYSCALL_WRITE 1
#define Y_ABSL_LOW_LEVEL_WRITE_SUPPORTED 1
#else
#undef Y_ABSL_HAVE_SYSCALL_WRITE
#endif

#ifdef _WIN32
#include <io.h>

#define Y_ABSL_HAVE_RAW_IO 1
#define Y_ABSL_LOW_LEVEL_WRITE_SUPPORTED 1
#else
#undef Y_ABSL_HAVE_RAW_IO
#endif

namespace y_absl {
Y_ABSL_NAMESPACE_BEGIN
namespace raw_logging_internal {
namespace {

// TODO(gfalcon): We want raw-logging to work on as many platforms as possible.
// Explicitly `#error` out when not `Y_ABSL_LOW_LEVEL_WRITE_SUPPORTED`, except for
// a selected set of platforms for which we expect not to be able to raw log.

#ifdef Y_ABSL_LOW_LEVEL_WRITE_SUPPORTED
constexpr char kTruncated[] = " ... (message truncated)\n";

// sprintf the format to the buffer, adjusting *buf and *size to reflect the
// consumed bytes, and return whether the message fit without truncation.  If
// truncation occurred, if possible leave room in the buffer for the message
// kTruncated[].
bool VADoRawLog(char** buf, int* size, const char* format, va_list ap)
    Y_ABSL_PRINTF_ATTRIBUTE(3, 0);
bool VADoRawLog(char** buf, int* size, const char* format, va_list ap) {
  int n = vsnprintf(*buf, *size, format, ap);
  bool result = true;
  if (n < 0 || n > *size) {
    result = false;
    if (static_cast<size_t>(*size) > sizeof(kTruncated)) {
      n = *size - sizeof(kTruncated);  // room for truncation message
    } else {
      n = 0;  // no room for truncation message
    }
  }
  *size -= n;
  *buf += n;
  return result;
}
#endif  // Y_ABSL_LOW_LEVEL_WRITE_SUPPORTED

constexpr int kLogBufSize = 3000;

// CAVEAT: vsnprintf called from *DoRawLog below has some (exotic) code paths
// that invoke malloc() and getenv() that might acquire some locks.

// Helper for RawLog below.
// *DoRawLog writes to *buf of *size and move them past the written portion.
// It returns true iff there was no overflow or error.
bool DoRawLog(char** buf, int* size, const char* format, ...)
    Y_ABSL_PRINTF_ATTRIBUTE(3, 4);
bool DoRawLog(char** buf, int* size, const char* format, ...) {
  va_list ap;
  va_start(ap, format);
  int n = vsnprintf(*buf, *size, format, ap);
  va_end(ap);
  if (n < 0 || n > *size) return false;
  *size -= n;
  *buf += n;
  return true;
}

bool DefaultLogFilterAndPrefix(y_absl::LogSeverity, const char* file, int line,
                               char** buf, int* buf_size) {
  DoRawLog(buf, buf_size, "[%s : %d] RAW: ", file, line);
  return true;
}

Y_ABSL_INTERNAL_ATOMIC_HOOK_ATTRIBUTES
y_absl::base_internal::AtomicHook<LogFilterAndPrefixHook>
    log_filter_and_prefix_hook(DefaultLogFilterAndPrefix);
Y_ABSL_INTERNAL_ATOMIC_HOOK_ATTRIBUTES
y_absl::base_internal::AtomicHook<AbortHook> abort_hook;

void RawLogVA(y_absl::LogSeverity severity, const char* file, int line,
              const char* format, va_list ap) Y_ABSL_PRINTF_ATTRIBUTE(4, 0);
void RawLogVA(y_absl::LogSeverity severity, const char* file, int line,
              const char* format, va_list ap) {
  char buffer[kLogBufSize];
  char* buf = buffer;
  int size = sizeof(buffer);
#ifdef Y_ABSL_LOW_LEVEL_WRITE_SUPPORTED
  bool enabled = true;
#else
  bool enabled = false;
#endif

#ifdef Y_ABSL_MIN_LOG_LEVEL
  if (severity < static_cast<y_absl::LogSeverity>(Y_ABSL_MIN_LOG_LEVEL) &&
      severity < y_absl::LogSeverity::kFatal) {
    enabled = false;
  }
#endif

  enabled = log_filter_and_prefix_hook(severity, file, line, &buf, &size);
  const char* const prefix_end = buf;

#ifdef Y_ABSL_LOW_LEVEL_WRITE_SUPPORTED
  if (enabled) {
    bool no_chop = VADoRawLog(&buf, &size, format, ap);
    if (no_chop) {
      DoRawLog(&buf, &size, "\n");
    } else {
      DoRawLog(&buf, &size, "%s", kTruncated);
    }
    AsyncSignalSafeWriteToStderr(buffer, strlen(buffer));
  }
#else
  static_cast<void>(format);
  static_cast<void>(ap);
  static_cast<void>(enabled);
#endif

  // Abort the process after logging a FATAL message, even if the output itself
  // was suppressed.
  if (severity == y_absl::LogSeverity::kFatal) {
    abort_hook(file, line, buffer, prefix_end, buffer + kLogBufSize);
    abort();
  }
}

// Non-formatting version of RawLog().
//
// TODO(gfalcon): When string_view no longer depends on base, change this
// interface to take its message as a string_view instead.
void DefaultInternalLog(y_absl::LogSeverity severity, const char* file, int line,
                        const TString& message) {
  RawLog(severity, file, line, "%.*s", static_cast<int>(message.size()),
         message.data());
}

}  // namespace

void AsyncSignalSafeWriteToStderr(const char* s, size_t len) {
  y_absl::base_internal::ErrnoSaver errno_saver;
#if defined(Y_ABSL_HAVE_SYSCALL_WRITE)
  // We prefer calling write via `syscall` to minimize the risk of libc doing
  // something "helpful".
  syscall(SYS_write, STDERR_FILENO, s, len);
#elif defined(Y_ABSL_HAVE_POSIX_WRITE)
  write(STDERR_FILENO, s, len);
#elif defined(Y_ABSL_HAVE_RAW_IO)
  _write(/* stderr */ 2, s, len);
#else
  // stderr logging unsupported on this platform
  (void) s;
  (void) len;
#endif
}

void RawLog(y_absl::LogSeverity severity, const char* file, int line,
            const char* format, ...) {
  va_list ap;
  va_start(ap, format);
  RawLogVA(severity, file, line, format, ap);
  va_end(ap);
}

bool RawLoggingFullySupported() {
#ifdef Y_ABSL_LOW_LEVEL_WRITE_SUPPORTED
  return true;
#else  // !Y_ABSL_LOW_LEVEL_WRITE_SUPPORTED
  return false;
#endif  // !Y_ABSL_LOW_LEVEL_WRITE_SUPPORTED
}

Y_ABSL_INTERNAL_ATOMIC_HOOK_ATTRIBUTES Y_ABSL_DLL
    y_absl::base_internal::AtomicHook<InternalLogFunction>
        internal_log_function(DefaultInternalLog);

void RegisterLogFilterAndPrefixHook(LogFilterAndPrefixHook func) {
  log_filter_and_prefix_hook.Store(func);
}

void RegisterAbortHook(AbortHook func) { abort_hook.Store(func); }

void RegisterInternalLogFunction(InternalLogFunction func) {
  internal_log_function.Store(func);
}

}  // namespace raw_logging_internal
Y_ABSL_NAMESPACE_END
}  // namespace y_absl
