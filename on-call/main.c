#include <assert.h>
#include <stdio.h>
#include <time.h>

#include "../pg/pg.h"

static const uint64_t hourly_week_end_rate = 15;
static const uint64_t hourly_week_rate = 10;

#define DATE_TIME(year, month, day, hour)                                      \
  (struct tm) {                                                                \
    .tm_year = year - 1900, .tm_mon = month - 1, .tm_mday = day,               \
    .tm_hour = hour,                                                           \
  }

typedef struct {
  struct tm start, end;
} datetime_range_t;

typedef struct {
  int month, year; // In ISO range
  uint64_t week_money, week_hours, week_end_money, week_end_hours, total_hours,
      total_money;
} monthly_bill_t;

static bool datetime_is_week_end(const struct tm *d) {
  return d->tm_wday == /* Sunday */ 0 || d->tm_wday == /* Saturday */ 6;
}

static bool datetime_is_working_hour(const struct tm *d) {
  // Work hours in local time: 9AM-6PM
  return !datetime_is_week_end(d) && 9 <= d->tm_hour && d->tm_hour < 18;
}

static void shift_bill_hour(monthly_bill_t *monthly_bill, time_t timestamp) {
  struct tm *hour = localtime(&timestamp);
  if (datetime_is_working_hour(hour))
    return;

  monthly_bill->total_hours += 1;
  if (datetime_is_week_end(hour)) {
    monthly_bill->week_end_hours += 1;
    monthly_bill->week_end_money += hourly_week_end_rate;
    monthly_bill->total_money += hourly_week_end_rate;
  } else {
    monthly_bill->week_hours += 1;
    monthly_bill->week_money += hourly_week_rate;
    monthly_bill->total_money += hourly_week_rate;
  }
}

static time_t get_start_of_next_month(const struct tm *d) {
  struct tm tmp = *d;
  tmp.tm_mday = 1;
  tmp.tm_mon = d->tm_mon + 1;
  tmp.tm_hour = 0;
  return timelocal(&tmp);
}

static void bill_shift_monthly(pg_array_t(monthly_bill_t) monthly_bills,
                               datetime_range_t *shift) {
  monthly_bill_t *monthly_bill = NULL;
  // Upsert
  {
    const uint64_t monthly_bills_len = pg_array_len(monthly_bills);
    if (monthly_bills_len > 0 &&
        (monthly_bills[monthly_bills_len - 1].month - 1) ==
            shift->start.tm_mon &&
        (monthly_bills[monthly_bills_len - 1].year - 1900) ==
            shift->start.tm_year) {
      monthly_bill = &monthly_bills[monthly_bills_len - 1];
    } else {
      pg_array_append(monthly_bills,
                      ((monthly_bill_t){.month = shift->start.tm_mon + 1,
                                        .year = shift->start.tm_year + 1900}));
      monthly_bill = &monthly_bills[pg_array_len(monthly_bills) - 1];
    }
  }

  time_t it = timelocal(&shift->start);
  const time_t end = timelocal(&shift->end);
  assert(it < end);
  const time_t start_of_next_month = get_start_of_next_month(&shift->start);

  // Bill from the start of the shift until the end of the month
  // or until the end of shift, whichever comes first
  while (it < MIN(end, start_of_next_month)) {
    shift_bill_hour(monthly_bill, it);
    it += 3600;
  }
  if (shift->start.tm_mon == shift->end.tm_mon)
    return;

  // Handle the case of a shift spanning 2 months so it appears in 2 monthly
  // bills
  assert(shift->start.tm_mon + 1 == shift->end.tm_mon);
  pg_array_append(monthly_bills,
                  ((monthly_bill_t){.month = shift->end.tm_mon + 1,
                                    .year = shift->end.tm_year + 1900}));
  monthly_bill = &monthly_bills[pg_array_len(monthly_bills) - 1];

  // Bill from the first of the month to the end of the shift
  while (it < end) {
    shift_bill_hour(monthly_bill, it);
    it += 3600;
  }
}

static void bill_shifts(datetime_range_t *shifts, uint64_t shifts_len) {
  assert(shifts != NULL);
  assert(shifts_len > 0);

  pg_array_t(monthly_bill_t) monthly_bills = {0};
  pg_array_init_reserve(monthly_bills, 100, pg_heap_allocator());

  for (uint64_t i = 0; i < shifts_len; i++) {
    bill_shift_monthly(monthly_bills, &shifts[i]);
  }

  const double tax_rate = 0.425;
  uint64_t total_money = 0;
  // clang-format off
    printf("┌──────────┬────────────┬────────┬────────────────┬────────────┬─────────────┬─────────────────┬───────────────┐\n");
    printf("│   Month  │ Week hours │ Week € │ Week-end hours │ Week-end € │ Total hours │ Total (gross) € │ Total (net) € │ \n");
    printf("├──────────┼────────────┼────────┼────────────────┼────────────┼─────────────┼─────────────────┼───────────────┤\n"); 
    for (uint64_t i = 0; i < pg_array_len(monthly_bills); i++) {
        const monthly_bill_t* const bill = &monthly_bills[i];
        printf("│ %4d-%02d  │   %5llu    │ %5llu  │     %5llu      │    %5llu   │    %5llu    │      %5llu      │   %7.02f     │  \n", 
            bill->year, bill->month, bill->week_hours, bill->week_money,
            bill->week_end_hours, bill->week_end_money, bill->total_hours,
            bill->total_money,            (double)bill->total_money*(1-tax_rate));

        total_money += bill->total_money;
    }

    printf("├──────────┴────────────┴────────┴────────────────┴────────────┴─────────────┼─────────────────┼───────────────┤\n"); 
    printf("│ Sum                                                                        │      %5llu      │   %7.02f    │\n", total_money, (double)total_money*(1-tax_rate));
    printf("└────────────────────────────────────────────────────────────────────────────┴─────────────────┴───────────────┘\n");
  // clang-format on
}

int main(void) {
  datetime_range_t shifts[] = {
      {DATE_TIME(2021, 9, 18, 9), DATE_TIME(2021, 9, 24, 9)},
      {DATE_TIME(2021, 10, 1, 18), DATE_TIME(2021, 10, 7, 9)},
      {DATE_TIME(2021, 11, 21, 9), DATE_TIME(2021, 11, 26, 9)},
      {DATE_TIME(2021, 12, 20, 18), DATE_TIME(2021, 12, 26, 9)},
      {DATE_TIME(2022, 2, 26, 18), DATE_TIME(2022, 3, 7, 9)},
      {DATE_TIME(2022, 3, 25, 18), DATE_TIME(2022, 3, 28, 9)},
      {DATE_TIME(2022, 4, 1, 18), DATE_TIME(2022, 4, 6, 18)},
      {DATE_TIME(2022, 4, 7, 18), DATE_TIME(2022, 4, 11, 9)},
      {DATE_TIME(2022, 5, 2, 18), DATE_TIME(2022, 5, 9, 9)},
      {DATE_TIME(2022, 6, 11, 9), DATE_TIME(2022, 6, 17, 9)},
      {DATE_TIME(2022, 7, 3, 9), DATE_TIME(2022, 7, 4, 9)},
      {DATE_TIME(2022, 7, 6, 18), DATE_TIME(2022, 7, 9, 0)},
      {DATE_TIME(2022, 7, 10, 0), DATE_TIME(2022, 7, 11, 9)},
      {DATE_TIME(2022, 7, 20, 18), DATE_TIME(2022, 7, 22, 9)},
      {DATE_TIME(2022, 7, 22, 20), DATE_TIME(2022, 7, 23, 9)},
      {DATE_TIME(2022, 9, 12, 18), DATE_TIME(2022, 9, 14, 9)},
      {DATE_TIME(2022, 9, 26, 18), DATE_TIME(2022, 9, 27, 9)},
      {DATE_TIME(2022, 9, 28, 18), DATE_TIME(2022, 10, 3, 9)},
      {DATE_TIME(2022, 10, 24, 18), DATE_TIME(2022, 10, 28, 9)},
      {DATE_TIME(2022, 11, 4, 18), DATE_TIME(2022, 11, 7, 9)},
      {DATE_TIME(2022, 11, 21, 18), DATE_TIME(2022, 11, 23, 9)},
      {DATE_TIME(2022, 12, 7, 18), DATE_TIME(2022, 12, 8, 9)},
      {DATE_TIME(2022, 12, 10, 0), DATE_TIME(2022, 12, 12, 9)},
      {DATE_TIME(2022, 12, 23, 18), DATE_TIME(2022, 12, 27, 9)},
  };
  bill_shifts(shifts, sizeof(shifts) / sizeof(shifts[0]));
}
