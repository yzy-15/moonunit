#include <moonunit/test.h>
#include <moonunit/harness.h>
#include <moonunit/util.h>

#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <stdio.h>

void
__mu_assert(MoonUnitTest* test, int result, const char* expr,
            const char* file, unsigned int line)
{

    if (result)
        return;
    else
    {
        MoonUnitTestSummary summary;
        
        summary.result = MOON_RESULT_ASSERTION;
        summary.stage = MOON_STAGE_TEST;
        summary.reason = format("Assertion '%s' failed", expr);
        summary.line = line;
        
        test->harness->result(test, &summary);

        free((void*) summary.reason);
    }
}

static void
assert_equal_integer(const char* expr, const char* expected, va_list ap, int* result, char** reason)
{
	int a = va_arg(ap, int);
	int b = va_arg(ap, int);
	
	*result = a == b;
	if (!*result)
		*reason = format("Assertion '%s == %s' failed (%i != %i)", expr, expected, a, b);
}

void
__mu_assert_equal(MoonUnitTest* test, const char* expr, const char* expected, const char* file, unsigned int line, const char* type, ...)
{
	int result;
	char* reason;
	
	va_list ap;
	
	va_start(ap, type);
	
	if (!strcmp (type, "%i"))
	{
		assert_equal_integer(expr, expected, ap, &result, &reason);
	}
	
    if (result)
        return;
    else
    {
        MoonUnitTestSummary summary;
        
        summary.result = MOON_RESULT_ASSERTION;
        summary.stage = MOON_STAGE_TEST;
        summary.reason = reason;
        summary.line = line;
        
        test->harness->result(test, &summary);

        free(reason);
    }
}

void
__mu_success(MoonUnitTest* test)
{
    MoonUnitTestSummary summary;

    summary.result = MOON_RESULT_SUCCESS;
    summary.stage = MOON_STAGE_TEST;
    summary.reason = NULL;
    summary.line = 0;

    test->harness->result(test, &summary);
}
 
void   
__mu_failure(MoonUnitTest* test, const char* file, unsigned int line, const char* message, ...)
{
    va_list ap;
    MoonUnitTestSummary summary;

    va_start(ap, message);
    summary.result = MOON_RESULT_FAILURE;
    summary.stage = MOON_STAGE_TEST;
    summary.reason = formatv(message, ap);
    summary.line = line;

    test->harness->result(test, &summary);

    free((void*) summary.reason);
}
