#include <assert.h>
#include <stdio.h>
#include <sys/_types/_time_t.h>
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
    struct tm start, end;
} datetime_range_t;

typedef struct {
    int month, year;
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

static bool datetime_is_working_hour(const struct tm* d) {
    return !datetime_is_week_end(d) && 9 <= d->tm_hour && d->tm_hour < 18;
}

static void shift_bill_hour(bill_summary_t* summary, time_t timestamp) {
    struct tm* hour = localtime(&timestamp);
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

static time_t datetime_end_of_month_timestamp(const struct tm* d) {
    struct tm tmp = *d;
    tmp.tm_mday = 1;
    tmp.tm_mon = d->tm_mon + 1;
    tmp.tm_hour = 0;
    return timelocal(&tmp);
}

static void shift_bill_monthly(gbArray(bill_summary_t) summaries,
                               datetime_range_t* shift) {
    time_t timestamp = timelocal(&shift->start);
    const time_t end_timestamp = timelocal(&shift->end);
    const time_t eom = datetime_end_of_month_timestamp(&shift->start);

    const bill_summary_t default_summary = {
        .month = shift->start.tm_mon + 1, .year = shift->start.tm_year + 1900};
    bill_summary_t* summary = NULL;

    const u64 summaries_len = gb_array_count(summaries);
    if (summaries_len > 0 &&
        summaries[summaries_len - 1].month == shift->start.tm_mon &&
        summaries[summaries_len - 1].year == shift->start.tm_year) {
        summary = &summaries[summaries_len - 1];
    } else {
        gb_array_append(summaries, default_summary);
        summary = &summaries[gb_array_count(summaries) - 1];
    }

    while (timestamp < eom) {
        shift_bill_hour(summary, timestamp);
        timestamp += 3600;
    }
    if (timestamp >= end_timestamp) return;

    gb_array_append(summaries, default_summary);
    summary = &summaries[gb_array_count(summaries) - 1];
    while (timestamp < end_timestamp) {
        shift_bill_hour(summary, timestamp);
        timestamp += 3600;
    }
}

/* static void shift_bill_datetime_range(bill_summary_t* summary, struct tm*
 * start, */
/*                                       struct tm* end) { */
/*     struct tm i = *start; */
/*     const time_t end_timestamp = timelocal(end); */
/*     while (timelocal(&i) < end_timestamp) { */
/*         shift_bill_hour(summary, &i); */
/*         /1* __builtin_dump_struct(&i, &printf); *1/ */
/*         /1* __builtin_dump_struct(summary, &printf); *1/ */
/*         datetime_add_hours(&i, 1); */
/*     } */
/* } */

static void bill(datetime_range_t* work, u64 work_len) {
    assert(work != NULL);
    assert(work_len > 0);

    gbArray(bill_summary_t) summaries = NULL;
    gb_array_init_reserve(summaries, gb_heap_allocator(), 100);
    for (int i = 0; i < work_len; i++) {
        shift_bill_monthly(summaries, &work[i]);
    }

    for (int i = 0; i < gb_array_count(summaries); i++) {
        __builtin_dump_struct(&summaries[i], &printf);
    }
}

int main() {
    datetime_range_t work[] = {
        {DATE_TIME(2021, 9, 18, 9), DATE_TIME(2021, 9, 24, 9)},
        {DATE_TIME(2021, 10, 1, 18), DATE_TIME(2021, 10, 7, 9)},
        {DATE_TIME(2021, 11, 21, 9), DATE_TIME(2021, 11, 26, 9)},
        {DATE_TIME(2021, 12, 20, 18), DATE_TIME(2021, 12, 26, 9)},
        {DATE_TIME(2022, 2, 26, 18), DATE_TIME(2022, 3, 7, 9)},
        {DATE_TIME(2022, 3, 25, 18), DATE_TIME(2022, 3, 28, 9)},
    };
    bill(work, sizeof(work) / sizeof(work[0]));

    /* { */
    /*     bill_summary_t summary = {0}; */
    /*     struct tm start = DATE_TIME(2021, 9, 18, 9); */
    /*     struct tm end = DATE_TIME(2021, 9, 24, 9); */
    /*     shift_bill_datetime_range(&summary, &start, &end); */
    /* } */
    /* { */
    /*     bill_summary_t summary = {0}; */
    /*     struct tm start = DATE_TIME(2021, 10, 1, 18); */
    /*     struct tm end = DATE_TIME(2021, 10, 7, 9); */
    /*     shift_bill_datetime_range(&summary, &start, &end); */
    /* } */
    /* { */
    /*     bill_summary_t summary = {0}; */
    /*     struct tm start = DATE_TIME(2021, 11, 21, 9); */
    /*     struct tm end = DATE_TIME(2021, 11, 26, 9); */
    /*     shift_bill_datetime_range(&summary, &start, &end); */
    /* } */
    /* { */
    /*     bill_summary_t summary = {0}; */
    /*     struct tm start = DATE_TIME(2021, 12, 20, 18); */
    /*     struct tm end = DATE_TIME(2021, 12, 26, 9); */
    /*     shift_bill_datetime_range(&summary, &start, &end); */
    /* } */
    /* { */
    /*     bill_summary_t summary = {0}; */
    /*     struct tm start = DATE_TIME(2022, 2, 26, 18); */
    /*     struct tm end = DATE_TIME(2022, 3, 1, 0); */
    /*     shift_bill_datetime_range(&summary, &start, &end); */
    /* } */
}
