//
//  FYCrashInstallationConsole.m
//  FYCrash-iOS
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

#import "FYCrashInstallationConsole.h"
#import "FYCrashInstallation+Private.h"
#import "FYCrashReportSinkConsole.h"
#import "FYCrashReportFilterAppleFmt.h"
#import "FYCrashReportFilterBasic.h"
#import "FYCrashReportFilterJSON.h"
#import "FYCrashReportFilterStringify.h"

@implementation FYCrashInstallationConsole

@synthesize printAppleFormat = _printAppleFormat;

+ (instancetype) sharedInstance
{
    static FYCrashInstallationConsole *sharedInstance = nil;
    static dispatch_once_t onceToken;
    
    dispatch_once(&onceToken, ^{
        sharedInstance = [[FYCrashInstallationConsole alloc] init];
    });
    return sharedInstance;
}

- (id) init
{
    if((self = [super initWithRequiredProperties:nil]))
    {
        self.printAppleFormat = NO;
    }
    return self;
}

- (id<FYCrashReportFilter>) sink
{
    id<FYCrashReportFilter> formatFilter;
    if(self.printAppleFormat)
    {
        formatFilter = [FYCrashReportFilterAppleFmt filterWithReportStyle:FYAppleReportStyleSymbolicated];
    }
    else
    {
        formatFilter = [FYCrashReportFilterPipeline filterWithFilters:
                        [FYCrashReportFilterJSONEncode filterWithOptions:FYJSONEncodeOptionPretty | FYJSONEncodeOptionSorted],
                        [FYCrashReportFilterStringify new],
                        nil];
    }

    return [FYCrashReportFilterPipeline filterWithFilters:
            formatFilter,
            [FYCrashReportSinkConsole new],
            nil];
}

@end
