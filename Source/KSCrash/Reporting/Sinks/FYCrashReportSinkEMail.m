//
//  FYCrashReportSinkEMail.m
//
//  Created by Karl Stenerud on 2012-05-06.
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


#import "FYCrashReportSinkEMail.h"

#import "FYCrashReportFilterAppleFmt.h"
#import "FYCrashReportFilterBasic.h"
#import "FYCrashReportFilterGZip.h"
#import "FYCrashReportFilterJSON.h"
#import "NSError+SimpleConstructor.h"
#import "FYSystemCapabilities.h"

//#define FYLogger_LocalLevel TRACE
#import "FYLogger.h"

#if FYCRASH_HAS_MESSAGEUI
#import <MessageUI/MessageUI.h>


@interface FYCrashMailProcess : NSObject <MFMailComposeViewControllerDelegate>

@property(nonatomic,readwrite,retain) NSArray* reports;
@property(nonatomic,readwrite,copy) FYCrashReportFilterCompletion onCompletion;

@property(nonatomic,readwrite,retain) UIViewController* dummyVC;

+ (FYCrashMailProcess*) process;

- (void) startWithController:(MFMailComposeViewController*) controller
                     reports:(NSArray*) reports
                 filenameFmt:(NSString*) filenameFmt
                onCompletion:(FYCrashReportFilterCompletion) onCompletion;

- (void) presentModalVC:(UIViewController*) vc;
- (void) dismissModalVC;

@end

@implementation FYCrashMailProcess

@synthesize reports = _reports;
@synthesize onCompletion = _onCompletion;
@synthesize dummyVC = _dummyVC;

+ (FYCrashMailProcess*) process
{
    return [[self alloc] init];
}

- (void) startWithController:(MFMailComposeViewController*) controller
                     reports:(NSArray*) reports
                 filenameFmt:(NSString*) filenameFmt
                onCompletion:(FYCrashReportFilterCompletion) onCompletion
{
    self.reports = reports;
    self.onCompletion = onCompletion;

    controller.mailComposeDelegate = self;

    int i = 1;
    for(NSData* report in reports)
    {
        if(![report isKindOfClass:[NSData class]])
        {
            FYLOG_ERROR(@"Report was of type %@", [report class]);
        }
        else
        {
            [controller addAttachmentData:report
                                 mimeType:@"binary"
                                 fileName:[NSString stringWithFormat:filenameFmt, i++]];
        }
    }

    [self presentModalVC:controller];
}

- (void) mailComposeController:(__unused MFMailComposeViewController*) mailController
           didFinishWithResult:(MFMailComposeResult) result
                         error:(NSError*) error
{
    [self dismissModalVC];

    switch (result)
    {
        case MFMailComposeResultSent:
            fycrash_callCompletion(self.onCompletion, self.reports, YES, nil);
            break;
        case MFMailComposeResultSaved:
            fycrash_callCompletion(self.onCompletion, self.reports, YES, nil);
            break;
        case MFMailComposeResultCancelled:
            fycrash_callCompletion(self.onCompletion, self.reports, NO,
                                     [NSError errorWithDomain:[[self class] description]
                                                         code:0
                                                  description:@"User cancelled"]);
            break;
        case MFMailComposeResultFailed:
            fycrash_callCompletion(self.onCompletion, self.reports, NO, error);
            break;
        default:
        {
            fycrash_callCompletion(self.onCompletion, self.reports, NO,
                                     [NSError errorWithDomain:[[self class] description]
                                                         code:0
                                                  description:@"Unknown MFMailComposeResult: %d", result]);
        }
    }
}

- (void) presentModalVC:(UIViewController*) vc
{
	self.dummyVC = [[UIViewController alloc] initWithNibName:nil bundle:nil];
	self.dummyVC.view = [[UIView alloc] init];

    UIWindow* window = [[[UIApplication sharedApplication] delegate] window];
    [window addSubview:self.dummyVC.view];

    if([self.dummyVC respondsToSelector:@selector(presentViewController:animated:completion:)])
    {
        [self.dummyVC presentViewController:vc animated:YES completion:nil];
    }
    else
    {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
        [self.dummyVC presentModalViewController:vc animated:YES];
#pragma clang diagnostic pop
    }
}

- (void) dismissModalVC
{
    if([self.dummyVC respondsToSelector:@selector(dismissViewControllerAnimated:completion:)])
    {
        [self.dummyVC dismissViewControllerAnimated:YES completion:^
         {
             [self.dummyVC.view removeFromSuperview];
             self.dummyVC = nil;
         }];
    }
    else
    {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
        [self.dummyVC dismissModalViewControllerAnimated:NO];
#pragma clang diagnostic pop
        [self.dummyVC.view removeFromSuperview];
        self.dummyVC = nil;
    }
}

@end


@interface FYCrashReportSinkEMail ()

@property(nonatomic,readwrite,retain) NSArray* recipients;

@property(nonatomic,readwrite,retain) NSString* subject;

@property(nonatomic,readwrite,retain) NSString* message;

@property(nonatomic,readwrite,retain) NSString* filenameFmt;

@end


@implementation FYCrashReportSinkEMail

@synthesize recipients = _recipients;
@synthesize subject = _subject;
@synthesize message = _message;
@synthesize filenameFmt = _filenameFmt;

+ (FYCrashReportSinkEMail*) sinkWithRecipients:(NSArray*) recipients
                                       subject:(NSString*) subject
                                       message:(NSString*) message
                                   filenameFmt:(NSString*) filenameFmt
{
    return [[self alloc] initWithRecipients:recipients
                                    subject:subject
                                    message:message
                                filenameFmt:filenameFmt];
}

- (id) initWithRecipients:(NSArray*) recipients
                  subject:(NSString*) subject
                  message:(NSString*) message
              filenameFmt:(NSString*) filenameFmt
{
    if((self = [super init]))
    {
        self.recipients = recipients;
        self.subject = subject;
        self.message = message;
        self.filenameFmt = filenameFmt;
    }
    return self;
}

- (id <FYCrashReportFilter>) defaultCrashReportFilterSet
{
    return [FYCrashReportFilterPipeline filterWithFilters:
            [FYCrashReportFilterJSONEncode filterWithOptions:FYJSONEncodeOptionSorted | FYJSONEncodeOptionPretty],
            [FYCrashReportFilterGZipCompress filterWithCompressionLevel:-1],
            self,
            nil];
}

- (id <FYCrashReportFilter>) defaultCrashReportFilterSetAppleFmt
{
    return [FYCrashReportFilterPipeline filterWithFilters:
            [FYCrashReportFilterAppleFmt filterWithReportStyle:FYAppleReportStyleSymbolicatedSideBySide],
            [FYCrashReportFilterStringToData filter],
            [FYCrashReportFilterGZipCompress filterWithCompressionLevel:-1],
            self,
            nil];
}

- (void) filterReports:(NSArray*) reports
          onCompletion:(FYCrashReportFilterCompletion) onCompletion
{
    if(![MFMailComposeViewController canSendMail])
    {
        [[[UIAlertView alloc] initWithTitle:@"Email Error"
                                    message:@"This device is not configured to send email."
                                   delegate:nil
                          cancelButtonTitle:@"OK"
                          otherButtonTitles:nil] show];

        fycrash_callCompletion(onCompletion, reports, NO,
                                 [NSError errorWithDomain:[[self class] description]
                                                     code:0
                                              description:@"E-Mail not enabled on device"]);
        return;
    }

    MFMailComposeViewController* mailController = [[MFMailComposeViewController alloc] init];
    [mailController setToRecipients:self.recipients];
    [mailController setSubject:self.subject];
    if(self.message != nil)
    {
        [mailController setMessageBody:self.message isHTML:NO];
    }
    NSString* filenameFmt = self.filenameFmt;

    dispatch_async(dispatch_get_main_queue(), ^
                   {
                       __block FYCrashMailProcess* process = [[FYCrashMailProcess alloc] init];
                       [process startWithController:mailController
                                            reports:reports
                                        filenameFmt:filenameFmt
                                       onCompletion:^(NSArray* filteredReports,
                                                      BOOL completed,
                                                      NSError* error)
                        {
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

#import "NSData+GZip.h"

@implementation FYCrashReportSinkEMail

+ (FYCrashReportSinkEMail*) sinkWithRecipients:(NSArray*) recipients
                                       subject:(NSString*) subject
                                       message:(NSString*) message
                                   filenameFmt:(NSString*) filenameFmt
{
    return [[self alloc] initWithRecipients:recipients
                                    subject:subject
                                    message:message
                                filenameFmt:filenameFmt];
}

- (id) initWithRecipients:(__unused NSArray*) recipients
                  subject:(__unused NSString*) subject
                  message:(__unused NSString*) message
              filenameFmt:(__unused NSString*) filenameFmt
{
    return [super init];
}

- (void) filterReports:(NSArray*) reports
          onCompletion:(FYCrashReportFilterCompletion) onCompletion
{
    for(NSData* reportData in reports)
    {
        NSString* report = [[NSString alloc] initWithData:[reportData gunzippedWithError:nil] encoding:NSUTF8StringEncoding];
        NSLog(@"Report\n%@", report);
    }
    fycrash_callCompletion(onCompletion, reports, NO,
                             [NSError errorWithDomain:[[self class] description]
                                                 code:0
                                          description:@"Cannot send mail on this platform"]);
}

- (id <FYCrashReportFilter>) defaultCrashReportFilterSet
{
    return [FYCrashReportFilterPipeline filterWithFilters:
            [FYCrashReportFilterJSONEncode filterWithOptions:FYJSONEncodeOptionSorted | FYJSONEncodeOptionPretty],
            [FYCrashReportFilterGZipCompress filterWithCompressionLevel:-1],
            self,
            nil];
}

- (id <FYCrashReportFilter>) defaultCrashReportFilterSetAppleFmt
{
    return [FYCrashReportFilterPipeline filterWithFilters:
            [FYCrashReportFilterAppleFmt filterWithReportStyle:FYAppleReportStyleSymbolicatedSideBySide],
            [FYCrashReportFilterStringToData filter],
            [FYCrashReportFilterGZipCompress filterWithCompressionLevel:-1],
            self,
            nil];
}

@end

#endif
