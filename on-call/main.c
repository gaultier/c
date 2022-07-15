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

static void shift_bill_monthly(bill_summary_t* summary, struct tm* start,
                               struct tm* end) {
    struct tm i = *start;
    const time_t end_timestamp = timelocal(end);
    while (timelocal(&i) < end_timestamp) {
        shift_bill_hour(summary, &i);
        /* __builtin_dump_struct(&i, &printf); */
        /* __builtin_dump_struct(summary, &printf); */
        datetime_add_hours(&i, 1);
    }
}
static void shift_bill_datetime_range(bill_summary_t* summary, struct tm* start,
                                      struct tm* end) {
    struct tm i = *start;
    const time_t end_timestamp = timelocal(end);
    while (timelocal(&i) < end_timestamp) {
        shift_bill_hour(summary, &i);
        /* __builtin_dump_struct(&i, &printf); */
        /* __builtin_dump_struct(summary, &printf); */
        datetime_add_hours(&i, 1);
    }
}

static time_t datetime_end_of_month_timestamp(const struct tm* d) {
    struct tm tmp = *d;
    tmp.tm_mday = 1;
    tmp.tm_mon = d->tm_mon + 1;
    tmp.tm_hour = 0;
    return timelocal(&tmp);
}

static void bill(datetime_range_t* work, u64 work_len) {
    assert(work != NULL);
    assert(work_len > 0);

    bill_summary_t summary = {0};

    time_t work_start = timelocal(&work[0].start);
    time_t work_end = timelocal(&work[work_len - 1].end);
    time_t timestamp = work_start;

    int work_j = 0;
    datetime_range_t* shift = &work[work_j];
    while (timestamp < work_end) {
        int month = shift->start.tm_mon;
        int year = shift->start.tm_year;
        shift_bill_datetime_range(&summary, &shift->start, &shift->end);

        work_j++;
        if (work_j == work_len) return;
        timestamp = timelocal(&work[work_j].start);
        shift = &work[work_j];
        if (month != shift->start.tm_mon) {
            printf("%d-%d\n", year + 1900, month + 1);
            __builtin_dump_struct(&summary, &printf);
            puts("");
            summary = (bill_summary_t){0};
        }
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
