//
//  FYCrashC.c
//
//  Created by Karl Stenerud on 2012-01-28.
//
//  Copyright (c) 2012 Karl Stenerud. All rights reserved.
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


#include "FYCrashC.h"

#include "FYCrashCachedData.h"
#include "FYCrashReport.h"
#include "FYCrashReportFixer.h"
#include "FYCrashReportStore.h"
#include "FYCrashMonitor_Deadlock.h"
#include "FYCrashMonitor_User.h"
#include "FYFileUtils.h"
#include "FYObjC.h"
#include "FYString.h"
#include "FYCrashMonitor_System.h"
#include "FYCrashMonitor_Zombie.h"
#include "FYCrashMonitor_AppState.h"
#include "FYCrashMonitorContext.h"
#include "FYSystemCapabilities.h"

//#define FYLogger_LocalLevel TRACE
#include "FYLogger.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


// ============================================================================
#pragma mark - Globals -
// ============================================================================

/** True if FYCrash has been installed. */
static volatile bool g_installed = 0;

static bool g_shouldAddConsoleLogToReport = false;
static bool g_shouldPrintPreviousLog = false;
static char g_consoleLogPath[FYFU_MAX_PATH_LENGTH];
static FYCrashMonitorType g_monitoring = FYCrashMonitorTypeProductionSafeMinimal;
static char g_lastCrashReportFilePath[FYFU_MAX_PATH_LENGTH];


// ============================================================================
#pragma mark - Utility -
// ============================================================================

static void printPreviousLog(const char* filePath)
{
    char* data;
    int length;
    if(fyfu_readEntireFile(filePath, &data, &length, 0))
    {
        printf("\nvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv Previous Log vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv\n\n");
        printf("%s\n", data);
        printf("^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^\n\n");
        fflush(stdout);
    }
}


// ============================================================================
#pragma mark - Callbacks -
// ============================================================================

/** Called when a crash occurs.
 *
 * This function gets passed as a callback to a crash handler.
 */
static void onCrash(struct FYCrash_MonitorContext* monitorContext)
{
    if (monitorContext->currentSnapshotUserReported == false) {
        FYLOG_DEBUG("Updating application state to note crash.");
        fycrashstate_notifyAppCrash();
    }
    monitorContext->consoleLogPath = g_shouldAddConsoleLogToReport ? g_consoleLogPath : NULL;

    if(monitorContext->crashedDuringCrashHandling)
    {
        fycrashreport_writeRecrashReport(monitorContext, g_lastCrashReportFilePath);
    }
    else
    {
        char crashReportFilePath[FYFU_MAX_PATH_LENGTH];
        fycrs_getNextCrashReportPath(crashReportFilePath);
        strncpy(g_lastCrashReportFilePath, crashReportFilePath, sizeof(g_lastCrashReportFilePath));
        fycrashreport_writeStandardReport(monitorContext, crashReportFilePath);
    }
}


// ============================================================================
#pragma mark - API -
// ============================================================================

FYCrashMonitorType fycrash_install(const char* appName, const char* const installPath)
{
    FYLOG_DEBUG("Installing crash reporter.");

    if(g_installed)
    {
        FYLOG_DEBUG("Crash reporter already installed.");
        return g_monitoring;
    }
    g_installed = 1;

    char path[FYFU_MAX_PATH_LENGTH];
    snprintf(path, sizeof(path), "%s/Reports", installPath);
    fyfu_makePath(path);
    fycrs_initialize(appName, path);

    snprintf(path, sizeof(path), "%s/Data", installPath);
    fyfu_makePath(path);
    snprintf(path, sizeof(path), "%s/Data/CrashState.json", installPath);
    fycrashstate_initialize(path);

    snprintf(g_consoleLogPath, sizeof(g_consoleLogPath), "%s/Data/ConsoleLog.txt", installPath);
    if(g_shouldPrintPreviousLog)
    {
        printPreviousLog(g_consoleLogPath);
    }
    fylog_setLogFilename(g_consoleLogPath, true);
    
    fyccd_init(60);

    fycm_setEventCallback(onCrash);
    FYCrashMonitorType monitors = fycrash_setMonitoring(g_monitoring);

    FYLOG_DEBUG("Installation complete.");
    return monitors;
}

FYCrashMonitorType fycrash_setMonitoring(FYCrashMonitorType monitors)
{
    g_monitoring = monitors;
    
    if(g_installed)
    {
        fycm_setActiveMonitors(monitors);
        return fycm_getActiveMonitors();
    }
    // Return what we will be monitoring in future.
    return g_monitoring;
}

void fycrash_setUserInfoJSON(const char* const userInfoJSON)
{
    fycrashreport_setUserInfoJSON(userInfoJSON);
}

void fycrash_setDeadlockWatchdogInterval(double deadlockWatchdogInterval)
{
#if FYCRASH_HAS_OBJC
    fycm_setDeadlockHandlerWatchdogInterval(deadlockWatchdogInterval);
#endif
}

void fycrash_setIntrospectMemory(bool introspectMemory)
{
    fycrashreport_setIntrospectMemory(introspectMemory);
}

void fycrash_setDoNotIntrospectClasses(const char** doNotIntrospectClasses, int length)
{
    fycrashreport_setDoNotIntrospectClasses(doNotIntrospectClasses, length);
}

void fycrash_setCrashNotifyCallback(const FYReportWriteCallback onCrashNotify)
{
    fycrashreport_setUserSectionWriteCallback(onCrashNotify);
}

void fycrash_setAddConsoleLogToReport(bool shouldAddConsoleLogToReport)
{
    g_shouldAddConsoleLogToReport = shouldAddConsoleLogToReport;
}

void fycrash_setPrintPreviousLog(bool shouldPrintPreviousLog)
{
    g_shouldPrintPreviousLog = shouldPrintPreviousLog;
}

void fycrash_setMaxReportCount(int maxReportCount)
{
    fycrs_setMaxReportCount(maxReportCount);
}

void fycrash_reportUserException(const char* name,
                                 const char* reason,
                                 const char* language,
                                 const char* lineOfCode,
                                 const char* stackTrace,
                                 bool logAllThreads,
                                 bool terminateProgram)
{
    fycm_reportUserException(name,
                             reason,
                             language,
                             lineOfCode,
                             stackTrace,
                             logAllThreads,
                             terminateProgram);
    if(g_shouldAddConsoleLogToReport)
    {
        fylog_clearLogFile();
    }
}

void fycrash_notifyAppActive(bool isActive)
{
    fycrashstate_notifyAppActive(isActive);
}

void fycrash_notifyAppInForeground(bool isInForeground)
{
    fycrashstate_notifyAppInForeground(isInForeground);
}

void fycrash_notifyAppTerminate(void)
{
    fycrashstate_notifyAppTerminate();
}

void fycrash_notifyAppCrash(void)
{
    fycrashstate_notifyAppCrash();
}

int fycrash_getReportCount()
{
    return fycrs_getReportCount();
}

int fycrash_getReportIDs(int64_t* reportIDs, int count)
{
    return fycrs_getReportIDs(reportIDs, count);
}

char* fycrash_readReport(int64_t reportID)
{
    if(reportID <= 0)
    {
        FYLOG_ERROR("Report ID was %" PRIx64, reportID);
        return NULL;
    }

    char* rawReport = fycrs_readReport(reportID);
    if(rawReport == NULL)
    {
        FYLOG_ERROR("Failed to load report ID %" PRIx64, reportID);
        return NULL;
    }

    char* fixedReport = fycrf_fixupCrashReport(rawReport);
    if(fixedReport == NULL)
    {
        FYLOG_ERROR("Failed to fixup report ID %" PRIx64, reportID);
    }

    free(rawReport);
    return fixedReport;
}

int64_t fycrash_addUserReport(const char* report, int reportLength)
{
    return fycrs_addUserReport(report, reportLength);
}

void fycrash_deleteAllReports()
{
    fycrs_deleteAllReports();
}

void fycrash_deleteReportWithID(int64_t reportID)
{
    fycrs_deleteReportWithID(reportID);
}
