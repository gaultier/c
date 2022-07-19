#pragma once

#include <stdbool.h>

typedef enum __attribute__((packed)) {
    OT_ST_OK = 0,
    OT_ST_CANCELLED = 1,
    OT_ST_UNKNOWN_ERROR = 2,
    OT_ST_INVALIDARGUMENT = 3,
    OT_ST_DEADLINE_EXCEEDED = 4,
    OT_ST_NOT_FOUND = 5,
    OT_ST_ALREADY_EXISTS = 6,
    OT_ST_PERMISSION_DENIED = 7,
    OT_ST_RESOURCE_EXHAUSTED = 8,
    OT_ST_FAILED_PRECONDITION = 9,
    OT_ST_ABORTED = 10,
    OT_ST_OUT_OF_RANGE = 11,
    OT_ST_UNIMPLEMENTED = 12,
    OT_ST_INTERNAL_ERROR = 13,
    OT_ST_UNAVAILABLE = 14,
    OT_ST_DATALOSS = 15,
    OT_ST_UNAUTHENTICATED = 16,
} ot_span_status_t;

typedef enum __attribute__((packed)) {
    OT_SK_UNSPECIFIED = 0,
    // Indicates that the span represents an internal operation within an
    // application, as opposed to an operations happening at the boundaries.
    // Default value.
    OT_SK_INTERNAL = 1,

    // Indicates that the span covers server-side handling of an RPC or other
    // remote network request.
    OT_SK_SERVER = 2,

    // Indicates that the span describes a request to some remote service.
    OT_SK_CLIENT = 3,

    // Indicates that the span describes a producer sending a message to a
    // broker. Unlike CLIENT and SERVER, there is often no direct critical path
    // latency relationship between producer and consumer spans. A PRODUCER span
    // ends when the message was accepted by the broker while the logical
    // processing of the message might span a much longer time.
    OT_SK_PRODUCER = 4,

    // Indicates that the span describes consumer receiving a message from a
    // broker. Like the PRODUCER kind, there is often no direct critical path
    // latency relationship between producer and consumer spans.
    OT_SK_CONSUMER = 5,
} ot_span_kind_t;

typedef struct ot_span_t ot_span_t;

ot_span_t* ot_span_create_root(__uint128_t trace_id, char* name,
                               ot_span_kind_t kind, char* message);
ot_span_t* ot_span_create_child_of(__uint128_t trace_id, char* name,
                                   ot_span_kind_t kind, char* message,
                                   const ot_span_t* parent_span);
void ot_span_end(ot_span_t* span);
bool ot_span_add_attribute(ot_span_t* span, char* key, char* value,
                           bool should_free_value);
void ot_span_set_status(ot_span_t* span, ot_span_status_t status);
void ot_span_set_udata(ot_span_t* span, void* udata);
void* ot_span_get_udata(ot_span_t* span);

void ot_start();
void ot_start_noop();
void ot_end();
__uint128_t ot_generate_trace_id();

