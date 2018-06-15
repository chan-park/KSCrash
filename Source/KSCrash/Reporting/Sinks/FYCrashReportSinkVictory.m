//
//  FYCrashReportSinkVictory.m
//
//  Created by Kelp on 2013-03-14.
//
//  Copyright (c) 2013 Karl Stenerud. All rights reserved.
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

#import "FYSystemCapabilities.h"

#if FYCRASH_HAS_UIKIT
#import <UIKit/UIKit.h>
#endif
#import "FYCrashReportSinkVictory.h"

#import "FYHTTPMultipartPostBody.h"
#import "FYHTTPRequestSender.h"
#import "NSData+GZip.h"
#import "FYJSONCodecObjC.h"
#import "FYReachabilityFYCrash.h"
#import "NSError+SimpleConstructor.h"
#import "FYSystemCapabilities.h"

//#define FYLogger_LocalLevel TRACE
#import "FYLogger.h"


@interface FYCrashReportSinkVictory ()

@property(nonatomic,readwrite,retain) NSURL* url;
@property(nonatomic,readwrite,retain) NSString* userName;
@property(nonatomic,readwrite,retain) NSString* userEmail;

@property(nonatomic,readwrite,retain) FYReachableOperationFYCrash* reachableOperation;


@end


@implementation FYCrashReportSinkVictory

@synthesize url = _url;
@synthesize userName = _userName;
@synthesize userEmail = _userEmail;
@synthesize reachableOperation = _reachableOperation;

+ (FYCrashReportSinkVictory*) sinkWithURL:(NSURL*) url
                                   userName:(NSString*) userName
                                  userEmail:(NSString*) userEmail
{
    return [[self alloc] initWithURL:url userName:userName userEmail:userEmail];
}

- (id) initWithURL:(NSURL*) url
          userName:(NSString*) userName
         userEmail:(NSString*) userEmail
{
    if((self = [super init]))
    {
        self.url = url;
        if (userName == nil || [userName length] == 0) {
#if FYCRASH_HAS_UIDEVICE
            self.userName = UIDevice.currentDevice.name;
#else
            self.userName = @"unknown";
#endif
        }
        else {
            self.userName = userName;
        }
        self.userEmail = userEmail;
    }
    return self;
}

- (id <FYCrashReportFilter>) defaultCrashReportFilterSet
{
    return self;
}

- (void) filterReports:(NSArray*) reports
          onCompletion:(FYCrashReportFilterCompletion) onCompletion
{
    // update user information in reports with KVC
    for (NSDictionary *report in reports) {
        NSDictionary *userDict = [report objectForKey:@"user"];
        if (userDict) {
            // user member is exist
            [userDict setValue:self.userName forKey:@"name"];
            [userDict setValue:self.userEmail forKey:@"email"];
        }
        else {
            // no user member, append user dictionary
            [report setValue:@{@"name": self.userName, @"email": self.userEmail} forKey:@"user"];
        }
    }
    
    NSError* error = nil;
    NSMutableURLRequest* request = [NSMutableURLRequest requestWithURL:self.url
                                                           cachePolicy:NSURLRequestReloadIgnoringLocalCacheData
                                                       timeoutInterval:15];
    FYHTTPMultipartPostBody* body = [FYHTTPMultipartPostBody body];
    NSData* jsonData = [FYJSONCodec encode:reports
                                   options:FYJSONEncodeOptionSorted
                                     error:&error];
    if(jsonData == nil)
    {
        fycrash_callCompletion(onCompletion, reports, NO, error);
        return;
    }
    
    [body appendData:jsonData
                name:@"reports"
         contentType:@"application/json"
            filename:@"reports.json"];

    // POST http request
    // Content-Type: multipart/form-data; boundary=xxx
    // Content-Encoding: gzip
    request.HTTPMethod = @"POST";
    request.HTTPBody = [[body data] gzippedWithCompressionLevel:-1 error:nil];
    [request setValue:body.contentType forHTTPHeaderField:@"Content-Type"];
    [request setValue:@"gzip" forHTTPHeaderField:@"Content-Encoding"];
    [request setValue:@"FYCrashReporter" forHTTPHeaderField:@"User-Agent"];

    self.reachableOperation = [FYReachableOperationFYCrash operationWithHost:[self.url host]
                                                                   allowWWAN:YES
                                                                       block:^
    {
        [[FYHTTPRequestSender sender] sendRequest:request
                                        onSuccess:^(__unused NSHTTPURLResponse* response, __unused NSData* data)
         {
             fycrash_callCompletion(onCompletion, reports, YES, nil);
         } onFailure:^(NSHTTPURLResponse* response, NSData* data)
         {
             NSString* text = [[NSString alloc] initWithData:data encoding:NSUTF8StringEncoding];
             fycrash_callCompletion(onCompletion, reports, NO,
                                      [NSError errorWithDomain:[[self class] description]
                                                          code:response.statusCode
                                                   description:text]);
         } onError:^(NSError* error2)
         {
             fycrash_callCompletion(onCompletion, reports, NO, error2);
         }];
    }];
}

@end
