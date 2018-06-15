//
//  FYCrashInstallation.m
//
//  Created by Karl Stenerud on 2013-02-10.
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


#import "FYCrashInstallation.h"
#import "FYCrashInstallation+Private.h"
#import "FYCrashReportFilterBasic.h"
#import "FYCrash.h"
#import "FYCString.h"
#import "FYJSONCodecObjC.h"
#import "FYLogger.h"
#import "NSError+SimpleConstructor.h"
#import <objc/runtime.h>


/** Max number of properties that can be defined for writing to the report */
#define kMaxProperties 500


typedef struct
{
    const char* key;
    const char* value;
} ReportField;

typedef struct
{
    FYReportWriteCallback userCrashCallback;
    int reportFieldsCount;
    ReportField* reportFields[0];
} CrashHandlerData;


static CrashHandlerData* g_crashHandlerData;


static void crashCallback(const FYCrashReportWriter* writer)
{
    for(int i = 0; i < g_crashHandlerData->reportFieldsCount; i++)
    {
        ReportField* field = g_crashHandlerData->reportFields[i];
        if(field->key != NULL && field->value != NULL)
        {
            writer->addJSONElement(writer, field->key, field->value, true);
        }
    }
    if(g_crashHandlerData->userCrashCallback != NULL)
    {
        g_crashHandlerData->userCrashCallback(writer);
    }
}


@interface FYCrashInstReportField: NSObject

@property(nonatomic,readonly,assign) int index;
@property(nonatomic,readonly,assign) ReportField* field;

@property(nonatomic,readwrite,retain) NSString* key;
@property(nonatomic,readwrite,retain) id value;

@property(nonatomic,readwrite,retain) NSMutableData* fieldBacking;
@property(nonatomic,readwrite,retain) FYCString* keyBacking;
@property(nonatomic,readwrite,retain) FYCString* valueBacking;

@end

@implementation FYCrashInstReportField

@synthesize index = _index;
@synthesize key = _key;
@synthesize value = _value;
@synthesize fieldBacking = _fieldBacking;
@synthesize keyBacking = _keyBacking;
@synthesize valueBacking= _valueBacking;

+ (FYCrashInstReportField*) fieldWithIndex:(int) index
{
    return [(FYCrashInstReportField*)[self alloc] initWithIndex:index];
}

- (id) initWithIndex:(int) index
{
    if((self = [super init]))
    {
        _index = index;
        self.fieldBacking = [NSMutableData dataWithLength:sizeof(*self.field)];
    }
    return self;
}

- (ReportField*) field
{
    return (ReportField*)self.fieldBacking.mutableBytes;
}

- (void) setKey:(NSString*) key
{
    _key = key;
    if(key == nil)
    {
        self.keyBacking = nil;
    }
    else
    {
        self.keyBacking = [FYCString stringWithString:key];
    }
    self.field->key = self.keyBacking.bytes;
}

- (void) setValue:(id) value
{
    if(value == nil)
    {
        _value = nil;
        self.valueBacking = nil;
        return;
    }
    
    NSError* error = nil;
    NSData* jsonData = [FYJSONCodec encode:value options:FYJSONEncodeOptionPretty | FYJSONEncodeOptionSorted error:&error];
    if(jsonData == nil)
    {
        FYLOG_ERROR(@"Could not set value %@ for property %@: %@", value, self.key, error);
    }
    else
    {
        _value = value;
        self.valueBacking = [FYCString stringWithData:jsonData];
        self.field->value = self.valueBacking.bytes;
    }
}

@end

@interface FYCrashInstallation ()

@property(nonatomic,readwrite,assign) int nextFieldIndex;
@property(nonatomic,readonly,assign) CrashHandlerData* crashHandlerData;
@property(nonatomic,readwrite,retain) NSMutableData* crashHandlerDataBacking;
@property(nonatomic,readwrite,retain) NSMutableDictionary* fields;
@property(nonatomic,readwrite,retain) NSArray* requiredProperties;
@property(nonatomic,readwrite,retain) FYCrashReportFilterPipeline* prependedFilters;

@end


@implementation FYCrashInstallation

@synthesize nextFieldIndex = _nextFieldIndex;
@synthesize crashHandlerDataBacking = _crashHandlerDataBacking;
@synthesize fields = _fields;
@synthesize requiredProperties = _requiredProperties;
@synthesize prependedFilters = _prependedFilters;

- (id) init
{
    [NSException raise:NSInternalInconsistencyException
                format:@"%@ does not support init. Subclasses must call initWithMaxReportFieldCount:requiredProperties:", [self class]];
    return nil;
}

- (id) initWithRequiredProperties:(NSArray*) requiredProperties
{
    if((self = [super init]))
    {
        self.crashHandlerDataBacking = [NSMutableData dataWithLength:sizeof(*self.crashHandlerData) +
                                        sizeof(*self.crashHandlerData->reportFields) * kMaxProperties];
        self.fields = [NSMutableDictionary dictionary];
        self.requiredProperties = requiredProperties;
        self.prependedFilters = [FYCrashReportFilterPipeline filterWithFilters:nil];
    }
    return self;
}

- (void) dealloc
{
    FYCrash* handler = [FYCrash sharedInstance];
    @synchronized(handler)
    {
        if(g_crashHandlerData == self.crashHandlerData)
        {
            g_crashHandlerData = NULL;
            handler.onCrash = NULL;
        }
    }
}

- (CrashHandlerData*) crashHandlerData
{
    return (CrashHandlerData*)self.crashHandlerDataBacking.mutableBytes;
}

- (FYCrashInstReportField*) reportFieldForProperty:(NSString*) propertyName
{
    FYCrashInstReportField* field = [self.fields objectForKey:propertyName];
    if(field == nil)
    {
        field = [FYCrashInstReportField fieldWithIndex:self.nextFieldIndex];
        self.nextFieldIndex++;
        self.crashHandlerData->reportFieldsCount = self.nextFieldIndex;
        self.crashHandlerData->reportFields[field.index] = field.field;
        [self.fields setObject:field forKey:propertyName];
    }
    return field;
}

- (void) reportFieldForProperty:(NSString*) propertyName setKey:(id) key
{
    FYCrashInstReportField* field = [self reportFieldForProperty:propertyName];
    field.key = key;
}

- (void) reportFieldForProperty:(NSString*) propertyName setValue:(id) value
{
    FYCrashInstReportField* field = [self reportFieldForProperty:propertyName];
    field.value = value;
}

- (NSError*) validateProperties
{
    NSMutableString* errors = [NSMutableString string];
    for(NSString* propertyName in self.requiredProperties)
    {
        NSString* nextError = nil;
        @try
        {
            id value = [self valueForKey:propertyName];
            if(value == nil)
            {
                nextError = @"is nil";
            }
        }
        @catch (NSException *exception)
        {
            nextError = @"property not found";
        }
        if(nextError != nil)
        {
            if([errors length] > 0)
            {
                [errors appendString:@", "];
            }
            [errors appendFormat:@"%@ (%@)", propertyName, nextError];
        }
    }
    if([errors length] > 0)
    {
        return [NSError errorWithDomain:[[self class] description]
                                   code:0
                            description:@"Installation properties failed validation: %@", errors];
    }
    return nil;
}

- (NSString*) makeKeyPath:(NSString*) keyPath
{
    if([keyPath length] == 0)
    {
        return keyPath;
    }
    BOOL isAbsoluteKeyPath = [keyPath length] > 0 && [keyPath characterAtIndex:0] == '/';
    return isAbsoluteKeyPath ? keyPath : [@"user/" stringByAppendingString:keyPath];
}

- (NSArray*) makeKeyPaths:(NSArray*) keyPaths
{
    if([keyPaths count] == 0)
    {
        return keyPaths;
    }
    NSMutableArray* result = [NSMutableArray arrayWithCapacity:[keyPaths count]];
    for(NSString* keyPath in keyPaths)
    {
        [result addObject:[self makeKeyPath:keyPath]];
    }
    return result;
}

- (FYReportWriteCallback) onCrash
{
    @synchronized(self)
    {
        return self.crashHandlerData->userCrashCallback;
    }
}

- (void) setOnCrash:(FYReportWriteCallback)onCrash
{
    @synchronized(self)
    {
        self.crashHandlerData->userCrashCallback = onCrash;
    }
}

- (void) install
{
    FYCrash* handler = [FYCrash sharedInstance];
    @synchronized(handler)
    {
        g_crashHandlerData = self.crashHandlerData;
        handler.onCrash = crashCallback;
        [handler install];
    }
}

- (void) sendAllReportsWithCompletion:(FYCrashReportFilterCompletion) onCompletion
{
    NSError* error = [self validateProperties];
    if(error != nil)
    {
        if(onCompletion != nil)
        {
            onCompletion(nil, NO, error);
        }
        return;
    }

    id<FYCrashReportFilter> sink = [self sink];
    if(sink == nil)
    {
        onCompletion(nil, NO, [NSError errorWithDomain:[[self class] description]
                                                  code:0
                                           description:@"Sink was nil (subclasses must implement method \"sink\")"]);
        return;
    }
    
    sink = [FYCrashReportFilterPipeline filterWithFilters:self.prependedFilters, sink, nil];

    FYCrash* handler = [FYCrash sharedInstance];
    handler.sink = sink;
    [handler sendAllReportsWithCompletion:onCompletion];
}

- (void) addPreFilter:(id<FYCrashReportFilter>) filter
{
    [self.prependedFilters addFilter:filter];
}

- (id<FYCrashReportFilter>) sink
{
    return nil;
}

@end
