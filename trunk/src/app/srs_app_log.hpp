/**
 * The MIT License (MIT)
 *
 * Copyright (c) 2013-2020 Winlin
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef SRS_APP_LOG_HPP
#define SRS_APP_LOG_HPP

#include <srs_core.hpp>

#include <string.h>
#include <string>

#include <srs_app_reload.hpp>
#include <srs_service_log.hpp>

class SrsAsyncFileWriter;

// For log TAGs.
#define TAG_MAIN "MAIN"
#define TAG_MAYBE "MAYBE"
#define TAG_DTLS_ALERT "DTLS_ALERT"
#define TAG_DTLS_HANG "DTLS_HANG"
#define TAG_RESOURCE_UNSUB "RESOURCE_UNSUB"
#define TAG_LARGE_TIMER "LARGE_TIMER"

// Use memory/disk cache and donot flush when write log.
// it's ok to use it without config, which will log to console, and default trace level.
// when you want to use different level, override this classs, set the protected _level.
class SrsFileLog : public ISrsLog, public ISrsReloadHandler
{
private:
    // Shared cache for each line of log.
    char* log_data;
    // Async file writer.
    SrsAsyncFileWriter* writer_;
private:
    // Defined in SrsLogLevel.
    SrsLogLevel level;
    // Whether log to file tank
    bool log_to_file_tank;
    // If log to file, the log filename.
    std::string filename_;
    // Whether use utc time.
    bool utc;
public:
    SrsFileLog();
    virtual ~SrsFileLog();
// Interface ISrsLog
public:
    virtual srs_error_t initialize();
    virtual void verbose(const char* tag, SrsContextId context_id, const char* fmt, ...);
    virtual void info(const char* tag, SrsContextId context_id, const char* fmt, ...);
    virtual void trace(const char* tag, SrsContextId context_id, const char* fmt, ...);
    virtual void warn(const char* tag, SrsContextId context_id, const char* fmt, ...);
    virtual void error(const char* tag, SrsContextId context_id, const char* fmt, ...);
private:
    virtual void write_log(char* str_log, int size, int level);
};

#endif

