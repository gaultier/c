#include <assert.h>
#include <curl/curl.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

#define CJSON_HIDE_SYMBOLS
#include "vendor/cJSON/cJSON.h"

typedef enum __attribute__((packed)) {
    OT_ST_Ok = 0,
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

struct ot_span_t {
    uint64_t start_time_unix_nano, end_time_unix_nano;
    uint64_t span_id, parent_span_id;
    __uint128_t trace_id;
    ot_span_kind_t kind;
    ot_span_status_t status;
    char name[128];
    char message[128];
    struct ot_span_t* next;
};
typedef struct ot_span_t ot_span_t;

static ot_span_t* spans = NULL;
static pthread_cond_t ot_spans_to_export;
static pthread_mutex_t ot_spans_mtx;
static bool ot_finished;

cJSON* ot_spans_to_json(const ot_span_t* span) {
    cJSON* root = cJSON_CreateObject();

    cJSON* resourceSpans = cJSON_AddArrayToObject(root, "resourceSpans");

    cJSON* resourceSpan = cJSON_CreateObject();
    cJSON_AddItemToArray(resourceSpans, resourceSpan);

    cJSON* resource = cJSON_AddObjectToObject(resourceSpan, "resource");

    cJSON* attributes = cJSON_AddArrayToObject(resource, "attributes");
    cJSON* attribute = cJSON_CreateObject();
    cJSON_AddItemToArray(attributes, attribute);
    cJSON_AddStringToObject(attribute, "key", "service.name");
    cJSON* value = cJSON_CreateObject();
    cJSON_AddItemToObject(attribute, "value", value);
    cJSON_AddStringToObject(value, "stringValue", "main.c");

    cJSON* instrumentationLibrarySpans =
        cJSON_AddArrayToObject(resourceSpan, "instrumentationLibrarySpans");

    cJSON* instrumentationLibrarySpan = cJSON_CreateObject();
    cJSON_AddItemToArray(instrumentationLibrarySpans,
                         instrumentationLibrarySpan);

    cJSON* j_spans =
        cJSON_AddArrayToObject(instrumentationLibrarySpan, "spans");

    cJSON* j_span = cJSON_CreateObject();
    cJSON_AddItemToArray(j_spans, j_span);
    cJSON_AddNumberToObject(j_span, "startTimeUnixNano",
                            span->start_time_unix_nano);
    cJSON_AddNumberToObject(j_span, "endTimeUnixNano",
                            span->end_time_unix_nano);

    char buf[64] = "";
    uint8_t trace_id[16] = {};
    memcpy(trace_id, &span->trace_id, sizeof(trace_id));
    for (int i = 0; i < sizeof(trace_id); i++) {
        snprintf(&buf[i * 2], sizeof(buf), "%02x", trace_id[i]);
    }
    cJSON_AddStringToObject(j_span, "traceId", buf);

    memset(buf, 0, sizeof(buf));
    uint8_t span_id[8] = {};
    memcpy(span_id, &span->span_id, sizeof(span_id));
    for (int i = 0; i < sizeof(span_id); i++) {
        snprintf(&buf[i * 2], sizeof(buf), "%02x", span_id[i]);
    }
    cJSON_AddStringToObject(j_span, "spanId", buf);
    cJSON_AddNumberToObject(j_span, "kind", span->kind);
    cJSON* status = cJSON_AddObjectToObject(j_span, "status");
    cJSON_AddNumberToObject(status, "code", span->status);
    cJSON_AddStringToObject(status, "message", span->message);

    cJSON_AddStringToObject(j_span, "name", span->name);

    return root;
}

__uint128_t ot_generate_trace_id() {
    __uint128_t trace_id = 0;
    arc4random_buf(&trace_id, sizeof(trace_id));
    return trace_id;
}

ot_span_t* ot_span_create(__uint128_t trace_id, char* name, uint8_t name_len,
                          ot_span_kind_t kind, ot_span_status_t status,
                          char* message, uint8_t message_len,
                          uint64_t parent_span_id) {
    struct timeval now = {0};
    gettimeofday(&now, NULL);

    ot_span_t* span = calloc(1, sizeof(ot_span_t));

    span->start_time_unix_nano =
        now.tv_sec * 1000 * 1000 * 1000 + now.tv_usec * 1000 * 1000;
    span->trace_id = trace_id;
    span->status = status;
    span->kind = kind;
    span->parent_span_id = parent_span_id;

    arc4random_buf(&span->span_id, sizeof(span->span_id));
    assert(name_len <= sizeof(span->name));
    memcpy(span->name, name, name_len);
    assert(message_len <= sizeof(span->message));
    memcpy(span->message, message, message_len);

    return span;
}

void ot_span_end(ot_span_t* span) {
    struct timeval now = {0};
    gettimeofday(&now, NULL);

    span->end_time_unix_nano =
        now.tv_sec * 1000 * 1000 * 1000 + now.tv_usec * 1000 * 1000;

    pthread_mutex_lock(&ot_spans_mtx);
    span->next = spans;
    spans = span;
    pthread_cond_signal(&ot_spans_to_export);
    printf("span_end: name=%s\n", span->name);
    fflush(stdout);
    pthread_mutex_unlock(&ot_spans_mtx);
}

void* ot_export(void* varg) {
    // TODO: batching
    while (true) {
        pthread_mutex_lock(&ot_spans_mtx);
        while (spans == NULL) {
            if (ot_finished) {
                pthread_mutex_unlock(&ot_spans_mtx);
                pthread_exit(NULL);
            }
            pthread_cond_wait(&ot_spans_to_export, &ot_spans_mtx);
        }
        ot_span_t* span = spans;
        spans = spans->next;
        printf("span would be exported: span_id=%02llx\n", span->span_id);
        cJSON* root = ot_spans_to_json(span);
        puts(cJSON_Print(root));
        cJSON_free(root);
        free(span);
        fflush(stdout);

        pthread_mutex_unlock(&ot_spans_mtx);
    }

    return NULL;
}

int main() {
    pthread_mutex_init(&ot_spans_mtx, NULL);
    pthread_cond_init(&ot_spans_to_export, NULL);
    pthread_t ot_exporter = NULL;
    pthread_create(&ot_exporter, NULL, ot_export, NULL);

    const __uint128_t trace_id = ot_generate_trace_id();

    ot_span_t* span_a =
        ot_span_create(trace_id, "span-a", sizeof("span-a") - 1, OT_SK_CLIENT,
                       OT_ST_OUT_OF_RANGE, "this is the first span",
                       sizeof("this is the first span") - 1, 0);

    ot_span_t* span_b =
        ot_span_create(trace_id, "span-b", sizeof("span-b") - 1, OT_SK_CLIENT,
                       OT_ST_OUT_OF_RANGE, "this is the second span",
                       sizeof("this is the second span") - 1, span_a->trace_id);

    usleep(8);
    ot_span_end(span_b);
    ot_span_t* span_c =
        ot_span_create(trace_id, "span-c", sizeof("span-c") - 1, OT_SK_SERVER,
                       OT_ST_FAILED_PRECONDITION, "this is the third span",
                       sizeof("this is the third span") - 1, span_a->trace_id);
    usleep(10);
    ot_span_end(span_c);
    usleep(5);
    ot_span_end(span_a);

    pthread_mutex_lock(&ot_spans_mtx);
    ot_finished = true;
    pthread_cond_broadcast(&ot_spans_to_export);
    pthread_mutex_unlock(&ot_spans_mtx);
    pthread_join(ot_exporter, NULL);
}
