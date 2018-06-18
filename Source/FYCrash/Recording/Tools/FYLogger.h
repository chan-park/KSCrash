//
//  FYLogger.h
//
//  Created by Karl Stenerud on 11-06-25.
//
//  Copyright (c) 2011 Karl Stenerud. All rights reserved.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall remain in place
// in this source code.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//


/**
 * FYLogger
 * ========
 *
 * Prints log entries to the console consisting of:
 * - Level (Error, Warn, Info, Debug, Trace)
 * - File
 * - Line
 * - Function
 * - Message
 *
 * Allows setting the minimum logging level in the preprocessor.
 *
 * Works in C or Objective-C contexts, with or without ARC, using CLANG or GCC.
 *
 *
 * =====
 * USAGE
 * =====
 *
 * Set the log level in your "Preprocessor Macros" build setting. You may choose
 * TRACE, DEBUG, INFO, WARN, ERROR. If nothing is set, it defaults to ERROR.
 *
 * Example: FYLogger_Level=WARN
 *
 * Anything below the level specified for FYLogger_Level will not be compiled
 * or printed.
 * 
 *
 * Next, include the header file:
 *
 * #include "FYLogger.h"
 *
 *
 * Next, call the logger functions from your code (using objective-c strings
 * in objective-C files and regular strings in regular C files):
 *
 * Code:
 *    FYLOG_ERROR(@"Some error message");
 *
 * Prints:
 *    2011-07-16 05:41:01.379 TestApp[4439:f803] ERROR: SomeClass.m (21): -[SomeFunction]: Some error message 
 *
 * Code:
 *    FYLOG_INFO(@"Info about %@", someObject);
 *
 * Prints:
 *    2011-07-16 05:44:05.239 TestApp[4473:f803] INFO : SomeClass.m (20): -[SomeFunction]: Info about <NSObject: 0xb622840>
 *
 *
 * The "BASIC" versions of the macros behave exactly like NSLog() or printf(),
 * except they respect the FYLogger_Level setting:
 *
 * Code:
 *    FYLOGBASIC_ERROR(@"A basic log entry");
 *
 * Prints:
 *    2011-07-16 05:44:05.916 TestApp[4473:f803] A basic log entry
 *
 *
 * NOTE: In C files, use "" instead of @"" in the format field. Logging calls
 *       in C files do not print the NSLog preamble:
 *
 * Objective-C version:
 *    FYLOG_ERROR(@"Some error message");
 *
 *    2011-07-16 05:41:01.379 TestApp[4439:f803] ERROR: SomeClass.m (21): -[SomeFunction]: Some error message
 *
 * C version:
 *    FYLOG_ERROR("Some error message");
 *
 *    ERROR: SomeClass.c (21): SomeFunction(): Some error message
 *
 *
 * =============
 * LOCAL LOGGING
 * =============
 *
 * You can control logging messages at the local file level using the
 * "FYLogger_LocalLevel" define. Note that it must be defined BEFORE
 * including FYLogger.h
 *
 * The FYLOG_XX() and FYLOGBASIC_XX() macros will print out based on the LOWER
 * of FYLogger_Level and FYLogger_LocalLevel, so if FYLogger_Level is DEBUG
 * and FYLogger_LocalLevel is TRACE, it will print all the way down to the trace
 * level for the local file where FYLogger_LocalLevel was defined, and to the
 * debug level everywhere else.
 *
 * Example:
 *
 * // FYLogger_LocalLevel, if defined, MUST come BEFORE including FYLogger.h
 * #define FYLogger_LocalLevel TRACE
 * #import "FYLogger.h"
 *
 *
 * ===============
 * IMPORTANT NOTES
 * ===============
 *
 * The C logger changes its behavior depending on the value of the preprocessor
 * define FYLogger_CBufferSize.
 *
 * If FYLogger_CBufferSize is > 0, the C logger will behave in an async-safe
 * manner, calling write() instead of printf(). Any log messages that exceed the
 * length specified by FYLogger_CBufferSize will be truncated.
 *
 * If FYLogger_CBufferSize == 0, the C logger will use printf(), and there will
 * be no limit on the log message length.
 *
 * FYLogger_CBufferSize can only be set as a preprocessor define, and will
 * default to 1024 if not specified during compilation.
 */


// ============================================================================
#pragma mark - (internal) -
// ============================================================================


#ifndef HDR_FYLogger_h
#define HDR_FYLogger_h

#ifdef __cplusplus
extern "C" {
#endif


#include <stdbool.h>


#ifdef __OBJC__

#import <CoreFoundation/CoreFoundation.h>

void i_fylog_logObjC(const char* level,
                     const char* file,
                     int line,
                     const char* function,
                     CFStringRef fmt, ...);

void i_fylog_logObjCBasic(CFStringRef fmt, ...);

#define i_FYLOG_FULL(LEVEL,FILE,LINE,FUNCTION,FMT,...) i_fylog_logObjC(LEVEL,FILE,LINE,FUNCTION,(__bridge CFStringRef)FMT,##__VA_ARGS__)
#define i_FYLOG_BASIC(FMT, ...) i_fylog_logObjCBasic((__bridge CFStringRef)FMT,##__VA_ARGS__)

#else // __OBJC__

void i_fylog_logC(const char* level,
                  const char* file,
                  int line,
                  const char* function,
                  const char* fmt, ...);

void i_fylog_logCBasic(const char* fmt, ...);

#define i_FYLOG_FULL i_fylog_logC
#define i_FYLOG_BASIC i_fylog_logCBasic

#endif // __OBJC__


/* Back up any existing defines by the same name */
#ifdef FY_NONE
    #define FYLOG_BAK_NONE FY_NONE
    #undef FY_NONE
#endif
#ifdef ERROR
    #define FYLOG_BAK_ERROR ERROR
    #undef ERROR
#endif
#ifdef WARN
    #define FYLOG_BAK_WARN WARN
    #undef WARN
#endif
#ifdef INFO
    #define FYLOG_BAK_INFO INFO
    #undef INFO
#endif
#ifdef DEBUG
    #define FYLOG_BAK_DEBUG DEBUG
    #undef DEBUG
#endif
#ifdef TRACE
    #define FYLOG_BAK_TRACE TRACE
    #undef TRACE
#endif


#define FYLogger_Level_None   0
#define FYLogger_Level_Error 10
#define FYLogger_Level_Warn  20
#define FYLogger_Level_Info  30
#define FYLogger_Level_Debug 40
#define FYLogger_Level_Trace 50

#define FY_NONE  FYLogger_Level_None
#define ERROR FYLogger_Level_Error
#define WARN  FYLogger_Level_Warn
#define INFO  FYLogger_Level_Info
#define DEBUG FYLogger_Level_Debug
#define TRACE FYLogger_Level_Trace


#ifndef FYLogger_Level
    #define FYLogger_Level FYLogger_Level_Error
#endif

#ifndef FYLogger_LocalLevel
    #define FYLogger_LocalLevel FYLogger_Level_None
#endif

#define a_FYLOG_FULL(LEVEL, FMT, ...) \
    i_FYLOG_FULL(LEVEL, \
                 __FILE__, \
                 __LINE__, \
                 __PRETTY_FUNCTION__, \
                 FMT, \
                 ##__VA_ARGS__)



// ============================================================================
#pragma mark - API -
// ============================================================================

/** Set the filename to log to.
 *
 * @param filename The file to write to (NULL = write to stdout).
 *
 * @param overwrite If true, overwrite the log file.
 */
bool fylog_setLogFilename(const char* filename, bool overwrite);

/** Clear the log file. */
bool fylog_clearLogFile(void);

/** Tests if the logger would print at the specified level.
 *
 * @param LEVEL The level to test for. One of:
 *            FYLogger_Level_Error,
 *            FYLogger_Level_Warn,
 *            FYLogger_Level_Info,
 *            FYLogger_Level_Debug,
 *            FYLogger_Level_Trace,
 *
 * @return TRUE if the logger would print at the specified level.
 */
#define FYLOG_PRINTS_AT_LEVEL(LEVEL) \
    (FYLogger_Level >= LEVEL || FYLogger_LocalLevel >= LEVEL)

/** Log a message regardless of the log settings.
 * Normal version prints out full context. Basic version prints directly.
 *
 * @param FMT The format specifier, followed by its arguments.
 */
#define FYLOG_ALWAYS(FMT, ...) a_FYLOG_FULL("FORCE", FMT, ##__VA_ARGS__)
#define FYLOGBASIC_ALWAYS(FMT, ...) i_FYLOG_BASIC(FMT, ##__VA_ARGS__)


/** Log an error.
 * Normal version prints out full context. Basic version prints directly.
 *
 * @param FMT The format specifier, followed by its arguments.
 */
#if FYLOG_PRINTS_AT_LEVEL(FYLogger_Level_Error)
    #define FYLOG_ERROR(FMT, ...) a_FYLOG_FULL("ERROR", FMT, ##__VA_ARGS__)
    #define FYLOGBASIC_ERROR(FMT, ...) i_FYLOG_BASIC(FMT, ##__VA_ARGS__)
#else
    #define FYLOG_ERROR(FMT, ...)
    #define FYLOGBASIC_ERROR(FMT, ...)
#endif

/** Log a warning.
 * Normal version prints out full context. Basic version prints directly.
 *
 * @param FMT The format specifier, followed by its arguments.
 */
#if FYLOG_PRINTS_AT_LEVEL(FYLogger_Level_Warn)
    #define FYLOG_WARN(FMT, ...)  a_FYLOG_FULL("WARN ", FMT, ##__VA_ARGS__)
    #define FYLOGBASIC_WARN(FMT, ...) i_FYLOG_BASIC(FMT, ##__VA_ARGS__)
#else
    #define FYLOG_WARN(FMT, ...)
    #define FYLOGBASIC_WARN(FMT, ...)
#endif

/** Log an info message.
 * Normal version prints out full context. Basic version prints directly.
 *
 * @param FMT The format specifier, followed by its arguments.
 */
#if FYLOG_PRINTS_AT_LEVEL(FYLogger_Level_Info)
    #define FYLOG_INFO(FMT, ...)  a_FYLOG_FULL("INFO ", FMT, ##__VA_ARGS__)
    #define FYLOGBASIC_INFO(FMT, ...) i_FYLOG_BASIC(FMT, ##__VA_ARGS__)
#else
    #define FYLOG_INFO(FMT, ...)
    #define FYLOGBASIC_INFO(FMT, ...)
#endif

/** Log a debug message.
 * Normal version prints out full context. Basic version prints directly.
 *
 * @param FMT The format specifier, followed by its arguments.
 */
#if FYLOG_PRINTS_AT_LEVEL(FYLogger_Level_Debug)
    #define FYLOG_DEBUG(FMT, ...) a_FYLOG_FULL("DEBUG", FMT, ##__VA_ARGS__)
    #define FYLOGBASIC_DEBUG(FMT, ...) i_FYLOG_BASIC(FMT, ##__VA_ARGS__)
#else
    #define FYLOG_DEBUG(FMT, ...)
    #define FYLOGBASIC_DEBUG(FMT, ...)
#endif

/** Log a trace message.
 * Normal version prints out full context. Basic version prints directly.
 *
 * @param FMT The format specifier, followed by its arguments.
 */
#if FYLOG_PRINTS_AT_LEVEL(FYLogger_Level_Trace)
    #define FYLOG_TRACE(FMT, ...) a_FYLOG_FULL("TRACE", FMT, ##__VA_ARGS__)
    #define FYLOGBASIC_TRACE(FMT, ...) i_FYLOG_BASIC(FMT, ##__VA_ARGS__)
#else
    #define FYLOG_TRACE(FMT, ...)
    #define FYLOGBASIC_TRACE(FMT, ...)
#endif



// ============================================================================
#pragma mark - (internal) -
// ============================================================================

/* Put everything back to the way we found it. */
#undef ERROR
#ifdef FYLOG_BAK_ERROR
    #define ERROR FYLOG_BAK_ERROR
    #undef FYLOG_BAK_ERROR
#endif
#undef WARNING
#ifdef FYLOG_BAK_WARN
    #define WARNING FYLOG_BAK_WARN
    #undef FYLOG_BAK_WARN
#endif
#undef INFO
#ifdef FYLOG_BAK_INFO
    #define INFO FYLOG_BAK_INFO
    #undef FYLOG_BAK_INFO
#endif
#undef DEBUG
#ifdef FYLOG_BAK_DEBUG
    #define DEBUG FYLOG_BAK_DEBUG
    #undef FYLOG_BAK_DEBUG
#endif
#undef TRACE
#ifdef FYLOG_BAK_TRACE
    #define TRACE FYLOG_BAK_TRACE
    #undef FYLOG_BAK_TRACE
#endif


#ifdef __cplusplus
}
#endif

#endif // HDR_FYLogger_h
