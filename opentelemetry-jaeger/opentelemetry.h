#include <assert.h>
#include <curl/curl.h>
#include <curl/easy.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

#define CJSON_HIDE_SYMBOLS
#include "../vendor/cJSON/cJSON.c"

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
    void* udata;
};
typedef struct ot_span_t ot_span_t;

typedef struct {
    ot_span_t* spans;
    pthread_cond_t spans_to_export;
    pthread_mutex_t spans_mtx;
    bool finished;
    pthread_t exporter;
} ot_t;

static ot_t ot;

static cJSON* ot_spans_to_json(const ot_span_t* span) {
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

    {
        char buf[33] = "";
        for (int i = 0; i < 16; i++) {
            snprintf(&buf[i * 2], 3, "%02x",
                     (uint8_t)((span->trace_id >> (8 * i)) & 0xff));
        }
        cJSON_AddStringToObject(j_span, "traceId", buf);
    }

    {
        char buf[17] = "";
        for (int i = 0; i < 8; i++) {
            snprintf(&buf[i * 2], 3, "%02x",
                     (uint8_t)((span->span_id >> (8 * i)) & 0xff));
        }
        cJSON_AddStringToObject(j_span, "spanId", buf);
    }
    if (span->parent_span_id != 0) {
        char buf[17] = "";
        for (int i = 0; i < 8; i++) {
            snprintf(&buf[i * 2], 3, "%02x",
                     (uint8_t)((span->parent_span_id >> (8 * i)) & 0xff));
        }
        cJSON_AddStringToObject(j_span, "parentSpanId", buf);
    }

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
    struct timespec tp = {0};
    clock_gettime(CLOCK_REALTIME, &tp);

    ot_span_t* span = calloc(1, sizeof(ot_span_t));

    span->start_time_unix_nano = tp.tv_sec * 1000 * 1000 * 1000 + tp.tv_nsec;
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

ot_span_t* ot_span_create_c(__uint128_t trace_id, char* name0,
                            ot_span_kind_t kind, ot_span_status_t status,
                            char* message0, uint64_t parent_span_id) {
    return ot_span_create(trace_id, name0, strlen(name0), kind, status,
                          message0, strlen(message0), parent_span_id);
}

void ot_span_end(ot_span_t* span) {
    struct timespec tp = {0};
    clock_gettime(CLOCK_REALTIME, &tp);

    span->end_time_unix_nano = tp.tv_sec * 1000 * 1000 * 1000 + tp.tv_nsec;
    if (span->end_time_unix_nano < span->start_time_unix_nano)
        span->end_time_unix_nano = span->start_time_unix_nano;

    pthread_mutex_lock(&ot.spans_mtx);
    span->next = ot.spans;
    ot.spans = span;
    pthread_cond_signal(&ot.spans_to_export);
    fflush(stdout);
    pthread_mutex_unlock(&ot.spans_mtx);
}

static uint64_t noop(void* contents, uint64_t size, uint64_t nmemb,
                     void* userp) {
    (void)contents;
    (void)userp;
    return size * nmemb;
}

void* ot_export(void* varg) {
    (void)varg;

#define OT_POST_DATA_LEN 4096
    static char post_data[OT_POST_DATA_LEN] = "";
    memset(post_data, 0, OT_POST_DATA_LEN);

    CURL* http_handle = curl_easy_init();
    assert(http_handle != NULL);

    const char url[] = "localhost:4318/v1/traces";
    assert(curl_easy_setopt(http_handle, CURLOPT_URL, url) == CURLE_OK);
    assert(curl_easy_setopt(http_handle, CURLOPT_WRITEFUNCTION, noop) ==
           CURLE_OK);
    assert(curl_easy_setopt(http_handle, CURLOPT_MAXREDIRS, 5) == CURLE_OK);
    assert(curl_easy_setopt(http_handle, CURLOPT_TIMEOUT, 60 /* seconds */) ==
           CURLE_OK);
    assert(curl_easy_setopt(http_handle, CURLOPT_FOLLOWLOCATION, true) ==
           CURLE_OK);
    assert(curl_easy_setopt(http_handle, CURLOPT_REDIR_PROTOCOLS,
                            "http,https") == CURLE_OK);
    struct curl_slist* slist = NULL;
    slist = curl_slist_append(slist, "Content-Type: application/json");
    assert(slist != NULL);
    assert(curl_easy_setopt(http_handle, CURLOPT_HTTPHEADER, slist) ==
           CURLE_OK);
    assert(curl_easy_setopt(http_handle, CURLOPT_VERBOSE, 0) == CURLE_OK);

    // TODO: batching
    while (true) {
        pthread_mutex_lock(&ot.spans_mtx);
        while (ot.spans == NULL) {
            if (ot.finished) {
                pthread_mutex_unlock(&ot.spans_mtx);
                curl_slist_free_all(slist);
                curl_easy_cleanup(http_handle);
                pthread_exit(NULL);
            }
            pthread_cond_wait(&ot.spans_to_export, &ot.spans_mtx);
        }
        ot_span_t* span = ot.spans;
        ot.spans = ot.spans->next;
        cJSON* root = ot_spans_to_json(span);
        /* printf( */
        /*     "Exporting span: trace_id=%02llx%02llx span_id=%02llx
         * json=`%s`\n", */
        /*     (uint64_t)(span->trace_id & UINT64_MAX), */
        /*     (uint64_t)(span->trace_id >> 64), span->span_id, post_data); */
        assert(cJSON_PrintPreallocated(root, post_data, OT_POST_DATA_LEN, 0) ==
               1);

        assert(curl_easy_setopt(http_handle, CURLOPT_POSTFIELDS, post_data) ==
               CURLE_OK);
        CURLcode res = curl_easy_perform(http_handle);
        if (res != CURLE_OK) {
            int64_t error = 0;
            curl_easy_getinfo(http_handle, CURLINFO_OS_ERRNO, &error);
            fprintf(stderr,
                    "Failed to post traces : url=%s res=%d err=%s "
                    "errno=%d\n",
                    url, res, curl_easy_strerror(res), res);
            // TODO: retry?
        } else {
            /* printf("Exported span: span_id=%02llx\n", span->span_id); */
        }

        cJSON_Delete(root);
        free(span);
        fflush(stdout);

        pthread_mutex_unlock(&ot.spans_mtx);
    }

    return NULL;
}

void ot_start() {
    pthread_mutex_init(&ot.spans_mtx, NULL);
    pthread_cond_init(&ot.spans_to_export, NULL);
    pthread_create(&ot.exporter, NULL, ot_export, NULL);
}

void ot_end() {
    pthread_mutex_lock(&ot.spans_mtx);
    ot.finished = true;
    pthread_cond_broadcast(&ot.spans_to_export);
    pthread_mutex_unlock(&ot.spans_mtx);
    pthread_join(ot.exporter, NULL);
}
