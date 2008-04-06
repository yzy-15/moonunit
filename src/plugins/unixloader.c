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

#include <moonunit/loader.h>
#include <moonunit/util.h>
#include <moonunit/test.h>
#include <moonunit/interface.h>
#include <moonunit/error.h>

#include <string.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>

#include <config.h>

#ifdef HAVE_LIBELF
#include "elfscan.h"
#endif

// Opens a library and returns a handle

struct MuLibrary
{
    MuLoader* loader;
	const char* path;
	void* dlhandle;
	MuTest** tests;
	MuLibrarySetup* library_setup;
    MuLibraryTeardown* library_teardown;
    MuFixtureSetup** fixture_setups;
    MuFixtureTeardown** fixture_teardowns;
    bool stub;
};

static void
test_init(MuTest* test, MuLibrary* library)
{
    test->loader = library->loader;
    test->library = library;
}

#ifdef HAVE_LIBELF

static bool
test_filter(const char* sym, void *unused)
{
	return !strncmp("__mu_", sym, strlen("__mu_"));
}

static bool
test_add(symbol* sym, void* _library, MuError **_err)
{
    MuLibrary* library = (MuLibrary*) _library;	

	if (!strncmp (MU_TEST_PREFIX, sym->name, strlen(MU_TEST_PREFIX)))
	{
		MuTest* test = (MuTest*) sym->addr;
		
		if (test->library)
        {
			return true; // Test was already added
        }
		else
        {
            library->tests = (MuTest**) array_append((array*) library->tests, test);

            if (!library->tests)
                MU_RAISE_RETURN(false, _err, Mu_ErrorDomain_General, MU_ERROR_NOMEM, "Out of memory");
	
            test_init(test, library);
        }
  	}
   	else if (!strncmp(MU_FS_PREFIX, sym->name, strlen(MU_FS_PREFIX)))
   	{
        MuFixtureSetup* setup = (MuFixtureSetup*) sym->addr;
		
        library->fixture_setups = (MuFixtureSetup**) array_append((array*) library->fixture_setups, setup);
	}
    else if (!strncmp(MU_FT_PREFIX, sym->name, strlen(MU_FT_PREFIX)))
   	{
        MuFixtureTeardown* teardown = (MuFixtureTeardown*) sym->addr;
		
        library->fixture_teardowns = (MuFixtureTeardown**) array_append((array*) library->fixture_teardowns, teardown);
	}
	else if (!strncmp("__mu_ls", sym->name, strlen("__mu_ls")))
	{
		library->library_setup = (MuLibrarySetup*) sym->addr;	
	}
	else if (!strncmp("__mu_lt", sym->name, strlen("__mu_lt")))
	{
		library->library_teardown = (MuLibraryTeardown*) sym->addr;
	}

    return true;
}

static bool
unixloader_scan (MuLoader* _self, MuLibrary* handle, MuError ** _err)
{
    MuError* err = NULL;

	if (!ElfScan_GetScanner()(handle->dlhandle, test_filter, test_add, handle, &err))
    {
        MU_RERAISE_GOTO(error, _err, err);
    }
	
    return true;

error:
    
    return false;
}

#endif

static bool
unixloader_can_open(MuLoader* self, const char* path)
{
    bool result;
    void* handle = mu_dlopen(path, RTLD_LAZY);

    result = (handle != NULL);

    if (handle)
	dlclose(handle);

    return result;
}

static MuLibrary*
unixloader_open(MuLoader* _self, const char* path, MuError** _err)
{
	MuLibrary* library = malloc(sizeof (MuLibrary));
#ifdef HAVE_LIBELF
    MuError* err = NULL;
#endif
    void (*stub_hook)(MuLibrarySetup** ls, MuLibraryTeardown** lt,
                      MuFixtureSetup*** fss, MuFixtureTeardown*** fts,
                      MuTest*** ts);


    if (!library)
    {
        MU_RAISE_RETURN(NULL, _err, Mu_ErrorDomain_General, MU_ERROR_NOMEM, "Out of memory");
    }

    library->loader = _self;
	library->tests = NULL;
	library->fixture_setups = NULL;
    library->fixture_teardowns = NULL;
	library->library_setup = NULL;
    library->library_teardown = NULL;
	library->path = strdup(path);
	library->dlhandle = mu_dlopen(library->path, RTLD_LAZY);
    library->stub = false;

    if (!library->dlhandle)
    {
        free(library);
        MU_RAISE_RETURN(NULL, _err, Mu_ErrorDomain_General, MU_ERROR_GENERIC, "%s", dlerror());
    }

    if ((stub_hook = dlsym(library->dlhandle, "__mu_stub_hook")))
    {
        int i;
        stub_hook(&library->library_setup, &library->library_teardown,
                  &library->fixture_setups, &library->fixture_teardowns,
                  &library->tests);

        for (i = 0; library->tests[i]; i++)
        {
            test_init(library->tests[i], library);
        }

        library->stub = true;
    }
#ifdef HAVE_LIBELF
    else if (!unixloader_scan(_self, library, &err))
    {
        dlclose(library->dlhandle);
        free(library);
        
        MU_RERAISE_RETURN(NULL, _err, err);
    }
#else
    else
    {
        MU_RAISE_RETURN(NULL, _err, Mu_ErrorDomain_General, MU_ERROR_GENERIC, 
                        "Library did not contain a test loading stub and no "
                        "reflection backend is available.");
    }
#endif
	return library;
}

static MuTest**
unixloader_get_tests (MuLoader* _self, MuLibrary* handle)
{
	return (MuTest**) array_from_generic((void**) handle->tests);
}
    
static void
unixloader_free_tests (MuLoader* _self, MuLibrary* handle, MuTest** tests)
{
    array_free((array*) tests);
}

// Returns the library setup routine for handle
static MuThunk
unixloader_library_setup (MuLoader* _self, MuLibrary* handle)
{
    if (handle->library_setup)
    	return handle->library_setup->run;
    else
        return NULL;
}

static MuThunk
unixloader_library_teardown (MuLoader* _self, MuLibrary* handle)
{
    if (handle->library_teardown)
    	return handle->library_teardown->run;
    else
        return NULL;
}

static MuTestThunk
unixloader_fixture_setup (MuLoader* _self, const char* name, MuLibrary* handle)
{
	unsigned int i;
	
	for (i = 0; handle->fixture_setups[i]; i++)
	{
		if (!strcmp(name, handle->fixture_setups[i]->name))
        {
			return handle->fixture_setups[i]->run;
        }
	}
    
	return NULL;
}

static MuTestThunk
unixloader_fixture_teardown (MuLoader* _self, const char* name, MuLibrary* handle)
{
	unsigned int i;
	
	for (i = 0; handle->fixture_teardowns[i]; i++)
	{
		if (!strcmp(name, handle->fixture_teardowns[i]->name))
        {
			return handle->fixture_teardowns[i]->run;
        }
	}
	
	return NULL;
}
   
static void
unixloader_close (MuLoader* _self, MuLibrary* handle)
{
	dlclose(handle->dlhandle);
	free((void*) handle->path);

    if (!handle->stub)
    {
    	free(handle->tests);
        free(handle->fixture_setups);
        free(handle->fixture_teardowns);
    }
   	free(handle);
}

static const char*
unixloader_name (MuLoader* _self, MuLibrary* handle)
{
	return basename_pure(handle->path);
}

MuLoader mu_unixloader =
{
    .can_open = unixloader_can_open,
	.open = unixloader_open,
	.get_tests = unixloader_get_tests,
	.free_tests = unixloader_free_tests,
	.library_setup = unixloader_library_setup,
	.library_teardown = unixloader_library_teardown,
	.fixture_setup = unixloader_fixture_setup,
	.fixture_teardown = unixloader_fixture_teardown,
	.close = unixloader_close,
	.name = unixloader_name
};