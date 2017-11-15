
/*
    pbrt source code is Copyright(c) 1998-2016
                        Matt Pharr, Greg Humphreys, and Wenzel Jakob.

    This file is part of pbrt.

    Redistribution and use in source and binary forms, with or without
    modification, are permitted provided that the following conditions are
    met:

    - Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.

    - Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.

    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
    IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
    TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
    PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
    HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
    SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
    LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
    DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
    THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
    (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
    OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

 */

// core/error.cpp*
#include <pbrt/core/error.h>

#include <pbrt/core/options.h>
#include <pbrt/core/parser.h>
#include <pbrt/util/parallel.h>
#include <pbrt/util/progressreporter.h>
#include <pbrt/util/stringprint.h>

#include <mutex>
#include <stdarg.h>

namespace pbrt {

static const char *findWordEnd(const char *buf) {
    while (*buf != '\0' && !isspace(*buf)) ++buf;
    return buf;
}

// Error Reporting Functions
template <typename... Args>
static std::string StringVaprintf(const std::string &fmt, va_list args) {
    // Figure out how much space we need to allocate; add an extra
    // character for '\0'.
    va_list argsCopy;
    va_copy(argsCopy, args);
    size_t size = vsnprintf(nullptr, 0, fmt.c_str(), args) + 1;
    std::string str;
    str.resize(size);
    vsnprintf(&str[0], size, fmt.c_str(), argsCopy);
    str.pop_back();  // remove trailing NUL
    return str;
}

static void processError(const char *format, va_list args,
                         const char *errorType) {
    // Build up an entire formatted error string and print it all at once;
    // this way, if multiple threads are printing messages at once, they
    // don't get jumbled up...
    std::string errorString;

    // Print line and position in input file, if available
    if (parse::currentLineNumber != 0) {
        errorString += parse::currentFilename;
        errorString += StringPrintf("(%d): ", parse::currentLineNumber);
    }

    errorString += StringVaprintf(format, args);

    // Print the error message (but not more than one time).
    static std::string lastError;
    static std::mutex mutex;
    std::lock_guard<std::mutex> lock(mutex);
    if (errorString != lastError) {
        if (!strcmp(errorType, "Warning"))
            LOG(WARNING) << errorString;
        else
            LOG(ERROR) << errorString;
        lastError = errorString;
    }
}

void Warning(const char *format, ...) {
    if (PbrtOptions.quiet) return;
    va_list args;
    va_start(args, format);
    processError(format, args, "Warning");
    va_end(args);
}

void Error(const char *format, ...) {
    va_list args;
    va_start(args, format);
    processError(format, args, "Error");
    va_end(args);
}

void ErrorExit(const char *format, ...) {
    va_list args;
    va_start(args, format);
    processError(format, args, "Error");
    va_end(args);
    // This is annoying, but gives a cleaner exit.
    ParallelCleanup();
    exit(1);
}

}  // namespace pbrt
