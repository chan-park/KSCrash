//
//  FYCPU_Tests.m
//
//  Created by Karl Stenerud on 2012-03-03.
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

#import "FYCPU.h"
#import "FYMachineContext.h"
#import "TestThread.h"

#import <mach/mach.h>


@interface FYCPU_Tests : XCTestCase @end

@implementation FYCPU_Tests

- (void) testCPUState
{
    TestThread* thread = [[TestThread alloc] init];
    [thread start];
    [NSThread sleepForTimeInterval:0.1];
    kern_return_t kr;
    kr = thread_suspend(thread.thread);
    XCTAssertTrue(kr == KERN_SUCCESS, @"");
    
    FYMC_NEW_CONTEXT(machineContext);
    fymc_getContextForThread(thread.thread, machineContext, NO);
    fycpu_getState(machineContext);
    
    int numRegisters = fycpu_numRegisters();
    for(int i = 0; i < numRegisters; i++)
    {
        const char* name = fycpu_registerName(i);
        XCTAssertTrue(name != NULL, @"Register %d was NULL", i);
        fycpu_registerValue(machineContext, i);
    }
    
    const char* name = fycpu_registerName(1000000);
    XCTAssertTrue(name == NULL, @"");
    uint64_t value = fycpu_registerValue(machineContext, 1000000);
    XCTAssertTrue(value == 0, @"");

    uintptr_t address;
    address = fycpu_framePointer(machineContext);
    XCTAssertTrue(address != 0, @"");
    address = fycpu_stackPointer(machineContext);
    XCTAssertTrue(address != 0, @"");
    address = fycpu_instructionAddress(machineContext);
    XCTAssertTrue(address != 0, @"");

    numRegisters = fycpu_numExceptionRegisters();
    for(int i = 0; i < numRegisters; i++)
    {
        name = fycpu_exceptionRegisterName(i);
        XCTAssertTrue(name != NULL, @"Register %d was NULL", i);
        fycpu_exceptionRegisterValue(machineContext, i);
    }
    
    name = fycpu_exceptionRegisterName(1000000);
    XCTAssertTrue(name == NULL, @"");
    value = fycpu_exceptionRegisterValue(machineContext, 1000000);
    XCTAssertTrue(value == 0, @"");
    
    fycpu_faultAddress(machineContext);

    thread_resume(thread.thread);
    [thread cancel];
}

- (void) testStackGrowDirection
{
    fycpu_stackGrowDirection();
}

@end
