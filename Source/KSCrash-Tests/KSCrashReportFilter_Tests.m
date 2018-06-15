//
//  FYCrashReportFilter_Tests.m
//
//  Created by Karl Stenerud on 2012-05-12.
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


#import <XCTest/XCTest.h>

#import "FYCrashReportFilter.h"
#import "FYCrashReportFilterBasic.h"
#import "FYCrashReportFilterGZip.h"
#import "FYCrashReportFilterJSON.h"
#import "NSData+GZip.h"
#import "NSError+SimpleConstructor.h"


@interface FYCrash_TestNilFilter: NSObject <FYCrashReportFilter>

@end

@implementation FYCrash_TestNilFilter

+ (FYCrash_TestNilFilter*) filter
{
    return [[self alloc] init];
}

- (void) filterReports:(__unused NSArray*) reports onCompletion:(FYCrashReportFilterCompletion) onCompletion
{
    onCompletion(nil, YES, nil);
}

@end


@interface FYCrash_TestFilter: NSObject <FYCrashReportFilter>

@property(nonatomic,readwrite,assign) NSTimeInterval delay;
@property(nonatomic,readwrite,assign) BOOL completed;
@property(nonatomic,readwrite,retain) NSError* error;
@property(nonatomic,readwrite,retain) NSTimer* timer;
@property(nonatomic,readwrite,retain) NSArray* reports;
@property(nonatomic,readwrite,copy) FYCrashReportFilterCompletion onCompletion;

@end

@implementation FYCrash_TestFilter

@synthesize delay = _delay;
@synthesize completed = _completed;
@synthesize error = _error;
@synthesize reports = _reports;
@synthesize timer = _timer;
@synthesize onCompletion = _onCompletion;

+ (FYCrash_TestFilter*) filterWithDelay:(NSTimeInterval) delay
                              completed:(BOOL) completed
                                  error:(NSError*) error
{
    return [[self alloc] initWithDelay:delay completed:completed error:error];
}

- (id) initWithDelay:(NSTimeInterval) delay
           completed:(BOOL) completed
               error:(NSError*) error
{
    if((self = [super init]))
    {
        self.delay = delay;
        self.completed = completed;
        self.error = error;
    }
    return self;
}

- (void) filterReports:(NSArray*) reports
          onCompletion:(FYCrashReportFilterCompletion) onCompletion
{
    self.reports = reports;
    self.onCompletion = onCompletion;
    if(self.delay > 0)
    {
        self.timer = [NSTimer timerWithTimeInterval:self.delay target:self selector:@selector(onTimeUp) userInfo:nil repeats:NO];
    }
    else
    {
        [self onTimeUp];
    }
}

- (void) onTimeUp
{
    fycrash_callCompletion(self.onCompletion, self.reports, self.completed, self.error);
}

@end


@interface FYCrashReportFilter_Tests : XCTestCase @end

@implementation FYCrashReportFilter_Tests

#if __has_feature(objc_arc)

- (void) testPassthroughLeak
{
    __block NSArray* reports = [NSArray arrayWithObject:@""];
    __weak id weakRef = reports;

    __block FYCrashReportFilterPassthrough* filter = [FYCrashReportFilterPassthrough filter];
    [filter filterReports:reports
             onCompletion:^(__unused NSArray* filteredReports,
                            __unused BOOL completed,
                            __unused NSError* error)
     {
         filter = nil;
         reports = nil;
         dispatch_async(dispatch_get_main_queue(), ^
                        {
                            XCTAssertNil(weakRef, @"Object leaked");
                        });
     }];
}

- (void) testPipeline
{
    NSArray* expected = [NSArray arrayWithObjects:@"1", @"2", @"3", nil];
    id<FYCrashReportFilter> filter = [FYCrashReportFilterPipeline filterWithFilters:
                                      [FYCrashReportFilterPassthrough filter],
                                      [FYCrashReportFilterPassthrough filter],
                                      nil];
    
    [filter filterReports:expected onCompletion:^(NSArray* filteredReports,
                                                  BOOL completed,
                                                  NSError* error)
     {
         XCTAssertTrue(completed, @"");
         XCTAssertNil(error, @"");
         XCTAssertEqualObjects(expected, filteredReports, @"");
     }];
}

- (void) testPipelineInit
{
    NSArray* expected = [NSArray arrayWithObjects:@"1", @"2", @"3", nil];
    id<FYCrashReportFilter> filter = [[FYCrashReportFilterPipeline alloc] initWithFilters:
                                      [FYCrashReportFilterPassthrough filter],
                                      [FYCrashReportFilterPassthrough filter],
                                      nil];
    filter = filter;
    
    [filter filterReports:expected onCompletion:^(NSArray* filteredReports,
                                                  BOOL completed,
                                                  NSError* error)
     {
         XCTAssertTrue(completed, @"");
         XCTAssertNil(error, @"");
         XCTAssertEqualObjects(expected, filteredReports, @"");
     }];
}

- (void) testPipelineNoFilters
{
    NSArray* expected = [NSArray arrayWithObjects:@"1", @"2", @"3", nil];
    id<FYCrashReportFilter> filter = [FYCrashReportFilterPipeline filterWithFilters:
                                      nil];
    
    [filter filterReports:expected onCompletion:^(NSArray* filteredReports,
                                                  BOOL completed,
                                                  NSError* error)
     {
         XCTAssertTrue(completed, @"");
         XCTAssertNil(error, @"");
         XCTAssertEqualObjects(expected, filteredReports, @"");
     }];
}

- (void) testFilterPipelineIncomplete
{
    NSArray* expected1 = [NSArray arrayWithObjects:@"1", @"2", @"3", nil];
    id<FYCrashReportFilter> filter = [FYCrashReportFilterPipeline filterWithFilters:
                                      [FYCrash_TestFilter filterWithDelay:0 completed:NO error:nil],
                                      nil];
    
    [filter filterReports:expected1 onCompletion:^(NSArray* filteredReports,
                                                   BOOL completed,
                                                   NSError* error)
     {
         XCTAssertNotNil(filteredReports, @"");
         XCTAssertFalse(completed, @"");
         XCTAssertNil(error, @"");
     }];
}

- (void) testFilterPipelineNilReports
{
    NSArray* expected1 = [NSArray arrayWithObjects:@"1", @"2", @"3", nil];
    id<FYCrashReportFilter> filter = [FYCrashReportFilterPipeline filterWithFilters:
                                      [FYCrash_TestNilFilter filter],
                                      nil];
    
    [filter filterReports:expected1 onCompletion:^(NSArray* filteredReports,
                                                   BOOL completed,
                                                   NSError* error)
     {
         XCTAssertNil(filteredReports, @"");
         XCTAssertFalse(completed, @"");
         XCTAssertNotNil(error, @"");
     }];
}

- (void) testPiplelineLeak1
{
    __block NSArray* reports = [NSArray arrayWithObjects:@"one", @"two", nil];
    __block id<FYCrashReportFilter> filter = [FYCrash_TestFilter filterWithDelay:0.1 completed:YES error:nil];

    __weak id weakReports = reports;
    __weak id weakFilter = filter;

    __block FYCrashReportFilterPipeline* pipeline = [FYCrashReportFilterPipeline filterWithFilters:filter, nil];
    [pipeline filterReports:reports
               onCompletion:^(__unused NSArray* filteredReports,
                              __unused BOOL completed,
                              __unused NSError* error)
     {
         reports = nil;
         filter = nil;
         pipeline = nil;
         XCTAssertTrue(completed, @"");
         dispatch_async(dispatch_get_main_queue(), ^
                        {
                            XCTAssertNil(weakReports, @"Object leaked");
                            XCTAssertNil(weakFilter, @"Object leaked");
                        });
     }];
}

- (void) testPiplelineLeak2
{
    __block NSArray* reports = [NSArray arrayWithObjects:@"one", @"two", nil];
    __block id<FYCrashReportFilter> filter = [FYCrash_TestFilter filterWithDelay:0.1 completed:NO error:nil];

    __weak id weakReports = reports;
    __weak id weakFilter = filter;

    __block FYCrashReportFilterPipeline* pipeline = [FYCrashReportFilterPipeline filterWithFilters:filter, nil];
    [pipeline filterReports:reports
               onCompletion:^(__unused NSArray* filteredReports,
                              __unused BOOL completed,
                              __unused NSError* error)
     {
         reports = nil;
         filter = nil;
         pipeline = nil;
         XCTAssertFalse(completed, @"");
         dispatch_async(dispatch_get_main_queue(), ^
                        {
                            XCTAssertNil(weakReports, @"Object leaked");
                            XCTAssertNil(weakFilter, @"Object leaked");
                        });
     }];
}

#endif

- (void) testFilterPassthrough
{
    NSArray* expected = [NSArray arrayWithObjects:@"1", @"2", @"3", nil];
    id<FYCrashReportFilter> filter = [FYCrashReportFilterPassthrough filter];

    [filter filterReports:expected onCompletion:^(NSArray* filteredReports,
                                                  BOOL completed,
                                                  NSError* error)
     {
         XCTAssertTrue(completed, @"");
         XCTAssertNil(error, @"");
         XCTAssertEqualObjects(expected, filteredReports, @"");
     }];
}

- (void) testFilterStringToData
{
    NSArray* source = [NSArray arrayWithObjects:@"1", @"2", @"3", nil];
    NSArray* expected = [NSArray arrayWithObjects:
                         (id _Nonnull)[@"1" dataUsingEncoding:NSUTF8StringEncoding],
                         (id _Nonnull)[@"2" dataUsingEncoding:NSUTF8StringEncoding],
                         (id _Nonnull)[@"3" dataUsingEncoding:NSUTF8StringEncoding],
                         nil];
    id<FYCrashReportFilter> filter = [FYCrashReportFilterStringToData filter];

    [filter filterReports:source onCompletion:^(NSArray* filteredReports,
                                                BOOL completed,
                                                NSError* error)
     {
         XCTAssertTrue(completed, @"");
         XCTAssertNil(error, @"");
         XCTAssertEqualObjects(expected, filteredReports, @"");
     }];
}

- (void) testFilterDataToString
{
    NSArray* source = [NSArray arrayWithObjects:
                       (id _Nonnull)[@"1" dataUsingEncoding:NSUTF8StringEncoding],
                       (id _Nonnull)[@"2" dataUsingEncoding:NSUTF8StringEncoding],
                       (id _Nonnull)[@"3" dataUsingEncoding:NSUTF8StringEncoding],
                       nil];
    NSArray* expected = [NSArray arrayWithObjects:@"1", @"2", @"3", nil];
    id<FYCrashReportFilter> filter = [FYCrashReportFilterDataToString filter];

    [filter filterReports:source onCompletion:^(NSArray* filteredReports,
                                                BOOL completed,
                                                NSError* error)
     {
         XCTAssertTrue(completed, @"");
         XCTAssertNil(error, @"");
         XCTAssertEqualObjects(expected, filteredReports, @"");
     }];
}

- (void) testFilterPipeline
{
    NSArray* expected = [NSArray arrayWithObjects:@"1", @"2", @"3", nil];
    id<FYCrashReportFilter> filter = [FYCrashReportFilterPipeline filterWithFilters:
                                      [FYCrashReportFilterStringToData filter],
                                      [FYCrashReportFilterDataToString filter],
                                      nil];

    [filter filterReports:expected onCompletion:^(NSArray* filteredReports,
                                                  BOOL completed,
                                                  NSError* error)
     {
         XCTAssertTrue(completed, @"");
         XCTAssertNil(error, @"");
         XCTAssertEqualObjects(expected, filteredReports, @"");
     }];
}

- (void) testFilterCombine
{
    NSArray* expected1 = [NSArray arrayWithObjects:@"1", @"2", @"3", nil];
    NSArray* expected2 = [NSArray arrayWithObjects:
                          (id _Nonnull)[@"1" dataUsingEncoding:NSUTF8StringEncoding],
                          (id _Nonnull)[@"2" dataUsingEncoding:NSUTF8StringEncoding],
                          (id _Nonnull)[@"3" dataUsingEncoding:NSUTF8StringEncoding],
                          nil];
    id<FYCrashReportFilter> filter = [FYCrashReportFilterCombine filterWithFiltersAndKeys:
                                      [FYCrashReportFilterPassthrough filter],
                                      @"normal",
                                      [FYCrashReportFilterStringToData filter],
                                      @"data",
                                      nil];

    [filter filterReports:expected1 onCompletion:^(NSArray* filteredReports,
                                                   BOOL completed,
                                                   NSError* error)
     {
         XCTAssertTrue(completed, @"");
         XCTAssertNil(error, @"");
         for(NSUInteger i = 0; i < [expected1 count]; i++)
         {
             id exp1 = [expected1 objectAtIndex:i];
             id exp2 = [expected2 objectAtIndex:i];
             NSDictionary* entry = [filteredReports objectAtIndex:i];
             id result1 = [entry objectForKey:@"normal"];
             id result2 = [entry objectForKey:@"data"];
             XCTAssertEqualObjects(result1, exp1, @"");
             XCTAssertEqualObjects(result2, exp2, @"");
         }
     }];
}

- (void) testFilterCombineInit
{
    NSArray* expected1 = [NSArray arrayWithObjects:@"1", @"2", @"3", nil];
    NSArray* expected2 = [NSArray arrayWithObjects:
                          (id _Nonnull)[@"1" dataUsingEncoding:NSUTF8StringEncoding],
                          (id _Nonnull)[@"2" dataUsingEncoding:NSUTF8StringEncoding],
                          (id _Nonnull)[@"3" dataUsingEncoding:NSUTF8StringEncoding],
                          nil];
    id<FYCrashReportFilter> filter = [[FYCrashReportFilterCombine alloc] initWithFiltersAndKeys:
                                      [FYCrashReportFilterPassthrough filter],
                                      @"normal",
                                      [FYCrashReportFilterStringToData filter],
                                      @"data",
                                      nil];
    filter = filter;
    
    [filter filterReports:expected1 onCompletion:^(NSArray* filteredReports,
                                                   BOOL completed,
                                                   NSError* error)
     {
         XCTAssertTrue(completed, @"");
         XCTAssertNil(error, @"");
         for(NSUInteger i = 0; i < [expected1 count]; i++)
         {
             id exp1 = [expected1 objectAtIndex:i];
             id exp2 = [expected2 objectAtIndex:i];
             NSDictionary* entry = [filteredReports objectAtIndex:i];
             id result1 = [entry objectForKey:@"normal"];
             id result2 = [entry objectForKey:@"data"];
             XCTAssertEqualObjects(result1, exp1, @"");
             XCTAssertEqualObjects(result2, exp2, @"");
         }
     }];
}

- (void) testFilterCombineNoFilters
{
    NSArray* expected1 = [NSArray arrayWithObjects:@"1", @"2", @"3", nil];
    id<FYCrashReportFilter> filter = [FYCrashReportFilterCombine filterWithFiltersAndKeys:
                                      nil];
    
    [filter filterReports:expected1 onCompletion:^(NSArray* filteredReports,
                                                   BOOL completed,
                                                   NSError* error)
     {
         XCTAssertTrue(completed, @"");
         XCTAssertNil(error, @"");
         for(NSUInteger i = 0; i < [expected1 count]; i++)
         {
             id exp = [expected1 objectAtIndex:i];
             NSString* entry = [filteredReports objectAtIndex:i];
             XCTAssertEqualObjects(entry, exp, @"");
         }
     }];
}

- (void) testFilterCombineIncomplete
{
    NSArray* expected1 = [NSArray arrayWithObjects:@"1", @"2", @"3", nil];
    id<FYCrashReportFilter> filter = [FYCrashReportFilterCombine filterWithFiltersAndKeys:
                                      [FYCrash_TestFilter filterWithDelay:0 completed:NO error:nil],
                                      @"Blah",
                                      nil];
    
    [filter filterReports:expected1 onCompletion:^(NSArray* filteredReports,
                                                   BOOL completed,
                                                   NSError* error)
     {
         XCTAssertNotNil(filteredReports, @"");
         XCTAssertFalse(completed, @"");
         XCTAssertNil(error, @"");
     }];
}

- (void) testFilterCombineNilReports
{
    NSArray* expected1 = [NSArray arrayWithObjects:@"1", @"2", @"3", nil];
    id<FYCrashReportFilter> filter = [FYCrashReportFilterCombine filterWithFiltersAndKeys:
                                      [FYCrash_TestNilFilter filter],
                                      @"Blah",
                                      nil];
    
    [filter filterReports:expected1 onCompletion:^(NSArray* filteredReports,
                                                   BOOL completed,
                                                   NSError* error)
     {
         XCTAssertNil(filteredReports, @"");
         XCTAssertFalse(completed, @"");
         XCTAssertNotNil(error, @"");
     }];
}

- (void) testFilterCombineArray
{
    NSArray* expected1 = [NSArray arrayWithObjects:@"1", @"2", @"3", nil];
    NSArray* expected2 = [NSArray arrayWithObjects:
                          (id _Nonnull)[@"1" dataUsingEncoding:NSUTF8StringEncoding],
                          (id _Nonnull)[@"2" dataUsingEncoding:NSUTF8StringEncoding],
                          (id _Nonnull)[@"3" dataUsingEncoding:NSUTF8StringEncoding],
                          nil];
    id<FYCrashReportFilter> filter = [FYCrashReportFilterCombine filterWithFiltersAndKeys:
                                      [NSArray arrayWithObject:[FYCrashReportFilterPassthrough filter]],
                                      @"normal",
                                      [NSArray arrayWithObject:[FYCrashReportFilterStringToData filter]],
                                      @"data",
                                      nil];
    
    [filter filterReports:expected1 onCompletion:^(NSArray* filteredReports,
                                                   BOOL completed,
                                                   NSError* error)
     {
         XCTAssertTrue(completed, @"");
         XCTAssertNil(error, @"");
         for(NSUInteger i = 0; i < [expected1 count]; i++)
         {
             id exp1 = [expected1 objectAtIndex:i];
             id exp2 = [expected2 objectAtIndex:i];
             NSDictionary* entry = [filteredReports objectAtIndex:i];
             id result1 = [entry objectForKey:@"normal"];
             id result2 = [entry objectForKey:@"data"];
             XCTAssertEqualObjects(result1, exp1, @"");
             XCTAssertEqualObjects(result2, exp2, @"");
         }
     }];
}

- (void) testFilterCombineMissingKey
{
    NSArray* expected1 = [NSArray arrayWithObjects:@"1", @"2", @"3", nil];
    id<FYCrashReportFilter> filter = [FYCrashReportFilterCombine filterWithFiltersAndKeys:
                                      [FYCrashReportFilterPassthrough filter],
                                      @"normal",
                                      [FYCrashReportFilterStringToData filter],
                                      // Missing key
                                      nil];
    
    [filter filterReports:expected1 onCompletion:^(__unused NSArray* filteredReports,
                                                   BOOL completed,
                                                   NSError* error)
     {
         XCTAssertFalse(completed, @"");
         XCTAssertNotNil(error, @"");
     }];
}

- (void) testObjectForKey
{
    NSString* key = @"someKey";
    NSString* expected = @"value";
    NSArray* reports = [NSArray arrayWithObjects:
                        [NSDictionary dictionaryWithObject:expected forKey:key],
                        nil];
    
    id<FYCrashReportFilter> filter = [FYCrashReportFilterObjectForKey filterWithKey:key allowNotFound:NO];

    [filter filterReports:reports onCompletion:^(__unused NSArray* filteredReports,
                                                 BOOL completed,
                                                 NSError* error)
     {
         XCTAssertTrue(completed, @"");
         XCTAssertNil(error, @"");
         XCTAssertEqualObjects([filteredReports objectAtIndex:0], expected, @"");
     }];
}

- (void) testObjectForKey2
{
    id key = [NSNumber numberWithInt:100];
    NSString* expected = @"value";
    NSArray* reports = [NSArray arrayWithObjects:
                        [NSDictionary dictionaryWithObject:expected forKey:key],
                        nil];
    
    id<FYCrashReportFilter> filter = [FYCrashReportFilterObjectForKey filterWithKey:key allowNotFound:NO];
    
    [filter filterReports:reports onCompletion:^(__unused NSArray* filteredReports,
                                                 BOOL completed,
                                                 NSError* error)
     {
         XCTAssertTrue(completed, @"");
         XCTAssertNil(error, @"");
         XCTAssertEqualObjects([filteredReports objectAtIndex:0], expected, @"");
     }];
}

- (void) testObjectForKeyNotFoundAllowed
{
    NSString* key = @"someKey";
    NSString* expected = @"value";
    NSArray* reports = [NSArray arrayWithObjects:
                        [NSDictionary dictionaryWithObject:expected forKey:key],
                        nil];
    
    id<FYCrashReportFilter> filter = [FYCrashReportFilterObjectForKey filterWithKey:@"someOtherKey" allowNotFound:YES];
    
    [filter filterReports:reports onCompletion:^(__unused NSArray* filteredReports,
                                                 BOOL completed,
                                                 NSError* error)
     {
         XCTAssertTrue(completed, @"");
         XCTAssertNil(error, @"");
         NSDictionary* firstReport = filteredReports[0];
         XCTAssertTrue(firstReport.count == 0, @"");
     }];
}

- (void) testObjectForKeyNotFoundNotAllowed
{
    NSString* key = @"someKey";
    NSString* expected = @"value";
    NSArray* reports = [NSArray arrayWithObjects:
                        [NSDictionary dictionaryWithObject:expected forKey:key],
                        nil];
    
    id<FYCrashReportFilter> filter = [FYCrashReportFilterObjectForKey filterWithKey:@"someOtherKey" allowNotFound:NO];
    
    [filter filterReports:reports onCompletion:^(__unused NSArray* filteredReports,
                                                 BOOL completed,
                                                 NSError* error)
     {
         XCTAssertFalse(completed, @"");
         XCTAssertNotNil(error, @"");
     }];
}

- (void) testConcatenate
{
    NSArray* reports = [NSArray arrayWithObjects:
                        [NSDictionary dictionaryWithObjectsAndKeys:
                         @"1", @"first",
                         @"a", @"second",
                         nil],
                        nil];
    NSString* expected = @"1,a";
    id<FYCrashReportFilter> filter = [FYCrashReportFilterConcatenate filterWithSeparatorFmt:@","
                                                                                       keys:@"first", @"second", nil];
    
    [filter filterReports:reports onCompletion:^(NSArray* filteredReports,
                                                 BOOL completed,
                                                 NSError* error)
     {
         XCTAssertTrue(completed, @"");
         XCTAssertNil(error, @"");
         XCTAssertEqualObjects([filteredReports objectAtIndex:0], expected, @"");
     }];
}

- (void) testConcatenateInit
{
    NSArray* reports = [NSArray arrayWithObjects:
                        [NSDictionary dictionaryWithObjectsAndKeys:
                         @"1", @"first",
                         @"a", @"second",
                         nil],
                        nil];
    NSString* expected = @"1,a";
    id<FYCrashReportFilter> filter = [[FYCrashReportFilterConcatenate alloc] initWithSeparatorFmt:@","
                                                                                             keys:@"first", @"second", nil];
    filter = filter;
    
    [filter filterReports:reports onCompletion:^(NSArray* filteredReports,
                                                 BOOL completed,
                                                 NSError* error)
     {
         XCTAssertTrue(completed, @"");
         XCTAssertNil(error, @"");
         XCTAssertEqualObjects([filteredReports objectAtIndex:0], expected, @"");
     }];
}

- (void) testSubset
{
    NSArray* reports = [NSArray arrayWithObjects:
                        [NSDictionary dictionaryWithObjectsAndKeys:
                         @"1", @"first",
                         @"a", @"second",
                         @"b", @"third",
                         nil],
                        nil];
    NSDictionary* expected = [NSDictionary dictionaryWithObjectsAndKeys:
                              @"1", @"first",
                              @"b", @"third",
                              nil];
    id<FYCrashReportFilter> filter = [FYCrashReportFilterSubset filterWithKeys:@"first", @"third", nil];
    
    [filter filterReports:reports onCompletion:^(NSArray* filteredReports,
                                                 BOOL completed,
                                                 NSError* error)
     {
         XCTAssertTrue(completed, @"");
         XCTAssertNil(error, @"");
         XCTAssertEqualObjects([filteredReports objectAtIndex:0], expected, @"");
     }];
}

- (void) testSubsetBadKeyPath
{
    NSArray* reports = [NSArray arrayWithObjects:
                        [NSDictionary dictionaryWithObjectsAndKeys:
                         @"1", @"first",
                         @"a", @"second",
                         @"b", @"third",
                         nil],
                        nil];
    id<FYCrashReportFilter> filter = [FYCrashReportFilterSubset filterWithKeys:@"first", @"aaa", nil];
    
    [filter filterReports:reports onCompletion:^(__unused NSArray* filteredReports,
                                                 BOOL completed,
                                                 NSError* error)
     {
         XCTAssertFalse(completed, @"");
         XCTAssertNotNil(error, @"");
     }];
}

- (void) testSubsetInit
{
    NSArray* reports = [NSArray arrayWithObjects:
                        [NSDictionary dictionaryWithObjectsAndKeys:
                         @"1", @"first",
                         @"a", @"second",
                         @"b", @"third",
                         nil],
                        nil];
    NSDictionary* expected = [NSDictionary dictionaryWithObjectsAndKeys:
                              @"1", @"first",
                              @"b", @"third",
                              nil];
    id<FYCrashReportFilter> filter = [[FYCrashReportFilterSubset alloc] initWithKeys:@"first", @"third", nil];
    filter = filter;
    
    [filter filterReports:reports onCompletion:^(NSArray* filteredReports,
                                                 BOOL completed,
                                                 NSError* error)
     {
         XCTAssertTrue(completed, @"");
         XCTAssertNil(error, @"");
         XCTAssertEqualObjects([filteredReports objectAtIndex:0], expected, @"");
     }];
}

@end
