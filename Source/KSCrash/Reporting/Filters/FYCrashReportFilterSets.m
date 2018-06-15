//
//  FYCrashFilterSets.m
//
//  Created by Karl Stenerud on 2012-08-21.
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


#import "FYCrashReportFilterSets.h"
#import "FYCrashReportFilterBasic.h"
#import "FYCrashReportFilterJSON.h"
#import "FYCrashReportFilterGZip.h"
#import "FYCrashReportFields.h"

@implementation FYCrashFilterSets

+ (id<FYCrashReportFilter>) appleFmtWithUserAndSystemData:(FYAppleReportStyle) reportStyle
                                               compressed:(BOOL) compressed
{
    id<FYCrashReportFilter> appleFilter = [FYCrashReportFilterAppleFmt filterWithReportStyle:reportStyle];
    id<FYCrashReportFilter> userSystemFilter = [FYCrashReportFilterPipeline filterWithFilters:
                                                [FYCrashReportFilterSubset filterWithKeys:
                                                 @FYCrashField_System,
                                                 @FYCrashField_User,
                                                 nil],
                                                [FYCrashReportFilterJSONEncode filterWithOptions:FYJSONEncodeOptionPretty | FYJSONEncodeOptionSorted],
                                                [FYCrashReportFilterDataToString filter],
                                                nil];

    NSString* appleName = @"Apple Report";
    NSString* userSystemName = @"User & System Data";

    NSMutableArray* filters = [NSMutableArray arrayWithObjects:
                               [FYCrashReportFilterCombine filterWithFiltersAndKeys:
                                appleFilter, appleName,
                                userSystemFilter, userSystemName,
                                nil],
                               [FYCrashReportFilterConcatenate filterWithSeparatorFmt:@"\n\n-------- %@ --------\n\n" keys:
                                appleName, userSystemName, nil],
                               nil];

    if(compressed)
    {
        [filters addObject:[FYCrashReportFilterStringToData filter]];
        [filters addObject:[FYCrashReportFilterGZipCompress filterWithCompressionLevel:-1]];
    }

    return [FYCrashReportFilterPipeline filterWithFilters:filters, nil];
}

@end
