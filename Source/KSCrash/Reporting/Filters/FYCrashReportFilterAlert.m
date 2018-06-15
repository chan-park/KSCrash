//
//  FYCrashReportFilterAlert.m
//
//  Created by Karl Stenerud on 2012-08-24.
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

#import "FYCrashReportFilterAlert.h"

#import "FYSystemCapabilities.h"

//#define FYLogger_LocalLevel TRACE
#import "FYLogger.h"

#if FYCRASH_HAS_ALERTVIEW

#if FYCRASH_HAS_UIKIT
#import <UIKit/UIKit.h>
#endif

#if FYCRASH_HAS_NSALERT
#import <AppKit/AppKit.h>
#endif 

@interface FYCrashAlertViewProcess : NSObject
#if FYCRASH_HAS_UIALERTVIEW
<UIAlertViewDelegate>
#endif

@property(nonatomic,readwrite,retain) NSArray* reports;
@property(nonatomic,readwrite,copy) FYCrashReportFilterCompletion onCompletion;
#if FYCRASH_HAS_UIALERTVIEW
@property(nonatomic,readwrite,retain) UIAlertView* alertView;
#endif
@property(nonatomic,readwrite,assign) NSInteger expectedButtonIndex;

+ (FYCrashAlertViewProcess*) process;

- (void) startWithTitle:(NSString*) title
                message:(NSString*) message
              yesAnswer:(NSString*) yesAnswer
               noAnswer:(NSString*) noAnswer
                reports:(NSArray*) reports
           onCompletion:(FYCrashReportFilterCompletion) onCompletion;

@end

@implementation FYCrashAlertViewProcess

@synthesize reports = _reports;
@synthesize onCompletion = _onCompletion;
#if FYCRASH_HAS_UIALERTVIEW
@synthesize alertView = _alertView;
#endif
@synthesize expectedButtonIndex = _expectedButtonIndex;

+ (FYCrashAlertViewProcess*) process
{
    return [[self alloc] init];
}

- (void) startWithTitle:(NSString*) title
                message:(NSString*) message
              yesAnswer:(NSString*) yesAnswer
               noAnswer:(NSString*) noAnswer
                reports:(NSArray*) reports
           onCompletion:(FYCrashReportFilterCompletion) onCompletion
{
    FYLOG_TRACE(@"Starting alert view process");
    self.reports = reports;
    self.onCompletion = onCompletion;
    self.expectedButtonIndex = noAnswer == nil ? 0 : 1;

#if FYCRASH_HAS_UIALERTVIEW
    self.alertView = [[UIAlertView alloc] init];
    self.alertView.title = title;
    self.alertView.message = message;
    if(noAnswer != nil)
    {
        [self.alertView addButtonWithTitle:noAnswer];
    }
    [self.alertView addButtonWithTitle:yesAnswer];
    self.alertView.delegate = self;
    
    FYLOG_TRACE(@"Showing alert view");
    [self.alertView show];
#elif FYCRASH_HAS_UIALERTCONTROLLER
    UIAlertController *alertController = [UIAlertController alertControllerWithTitle:title
                                                                             message:message
                                                                      preferredStyle:UIAlertControllerStyleAlert];
    UIAlertAction *yesAction = [UIAlertAction actionWithTitle:yesAnswer
                                                        style:UIAlertActionStyleDefault
                                                      handler:^(UIAlertAction * _Nonnull action) {
                                                          fycrash_callCompletion(self.onCompletion, self.reports, YES, nil);
                                                      }];
    UIAlertAction *noAction = [UIAlertAction actionWithTitle:noAnswer
                                                       style:UIAlertActionStyleCancel
                                                     handler:^(UIAlertAction * _Nonnull action) {
                                                         fycrash_callCompletion(self.onCompletion, self.reports, NO, nil);
                                                     }];
    [alertController addAction:yesAction];
    [alertController addAction:noAction];
    UIWindow *keyWindow = [[UIApplication sharedApplication] keyWindow];
    [keyWindow.rootViewController presentViewController:alertController animated:YES completion:NULL];
#elif FYCRASH_HAS_NSALERT
    NSAlert *alert = [[NSAlert alloc] init];
    [alert addButtonWithTitle:yesAnswer];
    if(noAnswer != nil)
    {
        [alert addButtonWithTitle:noAnswer];
    }
    [alert setMessageText:title];
    [alert setInformativeText:message];
    [alert setAlertStyle:NSInformationalAlertStyle];
    BOOL success = NO;
    if([alert runModal] == NSAlertFirstButtonReturn)
    {
        success = noAnswer != nil;
    }
    fycrash_callCompletion(self.onCompletion, self.reports, success, nil);
#endif
}

- (void) alertView:(__unused id) alertView clickedButtonAtIndex:(NSInteger) buttonIndex
{
    BOOL success = buttonIndex == self.expectedButtonIndex;
    fycrash_callCompletion(self.onCompletion, self.reports, success, nil);
}

@end


@interface FYCrashReportFilterAlert ()

@property(nonatomic, readwrite, retain) NSString* title;
@property(nonatomic, readwrite, retain) NSString* message;
@property(nonatomic, readwrite, retain) NSString* yesAnswer;
@property(nonatomic, readwrite, retain) NSString* noAnswer;

@end

@implementation FYCrashReportFilterAlert

@synthesize title = _title;
@synthesize message = _message;
@synthesize yesAnswer = _yesAnswer;
@synthesize noAnswer = _noAnswer;

+ (FYCrashReportFilterAlert*) filterWithTitle:(NSString*) title
                                      message:(NSString*) message
                                    yesAnswer:(NSString*) yesAnswer
                                     noAnswer:(NSString*) noAnswer
{
    return [[self alloc] initWithTitle:title
                               message:message
                             yesAnswer:yesAnswer
                              noAnswer:noAnswer];
}

- (id) initWithTitle:(NSString*) title
             message:(NSString*) message
           yesAnswer:(NSString*) yesAnswer
            noAnswer:(NSString*) noAnswer
{
    if((self = [super init]))
    {
        self.title = title;
        self.message = message;
        self.yesAnswer = yesAnswer;
        self.noAnswer = noAnswer;
    }
    return self;
}

- (void) filterReports:(NSArray*) reports
          onCompletion:(FYCrashReportFilterCompletion) onCompletion
{
    dispatch_async(dispatch_get_main_queue(), ^
                   {
                       FYLOG_TRACE(@"Launching new alert view process");
                       __block FYCrashAlertViewProcess* process = [[FYCrashAlertViewProcess alloc] init];
                       [process startWithTitle:self.title
                                       message:self.message
                                     yesAnswer:self.yesAnswer
                                      noAnswer:self.noAnswer
                                       reports:reports
                                  onCompletion:^(NSArray* filteredReports,
                                                 BOOL completed,
                                                 NSError* error)
                        {
                            FYLOG_TRACE(@"alert process complete");
                            fycrash_callCompletion(onCompletion, filteredReports, completed, error);
                            dispatch_async(dispatch_get_main_queue(), ^
                                           {
                                               process = nil;
                                           });
                        }];
                   });
}

@end

#else

@implementation FYCrashReportFilterAlert

+ (FYCrashReportFilterAlert*) filterWithTitle:(NSString*) title
                                      message:(NSString*) message
                                    yesAnswer:(NSString*) yesAnswer
                                     noAnswer:(NSString*) noAnswer
{
    return [[self alloc] initWithTitle:title
                               message:message
                             yesAnswer:yesAnswer
                              noAnswer:noAnswer];
}

- (id) initWithTitle:(__unused NSString*) title
             message:(__unused NSString*) message
           yesAnswer:(__unused NSString*) yesAnswer
            noAnswer:(__unused NSString*) noAnswer
{
    if((self = [super init]))
    {
        FYLOG_WARN(@"Alert filter not available on this platform.");
    }
    return self;
}

- (void) filterReports:(NSArray*) reports
          onCompletion:(FYCrashReportFilterCompletion) onCompletion
{
    FYLOG_WARN(@"Alert filter not available on this platform.");
    fycrash_callCompletion(onCompletion, reports, YES, nil);
}

@end

#endif
