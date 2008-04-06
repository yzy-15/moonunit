/*
 * Copyright (c) 2007-2008, Brian Koropoff
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the Moonunit project nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY BRIAN KOROPOFF ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL BRIAN KOROPOFF BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "run.h"
#include "gdb.h"

#include <moonunit/util.h>

#include <stdlib.h>
#include <string.h>

static int
test_compare(const void* _a, const void* _b)
{
	MuTest* a = *(MuTest**) _a;
	MuTest* b = *(MuTest**) _b;
	int result;
	
	if ((result = strcmp(a->suite, b->suite)))
		return result;
	else
		return (a == b) ? 0 : ((a < b) ? -1 : 1);
}

static unsigned int
test_count(MuTest** tests)
{
	unsigned int result;
	
	for (result = 0; tests[result]; result++);
	
	return result;
}

static bool
in_set(MuTest* test, int setc, char** set)
{
    unsigned int i;

    for (i = 0; i < setc; i++)
    {
        char* suite_name = set[i];
        char* slash = strchr(set[i], '/');
        char* test_name = slash ? slash+1 : NULL;

        if (test_name)
        {
            if (!strncmp(suite_name, test->suite, slash - suite_name) &&
                !strcmp(test_name, test->name))
                return true;
        }
        else
        {
            if (!strcmp(suite_name, test->suite))
                return true;
        }
    }

    return false;
}

static void
event_proxy_cb(MuLogEvent* event, void* data)
{
    MuLogger* logger = (MuLogger*) data;

    Mu_Logger_TestLog(logger, event);
}

unsigned int
run_tests(RunSettings* settings, const char* path, int setc, char** set, MuError** _err)
{
    MuError* err = NULL;
    unsigned int failed = 0;
    MuLogger* logger = settings->logger;
    MuLoader* loader = settings->loader;
    MuHarness* harness = settings->harness;
    MuLibrary* library = Mu_Loader_Open(loader, path, &err);
    MuTest** tests = NULL;
    MuThunk thunk;

    if (err)
    {
        MU_RERAISE_GOTO(error, _err, err);
    }
    
    Mu_Logger_LibraryEnter(logger, basename_pure(path));
    
    tests = Mu_Loader_GetTests(loader, library);
    
    if (tests)
    {
        qsort(tests, test_count(tests), sizeof(*tests), test_compare);
        
        unsigned int index;
        const char* current_suite = NULL;
        
        if ((thunk = Mu_Loader_LibrarySetup(loader, library)))
            thunk();
        
        for (index = 0; tests[index]; index++)
        {
            MuTestResult* summary = NULL;
            MuTest* test = tests[index];
            unsigned int count;

            if (set != NULL && !in_set(test, setc, set))
                continue;
            
            if (current_suite == NULL || strcmp(current_suite, test->suite))
            {
                if (current_suite)
                    Mu_Logger_SuiteLeave(logger);
                current_suite = test->suite;
                Mu_Logger_SuiteEnter(logger, test->suite);
            }
            
            Mu_Logger_TestEnter(logger, test);
            for (count = 0; count < settings->iterations; count++)
            {
                summary = harness->dispatch(harness, test, event_proxy_cb, logger);
                if (summary->status != MU_STATUS_SUCCESS)
                    break;
            }
            Mu_Logger_TestLeave(logger, test, summary);
            
            if (summary->status != summary->expected && settings->debug)
            {
                pid_t pid = harness->debug(harness, test);
                char* breakpoint;
                
                if (summary->line)
                    breakpoint = format("%s:%u", test->file, summary->line);
                else if (summary->stage == MU_STAGE_SETUP)
                    breakpoint = format("*%p", Mu_Loader_FixtureSetup(loader, library, test->suite));
                else if (summary->stage == MU_STAGE_TEARDOWN)
                    breakpoint = format("*%p", Mu_Loader_FixtureTeardown(loader, library, test->suite));
                else
                    /* FIXME: this isn't guaranteed to be meaningful */
                    breakpoint = format("*%p", test->run);
                
                gdb_attach_interactive(settings->self, pid, breakpoint);
            }
            
            if (summary->status != summary->expected)
                failed++;

            harness->free_result(harness, summary);
        }
        
        if (current_suite)
            Mu_Logger_SuiteLeave(logger);
    }
    
    Mu_Logger_LibraryLeave(logger);
    
    if ((thunk = Mu_Loader_LibraryTeardown(loader, library)))
        thunk();
error:
    
    if (library)
        Mu_Loader_Close(loader, library);

    if (tests)
        Mu_Loader_FreeTests(loader, library, tests);

    return failed;
}

unsigned int
run_all(RunSettings* settings, const char* path, MuError** _err)
{
    return run_tests(settings, path, 0, NULL, _err);
}