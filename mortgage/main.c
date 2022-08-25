#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "../pg.h"

int main(int argc, char* argv[]) {
    assert(argc == 4);

    const float yearly_interest_rate = strtof(argv[1], NULL);
    const float monthly_interest_rate = yearly_interest_rate / 12;
    const u64 months_total = strtoull(argv[2], NULL, 10);
    const float principal = strtoull(argv[3], NULL, 10);

    float paid = 0;
    const float monthly_payment =
        principal * monthly_interest_rate *
        powf(1 + monthly_interest_rate, months_total) /
        (powf(1 + monthly_interest_rate, months_total) - 1);
    for (u64 month = 1; month <= months_total; month++) {
        paid += monthly_payment;
    }
    assert(paid > principal);
    printf("Monthly payment=%2.f Paid=%.2f Interest paid=%.2f\n",
           monthly_payment, paid, paid - principal);
}
