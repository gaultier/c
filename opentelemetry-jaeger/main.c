#include <stdlib.h>
#include <unistd.h>

#include "opentelemetry.h"

int main() {
    getenv("NOOP") != NULL ? ot_start_noop() : ot_start();

    const __uint128_t trace_id = ot_generate_trace_id();

    ot_span_t* span_a = ot_span_create_root(trace_id, "span-a", OT_SK_CLIENT,
                                            "this is the first span");

    ot_span_t* span_b = ot_span_create_child_of(
        trace_id, "span-b", OT_SK_CLIENT, "this is the second span", span_a);
    ot_span_add_attribute(span_b, "service.name", "clone-gitlab-api");

    usleep(8);
    ot_span_end(span_b);
    ot_span_t* span_c = ot_span_create_child_of(
        trace_id, "span-c", OT_SK_SERVER, "this is the third span", span_a);
    usleep(10);
    ot_span_end(span_c);
    usleep(5);
    ot_span_end(span_a);

    ot_end();
}
