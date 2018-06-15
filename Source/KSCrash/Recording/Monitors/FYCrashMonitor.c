//
//  FYCrashMonitor.c
//
//  Created by Karl Stenerud on 2012-02-12.
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


#include "FYCrashMonitor.h"
#include "FYCrashMonitorContext.h"
#include "FYCrashMonitorType.h"

#include "FYCrashMonitor_Deadlock.h"
#include "FYCrashMonitor_MachException.h"
#include "FYCrashMonitor_CPPException.h"
#include "FYCrashMonitor_NSException.h"
#include "FYCrashMonitor_Signal.h"
#include "FYCrashMonitor_System.h"
#include "FYCrashMonitor_User.h"
#include "FYCrashMonitor_AppState.h"
#include "FYCrashMonitor_Zombie.h"
#include "FYDebug.h"
#include "FYThread.h"
#include "FYSystemCapabilities.h"

#include <memory.h>

//#define FYLogger_LocalLevel TRACE
#include "FYLogger.h"


// ============================================================================
#pragma mark - Globals -
// ============================================================================

typedef struct
{
    FYCrashMonitorType monitorType;
    FYCrashMonitorAPI* (*getAPI)(void);
} Monitor;

static Monitor g_monitors[] =
{
#if FYCRASH_HAS_MACH
    {
        .monitorType = FYCrashMonitorTypeMachException,
        .getAPI = fycm_machexception_getAPI,
    },
#endif
#if FYCRASH_HAS_SIGNAL
    {
        .monitorType = FYCrashMonitorTypeSignal,
        .getAPI = fycm_signal_getAPI,
    },
#endif
#if FYCRASH_HAS_OBJC
    {
        .monitorType = FYCrashMonitorTypeNSException,
        .getAPI = fycm_nsexception_getAPI,
    },
    {
        .monitorType = FYCrashMonitorTypeMainThreadDeadlock,
        .getAPI = fycm_deadlock_getAPI,
    },
    {
        .monitorType = FYCrashMonitorTypeZombie,
        .getAPI = fycm_zombie_getAPI,
    },
#endif
    {
        .monitorType = FYCrashMonitorTypeCPPException,
        .getAPI = fycm_cppexception_getAPI,
    },
    {
        .monitorType = FYCrashMonitorTypeUserReported,
        .getAPI = fycm_user_getAPI,
    },
    {
        .monitorType = FYCrashMonitorTypeSystem,
        .getAPI = fycm_system_getAPI,
    },
    {
        .monitorType = FYCrashMonitorTypeApplicationState,
        .getAPI = fycm_appstate_getAPI,
    },
};
static int g_monitorsCount = sizeof(g_monitors) / sizeof(*g_monitors);

static FYCrashMonitorType g_activeMonitors = FYCrashMonitorTypeNone;

static bool g_handlingFatalException = false;
static bool g_crashedDuringExceptionHandling = false;
static bool g_requiresAsyncSafety = false;

static void (*g_onExceptionEvent)(struct FYCrash_MonitorContext* monitorContext);

// ============================================================================
#pragma mark - API -
// ============================================================================

static inline FYCrashMonitorAPI* getAPI(Monitor* monitor)
{
    if(monitor != NULL && monitor->getAPI != NULL)
    {
        return monitor->getAPI();
    }
    return NULL;
}

static inline void setMonitorEnabled(Monitor* monitor, bool isEnabled)
{
    FYCrashMonitorAPI* api = getAPI(monitor);
    if(api != NULL && api->setEnabled != NULL)
    {
        api->setEnabled(isEnabled);
    }
}

static inline bool isMonitorEnabled(Monitor* monitor)
{
    FYCrashMonitorAPI* api = getAPI(monitor);
    if(api != NULL && api->isEnabled != NULL)
    {
        return api->isEnabled();
    }
    return false;
}

static inline void addContextualInfoToEvent(Monitor* monitor, struct FYCrash_MonitorContext* eventContext)
{
    FYCrashMonitorAPI* api = getAPI(monitor);
    if(api != NULL && api->addContextualInfoToEvent != NULL)
    {
        api->addContextualInfoToEvent(eventContext);
    }
}

void fycm_setEventCallback(void (*onEvent)(struct FYCrash_MonitorContext* monitorContext))
{
    g_onExceptionEvent = onEvent;
}

void fycm_setActiveMonitors(FYCrashMonitorType monitorTypes)
{
    if(fydebug_isBeingTraced() && (monitorTypes & FYCrashMonitorTypeDebuggerUnsafe))
    {
        static bool hasWarned = false;
        if(!hasWarned)
        {
            hasWarned = true;
            FYLOGBASIC_WARN("    ************************ Crash Handler Notice ************************");
            FYLOGBASIC_WARN("    *     App is running in a debugger. Masking out unsafe monitors.     *");
            FYLOGBASIC_WARN("    * This means that most crashes WILL NOT BE RECORDED while debugging! *");
            FYLOGBASIC_WARN("    **********************************************************************");
        }
        monitorTypes &= FYCrashMonitorTypeDebuggerSafe;
    }
    if(g_requiresAsyncSafety && (monitorTypes & FYCrashMonitorTypeAsyncUnsafe))
    {
        FYLOG_DEBUG("Async-safe environment detected. Masking out unsafe monitors.");
        monitorTypes &= FYCrashMonitorTypeAsyncSafe;
    }

    FYLOG_DEBUG("Changing active monitors from 0x%x tp 0x%x.", g_activeMonitors, monitorTypes);

    FYCrashMonitorType activeMonitors = FYCrashMonitorTypeNone;
    for(int i = 0; i < g_monitorsCount; i++)
    {
        Monitor* monitor = &g_monitors[i];
        bool isEnabled = monitor->monitorType & monitorTypes;
        setMonitorEnabled(monitor, isEnabled);
        if(isMonitorEnabled(monitor))
        {
            activeMonitors |= monitor->monitorType;
        }
        else
        {
            activeMonitors &= ~monitor->monitorType;
        }
    }

    FYLOG_DEBUG("Active monitors are now 0x%x.", activeMonitors);
    g_activeMonitors = activeMonitors;
}

FYCrashMonitorType fycm_getActiveMonitors()
{
    return g_activeMonitors;
}


// ============================================================================
#pragma mark - Private API -
// ============================================================================

bool fycm_notifyFatalExceptionCaptured(bool isAsyncSafeEnvironment)
{
    g_requiresAsyncSafety |= isAsyncSafeEnvironment; // Don't let it be unset.
    if(g_handlingFatalException)
    {
        g_crashedDuringExceptionHandling = true;
    }
    g_handlingFatalException = true;
    if(g_crashedDuringExceptionHandling)
    {
        FYLOG_INFO("Detected crash in the crash reporter. Uninstalling FYCrash.");
        fycm_setActiveMonitors(FYCrashMonitorTypeNone);
    }
    return g_crashedDuringExceptionHandling;
}

void fycm_handleException(struct FYCrash_MonitorContext* context)
{
    context->requiresAsyncSafety = g_requiresAsyncSafety;
    if(g_crashedDuringExceptionHandling)
    {
        context->crashedDuringCrashHandling = true;
    }
    for(int i = 0; i < g_monitorsCount; i++)
    {
        Monitor* monitor = &g_monitors[i];
        if(isMonitorEnabled(monitor))
        {
            addContextualInfoToEvent(monitor, context);
        }
    }

    g_onExceptionEvent(context);

    if (context->currentSnapshotUserReported) {
        g_handlingFatalException = false;
    } else {
        if(g_handlingFatalException && !g_crashedDuringExceptionHandling) {
            FYLOG_DEBUG("Exception is fatal. Restoring original handlers.");
            fycm_setActiveMonitors(FYCrashMonitorTypeNone);
        }
    }
}
