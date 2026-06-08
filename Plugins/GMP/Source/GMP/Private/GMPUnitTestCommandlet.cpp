//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.
//
// GMPUnitTest commandlet -- a thin, headless entry point for the GMP test suite:
//   <Editor>-Cmd.exe <Project> -run=GMPUnitTest [-Bench] [-NoDirect]
// Exit code 0 = all PASS, nonzero = number of failed cases. Designed for CI.
//
// The actual tests live in GMPTests.cpp (UE automation framework). This commandlet only
// forwards to GMPUnitTest::RunAllGMPTests(), which the automation framework does not cover
// (it has no headless single-exit-code runner). See GMPTests.cpp for the test bodies.

#include "GMPUnitTestCommandlet.h"

int32 UGMPUnitTestCommandlet::Main(const FString& Params)
{
	return GMPUnitTest::RunAllGMPTests(Params);
}
