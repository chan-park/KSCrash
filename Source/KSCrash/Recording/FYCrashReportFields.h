//
//  FYCrashReportFields.h
//
//  Created by Karl Stenerud on 2012-10-07.
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


#ifndef HDR_FYCrashReportFields_h
#define HDR_FYCrashReportFields_h


#pragma mark - Report Types -

#define FYCrashReportType_Minimal          "minimal"
#define FYCrashReportType_Standard         "standard"
#define FYCrashReportType_Custom           "custom"


#pragma mark - Memory Types -

#define FYCrashMemType_Block               "objc_block"
#define FYCrashMemType_Class               "objc_class"
#define FYCrashMemType_NullPointer         "null_pointer"
#define FYCrashMemType_Object              "objc_object"
#define FYCrashMemType_String              "string"
#define FYCrashMemType_Unknown             "unknown"


#pragma mark - Exception Types -

#define FYCrashExcType_CPPException        "cpp_exception"
#define FYCrashExcType_Deadlock            "deadlock"
#define FYCrashExcType_Mach                "mach"
#define FYCrashExcType_NSException         "nsexception"
#define FYCrashExcType_Signal              "signal"
#define FYCrashExcType_User                "user"


#pragma mark - Common -

#define FYCrashField_Address               "address"
#define FYCrashField_Contents              "contents"
#define FYCrashField_Exception             "exception"
#define FYCrashField_FirstObject           "first_object"
#define FYCrashField_Index                 "index"
#define FYCrashField_Ivars                 "ivars"
#define FYCrashField_Language              "language"
#define FYCrashField_Name                  "name"
#define FYCrashField_UserInfo              "userInfo"
#define FYCrashField_ReferencedObject      "referenced_object"
#define FYCrashField_Type                  "type"
#define FYCrashField_UUID                  "uuid"
#define FYCrashField_Value                 "value"

#define FYCrashField_Error                 "error"
#define FYCrashField_JSONData              "json_data"


#pragma mark - Notable Address -

#define FYCrashField_Class                 "class"
#define FYCrashField_LastDeallocObject     "last_deallocated_obj"


#pragma mark - Backtrace -

#define FYCrashField_InstructionAddr       "instruction_addr"
#define FYCrashField_LineOfCode            "line_of_code"
#define FYCrashField_ObjectAddr            "object_addr"
#define FYCrashField_ObjectName            "object_name"
#define FYCrashField_SymbolAddr            "symbol_addr"
#define FYCrashField_SymbolName            "symbol_name"


#pragma mark - Stack Dump -

#define FYCrashField_DumpEnd               "dump_end"
#define FYCrashField_DumpStart             "dump_start"
#define FYCrashField_GrowDirection         "grow_direction"
#define FYCrashField_Overflow              "overflow"
#define FYCrashField_StackPtr              "stack_pointer"


#pragma mark - Thread Dump -

#define FYCrashField_Backtrace             "backtrace"
#define FYCrashField_Basic                 "basic"
#define FYCrashField_Crashed               "crashed"
#define FYCrashField_CurrentThread         "current_thread"
#define FYCrashField_DispatchQueue         "dispatch_queue"
#define FYCrashField_NotableAddresses      "notable_addresses"
#define FYCrashField_Registers             "registers"
#define FYCrashField_Skipped               "skipped"
#define FYCrashField_Stack                 "stack"


#pragma mark - Binary Image -

#define FYCrashField_CPUSubType            "cpu_subtype"
#define FYCrashField_CPUType               "cpu_type"
#define FYCrashField_ImageAddress          "image_addr"
#define FYCrashField_ImageVmAddress        "image_vmaddr"
#define FYCrashField_ImageSize             "image_size"
#define FYCrashField_ImageMajorVersion     "major_version"
#define FYCrashField_ImageMinorVersion     "minor_version"
#define FYCrashField_ImageRevisionVersion  "revision_version"


#pragma mark - Memory -

#define FYCrashField_Free                  "free"
#define FYCrashField_Usable                "usable"


#pragma mark - Error -

#define FYCrashField_Backtrace             "backtrace"
#define FYCrashField_Code                  "code"
#define FYCrashField_CodeName              "code_name"
#define FYCrashField_CPPException          "cpp_exception"
#define FYCrashField_ExceptionName         "exception_name"
#define FYCrashField_Mach                  "mach"
#define FYCrashField_NSException           "nsexception"
#define FYCrashField_Reason                "reason"
#define FYCrashField_Signal                "signal"
#define FYCrashField_Subcode               "subcode"
#define FYCrashField_UserReported          "user_reported"


#pragma mark - Process State -

#define FYCrashField_LastDeallocedNSException "last_dealloced_nsexception"
#define FYCrashField_ProcessState             "process"


#pragma mark - App Stats -

#define FYCrashField_ActiveTimeSinceCrash  "active_time_since_last_crash"
#define FYCrashField_ActiveTimeSinceLaunch "active_time_since_launch"
#define FYCrashField_AppActive             "application_active"
#define FYCrashField_AppInFG               "application_in_foreground"
#define FYCrashField_BGTimeSinceCrash      "background_time_since_last_crash"
#define FYCrashField_BGTimeSinceLaunch     "background_time_since_launch"
#define FYCrashField_LaunchesSinceCrash    "launches_since_last_crash"
#define FYCrashField_SessionsSinceCrash    "sessions_since_last_crash"
#define FYCrashField_SessionsSinceLaunch   "sessions_since_launch"


#pragma mark - Report -

#define FYCrashField_Crash                 "crash"
#define FYCrashField_Debug                 "debug"
#define FYCrashField_Diagnosis             "diagnosis"
#define FYCrashField_ID                    "id"
#define FYCrashField_ProcessName           "process_name"
#define FYCrashField_Report                "report"
#define FYCrashField_Timestamp             "timestamp"
#define FYCrashField_Version               "version"

#pragma mark Minimal
#define FYCrashField_CrashedThread         "crashed_thread"

#pragma mark Standard
#define FYCrashField_AppStats              "application_stats"
#define FYCrashField_BinaryImages          "binary_images"
#define FYCrashField_System                "system"
#define FYCrashField_Memory                "memory"
#define FYCrashField_Threads               "threads"
#define FYCrashField_User                  "user"
#define FYCrashField_ConsoleLog            "console_log"

#pragma mark Incomplete
#define FYCrashField_Incomplete            "incomplete"
#define FYCrashField_RecrashReport         "recrash_report"

#pragma mark System
#define FYCrashField_AppStartTime          "app_start_time"
#define FYCrashField_AppUUID               "app_uuid"
#define FYCrashField_BootTime              "boot_time"
#define FYCrashField_BundleID              "CFBundleIdentifier"
#define FYCrashField_BundleName            "CFBundleName"
#define FYCrashField_BundleShortVersion    "CFBundleShortVersionString"
#define FYCrashField_BundleVersion         "CFBundleVersion"
#define FYCrashField_CPUArch               "cpu_arch"
#define FYCrashField_CPUType               "cpu_type"
#define FYCrashField_CPUSubType            "cpu_subtype"
#define FYCrashField_BinaryCPUType         "binary_cpu_type"
#define FYCrashField_BinaryCPUSubType      "binary_cpu_subtype"
#define FYCrashField_DeviceAppHash         "device_app_hash"
#define FYCrashField_Executable            "CFBundleExecutable"
#define FYCrashField_ExecutablePath        "CFBundleExecutablePath"
#define FYCrashField_Jailbroken            "jailbroken"
#define FYCrashField_KernelVersion         "kernel_version"
#define FYCrashField_Machine               "machine"
#define FYCrashField_Model                 "model"
#define FYCrashField_OSVersion             "os_version"
#define FYCrashField_ParentProcessID       "parent_process_id"
#define FYCrashField_ProcessID             "process_id"
#define FYCrashField_ProcessName           "process_name"
#define FYCrashField_Size                  "size"
#define FYCrashField_Storage               "storage"
#define FYCrashField_SystemName            "system_name"
#define FYCrashField_SystemVersion         "system_version"
#define FYCrashField_TimeZone              "time_zone"
#define FYCrashField_BuildType             "build_type"

#endif
