#include "opentelemetry.h"

int main() {
    ot_start();

    const __uint128_t trace_id = ot_generate_trace_id();

    ot_span_t* span_a = ot_span_create(trace_id, "span-a", OT_SK_CLIENT,
                                       "this is the first span", 0);

    ot_span_t* span_b =
        ot_span_create(trace_id, "span-b", OT_SK_CLIENT,
                       "this is the second span", span_a->trace_id);

    usleep(8);
    ot_span_end(span_b);
    ot_span_t* span_c =
        ot_span_create(trace_id, "span-c", OT_SK_SERVER,
                       "this is the third span", span_a->trace_id);
    usleep(10);
    ot_span_end(span_c);
    usleep(5);
    ot_span_end(span_a);

    ot_end();
}
