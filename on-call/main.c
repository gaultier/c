#include <assert.h>
#include <stdio.h>
#include <time.h>

#define GB_IMPLEMENTATION
#define GB_STATIC
#include "../vendor/gb/gb.h"

static const u16 hourly_week_end_rate = 15;
static const u16 hourly_week_rate = 10;

#define DATE_TIME(year, month, day, hour)                            \
    (struct tm) {                                                    \
        .tm_year = year - 1900, .tm_mon = month - 1, .tm_mday = day, \
        .tm_hour = hour,                                             \
    }

typedef struct {
    u16 week_money, week_hours, week_end_money, week_end_hours, total_hours,
        total_money;
} bill_summary_t;

static bool datetime_is_week_end(const struct tm* d) {
    return d->tm_wday == 0 || d->tm_wday == 6;
}

static void datetime_add_hours(struct tm* d, u16 hours) {
    struct tm cpy = *d;
    cpy.tm_hour += hours;
    time_t t = timelocal(&cpy);
    struct tm res = {0};
    assert(localtime_r(&t, &res) != NULL);
    *d = res;
}

static bool datetime_is_non_working_hour(const struct tm* d) {
    return !datetime_is_week_end(d) && d->tm_hour < 9 && d->tm_hour >= 18;
}

static bool datetime_is_working_hour(const struct tm* d) {
    return !datetime_is_week_end(d) && 9 <= d->tm_hour && d->tm_hour < 18;
}

static void shift_bill_hour(bill_summary_t* summary, const struct tm* hour) {
    if (datetime_is_working_hour(hour)) return;

    summary->total_hours += 1;
    if (datetime_is_week_end(hour)) {
        summary->week_end_hours += 1;
        summary->week_end_money += hourly_week_end_rate;
        summary->total_money += hourly_week_end_rate;
    } else {
        summary->week_hours += 1;
        summary->week_money += hourly_week_rate;
        summary->total_money += hourly_week_rate;
    }
}

static void shift_bill_datetime_range(bill_summary_t* summary, struct tm* start,
                                      struct tm* end) {
    struct tm i = *start;
    const time_t end_timestamp = timelocal(end);
    while (timelocal(&i) < end_timestamp) {
        shift_bill_hour(summary, &i);
        __builtin_dump_struct(&i, &printf);
        __builtin_dump_struct(summary, &printf);
        datetime_add_hours(&i, 1);
    }
    __builtin_dump_struct(summary, &printf);
}

int main() {
    bill_summary_t summary = {0};
    struct tm start = DATE_TIME(2021, 9, 18, 9);
    struct tm end = DATE_TIME(2021, 9, 24, 9);

    /* struct tm start = DATE_TIME(2021, 9, 17, 0); */
    /* struct tm end = DATE_TIME(2021, 9, 17, 9); */
    shift_bill_datetime_range(&summary, &start, &end);
}
