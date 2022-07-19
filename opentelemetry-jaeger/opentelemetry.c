#include "opentelemetry.h"

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
#include <time.h>
#include <unistd.h>

#define CJSON_HIDE_SYMBOLS
#include "../vendor/cJSON/cJSON.c"

#define GB_IMPLEMENTATION
#include "../vendor/gb/gb.h"

typedef struct {
    char *key, *value;
    bool should_free_value;
} ot_attribute_t;

struct ot_span_t {
    uint64_t start_time_unix_nano, end_time_unix_nano;
    uint64_t id, parent_span_id;
    __uint128_t trace_id;
    ot_span_kind_t kind;
    ot_span_status_t status;
    char* name;
    char* message;
    struct ot_span_t* next;
    void* udata;
    gbArray(ot_attribute_t) attributes;
};

typedef ot_span_t*(ot_span_create_root_fn_t(__uint128_t, char*, ot_span_kind_t,
                                            char*));
typedef ot_span_t*(ot_span_create_child_of_fn_t(__uint128_t, char*,
                                                ot_span_kind_t, char*,
                                                const ot_span_t*));
typedef void(ot_span_end_fn_t(ot_span_t*));
typedef bool(ot_span_add_attribute_fn_t(ot_span_t*, char*, char*,
                                        bool should_free_value));
typedef __uint128_t(ot_span_trace_id_fn_t());
typedef void(ot_end_fn_t());
typedef void(ot_span_set_status_fn_t(ot_span_t* span, ot_span_status_t status));
typedef void(ot_span_set_udata_fn_t(ot_span_t* span, void* udata));
typedef void*(ot_span_get_udata_fn_t(ot_span_t* span));

typedef struct {
    ot_span_t* spans;
    pthread_cond_t spans_to_export;
    pthread_mutex_t spans_mtx;
    bool finished;
    pthread_t exporter;
    ot_span_create_root_fn_t* span_create_root;
    ot_span_create_child_of_fn_t* span_create_child_of;
    ot_span_end_fn_t* span_end;
    ot_span_add_attribute_fn_t* span_add_attribute;
    ot_end_fn_t* end;
    ot_span_set_status_fn_t* span_set_status;
    ot_span_set_udata_fn_t* span_set_udata;
    ot_span_get_udata_fn_t* span_get_udata;
    char* url;
} ot_t;

static ot_t ot;

__uint128_t ot_generate_trace_id() {
    __uint128_t trace_id = 0;
    arc4random_buf(&trace_id, sizeof(trace_id));
    return trace_id;
}

static ot_span_t* ot_span_create_root_noop(__uint128_t trace_id, char* name,
                                           ot_span_kind_t kind, char* message) {
    (void)trace_id;
    (void)name;
    (void)kind;
    (void)message;
    return NULL;
}

static ot_span_t* ot_span_create_child_of_noop(__uint128_t trace_id, char* name,
                                               ot_span_kind_t kind,
                                               char* message,
                                               const ot_span_t* parent) {
    (void)trace_id;
    (void)name;
    (void)kind;
    (void)message;
    (void)parent;
    return NULL;
}

static bool ot_span_add_attribute_noop(ot_span_t* span, char* key, char* value,
                                       bool should_free_value) {
    (void)span;
    (void)key;
    (void)value;
    (void)should_free_value;
    return true;
}

static bool ot_span_add_attribute_impl(ot_span_t* span, char* key, char* value,
                                       bool should_free_value) {
    gb_array_append(span->attributes,
                    ((ot_attribute_t){.key = key,
                                      .value = value,
                                      .should_free_value = should_free_value}));
    return true;
}

bool ot_span_add_attribute(ot_span_t* span, char* key, char* value,
                           bool should_free_value) {
    return ot.span_add_attribute(span, key, value, should_free_value);
}

static cJSON* ot_spans_to_json(const ot_span_t* span) {
    cJSON* j_root = cJSON_CreateObject();

    cJSON* j_resource_spans = cJSON_AddArrayToObject(j_root, "resourceSpans");

    cJSON* j_resource_span = cJSON_CreateObject();
    cJSON_AddItemToArray(j_resource_spans, j_resource_span);

    cJSON* j_resource = cJSON_AddObjectToObject(j_resource_span, "resource");

    if (gb_array_count(span->attributes) > 0) {
        cJSON* j_attributes = cJSON_AddArrayToObject(j_resource, "attributes");

        for (int i = 0; i < gb_array_count(span->attributes); i++) {
            cJSON* j_attribute = cJSON_CreateObject();
            cJSON_AddItemToArray(j_attributes, j_attribute);
            cJSON_AddStringToObject(j_attribute, "key",
                                    span->attributes[i].key);

            cJSON* j_value = cJSON_AddObjectToObject(j_attribute, "value");
            cJSON_AddStringToObject(j_value, "stringValue",
                                    span->attributes[i].value);
        }
    }

    cJSON* j_instrumentation_library_spans =
        cJSON_AddArrayToObject(j_resource_span, "instrumentationLibrarySpans");

    cJSON* j_instrumentation_library_span = cJSON_CreateObject();
    cJSON_AddItemToArray(j_instrumentation_library_spans,
                         j_instrumentation_library_span);

    cJSON* j_spans =
        cJSON_AddArrayToObject(j_instrumentation_library_span, "spans");

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
                     (uint8_t)((span->id >> (8 * i)) & 0xff));
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

    return j_root;
}

static void ot_span_destroy_impl(ot_span_t* span) {
    for (int i = 0; i < gb_array_count(span->attributes); i++) {
        ot_attribute_t* attr = &span->attributes[i];
        if (attr->should_free_value) {
            free(attr->value);
        }
    }
    gb_array_free(span->attributes);
    free(span);
}

static ot_span_t* ot_span_create_child_of_impl(__uint128_t trace_id, char* name,
                                               ot_span_kind_t kind,
                                               char* message,
                                               const ot_span_t* parent) {
    struct timespec tp = {0};
    clock_gettime(CLOCK_REALTIME, &tp);

    ot_span_t* span = calloc(1, sizeof(ot_span_t));

    gb_array_init_reserve(span->attributes, gb_heap_allocator(), 3);
    span->start_time_unix_nano = tp.tv_sec * 1000 * 1000 * 1000 + tp.tv_nsec;
    span->trace_id = trace_id;
    span->kind = kind;
    if (parent != NULL) span->parent_span_id = parent->id;

    arc4random_buf(&span->id, sizeof(span->id));
    span->name = name;
    span->message = message;

    return span;
}

ot_span_t* ot_span_create_child_of(__uint128_t trace_id, char* name,
                                   ot_span_kind_t kind, char* message,
                                   const ot_span_t* parent) {
    return ot.span_create_child_of(trace_id, name, kind, message, parent);
}

static ot_span_t* ot_span_create_root_impl(__uint128_t trace_id, char* name,
                                           ot_span_kind_t kind, char* message) {
    return ot_span_create_child_of_impl(trace_id, name, kind, message, NULL);
}

ot_span_t* ot_span_create_root(__uint128_t trace_id, char* name,
                               ot_span_kind_t kind, char* message) {
    return ot.span_create_root(trace_id, name, kind, message);
}

static void ot_span_end_noop(ot_span_t* span) { (void)span; }

static void ot_span_end_impl(ot_span_t* span) {
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

void ot_span_end(ot_span_t* span) { return ot.span_end(span); }

static uint64_t on_http_body_chunk_noop(void* contents, uint64_t size,
                                        uint64_t nmemb, void* userp) {
    (void)contents;
    (void)userp;
    return size * nmemb;
}

static void* ot_export(void* varg) {
    (void)varg;

#define OT_POST_DATA_LEN 16384
    static char post_data[OT_POST_DATA_LEN] = "";
    memset(post_data, 0, OT_POST_DATA_LEN);

    CURL* http_handle = curl_easy_init();
    assert(http_handle != NULL);

    assert(curl_easy_setopt(http_handle, CURLOPT_URL, ot.url) == CURLE_OK);
    assert(curl_easy_setopt(http_handle, CURLOPT_WRITEFUNCTION,
                            on_http_body_chunk_noop) == CURLE_OK);
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
        assert(cJSON_PrintPreallocated(root, post_data, OT_POST_DATA_LEN, 0) ==
               1);
        // printf(
        //     "Exporting span: trace_id=%02llx%02llx span_id=%02llx json "
        //     "=`%s`\n ",
        //     (uint64_t)(span->trace_id & UINT64_MAX),
        //     (uint64_t)(span->trace_id >> 64), span->id, post_data);

        assert(curl_easy_setopt(http_handle, CURLOPT_POSTFIELDS, post_data) ==
               CURLE_OK);
        CURLcode res = curl_easy_perform(http_handle);
        if (res != CURLE_OK) {
            int64_t error = 0;
            curl_easy_getinfo(http_handle, CURLINFO_OS_ERRNO, &error);
            fprintf(stderr,
                    "Failed to post traces : url=%s res=%d err=%s "
                    "errno=%d\n",
                    ot.url, res, curl_easy_strerror(res), res);
            // TODO: retry?
        } else {
            // printf("Exported span: span_id=%02llx\n", span->id);
        }

        cJSON_Delete(root);
        ot_span_destroy_impl(span);
        fflush(stdout);

        pthread_mutex_unlock(&ot.spans_mtx);
    }

    return NULL;
}

static void ot_end_noop() {}

static void ot_end_impl() {
    pthread_mutex_lock(&ot.spans_mtx);
    ot.finished = true;
    pthread_cond_broadcast(&ot.spans_to_export);
    pthread_mutex_unlock(&ot.spans_mtx);
    pthread_join(ot.exporter, NULL);
}

void ot_end() { return ot.end(); }

void ot_span_set_status_noop(ot_span_t* span, ot_span_status_t status) {
    (void)span;
    (void)status;
}

void ot_span_set_status_impl(ot_span_t* span, ot_span_status_t status) {
    span->status = status;
}

void ot_span_set_status(ot_span_t* span, ot_span_status_t status) {
    ot.span_set_status(span, status);
}

void ot_span_set_udata_noop(ot_span_t* span, void* udata) {
    (void)span;
    (void)udata;
}

void ot_span_set_udata_impl(ot_span_t* span, void* udata) {
    span->udata = udata;
}

void ot_span_set_udata(ot_span_t* span, void* udata) {
    ot.span_set_udata(span, udata);
}

void* ot_span_get_udata_noop(ot_span_t* span) {
    (void)span;
    return NULL;
}

void* ot_span_get_udata_impl(ot_span_t* span) { return span->udata; }

void* ot_span_get_udata(ot_span_t* span) { return ot.span_get_udata(span); }

void ot_start_noop(char* url) {
    (void)url;

    ot.span_create_root = ot_span_create_root_noop;
    ot.span_create_child_of = ot_span_create_child_of_noop;
    ot.span_end = ot_span_end_noop;
    ot.span_add_attribute = ot_span_add_attribute_noop;
    ot.end = ot_end_noop;
    ot.span_set_status = ot_span_set_status_noop;
    ot.span_set_udata = ot_span_set_udata_noop;
    ot.span_get_udata = ot_span_get_udata_noop;
}

void ot_start(char* url) {
    pthread_mutex_init(&ot.spans_mtx, NULL);
    pthread_cond_init(&ot.spans_to_export, NULL);
    pthread_create(&ot.exporter, NULL, ot_export, NULL);
    ot.url = url;
    ot.span_create_root = ot_span_create_root_impl;
    ot.span_create_child_of = ot_span_create_child_of_impl;
    ot.span_end = ot_span_end_impl;
    ot.span_add_attribute = ot_span_add_attribute_impl;
    ot.end = ot_end_impl;
    ot.span_set_status = ot_span_set_status_impl;
    ot.span_set_udata = ot_span_set_udata_impl;
    ot.span_get_udata = ot_span_get_udata_impl;
}

