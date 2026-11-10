/*
 * Мини-фреймворк спецификаций (в духе BDD):
 *
 *     DESCRIBE("Многоугольник") {
 *         IT("считает площадь квадрата") {
 *             EXPECT_NEAR(tl_polygon_area(&square), 100.0, 1e-9);
 *         }
 *     }
 *
 * Каждое IT печатается как пункт спецификации с отметкой «✓» или «✗»;
 * при ошибках печатаются выражение, файл и строка.
 */
#ifndef SPEC_H
#define SPEC_H

#include <stdbool.h>

int spec_enter_describe(const char *subject);
int spec_leave_describe(void);
int spec_enter_it(const char *behaviour);
int spec_leave_it(void);

void spec_check(bool passed, const char *expression, const char *file, int line);
void spec_check_near(double actual, double expected, double tolerance, const char *actual_text,
                     const char *file, int line);
void spec_check_int(long long actual, long long expected, const char *actual_text,
                    const char *file, int line);
void spec_check_str(const char *actual, const char *expected, const char *actual_text,
                    const char *file, int line);

/* Итог: количество упавших пунктов. */
int spec_report(void);

#define DESCRIBE(subject)                                                           \
    for (int spec_describe_once_ = spec_enter_describe(subject); spec_describe_once_; \
         spec_describe_once_ = spec_leave_describe())

#define IT(behaviour)                                                               \
    for (int spec_it_once_ = spec_enter_it(behaviour); spec_it_once_;                \
         spec_it_once_ = spec_leave_it())

#define EXPECT(condition) spec_check((condition), #condition, __FILE__, __LINE__)
#define EXPECT_NEAR(actual, expected, tolerance) \
    spec_check_near((actual), (expected), (tolerance), #actual, __FILE__, __LINE__)
#define EXPECT_INT(actual, expected) \
    spec_check_int((long long)(actual), (long long)(expected), #actual, __FILE__, __LINE__)
#define EXPECT_STR(actual, expected) \
    spec_check_str((actual), (expected), #actual, __FILE__, __LINE__)

#endif
