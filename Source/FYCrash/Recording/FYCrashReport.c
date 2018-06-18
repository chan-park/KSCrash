//
//  FYCrashReport.m
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


#include "FYCrashReport.h"

#include "FYCrashReportFields.h"
#include "FYCrashReportWriter.h"
#include "FYDynamicLinker.h"
#include "FYFileUtils.h"
#include "FYJSONCodec.h"
#include "FYCPU.h"
#include "FYMemory.h"
#include "FYMach.h"
#include "FYThread.h"
#include "FYObjC.h"
#include "FYSignalInfo.h"
#include "FYCrashMonitor_Zombie.h"
#include "FYString.h"
#include "FYCrashReportVersion.h"
#include "FYStackCursor_Backtrace.h"
#include "FYStackCursor_MachineContext.h"
#include "FYSystemCapabilities.h"
#include "FYCrashCachedData.h"

//#define FYLogger_LocalLevel TRACE
#include "FYLogger.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>


// ============================================================================
#pragma mark - Constants -
// ============================================================================

/** Default number of objects, subobjects, and ivars to record from a memory loc */
#define kDefaultMemorySearchDepth 15

/** How far to search the stack (in pointer sized jumps) for notable data. */
#define kStackNotableSearchBackDistance 20
#define kStackNotableSearchForwardDistance 10

/** How much of the stack to dump (in pointer sized jumps). */
#define kStackContentsPushedDistance 20
#define kStackContentsPoppedDistance 10
#define kStackContentsTotalDistance (kStackContentsPushedDistance + kStackContentsPoppedDistance)

/** The minimum length for a valid string. */
#define kMinStringLength 4


// ============================================================================
#pragma mark - JSON Encoding -
// ============================================================================

#define getJsonContext(REPORT_WRITER) ((FYJSONEncodeContext*)((REPORT_WRITER)->context))

/** Used for writing hex string values. */
static const char g_hexNybbles[] =
{
    '0', '1', '2', '3', '4', '5', '6', '7',
    '8', '9', 'A', 'B', 'C', 'D', 'E', 'F'
};

// ============================================================================
#pragma mark - Runtime Config -
// ============================================================================

typedef struct
{
    /** If YES, introspect memory contents during a crash.
     * Any Objective-C objects or C strings near the stack pointer or referenced by
     * cpu registers or exceptions will be recorded in the crash report, along with
     * their contents.
     */
    bool enabled;
    
    /** List of classes that should never be introspected.
     * Whenever a class in this list is encountered, only the class name will be recorded.
     */
    const char** restrictedClasses;
    int restrictedClassesCount;
} FYCrash_IntrospectionRules;

static const char* g_userInfoJSON;
static FYCrash_IntrospectionRules g_introspectionRules;
static FYReportWriteCallback g_userSectionWriteCallback;


#pragma mark Callbacks

static void addBooleanElement(const FYCrashReportWriter* const writer, const char* const key, const bool value)
{
    fyjson_addBooleanElement(getJsonContext(writer), key, value);
}

static void addFloatingPointElement(const FYCrashReportWriter* const writer, const char* const key, const double value)
{
    fyjson_addFloatingPointElement(getJsonContext(writer), key, value);
}

static void addIntegerElement(const FYCrashReportWriter* const writer, const char* const key, const int64_t value)
{
    fyjson_addIntegerElement(getJsonContext(writer), key, value);
}

static void addUIntegerElement(const FYCrashReportWriter* const writer, const char* const key, const uint64_t value)
{
    fyjson_addIntegerElement(getJsonContext(writer), key, (int64_t)value);
}

static void addStringElement(const FYCrashReportWriter* const writer, const char* const key, const char* const value)
{
    fyjson_addStringElement(getJsonContext(writer), key, value, FYJSON_SIZE_AUTOMATIC);
}

static void addTextFileElement(const FYCrashReportWriter* const writer, const char* const key, const char* const filePath)
{
    const int fd = open(filePath, O_RDONLY);
    if(fd < 0)
    {
        FYLOG_ERROR("Could not open file %s: %s", filePath, strerror(errno));
        return;
    }

    if(fyjson_beginStringElement(getJsonContext(writer), key) != FYJSON_OK)
    {
        FYLOG_ERROR("Could not start string element");
        goto done;
    }

    char buffer[512];
    int bytesRead;
    for(bytesRead = (int)read(fd, buffer, sizeof(buffer));
        bytesRead > 0;
        bytesRead = (int)read(fd, buffer, sizeof(buffer)))
    {
        if(fyjson_appendStringElement(getJsonContext(writer), buffer, bytesRead) != FYJSON_OK)
        {
            FYLOG_ERROR("Could not append string element");
            goto done;
        }
    }

done:
    fyjson_endStringElement(getJsonContext(writer));
    close(fd);
}

static void addDataElement(const FYCrashReportWriter* const writer,
                           const char* const key,
                           const char* const value,
                           const int length)
{
    fyjson_addDataElement(getJsonContext(writer), key, value, length);
}

static void beginDataElement(const FYCrashReportWriter* const writer, const char* const key)
{
    fyjson_beginDataElement(getJsonContext(writer), key);
}

static void appendDataElement(const FYCrashReportWriter* const writer, const char* const value, const int length)
{
    fyjson_appendDataElement(getJsonContext(writer), value, length);
}

static void endDataElement(const FYCrashReportWriter* const writer)
{
    fyjson_endDataElement(getJsonContext(writer));
}

static void addUUIDElement(const FYCrashReportWriter* const writer, const char* const key, const unsigned char* const value)
{
    if(value == NULL)
    {
        fyjson_addNullElement(getJsonContext(writer), key);
    }
    else
    {
        char uuidBuffer[37];
        const unsigned char* src = value;
        char* dst = uuidBuffer;
        for(int i = 0; i < 4; i++)
        {
            *dst++ = g_hexNybbles[(*src>>4)&15];
            *dst++ = g_hexNybbles[(*src++)&15];
        }
        *dst++ = '-';
        for(int i = 0; i < 2; i++)
        {
            *dst++ = g_hexNybbles[(*src>>4)&15];
            *dst++ = g_hexNybbles[(*src++)&15];
        }
        *dst++ = '-';
        for(int i = 0; i < 2; i++)
        {
            *dst++ = g_hexNybbles[(*src>>4)&15];
            *dst++ = g_hexNybbles[(*src++)&15];
        }
        *dst++ = '-';
        for(int i = 0; i < 2; i++)
        {
            *dst++ = g_hexNybbles[(*src>>4)&15];
            *dst++ = g_hexNybbles[(*src++)&15];
        }
        *dst++ = '-';
        for(int i = 0; i < 6; i++)
        {
            *dst++ = g_hexNybbles[(*src>>4)&15];
            *dst++ = g_hexNybbles[(*src++)&15];
        }

        fyjson_addStringElement(getJsonContext(writer), key, uuidBuffer, (int)(dst - uuidBuffer));
    }
}

static void addJSONElement(const FYCrashReportWriter* const writer,
                           const char* const key,
                           const char* const jsonElement,
                           bool closeLastContainer)
{
    int jsonResult = fyjson_addJSONElement(getJsonContext(writer),
                                           key,
                                           jsonElement,
                                           (int)strlen(jsonElement),
                                           closeLastContainer);
    if(jsonResult != FYJSON_OK)
    {
        char errorBuff[100];
        snprintf(errorBuff,
                 sizeof(errorBuff),
                 "Invalid JSON data: %s",
                 fyjson_stringForError(jsonResult));
        fyjson_beginObject(getJsonContext(writer), key);
        fyjson_addStringElement(getJsonContext(writer),
                                FYCrashField_Error,
                                errorBuff,
                                FYJSON_SIZE_AUTOMATIC);
        fyjson_addStringElement(getJsonContext(writer),
                                FYCrashField_JSONData,
                                jsonElement,
                                FYJSON_SIZE_AUTOMATIC);
        fyjson_endContainer(getJsonContext(writer));
    }
}

static void addJSONElementFromFile(const FYCrashReportWriter* const writer,
                                   const char* const key,
                                   const char* const filePath,
                                   bool closeLastContainer)
{
    fyjson_addJSONFromFile(getJsonContext(writer), key, filePath, closeLastContainer);
}

static void beginObject(const FYCrashReportWriter* const writer, const char* const key)
{
    fyjson_beginObject(getJsonContext(writer), key);
}

static void beginArray(const FYCrashReportWriter* const writer, const char* const key)
{
    fyjson_beginArray(getJsonContext(writer), key);
}

static void endContainer(const FYCrashReportWriter* const writer)
{
    fyjson_endContainer(getJsonContext(writer));
}


static void addTextLinesFromFile(const FYCrashReportWriter* const writer, const char* const key, const char* const filePath)
{
    char readBuffer[1024];
    FYBufferedReader reader;
    if(!fyfu_openBufferedReader(&reader, filePath, readBuffer, sizeof(readBuffer)))
    {
        return;
    }
    char buffer[1024];
    beginArray(writer, key);
    {
        for(;;)
        {
            int length = sizeof(buffer);
            fyfu_readBufferedReaderUntilChar(&reader, '\n', buffer, &length);
            if(length <= 0)
            {
                break;
            }
            buffer[length - 1] = '\0';
            fyjson_addStringElement(getJsonContext(writer), NULL, buffer, FYJSON_SIZE_AUTOMATIC);
        }
    }
    endContainer(writer);
    fyfu_closeBufferedReader(&reader);
}

static int addJSONData(const char* restrict const data, const int length, void* restrict userData)
{
    FYBufferedWriter* writer = (FYBufferedWriter*)userData;
    const bool success = fyfu_writeBufferedWriter(writer, data, length);
    return success ? FYJSON_OK : FYJSON_ERROR_CANNOT_ADD_DATA;
}


// ============================================================================
#pragma mark - Utility -
// ============================================================================

/** Check if a memory address points to a valid null terminated UTF-8 string.
 *
 * @param address The address to check.
 *
 * @return true if the address points to a string.
 */
static bool isValidString(const void* const address)
{
    if((void*)address == NULL)
    {
        return false;
    }

    char buffer[500];
    if((uintptr_t)address+sizeof(buffer) < (uintptr_t)address)
    {
        // Wrapped around the address range.
        return false;
    }
    if(!fymem_copySafely(address, buffer, sizeof(buffer)))
    {
        return false;
    }
    return fystring_isNullTerminatedUTF8String(buffer, kMinStringLength, sizeof(buffer));
}

/** Get the backtrace for the specified machine context.
 *
 * This function will choose how to fetch the backtrace based on the crash and
 * machine context. It may store the backtrace in backtraceBuffer unless it can
 * be fetched directly from memory. Do not count on backtraceBuffer containing
 * anything. Always use the return value.
 *
 * @param crash The crash handler context.
 *
 * @param machineContext The machine context.
 *
 * @param cursor The stack cursor to fill.
 *
 * @return True if the cursor was filled.
 */
static bool getStackCursor(const FYCrash_MonitorContext* const crash,
                           const struct FYMachineContext* const machineContext,
                           FYStackCursor *cursor)
{
    if(fymc_getThreadFromContext(machineContext) == fymc_getThreadFromContext(crash->offendingMachineContext))
    {
        *cursor = *((FYStackCursor*)crash->stackCursor);
        return true;
    }

    fysc_initWithMachineContext(cursor, FYSC_STACK_OVERFLOW_THRESHOLD, machineContext);
    return true;
}


// ============================================================================
#pragma mark - Report Writing -
// ============================================================================

/** Write the contents of a memory location.
 * Also writes meta information about the data.
 *
 * @param writer The writer.
 *
 * @param key The object key, if needed.
 *
 * @param address The memory address.
 *
 * @param limit How many more subreferenced objects to write, if any.
 */
static void writeMemoryContents(const FYCrashReportWriter* const writer,
                                const char* const key,
                                const uintptr_t address,
                                int* limit);

/** Write a string to the report.
 * This will only print the first child of the array.
 *
 * @param writer The writer.
 *
 * @param key The object key, if needed.
 *
 * @param objectAddress The object's address.
 *
 * @param limit How many more subreferenced objects to write, if any.
 */
static void writeNSStringContents(const FYCrashReportWriter* const writer,
                                  const char* const key,
                                  const uintptr_t objectAddress,
                                  __unused int* limit)
{
    const void* object = (const void*)objectAddress;
    char buffer[200];
    if(fyobjc_copyStringContents(object, buffer, sizeof(buffer)))
    {
        writer->addStringElement(writer, key, buffer);
    }
}

/** Write a URL to the report.
 * This will only print the first child of the array.
 *
 * @param writer The writer.
 *
 * @param key The object key, if needed.
 *
 * @param objectAddress The object's address.
 *
 * @param limit How many more subreferenced objects to write, if any.
 */
static void writeURLContents(const FYCrashReportWriter* const writer,
                             const char* const key,
                             const uintptr_t objectAddress,
                             __unused int* limit)
{
    const void* object = (const void*)objectAddress;
    char buffer[200];
    if(fyobjc_copyStringContents(object, buffer, sizeof(buffer)))
    {
        writer->addStringElement(writer, key, buffer);
    }
}

/** Write a date to the report.
 * This will only print the first child of the array.
 *
 * @param writer The writer.
 *
 * @param key The object key, if needed.
 *
 * @param objectAddress The object's address.
 *
 * @param limit How many more subreferenced objects to write, if any.
 */
static void writeDateContents(const FYCrashReportWriter* const writer,
                              const char* const key,
                              const uintptr_t objectAddress,
                              __unused int* limit)
{
    const void* object = (const void*)objectAddress;
    writer->addFloatingPointElement(writer, key, fyobjc_dateContents(object));
}

/** Write a number to the report.
 * This will only print the first child of the array.
 *
 * @param writer The writer.
 *
 * @param key The object key, if needed.
 *
 * @param objectAddress The object's address.
 *
 * @param limit How many more subreferenced objects to write, if any.
 */
static void writeNumberContents(const FYCrashReportWriter* const writer,
                                const char* const key,
                                const uintptr_t objectAddress,
                                __unused int* limit)
{
    const void* object = (const void*)objectAddress;
    writer->addFloatingPointElement(writer, key, fyobjc_numberAsFloat(object));
}

/** Write an array to the report.
 * This will only print the first child of the array.
 *
 * @param writer The writer.
 *
 * @param key The object key, if needed.
 *
 * @param objectAddress The object's address.
 *
 * @param limit How many more subreferenced objects to write, if any.
 */
static void writeArrayContents(const FYCrashReportWriter* const writer,
                               const char* const key,
                               const uintptr_t objectAddress,
                               int* limit)
{
    const void* object = (const void*)objectAddress;
    uintptr_t firstObject;
    if(fyobjc_arrayContents(object, &firstObject, 1) == 1)
    {
        writeMemoryContents(writer, key, firstObject, limit);
    }
}

/** Write out ivar information about an unknown object.
 *
 * @param writer The writer.
 *
 * @param key The object key, if needed.
 *
 * @param objectAddress The object's address.
 *
 * @param limit How many more subreferenced objects to write, if any.
 */
static void writeUnknownObjectContents(const FYCrashReportWriter* const writer,
                                       const char* const key,
                                       const uintptr_t objectAddress,
                                       int* limit)
{
    (*limit)--;
    const void* object = (const void*)objectAddress;
    FYObjCIvar ivars[10];
    int8_t s8;
    int16_t s16;
    int sInt;
    int32_t s32;
    int64_t s64;
    uint8_t u8;
    uint16_t u16;
    unsigned int uInt;
    uint32_t u32;
    uint64_t u64;
    float f32;
    double f64;
    bool b;
    void* pointer;
    
    
    writer->beginObject(writer, key);
    {
        if(fyobjc_isTaggedPointer(object))
        {
            writer->addIntegerElement(writer, "tagged_payload", (int64_t)fyobjc_taggedPointerPayload(object));
        }
        else
        {
            const void* class = fyobjc_isaPointer(object);
            int ivarCount = fyobjc_ivarList(class, ivars, sizeof(ivars)/sizeof(*ivars));
            *limit -= ivarCount;
            for(int i = 0; i < ivarCount; i++)
            {
                FYObjCIvar* ivar = &ivars[i];
                switch(ivar->type[0])
                {
                    case 'c':
                        fyobjc_ivarValue(object, ivar->index, &s8);
                        writer->addIntegerElement(writer, ivar->name, s8);
                        break;
                    case 'i':
                        fyobjc_ivarValue(object, ivar->index, &sInt);
                        writer->addIntegerElement(writer, ivar->name, sInt);
                        break;
                    case 's':
                        fyobjc_ivarValue(object, ivar->index, &s16);
                        writer->addIntegerElement(writer, ivar->name, s16);
                        break;
                    case 'l':
                        fyobjc_ivarValue(object, ivar->index, &s32);
                        writer->addIntegerElement(writer, ivar->name, s32);
                        break;
                    case 'q':
                        fyobjc_ivarValue(object, ivar->index, &s64);
                        writer->addIntegerElement(writer, ivar->name, s64);
                        break;
                    case 'C':
                        fyobjc_ivarValue(object, ivar->index, &u8);
                        writer->addUIntegerElement(writer, ivar->name, u8);
                        break;
                    case 'I':
                        fyobjc_ivarValue(object, ivar->index, &uInt);
                        writer->addUIntegerElement(writer, ivar->name, uInt);
                        break;
                    case 'S':
                        fyobjc_ivarValue(object, ivar->index, &u16);
                        writer->addUIntegerElement(writer, ivar->name, u16);
                        break;
                    case 'L':
                        fyobjc_ivarValue(object, ivar->index, &u32);
                        writer->addUIntegerElement(writer, ivar->name, u32);
                        break;
                    case 'Q':
                        fyobjc_ivarValue(object, ivar->index, &u64);
                        writer->addUIntegerElement(writer, ivar->name, u64);
                        break;
                    case 'f':
                        fyobjc_ivarValue(object, ivar->index, &f32);
                        writer->addFloatingPointElement(writer, ivar->name, f32);
                        break;
                    case 'd':
                        fyobjc_ivarValue(object, ivar->index, &f64);
                        writer->addFloatingPointElement(writer, ivar->name, f64);
                        break;
                    case 'B':
                        fyobjc_ivarValue(object, ivar->index, &b);
                        writer->addBooleanElement(writer, ivar->name, b);
                        break;
                    case '*':
                    case '@':
                    case '#':
                    case ':':
                        fyobjc_ivarValue(object, ivar->index, &pointer);
                        writeMemoryContents(writer, ivar->name, (uintptr_t)pointer, limit);
                        break;
                    default:
                        FYLOG_DEBUG("%s: Unknown ivar type [%s]", ivar->name, ivar->type);
                }
            }
        }
    }
    writer->endContainer(writer);
}

static bool isRestrictedClass(const char* name)
{
    if(g_introspectionRules.restrictedClasses != NULL)
    {
        for(int i = 0; i < g_introspectionRules.restrictedClassesCount; i++)
        {
            if(strcmp(name, g_introspectionRules.restrictedClasses[i]) == 0)
            {
                return true;
            }
        }
    }
    return false;
}

static void writeZombieIfPresent(const FYCrashReportWriter* const writer,
                                 const char* const key,
                                 const uintptr_t address)
{
#if FYCRASH_HAS_OBJC
    const void* object = (const void*)address;
    const char* zombieClassName = fyzombie_className(object);
    if(zombieClassName != NULL)
    {
        writer->addStringElement(writer, key, zombieClassName);
    }
#endif
}

static bool writeObjCObject(const FYCrashReportWriter* const writer,
                            const uintptr_t address,
                            int* limit)
{
#if FYCRASH_HAS_OBJC
    const void* object = (const void*)address;
    switch(fyobjc_objectType(object))
    {
        case FYObjCTypeClass:
            writer->addStringElement(writer, FYCrashField_Type, FYCrashMemType_Class);
            writer->addStringElement(writer, FYCrashField_Class, fyobjc_className(object));
            return true;
        case FYObjCTypeObject:
        {
            writer->addStringElement(writer, FYCrashField_Type, FYCrashMemType_Object);
            const char* className = fyobjc_objectClassName(object);
            writer->addStringElement(writer, FYCrashField_Class, className);
            if(!isRestrictedClass(className))
            {
                switch(fyobjc_objectClassType(object))
                {
                    case FYObjCClassTypeString:
                        writeNSStringContents(writer, FYCrashField_Value, address, limit);
                        return true;
                    case FYObjCClassTypeURL:
                        writeURLContents(writer, FYCrashField_Value, address, limit);
                        return true;
                    case FYObjCClassTypeDate:
                        writeDateContents(writer, FYCrashField_Value, address, limit);
                        return true;
                    case FYObjCClassTypeArray:
                        if(*limit > 0)
                        {
                            writeArrayContents(writer, FYCrashField_FirstObject, address, limit);
                        }
                        return true;
                    case FYObjCClassTypeNumber:
                        writeNumberContents(writer, FYCrashField_Value, address, limit);
                        return true;
                    case FYObjCClassTypeDictionary:
                    case FYObjCClassTypeException:
                        // TODO: Implement these.
                        if(*limit > 0)
                        {
                            writeUnknownObjectContents(writer, FYCrashField_Ivars, address, limit);
                        }
                        return true;
                    case FYObjCClassTypeUnknown:
                        if(*limit > 0)
                        {
                            writeUnknownObjectContents(writer, FYCrashField_Ivars, address, limit);
                        }
                        return true;
                }
            }
            break;
        }
        case FYObjCTypeBlock:
            writer->addStringElement(writer, FYCrashField_Type, FYCrashMemType_Block);
            const char* className = fyobjc_objectClassName(object);
            writer->addStringElement(writer, FYCrashField_Class, className);
            return true;
        case FYObjCTypeUnknown:
            break;
    }
#endif

    return false;
}

/** Write the contents of a memory location.
 * Also writes meta information about the data.
 *
 * @param writer The writer.
 *
 * @param key The object key, if needed.
 *
 * @param address The memory address.
 *
 * @param limit How many more subreferenced objects to write, if any.
 */
static void writeMemoryContents(const FYCrashReportWriter* const writer,
                                const char* const key,
                                const uintptr_t address,
                                int* limit)
{
    (*limit)--;
    const void* object = (const void*)address;
    writer->beginObject(writer, key);
    {
        writer->addUIntegerElement(writer, FYCrashField_Address, address);
        writeZombieIfPresent(writer, FYCrashField_LastDeallocObject, address);
        if(!writeObjCObject(writer, address, limit))
        {
            if(object == NULL)
            {
                writer->addStringElement(writer, FYCrashField_Type, FYCrashMemType_NullPointer);
            }
            else if(isValidString(object))
            {
                writer->addStringElement(writer, FYCrashField_Type, FYCrashMemType_String);
                writer->addStringElement(writer, FYCrashField_Value, (const char*)object);
            }
            else
            {
                writer->addStringElement(writer, FYCrashField_Type, FYCrashMemType_Unknown);
            }
        }
    }
    writer->endContainer(writer);
}

static bool isValidPointer(const uintptr_t address)
{
    if(address == (uintptr_t)NULL)
    {
        return false;
    }

#if FYCRASH_HAS_OBJC
    if(fyobjc_isTaggedPointer((const void*)address))
    {
        if(!fyobjc_isValidTaggedPointer((const void*)address))
        {
            return false;
        }
    }
#endif

    return true;
}

static bool isNotableAddress(const uintptr_t address)
{
    if(!isValidPointer(address))
    {
        return false;
    }
    
    const void* object = (const void*)address;

#if FYCRASH_HAS_OBJC
    if(fyzombie_className(object) != NULL)
    {
        return true;
    }

    if(fyobjc_objectType(object) != FYObjCTypeUnknown)
    {
        return true;
    }
#endif

    if(isValidString(object))
    {
        return true;
    }

    return false;
}

/** Write the contents of a memory location only if it contains notable data.
 * Also writes meta information about the data.
 *
 * @param writer The writer.
 *
 * @param key The object key, if needed.
 *
 * @param address The memory address.
 */
static void writeMemoryContentsIfNotable(const FYCrashReportWriter* const writer,
                                         const char* const key,
                                         const uintptr_t address)
{
    if(isNotableAddress(address))
    {
        int limit = kDefaultMemorySearchDepth;
        writeMemoryContents(writer, key, address, &limit);
    }
}

/** Look for a hex value in a string and try to write whatever it references.
 *
 * @param writer The writer.
 *
 * @param key The object key, if needed.
 *
 * @param string The string to search.
 */
static void writeAddressReferencedByString(const FYCrashReportWriter* const writer,
                                           const char* const key,
                                           const char* string)
{
    uint64_t address = 0;
    if(string == NULL || !fystring_extractHexValue(string, (int)strlen(string), &address))
    {
        return;
    }
    
    int limit = kDefaultMemorySearchDepth;
    writeMemoryContents(writer, key, (uintptr_t)address, &limit);
}

#pragma mark Backtrace

/** Write a backtrace to the report.
 *
 * @param writer The writer to write the backtrace to.
 *
 * @param key The object key, if needed.
 *
 * @param stackCursor The stack cursor to read from.
 */
static void writeBacktrace(const FYCrashReportWriter* const writer,
                           const char* const key,
                           FYStackCursor* stackCursor)
{
    writer->beginObject(writer, key);
    {
        writer->beginArray(writer, FYCrashField_Contents);
        {
            while(stackCursor->advanceCursor(stackCursor))
            {
                writer->beginObject(writer, NULL);
                {
                    if(stackCursor->symbolicate(stackCursor))
                    {
                        if(stackCursor->stackEntry.imageName != NULL)
                        {
                            writer->addStringElement(writer, FYCrashField_ObjectName, fyfu_lastPathEntry(stackCursor->stackEntry.imageName));
                        }
                        writer->addUIntegerElement(writer, FYCrashField_ObjectAddr, stackCursor->stackEntry.imageAddress);
                        if(stackCursor->stackEntry.symbolName != NULL)
                        {
                            writer->addStringElement(writer, FYCrashField_SymbolName, stackCursor->stackEntry.symbolName);
                        }
                        writer->addUIntegerElement(writer, FYCrashField_SymbolAddr, stackCursor->stackEntry.symbolAddress);
                    }
                    writer->addUIntegerElement(writer, FYCrashField_InstructionAddr, stackCursor->stackEntry.address);
                }
                writer->endContainer(writer);
            }
        }
        writer->endContainer(writer);
        writer->addIntegerElement(writer, FYCrashField_Skipped, 0);
    }
    writer->endContainer(writer);
}
                              

#pragma mark Stack

/** Write a dump of the stack contents to the report.
 *
 * @param writer The writer.
 *
 * @param key The object key, if needed.
 *
 * @param machineContext The context to retrieve the stack from.
 *
 * @param isStackOverflow If true, the stack has overflowed.
 */
static void writeStackContents(const FYCrashReportWriter* const writer,
                               const char* const key,
                               const struct FYMachineContext* const machineContext,
                               const bool isStackOverflow)
{
    uintptr_t sp = fycpu_stackPointer(machineContext);
    if((void*)sp == NULL)
    {
        return;
    }

    uintptr_t lowAddress = sp + (uintptr_t)(kStackContentsPushedDistance * (int)sizeof(sp) * fycpu_stackGrowDirection() * -1);
    uintptr_t highAddress = sp + (uintptr_t)(kStackContentsPoppedDistance * (int)sizeof(sp) * fycpu_stackGrowDirection());
    if(highAddress < lowAddress)
    {
        uintptr_t tmp = lowAddress;
        lowAddress = highAddress;
        highAddress = tmp;
    }
    writer->beginObject(writer, key);
    {
        writer->addStringElement(writer, FYCrashField_GrowDirection, fycpu_stackGrowDirection() > 0 ? "+" : "-");
        writer->addUIntegerElement(writer, FYCrashField_DumpStart, lowAddress);
        writer->addUIntegerElement(writer, FYCrashField_DumpEnd, highAddress);
        writer->addUIntegerElement(writer, FYCrashField_StackPtr, sp);
        writer->addBooleanElement(writer, FYCrashField_Overflow, isStackOverflow);
        uint8_t stackBuffer[kStackContentsTotalDistance * sizeof(sp)];
        int copyLength = (int)(highAddress - lowAddress);
        if(fymem_copySafely((void*)lowAddress, stackBuffer, copyLength))
        {
            writer->addDataElement(writer, FYCrashField_Contents, (void*)stackBuffer, copyLength);
        }
        else
        {
            writer->addStringElement(writer, FYCrashField_Error, "Stack contents not accessible");
        }
    }
    writer->endContainer(writer);
}

/** Write any notable addresses near the stack pointer (above and below).
 *
 * @param writer The writer.
 *
 * @param machineContext The context to retrieve the stack from.
 *
 * @param backDistance The distance towards the beginning of the stack to check.
 *
 * @param forwardDistance The distance past the end of the stack to check.
 */
static void writeNotableStackContents(const FYCrashReportWriter* const writer,
                                      const struct FYMachineContext* const machineContext,
                                      const int backDistance,
                                      const int forwardDistance)
{
    uintptr_t sp = fycpu_stackPointer(machineContext);
    if((void*)sp == NULL)
    {
        return;
    }

    uintptr_t lowAddress = sp + (uintptr_t)(backDistance * (int)sizeof(sp) * fycpu_stackGrowDirection() * -1);
    uintptr_t highAddress = sp + (uintptr_t)(forwardDistance * (int)sizeof(sp) * fycpu_stackGrowDirection());
    if(highAddress < lowAddress)
    {
        uintptr_t tmp = lowAddress;
        lowAddress = highAddress;
        highAddress = tmp;
    }
    uintptr_t contentsAsPointer;
    char nameBuffer[40];
    for(uintptr_t address = lowAddress; address < highAddress; address += sizeof(address))
    {
        if(fymem_copySafely((void*)address, &contentsAsPointer, sizeof(contentsAsPointer)))
        {
            sprintf(nameBuffer, "stack@%p", (void*)address);
            writeMemoryContentsIfNotable(writer, nameBuffer, contentsAsPointer);
        }
    }
}


#pragma mark Registers

/** Write the contents of all regular registers to the report.
 *
 * @param writer The writer.
 *
 * @param key The object key, if needed.
 *
 * @param machineContext The context to retrieve the registers from.
 */
static void writeBasicRegisters(const FYCrashReportWriter* const writer,
                                const char* const key,
                                const struct FYMachineContext* const machineContext)
{
    char registerNameBuff[30];
    const char* registerName;
    writer->beginObject(writer, key);
    {
        const int numRegisters = fycpu_numRegisters();
        for(int reg = 0; reg < numRegisters; reg++)
        {
            registerName = fycpu_registerName(reg);
            if(registerName == NULL)
            {
                snprintf(registerNameBuff, sizeof(registerNameBuff), "r%d", reg);
                registerName = registerNameBuff;
            }
            writer->addUIntegerElement(writer, registerName,
                                       fycpu_registerValue(machineContext, reg));
        }
    }
    writer->endContainer(writer);
}

/** Write the contents of all exception registers to the report.
 *
 * @param writer The writer.
 *
 * @param key The object key, if needed.
 *
 * @param machineContext The context to retrieve the registers from.
 */
static void writeExceptionRegisters(const FYCrashReportWriter* const writer,
                                    const char* const key,
                                    const struct FYMachineContext* const machineContext)
{
    char registerNameBuff[30];
    const char* registerName;
    writer->beginObject(writer, key);
    {
        const int numRegisters = fycpu_numExceptionRegisters();
        for(int reg = 0; reg < numRegisters; reg++)
        {
            registerName = fycpu_exceptionRegisterName(reg);
            if(registerName == NULL)
            {
                snprintf(registerNameBuff, sizeof(registerNameBuff), "r%d", reg);
                registerName = registerNameBuff;
            }
            writer->addUIntegerElement(writer,registerName,
                                       fycpu_exceptionRegisterValue(machineContext, reg));
        }
    }
    writer->endContainer(writer);
}

/** Write all applicable registers.
 *
 * @param writer The writer.
 *
 * @param key The object key, if needed.
 *
 * @param machineContext The context to retrieve the registers from.
 */
static void writeRegisters(const FYCrashReportWriter* const writer,
                           const char* const key,
                           const struct FYMachineContext* const machineContext)
{
    writer->beginObject(writer, key);
    {
        writeBasicRegisters(writer, FYCrashField_Basic, machineContext);
        if(fymc_hasValidExceptionRegisters(machineContext))
        {
            writeExceptionRegisters(writer, FYCrashField_Exception, machineContext);
        }
    }
    writer->endContainer(writer);
}

/** Write any notable addresses contained in the CPU registers.
 *
 * @param writer The writer.
 *
 * @param machineContext The context to retrieve the registers from.
 */
static void writeNotableRegisters(const FYCrashReportWriter* const writer,
                                  const struct FYMachineContext* const machineContext)
{
    char registerNameBuff[30];
    const char* registerName;
    const int numRegisters = fycpu_numRegisters();
    for(int reg = 0; reg < numRegisters; reg++)
    {
        registerName = fycpu_registerName(reg);
        if(registerName == NULL)
        {
            snprintf(registerNameBuff, sizeof(registerNameBuff), "r%d", reg);
            registerName = registerNameBuff;
        }
        writeMemoryContentsIfNotable(writer,
                                     registerName,
                                     (uintptr_t)fycpu_registerValue(machineContext, reg));
    }
}

#pragma mark Thread-specific

/** Write any notable addresses in the stack or registers to the report.
 *
 * @param writer The writer.
 *
 * @param key The object key, if needed.
 *
 * @param machineContext The context to retrieve the registers from.
 */
static void writeNotableAddresses(const FYCrashReportWriter* const writer,
                                  const char* const key,
                                  const struct FYMachineContext* const machineContext)
{
    writer->beginObject(writer, key);
    {
        writeNotableRegisters(writer, machineContext);
        writeNotableStackContents(writer,
                                  machineContext,
                                  kStackNotableSearchBackDistance,
                                  kStackNotableSearchForwardDistance);
    }
    writer->endContainer(writer);
}

/** Write information about a thread to the report.
 *
 * @param writer The writer.
 *
 * @param key The object key, if needed.
 *
 * @param crash The crash handler context.
 *
 * @param machineContext The context whose thread to write about.
 *
 * @param shouldWriteNotableAddresses If true, write any notable addresses found.
 */
static void writeThread(const FYCrashReportWriter* const writer,
                        const char* const key,
                        const FYCrash_MonitorContext* const crash,
                        const struct FYMachineContext* const machineContext,
                        const int threadIndex,
                        const bool shouldWriteNotableAddresses)
{
    bool isCrashedThread = fymc_isCrashedContext(machineContext);
    FYThread thread = fymc_getThreadFromContext(machineContext);
    FYLOG_DEBUG("Writing thread %x (index %d). is crashed: %d", thread, threadIndex, isCrashedThread);

    FYStackCursor stackCursor;
    bool hasBacktrace = getStackCursor(crash, machineContext, &stackCursor);

    writer->beginObject(writer, key);
    {
        if(hasBacktrace)
        {
            writeBacktrace(writer, FYCrashField_Backtrace, &stackCursor);
        }
        if(fymc_canHaveCPUState(machineContext))
        {
            writeRegisters(writer, FYCrashField_Registers, machineContext);
        }
        writer->addIntegerElement(writer, FYCrashField_Index, threadIndex);
        const char* name = fyccd_getThreadName(thread);
        if(name != NULL)
        {
            writer->addStringElement(writer, FYCrashField_Name, name);
        }
        name = fyccd_getQueueName(thread);
        if(name != NULL)
        {
            writer->addStringElement(writer, FYCrashField_DispatchQueue, name);
        }
        writer->addBooleanElement(writer, FYCrashField_Crashed, isCrashedThread);
        writer->addBooleanElement(writer, FYCrashField_CurrentThread, thread == fythread_self());
        if(isCrashedThread)
        {
            writeStackContents(writer, FYCrashField_Stack, machineContext, stackCursor.state.hasGivenUp);
            if(shouldWriteNotableAddresses)
            {
                writeNotableAddresses(writer, FYCrashField_NotableAddresses, machineContext);
            }
        }
    }
    writer->endContainer(writer);
}

/** Write information about all threads to the report.
 *
 * @param writer The writer.
 *
 * @param key The object key, if needed.
 *
 * @param crash The crash handler context.
 */
static void writeAllThreads(const FYCrashReportWriter* const writer,
                            const char* const key,
                            const FYCrash_MonitorContext* const crash,
                            bool writeNotableAddresses)
{
    const struct FYMachineContext* const context = crash->offendingMachineContext;
    FYThread offendingThread = fymc_getThreadFromContext(context);
    int threadCount = fymc_getThreadCount(context);
    FYMC_NEW_CONTEXT(machineContext);

    // Fetch info for all threads.
    writer->beginArray(writer, key);
    {
        FYLOG_DEBUG("Writing %d threads.", threadCount);
        for(int i = 0; i < threadCount; i++)
        {
            FYThread thread = fymc_getThreadAtIndex(context, i);
            if(thread == offendingThread)
            {
                writeThread(writer, NULL, crash, context, i, writeNotableAddresses);
            }
            else
            {
                fymc_getContextForThread(thread, machineContext, false);
                writeThread(writer, NULL, crash, machineContext, i, writeNotableAddresses);
            }
        }
    }
    writer->endContainer(writer);
}

#pragma mark Global Report Data

/** Write information about a binary image to the report.
 *
 * @param writer The writer.
 *
 * @param key The object key, if needed.
 *
 * @param index Which image to write about.
 */
static void writeBinaryImage(const FYCrashReportWriter* const writer,
                             const char* const key,
                             const int index)
{
    FYBinaryImage image = {0};
    if(!fydl_getBinaryImage(index, &image))
    {
        return;
    }

    writer->beginObject(writer, key);
    {
        writer->addUIntegerElement(writer, FYCrashField_ImageAddress, image.address);
        writer->addUIntegerElement(writer, FYCrashField_ImageVmAddress, image.vmAddress);
        writer->addUIntegerElement(writer, FYCrashField_ImageSize, image.size);
        writer->addStringElement(writer, FYCrashField_Name, image.name);
        writer->addUUIDElement(writer, FYCrashField_UUID, image.uuid);
        writer->addIntegerElement(writer, FYCrashField_CPUType, image.cpuType);
        writer->addIntegerElement(writer, FYCrashField_CPUSubType, image.cpuSubType);
        writer->addUIntegerElement(writer, FYCrashField_ImageMajorVersion, image.majorVersion);
        writer->addUIntegerElement(writer, FYCrashField_ImageMinorVersion, image.minorVersion);
        writer->addUIntegerElement(writer, FYCrashField_ImageRevisionVersion, image.revisionVersion);
    }
    writer->endContainer(writer);
}

/** Write information about all images to the report.
 *
 * @param writer The writer.
 *
 * @param key The object key, if needed.
 */
static void writeBinaryImages(const FYCrashReportWriter* const writer, const char* const key)
{
    const int imageCount = fydl_imageCount();

    writer->beginArray(writer, key);
    {
        for(int iImg = 0; iImg < imageCount; iImg++)
        {
            writeBinaryImage(writer, NULL, iImg);
        }
    }
    writer->endContainer(writer);
}

/** Write information about system memory to the report.
 *
 * @param writer The writer.
 *
 * @param key The object key, if needed.
 */
static void writeMemoryInfo(const FYCrashReportWriter* const writer,
                            const char* const key,
                            const FYCrash_MonitorContext* const monitorContext)
{
    writer->beginObject(writer, key);
    {
        writer->addUIntegerElement(writer, FYCrashField_Size, monitorContext->System.memorySize);
        writer->addUIntegerElement(writer, FYCrashField_Usable, monitorContext->System.usableMemory);
        writer->addUIntegerElement(writer, FYCrashField_Free, monitorContext->System.freeMemory);
    }
    writer->endContainer(writer);
}

/** Write information about the error leading to the crash to the report.
 *
 * @param writer The writer.
 *
 * @param key The object key, if needed.
 *
 * @param crash The crash handler context.
 */
static void writeError(const FYCrashReportWriter* const writer,
                       const char* const key,
                       const FYCrash_MonitorContext* const crash)
{
    writer->beginObject(writer, key);
    {
#if FYCRASH_HOST_APPLE
        writer->beginObject(writer, FYCrashField_Mach);
        {
            const char* machExceptionName = fymach_exceptionName(crash->mach.type);
            const char* machCodeName = crash->mach.code == 0 ? NULL : fymach_kernelReturnCodeName(crash->mach.code);
            writer->addUIntegerElement(writer, FYCrashField_Exception, (unsigned)crash->mach.type);
            if(machExceptionName != NULL)
            {
                writer->addStringElement(writer, FYCrashField_ExceptionName, machExceptionName);
            }
            writer->addUIntegerElement(writer, FYCrashField_Code, (unsigned)crash->mach.code);
            if(machCodeName != NULL)
            {
                writer->addStringElement(writer, FYCrashField_CodeName, machCodeName);
            }
            writer->addUIntegerElement(writer, FYCrashField_Subcode, (unsigned)crash->mach.subcode);
        }
        writer->endContainer(writer);
#endif
        writer->beginObject(writer, FYCrashField_Signal);
        {
            const char* sigName = fysignal_signalName(crash->signal.signum);
            const char* sigCodeName = fysignal_signalCodeName(crash->signal.signum, crash->signal.sigcode);
            writer->addUIntegerElement(writer, FYCrashField_Signal, (unsigned)crash->signal.signum);
            if(sigName != NULL)
            {
                writer->addStringElement(writer, FYCrashField_Name, sigName);
            }
            writer->addUIntegerElement(writer, FYCrashField_Code, (unsigned)crash->signal.sigcode);
            if(sigCodeName != NULL)
            {
                writer->addStringElement(writer, FYCrashField_CodeName, sigCodeName);
            }
        }
        writer->endContainer(writer);

        writer->addUIntegerElement(writer, FYCrashField_Address, crash->faultAddress);
        if(crash->crashReason != NULL)
        {
            writer->addStringElement(writer, FYCrashField_Reason, crash->crashReason);
        }

        // Gather specific info.
        switch(crash->crashType)
        {
            case FYCrashMonitorTypeMainThreadDeadlock:
                writer->addStringElement(writer, FYCrashField_Type, FYCrashExcType_Deadlock);
                break;
                
            case FYCrashMonitorTypeMachException:
                writer->addStringElement(writer, FYCrashField_Type, FYCrashExcType_Mach);
                break;

            case FYCrashMonitorTypeCPPException:
            {
                writer->addStringElement(writer, FYCrashField_Type, FYCrashExcType_CPPException);
                writer->beginObject(writer, FYCrashField_CPPException);
                {
                    writer->addStringElement(writer, FYCrashField_Name, crash->CPPException.name);
                }
                writer->endContainer(writer);
                break;
            }
            case FYCrashMonitorTypeNSException:
            {
                writer->addStringElement(writer, FYCrashField_Type, FYCrashExcType_NSException);
                writer->beginObject(writer, FYCrashField_NSException);
                {
                    writer->addStringElement(writer, FYCrashField_Name, crash->NSException.name);
                    writer->addStringElement(writer, FYCrashField_UserInfo, crash->NSException.userInfo);
                    writeAddressReferencedByString(writer, FYCrashField_ReferencedObject, crash->crashReason);
                }
                writer->endContainer(writer);
                break;
            }
            case FYCrashMonitorTypeSignal:
                writer->addStringElement(writer, FYCrashField_Type, FYCrashExcType_Signal);
                break;

            case FYCrashMonitorTypeUserReported:
            {
                writer->addStringElement(writer, FYCrashField_Type, FYCrashExcType_User);
                writer->beginObject(writer, FYCrashField_UserReported);
                {
                    writer->addStringElement(writer, FYCrashField_Name, crash->userException.name);
                    if(crash->userException.language != NULL)
                    {
                        writer->addStringElement(writer, FYCrashField_Language, crash->userException.language);
                    }
                    if(crash->userException.lineOfCode != NULL)
                    {
                        writer->addStringElement(writer, FYCrashField_LineOfCode, crash->userException.lineOfCode);
                    }
                    if(crash->userException.customStackTrace != NULL)
                    {
                        writer->addJSONElement(writer, FYCrashField_Backtrace, crash->userException.customStackTrace, true);
                    }
                }
                writer->endContainer(writer);
                break;
            }
            case FYCrashMonitorTypeSystem:
            case FYCrashMonitorTypeApplicationState:
            case FYCrashMonitorTypeZombie:
                FYLOG_ERROR("Crash monitor type 0x%x shouldn't be able to cause events!", crash->crashType);
                break;
        }
    }
    writer->endContainer(writer);
}

/** Write information about app runtime, etc to the report.
 *
 * @param writer The writer.
 *
 * @param key The object key, if needed.
 *
 * @param monitorContext The event monitor context.
 */
static void writeAppStats(const FYCrashReportWriter* const writer,
                          const char* const key,
                          const FYCrash_MonitorContext* const monitorContext)
{
    writer->beginObject(writer, key);
    {
        writer->addBooleanElement(writer, FYCrashField_AppActive, monitorContext->AppState.applicationIsActive);
        writer->addBooleanElement(writer, FYCrashField_AppInFG, monitorContext->AppState.applicationIsInForeground);

        writer->addIntegerElement(writer, FYCrashField_LaunchesSinceCrash, monitorContext->AppState.launchesSinceLastCrash);
        writer->addIntegerElement(writer, FYCrashField_SessionsSinceCrash, monitorContext->AppState.sessionsSinceLastCrash);
        writer->addFloatingPointElement(writer, FYCrashField_ActiveTimeSinceCrash, monitorContext->AppState.activeDurationSinceLastCrash);
        writer->addFloatingPointElement(writer, FYCrashField_BGTimeSinceCrash, monitorContext->AppState.backgroundDurationSinceLastCrash);

        writer->addIntegerElement(writer, FYCrashField_SessionsSinceLaunch, monitorContext->AppState.sessionsSinceLaunch);
        writer->addFloatingPointElement(writer, FYCrashField_ActiveTimeSinceLaunch, monitorContext->AppState.activeDurationSinceLaunch);
        writer->addFloatingPointElement(writer, FYCrashField_BGTimeSinceLaunch, monitorContext->AppState.backgroundDurationSinceLaunch);
    }
    writer->endContainer(writer);
}

/** Write information about this process.
 *
 * @param writer The writer.
 *
 * @param key The object key, if needed.
 */
static void writeProcessState(const FYCrashReportWriter* const writer,
                              const char* const key,
                              const FYCrash_MonitorContext* const monitorContext)
{
    writer->beginObject(writer, key);
    {
        if(monitorContext->ZombieException.address != 0)
        {
            writer->beginObject(writer, FYCrashField_LastDeallocedNSException);
            {
                writer->addUIntegerElement(writer, FYCrashField_Address, monitorContext->ZombieException.address);
                writer->addStringElement(writer, FYCrashField_Name, monitorContext->ZombieException.name);
                writer->addStringElement(writer, FYCrashField_Reason, monitorContext->ZombieException.reason);
                writeAddressReferencedByString(writer, FYCrashField_ReferencedObject, monitorContext->ZombieException.reason);
            }
            writer->endContainer(writer);
        }
    }
    writer->endContainer(writer);
}

/** Write basic report information.
 *
 * @param writer The writer.
 *
 * @param key The object key, if needed.
 *
 * @param type The report type.
 *
 * @param reportID The report ID.
 */
static void writeReportInfo(const FYCrashReportWriter* const writer,
                            const char* const key,
                            const char* const type,
                            const char* const reportID,
                            const char* const processName)
{
    writer->beginObject(writer, key);
    {
        writer->addStringElement(writer, FYCrashField_Version, FYCRASH_REPORT_VERSION);
        writer->addStringElement(writer, FYCrashField_ID, reportID);
        writer->addStringElement(writer, FYCrashField_ProcessName, processName);
        writer->addIntegerElement(writer, FYCrashField_Timestamp, time(NULL));
        writer->addStringElement(writer, FYCrashField_Type, type);
    }
    writer->endContainer(writer);
}

static void writeRecrash(const FYCrashReportWriter* const writer,
                         const char* const key,
                         const char* crashReportPath)
{
    writer->addJSONFileElement(writer, key, crashReportPath, true);
}


#pragma mark Setup

/** Prepare a report writer for use.
 *
 * @oaram writer The writer to prepare.
 *
 * @param context JSON writer contextual information.
 */
static void prepareReportWriter(FYCrashReportWriter* const writer, FYJSONEncodeContext* const context)
{
    writer->addBooleanElement = addBooleanElement;
    writer->addFloatingPointElement = addFloatingPointElement;
    writer->addIntegerElement = addIntegerElement;
    writer->addUIntegerElement = addUIntegerElement;
    writer->addStringElement = addStringElement;
    writer->addTextFileElement = addTextFileElement;
    writer->addTextFileLinesElement = addTextLinesFromFile;
    writer->addJSONFileElement = addJSONElementFromFile;
    writer->addDataElement = addDataElement;
    writer->beginDataElement = beginDataElement;
    writer->appendDataElement = appendDataElement;
    writer->endDataElement = endDataElement;
    writer->addUUIDElement = addUUIDElement;
    writer->addJSONElement = addJSONElement;
    writer->beginObject = beginObject;
    writer->beginArray = beginArray;
    writer->endContainer = endContainer;
    writer->context = context;
}


// ============================================================================
#pragma mark - Main API -
// ============================================================================

void fycrashreport_writeRecrashReport(const FYCrash_MonitorContext* const monitorContext, const char* const path)
{
    char writeBuffer[1024];
    FYBufferedWriter bufferedWriter;
    static char tempPath[FYFU_MAX_PATH_LENGTH];
    strncpy(tempPath, path, sizeof(tempPath) - 10);
    strncpy(tempPath + strlen(tempPath) - 5, ".old", 5);
    FYLOG_INFO("Writing recrash report to %s", path);

    if(rename(path, tempPath) < 0)
    {
        FYLOG_ERROR("Could not rename %s to %s: %s", path, tempPath, strerror(errno));
    }
    if(!fyfu_openBufferedWriter(&bufferedWriter, path, writeBuffer, sizeof(writeBuffer)))
    {
        return;
    }

    fyccd_freeze();

    FYJSONEncodeContext jsonContext;
    jsonContext.userData = &bufferedWriter;
    FYCrashReportWriter concreteWriter;
    FYCrashReportWriter* writer = &concreteWriter;
    prepareReportWriter(writer, &jsonContext);

    fyjson_beginEncode(getJsonContext(writer), true, addJSONData, &bufferedWriter);

    writer->beginObject(writer, FYCrashField_Report);
    {
        writeRecrash(writer, FYCrashField_RecrashReport, tempPath);
        fyfu_flushBufferedWriter(&bufferedWriter);
        if(remove(tempPath) < 0)
        {
            FYLOG_ERROR("Could not remove %s: %s", tempPath, strerror(errno));
        }
        writeReportInfo(writer,
                        FYCrashField_Report,
                        FYCrashReportType_Minimal,
                        monitorContext->eventID,
                        monitorContext->System.processName);
        fyfu_flushBufferedWriter(&bufferedWriter);

        writer->beginObject(writer, FYCrashField_Crash);
        {
            writeError(writer, FYCrashField_Error, monitorContext);
            fyfu_flushBufferedWriter(&bufferedWriter);
            int threadIndex = fymc_indexOfThread(monitorContext->offendingMachineContext,
                                                 fymc_getThreadFromContext(monitorContext->offendingMachineContext));
            writeThread(writer,
                        FYCrashField_CrashedThread,
                        monitorContext,
                        monitorContext->offendingMachineContext,
                        threadIndex,
                        false);
            fyfu_flushBufferedWriter(&bufferedWriter);
        }
        writer->endContainer(writer);
    }
    writer->endContainer(writer);

    fyjson_endEncode(getJsonContext(writer));
    fyfu_closeBufferedWriter(&bufferedWriter);
    fyccd_unfreeze();
}

static void writeSystemInfo(const FYCrashReportWriter* const writer,
                            const char* const key,
                            const FYCrash_MonitorContext* const monitorContext)
{
    writer->beginObject(writer, key);
    {
        writer->addStringElement(writer, FYCrashField_SystemName, monitorContext->System.systemName);
        writer->addStringElement(writer, FYCrashField_SystemVersion, monitorContext->System.systemVersion);
        writer->addStringElement(writer, FYCrashField_Machine, monitorContext->System.machine);
        writer->addStringElement(writer, FYCrashField_Model, monitorContext->System.model);
        writer->addStringElement(writer, FYCrashField_KernelVersion, monitorContext->System.kernelVersion);
        writer->addStringElement(writer, FYCrashField_OSVersion, monitorContext->System.osVersion);
        writer->addBooleanElement(writer, FYCrashField_Jailbroken, monitorContext->System.isJailbroken);
        writer->addStringElement(writer, FYCrashField_BootTime, monitorContext->System.bootTime);
        writer->addStringElement(writer, FYCrashField_AppStartTime, monitorContext->System.appStartTime);
        writer->addStringElement(writer, FYCrashField_ExecutablePath, monitorContext->System.executablePath);
        writer->addStringElement(writer, FYCrashField_Executable, monitorContext->System.executableName);
        writer->addStringElement(writer, FYCrashField_BundleID, monitorContext->System.bundleID);
        writer->addStringElement(writer, FYCrashField_BundleName, monitorContext->System.bundleName);
        writer->addStringElement(writer, FYCrashField_BundleVersion, monitorContext->System.bundleVersion);
        writer->addStringElement(writer, FYCrashField_BundleShortVersion, monitorContext->System.bundleShortVersion);
        writer->addStringElement(writer, FYCrashField_AppUUID, monitorContext->System.appID);
        writer->addStringElement(writer, FYCrashField_CPUArch, monitorContext->System.cpuArchitecture);
        writer->addIntegerElement(writer, FYCrashField_CPUType, monitorContext->System.cpuType);
        writer->addIntegerElement(writer, FYCrashField_CPUSubType, monitorContext->System.cpuSubType);
        writer->addIntegerElement(writer, FYCrashField_BinaryCPUType, monitorContext->System.binaryCPUType);
        writer->addIntegerElement(writer, FYCrashField_BinaryCPUSubType, monitorContext->System.binaryCPUSubType);
        writer->addStringElement(writer, FYCrashField_TimeZone, monitorContext->System.timezone);
        writer->addStringElement(writer, FYCrashField_ProcessName, monitorContext->System.processName);
        writer->addIntegerElement(writer, FYCrashField_ProcessID, monitorContext->System.processID);
        writer->addIntegerElement(writer, FYCrashField_ParentProcessID, monitorContext->System.parentProcessID);
        writer->addStringElement(writer, FYCrashField_DeviceAppHash, monitorContext->System.deviceAppHash);
        writer->addStringElement(writer, FYCrashField_BuildType, monitorContext->System.buildType);
        writer->addIntegerElement(writer, FYCrashField_Storage, (int64_t)monitorContext->System.storageSize);

        writeMemoryInfo(writer, FYCrashField_Memory, monitorContext);
        writeAppStats(writer, FYCrashField_AppStats, monitorContext);
    }
    writer->endContainer(writer);

}

static void writeDebugInfo(const FYCrashReportWriter* const writer,
                            const char* const key,
                            const FYCrash_MonitorContext* const monitorContext)
{
    writer->beginObject(writer, key);
    {
        if(monitorContext->consoleLogPath != NULL)
        {
            addTextLinesFromFile(writer, FYCrashField_ConsoleLog, monitorContext->consoleLogPath);
        }
    }
    writer->endContainer(writer);
    
}

void fycrashreport_writeStandardReport(const FYCrash_MonitorContext* const monitorContext, const char* const path)
{
    FYLOG_INFO("Writing crash report to %s", path);
    char writeBuffer[1024];
    FYBufferedWriter bufferedWriter;

    if(!fyfu_openBufferedWriter(&bufferedWriter, path, writeBuffer, sizeof(writeBuffer)))
    {
        return;
    }

    fyccd_freeze();
    
    FYJSONEncodeContext jsonContext;
    jsonContext.userData = &bufferedWriter;
    FYCrashReportWriter concreteWriter;
    FYCrashReportWriter* writer = &concreteWriter;
    prepareReportWriter(writer, &jsonContext);

    fyjson_beginEncode(getJsonContext(writer), true, addJSONData, &bufferedWriter);

    writer->beginObject(writer, FYCrashField_Report);
    {
        writeReportInfo(writer,
                        FYCrashField_Report,
                        FYCrashReportType_Standard,
                        monitorContext->eventID,
                        monitorContext->System.processName);
        fyfu_flushBufferedWriter(&bufferedWriter);

        writeBinaryImages(writer, FYCrashField_BinaryImages);
        fyfu_flushBufferedWriter(&bufferedWriter);

        writeProcessState(writer, FYCrashField_ProcessState, monitorContext);
        fyfu_flushBufferedWriter(&bufferedWriter);

        writeSystemInfo(writer, FYCrashField_System, monitorContext);
        fyfu_flushBufferedWriter(&bufferedWriter);

        writer->beginObject(writer, FYCrashField_Crash);
        {
            writeError(writer, FYCrashField_Error, monitorContext);
            fyfu_flushBufferedWriter(&bufferedWriter);
            writeAllThreads(writer,
                            FYCrashField_Threads,
                            monitorContext,
                            g_introspectionRules.enabled);
            fyfu_flushBufferedWriter(&bufferedWriter);
        }
        writer->endContainer(writer);

        if(g_userInfoJSON != NULL)
        {
            addJSONElement(writer, FYCrashField_User, g_userInfoJSON, false);
            fyfu_flushBufferedWriter(&bufferedWriter);
        }
        else
        {
            writer->beginObject(writer, FYCrashField_User);
        }
        if(g_userSectionWriteCallback != NULL)
        {
            fyfu_flushBufferedWriter(&bufferedWriter);
            if (monitorContext->currentSnapshotUserReported == false) {
                g_userSectionWriteCallback(writer);
            }
        }
        writer->endContainer(writer);
        fyfu_flushBufferedWriter(&bufferedWriter);

        writeDebugInfo(writer, FYCrashField_Debug, monitorContext);
    }
    writer->endContainer(writer);
    
    fyjson_endEncode(getJsonContext(writer));
    fyfu_closeBufferedWriter(&bufferedWriter);
    fyccd_unfreeze();
}



void fycrashreport_setUserInfoJSON(const char* const userInfoJSON)
{
    static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
    FYLOG_TRACE("set userInfoJSON to %p", userInfoJSON);

    pthread_mutex_lock(&mutex);
    if(g_userInfoJSON != NULL)
    {
        free((void*)g_userInfoJSON);
    }
    if(userInfoJSON == NULL)
    {
        g_userInfoJSON = NULL;
    }
    else
    {
        g_userInfoJSON = strdup(userInfoJSON);
    }
    pthread_mutex_unlock(&mutex);
}

void fycrashreport_setIntrospectMemory(bool shouldIntrospectMemory)
{
    g_introspectionRules.enabled = shouldIntrospectMemory;
}

void fycrashreport_setDoNotIntrospectClasses(const char** doNotIntrospectClasses, int length)
{
    const char** oldClasses = g_introspectionRules.restrictedClasses;
    int oldClassesLength = g_introspectionRules.restrictedClassesCount;
    const char** newClasses = NULL;
    int newClassesLength = 0;
    
    if(doNotIntrospectClasses != NULL && length > 0)
    {
        newClassesLength = length;
        newClasses = malloc(sizeof(*newClasses) * (unsigned)newClassesLength);
        if(newClasses == NULL)
        {
            FYLOG_ERROR("Could not allocate memory");
            return;
        }
        
        for(int i = 0; i < newClassesLength; i++)
        {
            newClasses[i] = strdup(doNotIntrospectClasses[i]);
        }
    }
    
    g_introspectionRules.restrictedClasses = newClasses;
    g_introspectionRules.restrictedClassesCount = newClassesLength;
    
    if(oldClasses != NULL)
    {
        for(int i = 0; i < oldClassesLength; i++)
        {
            free((void*)oldClasses[i]);
        }
        free(oldClasses);
    }
}

void fycrashreport_setUserSectionWriteCallback(const FYReportWriteCallback userSectionWriteCallback)
{
    FYLOG_TRACE("Set userSectionWriteCallback to %p", userSectionWriteCallback);
    g_userSectionWriteCallback = userSectionWriteCallback;
}
