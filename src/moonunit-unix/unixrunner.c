/*
 * Copyright (c) 2007, Brian Koropoff
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

#include <moonunit/plugin.h>
#include <moonunit/runner.h>
#include <moonunit/loader.h>
#include <moonunit/logger.h>
#include <moonunit/harness.h>
#include <moonunit/test.h>
#include <moonunit/util.h>


#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include "gdb.h"

typedef struct UnixRunner
{
    MuRunner base;

	MuLoader* loader;
	MuHarness* harness;
	MuLogger* logger;
    const char* self;

    struct 
    {
        bool gdb;
    } option;
} UnixRunner;

static int test_compare(const void* _a, const void* _b)
{
	MuTest* a = *(MuTest**) _a;
	MuTest* b = *(MuTest**) _b;
	int result;
	
	if ((result = strcmp(a->suite, b->suite)))
		return result;
	else
		return (a == b) ? 0 : ((a < b) ? -1 : 1);
}

static unsigned int test_count(MuTest** tests)
{
	unsigned int result;
	
	for (result = 0; tests[result]; result++);
	
	return result;
}

static MuType
UnixRunner_OptionType(void* _runner, const char* name)
{
    if (!strcmp(name, "gdb"))
    {
        return MU_BOOLEAN;
    }
    else
    {
        return MU_UNKNOWN_TYPE;
    }
}

static void
UnixRunner_OptionSet(void* _runner, const char* name, void* _value)
{
    UnixRunner* runner = (UnixRunner*) _runner;

    if (!strcmp(name, "gdb"))
    {
        bool value = *(bool*) _value;
        
        runner->option.gdb = value;
    }
}

static const void *
UnixRunner_OptionGet(void* _runner, const char* name)
{
    UnixRunner* runner = (UnixRunner*) _runner;

    if (!strcmp(name, "gdb"))
    {
        return &runner->option.gdb;
    }
    else
    {
        return NULL;
    }
}

static MuOption unixrunner_option =
{
    .set = UnixRunner_OptionSet,
    .get = UnixRunner_OptionGet,
    .type = UnixRunner_OptionType
};

static bool in_set(MuTest* test, int setc, char** set)
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

static void event_proxy_cb(MuLogEvent* event, void* data)
{
    MuLogger* logger = (MuLogger*) data;

    logger->test_log(logger, event);
}

static void UnixRunner_Run(UnixRunner* runner, const char* path, 
                           int setc, char** set, MuError** _err)
{
    MuError* err = NULL;
   	MuLibrary* library = runner->loader->open(runner->loader, path, &err);

    if (err)
    {
        MU_RERAISE_GOTO(error, _err, err);
    }

    MuLogger* logger = runner->logger;
    
    logger->library_enter(logger, basename_pure(path));
    
    MuTest** tests = runner->loader->tests(runner->loader, library);
    MuThunk thunk;
    
    if (tests)
    {
	
	/* FIXME: it's probably not ok to sort this array in place since
	 * it's owned by the loader.  It might be worthwhile to change the
	 * semantics of the loader->tests method to return something that
	 * should be freed
	 */
	qsort(tests, test_count(tests), sizeof(*tests), test_compare);
	
	unsigned int index;
	const char* current_suite = NULL;
	
	if ((thunk = runner->loader->library_setup(runner->loader, library)))
	    thunk();
	
	for (index = 0; tests[index]; index++)
	{
	    MuTestSummary summary;
	    MuTest* test = tests[index];
	    
	    if (set != NULL && !in_set(test, setc, set))
		continue;
	    
	    if (current_suite == NULL || strcmp(current_suite, test->suite))
	    {
		if (current_suite)
		    logger->suite_leave(logger);
		current_suite = test->suite;
		logger->suite_enter(logger, test->suite);
	    }
	    
	    logger->test_enter(logger, test);
	    runner->harness->dispatch(runner->harness, test, &summary, event_proxy_cb, logger);
	    logger->test_leave(logger, test, &summary);
	    
	    if (summary.result != MOON_RESULT_SUCCESS && runner->option.gdb)
	    {
		pid_t pid = runner->harness->debug(runner->harness, test);
		char* breakpoint;
		
		if (summary.line)
		    breakpoint = format("%s:%u", test->file, summary.line);
		else if (summary.stage == MOON_STAGE_SETUP)
		    breakpoint = format("*%p", 
					runner->loader->
					fixture_setup(runner->loader, test->suite, library));
		else if (summary.stage == MOON_STAGE_TEARDOWN)
		    breakpoint = format("*%p", 
					runner->loader->
					fixture_teardown(runner->loader, test->suite, library));
		else
		    breakpoint = format("*%p", test->function);
		
		gdb_attach_interactive(runner->self, pid, breakpoint);
	    }
	    
	    runner->harness->cleanup(runner->harness, &summary);
	}
	
	if (current_suite)
	    logger->suite_leave(logger);
    }
    
    logger->library_leave(logger);
    
    if ((thunk = runner->loader->library_teardown(runner->loader, library)))
	thunk();
error:
    
    if (library)
        runner->loader->close(runner->loader, library);   
}

static void UnixRunner_RunSet(MuRunner* _runner, const char* path, 
                              int setc, char** set, MuError** _err)
{
    UnixRunner_Run((UnixRunner*) _runner, path, setc, set, _err);
}

static void UnixRunner_RunAll(MuRunner* _runner, const char* path, MuError** _err)
{
    UnixRunner_Run((UnixRunner*) _runner, path, 0, NULL, _err);
}

MuRunner*
Mu_UnixRunner_Create(const char* self, MuLoader* loader, MuHarness* harness, MuLogger* logger)
{
	UnixRunner* runner = malloc(sizeof(*runner));

	runner->loader = loader;
	runner->harness = harness;
	runner->logger = logger;
    runner->self = strdup(self);
	
    runner->base.run_all = UnixRunner_RunAll;
    runner->base.run_set = UnixRunner_RunSet;
    runner->base.option = unixrunner_option;

    runner->option.gdb = false;

	return (MuRunner*) runner;
}

void
Mu_UnixRunner_Destroy(MuRunner* _runner)
{
    UnixRunner* runner = (UnixRunner*) _runner;

    Mu_Plugin_DestroyLoader(runner->loader);
    Mu_Plugin_DestroyHarness(runner->harness);
    Mu_Plugin_DestroyLogger(runner->logger);

    free((void*) runner->self);
    free(runner);
}
