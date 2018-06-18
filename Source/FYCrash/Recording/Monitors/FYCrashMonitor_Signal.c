//
//  FYCrashMonitor_Signal.c
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

#include "FYCrashMonitor_Signal.h"
#include "FYCrashMonitorContext.h"
#include "FYID.h"
#include "FYSignalInfo.h"
#include "FYMachineContext.h"
#include "FYSystemCapabilities.h"
#include "FYStackCursor_MachineContext.h"

//#define FYLogger_LocalLevel TRACE
#include "FYLogger.h"

#if FYCRASH_HAS_SIGNAL

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


// ============================================================================
#pragma mark - Globals -
// ============================================================================

static volatile bool g_isEnabled = false;

static FYCrash_MonitorContext g_monitorContext;
static FYStackCursor g_stackCursor;

#if FYCRASH_HAS_SIGNAL_STACK
/** Our custom signal stack. The signal handler will use this as its stack. */
static stack_t g_signalStack = {0};
#endif

/** Signal handlers that were installed before we installed ours. */
static struct sigaction* g_previousSignalHandlers = NULL;

static char g_eventID[37];

// ============================================================================
#pragma mark - Callbacks -
// ============================================================================

/** Our custom signal handler.
 * Restore the default signal handlers, record the signal information, and
 * write a crash report.
 * Once we're done, re-raise the signal and let the default handlers deal with
 * it.
 *
 * @param sigNum The signal that was raised.
 *
 * @param signalInfo Information about the signal.
 *
 * @param userContext Other contextual information.
 */
static void handleSignal(int sigNum, siginfo_t* signalInfo, void* userContext)
{
    FYLOG_DEBUG("Trapped signal %d", sigNum);
    if(g_isEnabled)
    {
        fymc_suspendEnvironment();
        fycm_notifyFatalExceptionCaptured(false);

        FYLOG_DEBUG("Filling out context.");
        FYMC_NEW_CONTEXT(machineContext);
        fymc_getContextForSignal(userContext, machineContext);
        fysc_initWithMachineContext(&g_stackCursor, 100, machineContext);

        FYCrash_MonitorContext* crashContext = &g_monitorContext;
        memset(crashContext, 0, sizeof(*crashContext));
        crashContext->crashType = FYCrashMonitorTypeSignal;
        crashContext->eventID = g_eventID;
        crashContext->offendingMachineContext = machineContext;
        crashContext->registersAreValid = true;
        crashContext->faultAddress = (uintptr_t)signalInfo->si_addr;
        crashContext->signal.userContext = userContext;
        crashContext->signal.signum = signalInfo->si_signo;
        crashContext->signal.sigcode = signalInfo->si_code;
        crashContext->stackCursor = &g_stackCursor;

        fycm_handleException(crashContext);
        fymc_resumeEnvironment();
    }

    FYLOG_DEBUG("Re-raising signal for regular handlers to catch.");
    // This is technically not allowed, but it works in OSX and iOS.
    raise(sigNum);
}


// ============================================================================
#pragma mark - API -
// ============================================================================

static bool installSignalHandler()
{
    FYLOG_DEBUG("Installing signal handler.");

#if FYCRASH_HAS_SIGNAL_STACK

    if(g_signalStack.ss_size == 0)
    {
        FYLOG_DEBUG("Allocating signal stack area.");
        g_signalStack.ss_size = SIGSTKSZ;
        g_signalStack.ss_sp = malloc(g_signalStack.ss_size);
    }

    FYLOG_DEBUG("Setting signal stack area.");
    if(sigaltstack(&g_signalStack, NULL) != 0)
    {
        FYLOG_ERROR("signalstack: %s", strerror(errno));
        goto failed;
    }
#endif

    const int* fatalSignals = fysignal_fatalSignals();
    int fatalSignalsCount = fysignal_numFatalSignals();

    if(g_previousSignalHandlers == NULL)
    {
        FYLOG_DEBUG("Allocating memory to store previous signal handlers.");
        g_previousSignalHandlers = malloc(sizeof(*g_previousSignalHandlers)
                                          * (unsigned)fatalSignalsCount);
    }

    struct sigaction action = {{0}};
    action.sa_flags = SA_SIGINFO | SA_ONSTACK;
#if FYCRASH_HOST_APPLE && defined(__LP64__)
    action.sa_flags |= SA_64REGSET;
#endif
    sigemptyset(&action.sa_mask);
    action.sa_sigaction = &handleSignal;

    for(int i = 0; i < fatalSignalsCount; i++)
    {
        FYLOG_DEBUG("Assigning handler for signal %d", fatalSignals[i]);
        if(sigaction(fatalSignals[i], &action, &g_previousSignalHandlers[i]) != 0)
        {
            char sigNameBuff[30];
            const char* sigName = fysignal_signalName(fatalSignals[i]);
            if(sigName == NULL)
            {
                snprintf(sigNameBuff, sizeof(sigNameBuff), "%d", fatalSignals[i]);
                sigName = sigNameBuff;
            }
            FYLOG_ERROR("sigaction (%s): %s", sigName, strerror(errno));
            // Try to reverse the damage
            for(i--;i >= 0; i--)
            {
                sigaction(fatalSignals[i], &g_previousSignalHandlers[i], NULL);
            }
            goto failed;
        }
    }
    FYLOG_DEBUG("Signal handlers installed.");
    return true;

failed:
    FYLOG_DEBUG("Failed to install signal handlers.");
    return false;
}

static void uninstallSignalHandler(void)
{
    FYLOG_DEBUG("Uninstalling signal handlers.");

    const int* fatalSignals = fysignal_fatalSignals();
    int fatalSignalsCount = fysignal_numFatalSignals();

    for(int i = 0; i < fatalSignalsCount; i++)
    {
        FYLOG_DEBUG("Restoring original handler for signal %d", fatalSignals[i]);
        sigaction(fatalSignals[i], &g_previousSignalHandlers[i], NULL);
    }
    
#if FYCRASH_HAS_SIGNAL_STACK
    g_signalStack = (stack_t){0};
#endif
    FYLOG_DEBUG("Signal handlers uninstalled.");
}

static void setEnabled(bool isEnabled)
{
    if(isEnabled != g_isEnabled)
    {
        g_isEnabled = isEnabled;
        if(isEnabled)
        {
            fyid_generate(g_eventID);
            if(!installSignalHandler())
            {
                return;
            }
        }
        else
        {
            uninstallSignalHandler();
        }
    }
}

static bool isEnabled()
{
    return g_isEnabled;
}

static void addContextualInfoToEvent(struct FYCrash_MonitorContext* eventContext)
{
    if(!(eventContext->crashType & (FYCrashMonitorTypeSignal | FYCrashMonitorTypeMachException)))
    {
        eventContext->signal.signum = SIGABRT;
    }
}

#endif

FYCrashMonitorAPI* fycm_signal_getAPI()
{
    static FYCrashMonitorAPI api =
    {
#if FYCRASH_HAS_SIGNAL
        .setEnabled = setEnabled,
        .isEnabled = isEnabled,
        .addContextualInfoToEvent = addContextualInfoToEvent
#endif
    };
    return &api;
}