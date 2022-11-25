#include <_types/_uint64_t.h>
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char *argv[]) {
  assert(argc == 4);

  const double yearly_interest_rate = strtod(argv[1], NULL);
  const double monthly_interest_rate = yearly_interest_rate / 12;
  const uint64_t months_total = strtoull(argv[2], NULL, 10);
  const double principal = (double)(strtoull(argv[3], NULL, 10));

  double paid = 0;
  const double monthly_payment =
      principal * monthly_interest_rate *
      pow(1 + monthly_interest_rate, (double)months_total) /
      (pow(1 + monthly_interest_rate, (double)months_total) - 1);
  for (uint64_t month = 1; month <= months_total; month++) {
    paid += monthly_payment;
  }
  assert(paid > principal);
  printf("Monthly payment=%2.f Paid=%.2f Interest paid=%.2f\n", monthly_payment,
         paid, paid - principal);
}
