#include "spec.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define MAX_MESSAGES 8
#define MESSAGE_LENGTH 512

static int failed_behaviours;
static int passed_behaviours;
static int current_failures;
static char messages[MAX_MESSAGES][MESSAGE_LENGTH];
static int message_count;
static const char *current_behaviour;

static void remember_failure(const char *text)
{
    current_failures++;
    if (message_count < MAX_MESSAGES) {
        snprintf(messages[message_count], MESSAGE_LENGTH, "%s", text);
        message_count++;
    }
}

int spec_enter_describe(const char *subject)
{
    printf("\n%s\n", subject);
    return 1;
}

int spec_leave_describe(void) { return 0; }

int spec_enter_it(const char *behaviour)
{
    current_behaviour = behaviour;
    current_failures = 0;
    message_count = 0;
    return 1;
}

int spec_leave_it(void)
{
    if (current_failures == 0) {
        passed_behaviours++;
        printf("  ✓ %s\n", current_behaviour);
    } else {
        failed_behaviours++;
        printf("  ✗ %s\n", current_behaviour);
        for (int i = 0; i < message_count; i++)
            printf("      %s\n", messages[i]);
    }
    return 0;
}

void spec_check(bool passed, const char *expression, const char *file, int line)
{
    if (passed)
        return;
    char text[MESSAGE_LENGTH];
    snprintf(text, sizeof text, "%s:%d: ожидалось истинное: %s", file, line, expression);
    remember_failure(text);
}

void spec_check_near(double actual, double expected, double tolerance, const char *actual_text,
                     const char *file, int line)
{
    if (fabs(actual - expected) <= tolerance)
        return;
    char text[MESSAGE_LENGTH];
    snprintf(text, sizeof text, "%s:%d: %s = %.6g, ожидалось %.6g (±%g)", file, line,
             actual_text, actual, expected, tolerance);
    remember_failure(text);
}

void spec_check_int(long long actual, long long expected, const char *actual_text,
                    const char *file, int line)
{
    if (actual == expected)
        return;
    char text[MESSAGE_LENGTH];
    snprintf(text, sizeof text, "%s:%d: %s = %lld, ожидалось %lld", file, line, actual_text,
             actual, expected);
    remember_failure(text);
}

void spec_check_str(const char *actual, const char *expected, const char *actual_text,
                    const char *file, int line)
{
    if (actual != NULL && expected != NULL && strcmp(actual, expected) == 0)
        return;
    char text[MESSAGE_LENGTH];
    snprintf(text, sizeof text, "%s:%d: %s = \"%s\", ожидалось \"%s\"", file, line, actual_text,
             actual ? actual : "(null)", expected ? expected : "(null)");
    remember_failure(text);
}

int spec_report(void)
{
    printf("\nПунктов спецификации: %d, выполнено: %d, не выполнено: %d\n",
           passed_behaviours + failed_behaviours, passed_behaviours, failed_behaviours);
    return failed_behaviours;
}
