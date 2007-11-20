#include <dlfcn.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <signal.h>

#include <moonunit/harness.h>
#include <moonunit/test.h>
#include <moonunit/loader.h>
#include <moonunit/runner.h>
#include <moonunit/util.h>

#define ALIGNMENT 60

#define die(...)                                \
    do {                                        \
        fprintf(stderr, __VA_ARGS__);           \
        exit(1);                                \
    } while (0);                                \

static void library_enter(const char* name)
{
	printf("Library: %s\n", name);
}

static void library_leave()
{
	printf("\n");
}

static void suite_enter(const char* name)
{
	printf("  Suite: %s\n", name);
}

static void suite_leave()
{
	printf("\n");
}

static void result(MoonUnitTest* test, MoonUnitTestSummary* summary)
{
	int i;
	const char* reason, * stage;
	char* failure_message;
	printf("    %s:", test->name);
	
	switch (summary->result)
	{
		case MOON_RESULT_SUCCESS:
			for (i = ALIGNMENT - strlen(test->name) - 5 - 4; i > 0; i--)
				printf(" ");
			printf("\e[32mPASS\e[0m\n");
			break;
		case MOON_RESULT_FAILURE:
		case MOON_RESULT_ASSERTION:
		case MOON_RESULT_CRASH:
			stage = Mu_TestStageToString(summary->stage);
			
			for (i = ALIGNMENT - strlen(test->name) - strlen(stage) - 3 - 5 - 4; i > 0; i--)
				printf(" ");
			
			reason = summary->reason ? summary->reason : "unknown";
			printf("(%s) \e[31mFAIL\e[0m\n", stage);
			
			failure_message = summary->line != 0 ? 
				  format("%s:%i: %s", test->file, summary->line, reason)
				: format("%s", reason);

			for (i = ALIGNMENT - strlen(failure_message); i > 0; i--)
				printf(" ");
			printf("%s\n", failure_message);
	}
}

static MoonUnitLogger logger =
{
	library_enter,
	library_leave,
	suite_enter,
	suite_leave,
	result
};

int main (int argc, char** argv)
{
	MoonUnitRunner* runner = Mu_Runner_New(&mu_unixloader, &mu_unixharness, &logger);
	int i;
	
	for (i = 1; i < argc; i++)
	{
		Mu_Runner_RunTests(runner, argv[i]);
	}
	
	return 0;
}
